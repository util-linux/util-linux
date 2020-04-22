/*
 * devname.c - get a dev by its device inode name
 *
 * Copyright (C) Andries Brouwer
 * Copyright (C) 1999, 2000, 2001, 2002, 2003 Theodore Ts'o
 * Copyright (C) 2001 Andreas Dilger
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 * %End-Header%
 */

#define _GNU_SOURCE 1

#include <stdio.h>
#include <string.h>
#include <limits.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <stdlib.h>
#include <ctype.h>
#include <fcntl.h>
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#include <dirent.h>
#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#include <time.h>

#include "blkidP.h"

#include "canonicalize.h"		/* $(top_srcdir)/include */
#include "pathnames.h"
#include "sysfs.h"

/*
 * Find a dev struct in the cache by device name, if available.
 *
 * If there is no entry with the specified device name, and the create
 * flag is set, then create an empty device entry.
 */
blkid_dev blkid_get_dev(blkid_cache cache, const char *devname, int flags)
{
	blkid_dev dev = NULL, tmp;
	struct list_head *p, *pnext;
	char *cn = NULL;

	if (!cache || !devname)
		return NULL;

	/* search by name */
	list_for_each(p, &cache->bic_devs) {
		tmp = list_entry(p, struct blkid_struct_dev, bid_devs);
		if (strcmp(tmp->bid_name, devname) != 0)
			continue;
		dev = tmp;
		break;
	}

	/* try canonicalize the name */
	if (!dev && (cn = canonicalize_path(devname))) {
		if (strcmp(cn, devname) != 0) {
			DBG(DEVNAME, ul_debug("search canonical %s", cn));
			list_for_each(p, &cache->bic_devs) {
				tmp = list_entry(p, struct blkid_struct_dev, bid_devs);
				if (strcmp(tmp->bid_name, cn) != 0)
					continue;
				dev = tmp;

				/* update name returned by blkid_dev_devname() */
				free(dev->bid_xname);
				dev->bid_xname = strdup(devname);
				break;
			}
		} else {
			free(cn);
			cn = NULL;
		}
	}

	if (!dev && (flags & BLKID_DEV_CREATE)) {
		if (access(devname, F_OK) < 0)
			goto done;
		dev = blkid_new_dev();
		if (!dev)
			goto done;
		dev->bid_time = INT_MIN;
		if (cn) {
			dev->bid_name = cn;
			dev->bid_xname = strdup(devname);
			cn = NULL;	/* see free() below */
		} else
			dev->bid_name = strdup(devname);

		dev->bid_cache = cache;
		list_add_tail(&dev->bid_devs, &cache->bic_devs);
		cache->bic_flags |= BLKID_BIC_FL_CHANGED;
	}

	if (flags & BLKID_DEV_VERIFY) {
		dev = blkid_verify(cache, dev);
		if (!dev || !(dev->bid_flags & BLKID_BID_FL_VERIFIED))
			goto done;
		/*
		 * If the device is verified, then search the blkid
		 * cache for any entries that match on the type, uuid,
		 * and label, and verify them; if a cache entry can
		 * not be verified, then it's stale and so we remove
		 * it.
		 */
		list_for_each_safe(p, pnext, &cache->bic_devs) {
			blkid_dev dev2 = list_entry(p, struct blkid_struct_dev, bid_devs);
			if (dev2->bid_flags & BLKID_BID_FL_VERIFIED)
				continue;
			if (!dev->bid_type || !dev2->bid_type ||
			    strcmp(dev->bid_type, dev2->bid_type) != 0)
				continue;
			if (dev->bid_label && dev2->bid_label &&
			    strcmp(dev->bid_label, dev2->bid_label) != 0)
				continue;
			if (dev->bid_uuid && dev2->bid_uuid &&
			    strcmp(dev->bid_uuid, dev2->bid_uuid) != 0)
				continue;
			if ((dev->bid_label && !dev2->bid_label) ||
			    (!dev->bid_label && dev2->bid_label) ||
			    (dev->bid_uuid && !dev2->bid_uuid) ||
			    (!dev->bid_uuid && dev2->bid_uuid))
				continue;
			dev2 = blkid_verify(cache, dev2);
			if (dev2 && !(dev2->bid_flags & BLKID_BID_FL_VERIFIED))
				blkid_free_dev(dev2);
		}
	}
done:
	if (dev)
		DBG(DEVNAME, ul_debug("%s requested, found %s in cache", devname, dev->bid_name));
	free(cn);
	return dev;
}

/* Directories where we will try to search for device names */
static const char *dirlist[] = { "/dev", "/devfs", "/devices", NULL };

static int is_dm_leaf(const char *devname)
{
	struct dirent	*de, *d_de;
	DIR		*dir, *d_dir;
	char		path[NAME_MAX + 18 + 1];
	int		ret = 1;

	if ((dir = opendir("/sys/block")) == NULL)
		return 0;
	while ((de = readdir(dir)) != NULL) {
		if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..") ||
		    !strcmp(de->d_name, devname) ||
		    strncmp(de->d_name, "dm-", 3) != 0 ||
		    strlen(de->d_name) > sizeof(path)-32)
			continue;
		sprintf(path, "/sys/block/%s/slaves", de->d_name);
		if ((d_dir = opendir(path)) == NULL)
			continue;
		while ((d_de = readdir(d_dir)) != NULL) {
			if (!strcmp(d_de->d_name, devname)) {
				ret = 0;
				break;
			}
		}
		closedir(d_dir);
		if (!ret)
			break;
	}
	closedir(dir);
	return ret;
}

/*
 * Probe a single block device to add to the device cache.
 */
static void probe_one(blkid_cache cache, const char *ptname,
		      dev_t devno, int pri, int only_if_new, int removable)
{
	blkid_dev dev = NULL;
	struct list_head *p, *pnext;
	const char **dir;
	char *devname = NULL;

	/* See if we already have this device number in the cache. */
	list_for_each_safe(p, pnext, &cache->bic_devs) {
		blkid_dev tmp = list_entry(p, struct blkid_struct_dev,
					   bid_devs);
		if (tmp->bid_devno == devno) {
			if (only_if_new && !access(tmp->bid_name, F_OK))
				return;
			dev = blkid_verify(cache, tmp);
			if (dev && (dev->bid_flags & BLKID_BID_FL_VERIFIED))
				break;
			dev = NULL;
		}
	}
	if (dev && dev->bid_devno == devno)
		goto set_pri;

	/* Try to translate private device-mapper dm-<N> names
	 * to standard /dev/mapper/<name>.
	 */
	if (!strncmp(ptname, "dm-", 3) && isdigit(ptname[3])) {
		devname = canonicalize_dm_name(ptname);
		if (!devname)
			blkid__scan_dir("/dev/mapper", devno, NULL, &devname);
		if (devname)
			goto get_dev;
	}

	/*
	 * Take a quick look at /dev/ptname for the device number.  We check
	 * all of the likely device directories.  If we don't find it, or if
	 * the stat information doesn't check out, use blkid_devno_to_devname()
	 * to find it via an exhaustive search for the device major/minor.
	 */
	for (dir = dirlist; *dir; dir++) {
		struct stat st;
		char device[256];

		snprintf(device, sizeof(device), "%s/%s", *dir, ptname);
		if ((dev = blkid_get_dev(cache, device, BLKID_DEV_FIND)) &&
		    dev->bid_devno == devno)
			goto set_pri;

		if (stat(device, &st) == 0 &&
		    (S_ISBLK(st.st_mode) ||
		     (S_ISCHR(st.st_mode) && !strncmp(ptname, "ubi", 3))) &&
		    st.st_rdev == devno) {
			devname = strdup(device);
			goto get_dev;
		}
	}
	/* Do a short-cut scan of /dev/mapper first */
	if (!devname)
		blkid__scan_dir("/dev/mapper", devno, NULL, &devname);
	if (!devname) {
		devname = blkid_devno_to_devname(devno);
		if (!devname)
			return;
	}

get_dev:
	dev = blkid_get_dev(cache, devname, BLKID_DEV_NORMAL);
	free(devname);

set_pri:
	if (dev) {
		if (pri)
			dev->bid_pri = pri;
		else if (!strncmp(dev->bid_name, "/dev/mapper/", 12)) {
			dev->bid_pri = BLKID_PRI_DM;
			if (is_dm_leaf(ptname))
				dev->bid_pri += 5;
		} else if (!strncmp(ptname, "md", 2))
			dev->bid_pri = BLKID_PRI_MD;
		if (removable)
			dev->bid_flags |= BLKID_BID_FL_REMOVABLE;
	}
}

#define PROC_PARTITIONS "/proc/partitions"
#define VG_DIR		"/proc/lvm/VGs"

/*
 * This function initializes the UUID cache with devices from the LVM
 * proc hierarchy.  We currently depend on the names of the LVM
 * hierarchy giving us the device structure in /dev.  (XXX is this a
 * safe thing to do?)
 */
#ifdef VG_DIR
static dev_t lvm_get_devno(const char *lvm_device)
{
	FILE *lvf;
	char buf[1024];
	int ma, mi;
	dev_t ret = 0;

	DBG(DEVNAME, ul_debug("opening %s", lvm_device));
	if ((lvf = fopen(lvm_device, "r" UL_CLOEXECSTR)) == NULL) {
		DBG(DEVNAME, ul_debug("%s: (%d) %m", lvm_device, errno));
		return 0;
	}

	while (fgets(buf, sizeof(buf), lvf)) {
		if (sscanf(buf, "device: %d:%d", &ma, &mi) == 2) {
			ret = makedev(ma, mi);
			break;
		}
	}
	fclose(lvf);

	return ret;
}

static void lvm_probe_all(blkid_cache cache, int only_if_new)
{
	DIR		*vg_list;
	struct dirent	*vg_iter;
	int		vg_len = strlen(VG_DIR);
	dev_t		dev;

	if ((vg_list = opendir(VG_DIR)) == NULL)
		return;

	DBG(DEVNAME, ul_debug("probing LVM devices under %s", VG_DIR));

	while ((vg_iter = readdir(vg_list)) != NULL) {
		DIR		*lv_list;
		char		*vdirname;
		char		*vg_name;
		struct dirent	*lv_iter;

		vg_name = vg_iter->d_name;
		if (!strcmp(vg_name, ".") || !strcmp(vg_name, ".."))
			continue;
		vdirname = malloc(vg_len + strlen(vg_name) + 8);
		if (!vdirname)
			goto exit;
		sprintf(vdirname, "%s/%s/LVs", VG_DIR, vg_name);

		lv_list = opendir(vdirname);
		free(vdirname);
		if (lv_list == NULL)
			continue;

		while ((lv_iter = readdir(lv_list)) != NULL) {
			char		*lv_name, *lvm_device;

			lv_name = lv_iter->d_name;
			if (!strcmp(lv_name, ".") || !strcmp(lv_name, ".."))
				continue;

			lvm_device = malloc(vg_len + strlen(vg_name) +
					    strlen(lv_name) + 8);
			if (!lvm_device) {
				closedir(lv_list);
				goto exit;
			}
			sprintf(lvm_device, "%s/%s/LVs/%s", VG_DIR, vg_name,
				lv_name);
			dev = lvm_get_devno(lvm_device);
			sprintf(lvm_device, "%s/%s", vg_name, lv_name);
			DBG(DEVNAME, ul_debug("LVM dev %s: devno 0x%04X",
						  lvm_device,
						  (unsigned int) dev));
			probe_one(cache, lvm_device, dev, BLKID_PRI_LVM,
				  only_if_new, 0);
			free(lvm_device);
		}
		closedir(lv_list);
	}
exit:
	closedir(vg_list);
}
#endif

#define PROC_EVMS_VOLUMES "/proc/evms/volumes"

static int
evms_probe_all(blkid_cache cache, int only_if_new)
{
	char line[100];
	int ma, mi, sz, num = 0;
	FILE *procpt;
	char device[110];

	procpt = fopen(PROC_EVMS_VOLUMES, "r" UL_CLOEXECSTR);
	if (!procpt)
		return 0;
	while (fgets(line, sizeof(line), procpt)) {
		if (sscanf (line, " %d %d %d %*s %*s %[^\n ]",
			    &ma, &mi, &sz, device) != 4)
			continue;

		DBG(DEVNAME, ul_debug("Checking partition %s (%d, %d)",
					  device, ma, mi));

		probe_one(cache, device, makedev(ma, mi), BLKID_PRI_EVMS,
			  only_if_new, 0);
		num++;
	}
	fclose(procpt);
	return num;
}

static void
ubi_probe_all(blkid_cache cache, int only_if_new)
{
	const char **dirname;

	for (dirname = dirlist; *dirname; dirname++) {
		DIR		*dir;
		struct dirent	*iter;

		DBG(DEVNAME, ul_debug("probing UBI volumes under %s",
					  *dirname));

		dir = opendir(*dirname);
		if (dir == NULL)
			continue ;

		while ((iter = readdir(dir)) != NULL) {
			char		*name;
			struct stat	st;
			dev_t		dev;

			name = iter->d_name;
#ifdef _DIRENT_HAVE_D_TYPE
			if (iter->d_type != DT_UNKNOWN &&
			    iter->d_type != DT_CHR && iter->d_type != DT_LNK)
				continue;
#endif
			if (!strcmp(name, ".") || !strcmp(name, "..") ||
			    !strstr(name, "ubi"))
				continue;
			if (!strcmp(name, "ubi_ctrl"))
				continue;
			if (fstatat(dirfd(dir), name, &st, 0))
				continue;

			dev = st.st_rdev;

			if (!S_ISCHR(st.st_mode) || !minor(dev))
				continue;
			DBG(DEVNAME, ul_debug("UBI vol %s/%s: devno 0x%04X",
				  *dirname, name, (int) dev));
			probe_one(cache, name, dev, BLKID_PRI_UBI, only_if_new, 0);
		}
		closedir(dir);
	}
}

/*
 * Read the device data for all available block devices in the system.
 */
static int probe_all(blkid_cache cache, int only_if_new)
{
	FILE *proc;
	char line[1024];
	char ptname0[128 + 1], ptname1[128 + 1], *ptname = NULL;
	char *ptnames[2];
	dev_t devs[2] = { 0, 0 };
	int iswhole[2] = { 0, 0 };
	int ma, mi;
	unsigned long long sz;
	int lens[2] = { 0, 0 };
	int which = 0, last = 0;
	struct list_head *p, *pnext;

	ptnames[0] = ptname0;
	ptnames[1] = ptname1;

	if (!cache)
		return -BLKID_ERR_PARAM;

	if (cache->bic_flags & BLKID_BIC_FL_PROBED &&
	    time(NULL) - cache->bic_time < BLKID_PROBE_INTERVAL)
		return 0;

	blkid_read_cache(cache);
	evms_probe_all(cache, only_if_new);
#ifdef VG_DIR
	lvm_probe_all(cache, only_if_new);
#endif
	ubi_probe_all(cache, only_if_new);

	proc = fopen(PROC_PARTITIONS, "r" UL_CLOEXECSTR);
	if (!proc)
		return -BLKID_ERR_PROC;

	while (fgets(line, sizeof(line), proc)) {
		last = which;
		which ^= 1;
		ptname = ptnames[which];

		if (sscanf(line, " %d %d %llu %128[^\n ]",
			   &ma, &mi, &sz, ptname) != 4)
			continue;
		devs[which] = makedev(ma, mi);

		DBG(DEVNAME, ul_debug("read device name %s", ptname));

		/* Skip whole disk devs unless they have no partitions.
		 * If base name of device has changed, also
		 * check previous dev to see if it didn't have a partn.
		 * heuristic: partition name ends in a digit, & partition
		 * names contain whole device name as substring.
		 *
		 * Skip extended partitions.
		 * heuristic: size is 1
		 */

		lens[which] = strlen(ptname);
		iswhole[which] = sysfs_devno_is_wholedisk(devs[which]);

		/* probably partition, so check */
		if (!iswhole[which]) {
			DBG(DEVNAME, ul_debug(" partition dev %s, devno 0x%04X",
				   ptname, (unsigned int) devs[which]));

			if (sz > 1)
				probe_one(cache, ptname, devs[which], 0,
					  only_if_new, 0);
			lens[which] = 0;	/* mark as checked */
		}

		/*
		 * If last was a whole disk and we just found a partition
		 * on it, remove the whole-disk dev from the cache if
		 * it exists.
		 */
		if (lens[last] && iswhole[last]
		    && !strncmp(ptnames[last], ptname, lens[last])) {

			list_for_each_safe(p, pnext, &cache->bic_devs) {
				blkid_dev tmp;

				/* find blkid dev for the whole-disk devno */
				tmp = list_entry(p, struct blkid_struct_dev,
						 bid_devs);
				if (tmp->bid_devno == devs[last]) {
					DBG(DEVNAME, ul_debug(" freeing %s",
						       tmp->bid_name));
					blkid_free_dev(tmp);
					cache->bic_flags |= BLKID_BIC_FL_CHANGED;
					break;
				}
			}
			lens[last] = 0;		/* mark as checked */
		}
		/*
		 * If last was not checked because it looked like a whole-disk
		 * dev, and the device's base name has changed,
		 * check last as well.
		 */
		if (lens[last] && strncmp(ptnames[last], ptname, lens[last]) != 0) {
			DBG(DEVNAME, ul_debug(" whole dev %s, devno 0x%04X",
				   ptnames[last], (unsigned int) devs[last]));
			probe_one(cache, ptnames[last], devs[last], 0,
				  only_if_new, 0);

			lens[last] = 0;		/* mark as checked */
		}
	}

	/* Handle the last device if it wasn't partitioned */
	if (lens[which])
		probe_one(cache, ptname, devs[which], 0, only_if_new, 0);

	fclose(proc);
	blkid_flush_cache(cache);
	return 0;
}

/* Don't use it by default -- it's pretty slow (because cdroms, floppy, ...)
 */
static int probe_all_removable(blkid_cache cache)
{
	struct path_cxt *pc;
	DIR *dir;
	struct dirent *d;

	if (!cache)
		return -BLKID_ERR_PARAM;

	dir = opendir(_PATH_SYS_BLOCK);
	if (!dir)
		return -BLKID_ERR_PROC;

	pc = ul_new_path(NULL);

	while((d = readdir(dir))) {
		int removable = 0;
		dev_t devno;

#ifdef _DIRENT_HAVE_D_TYPE
		if (d->d_type != DT_UNKNOWN && d->d_type != DT_LNK)
			continue;
#endif
		if (d->d_name[0] == '.' &&
		    ((d->d_name[1] == 0) ||
		     ((d->d_name[1] == '.') && (d->d_name[2] == 0))))
			continue;

		devno = sysfs_devname_to_devno(d->d_name);
		if (!devno)
			continue;

		if (sysfs_blkdev_init_path(pc, devno, NULL) == 0
		    && ul_path_read_s32(pc, &removable, "removable") != 0)
				removable = 0;

		if (removable)
			probe_one(cache, d->d_name, devno, 0, 0, 1);
	}

	ul_unref_path(pc);
	closedir(dir);
	return 0;
}


/**
 * blkid_probe_all:
 * @cache: cache handler
 *
 * Probes all block devices.
 *
 * Returns: 0 on success, or number less than zero in case of error.
 */
int blkid_probe_all(blkid_cache cache)
{
	int ret;

	DBG(PROBE, ul_debug("Begin blkid_probe_all()"));
	ret = probe_all(cache, 0);
	if (ret == 0) {
		cache->bic_time = time(NULL);
		cache->bic_flags |= BLKID_BIC_FL_PROBED;
	}
	DBG(PROBE, ul_debug("End blkid_probe_all() [rc=%d]", ret));
	return ret;
}

/**
 * blkid_probe_all_new:
 * @cache: cache handler
 *
 * Probes all new block devices.
 *
 * Returns: 0 on success, or number less than zero in case of error.
 */
int blkid_probe_all_new(blkid_cache cache)
{
	int ret;

	DBG(PROBE, ul_debug("Begin blkid_probe_all_new()"));
	ret = probe_all(cache, 1);
	DBG(PROBE, ul_debug("End blkid_probe_all_new() [rc=%d]", ret));
	return ret;
}

/**
 * blkid_probe_all_removable:
 * @cache: cache handler
 *
 * The libblkid probing is based on devices from /proc/partitions by default.
 * This file usually does not contain removable devices (e.g. CDROMs) and this kind
 * of devices are invisible for libblkid.
 *
 * This function adds removable block devices to @cache (probing is based on
 * information from the /sys directory). Don't forget that removable devices
 * (floppies, CDROMs, ...) could be pretty slow. It's very bad idea to call
 * this function by default.
 *
 * Note that devices which were detected by this function won't be written to
 * blkid.tab cache file.
 *
 * Returns: 0 on success, or number less than zero in case of error.
 */
int blkid_probe_all_removable(blkid_cache cache)
{
	int ret;

	DBG(PROBE, ul_debug("Begin blkid_probe_all_removable()"));
	ret = probe_all_removable(cache);
	DBG(PROBE, ul_debug("End blkid_probe_all_removable() [rc=%d]", ret));
	return ret;
}

#ifdef TEST_PROGRAM
int main(int argc, char **argv)
{
	blkid_cache cache = NULL;
	int ret;

	blkid_init_debug(BLKID_DEBUG_ALL);
	if (argc != 1) {
		fprintf(stderr, "Usage: %s\n"
			"Probe all devices and exit\n", argv[0]);
		exit(1);
	}
	if ((ret = blkid_get_cache(&cache, "/dev/null")) != 0) {
		fprintf(stderr, "%s: error creating cache (%d)\n",
			argv[0], ret);
		exit(1);
	}
	if (blkid_probe_all(cache) < 0)
		printf("%s: error probing devices\n", argv[0]);

	if (blkid_probe_all_removable(cache) < 0)
		printf("%s: error probing removable devices\n", argv[0]);

	blkid_put_cache(cache);
	return (0);
}
#endif
