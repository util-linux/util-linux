/*
 * lsblk(8) - list block devices
 *
 * Copyright (C) 2010,2011,2012 Red Hat, Inc. All rights reserved.
 * Written by Milan Broz <mbroz@redhat.com>
 *            Karel Zak <kzak@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <stdio.h>
#include <errno.h>
#include <getopt.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <inttypes.h>
#include <stdarg.h>
#include <locale.h>
#include <pwd.h>
#include <grp.h>
#include <ctype.h>

#include <blkid.h>
#include <libmount.h>
#include <libsmartcols.h>

#ifdef HAVE_LIBUDEV
#include <libudev.h>
#endif

#include <assert.h>

#include "c.h"
#include "pathnames.h"
#include "blkdev.h"
#include "canonicalize.h"
#include "nls.h"
#include "xalloc.h"
#include "strutils.h"
#include "sysfs.h"
#include "closestream.h"
#include "mangle.h"
#include "optutils.h"

#include "debug.h"

UL_DEBUG_DEFINE_MASK(lsblk);
UL_DEBUG_DEFINE_MASKNAMES(lsblk) = UL_DEBUG_EMPTY_MASKNAMES;

#define LSBLK_DEBUG_INIT	(1 << 1)
#define LSBLK_DEBUG_FILTER	(1 << 2)
#define LSBLK_DEBUG_DEV		(1 << 3)
#define LSBLK_DEBUG_CXT		(1 << 4)
#define LSBLK_DEBUG_ALL		0xFFFF

#define DBG(m, x)       __UL_DBG(lsblk, LSBLK_DEBUG_, m, x)
#define ON_DBG(m, x)    __UL_DBG_CALL(lsblk, LSBLK_DEBUG_, m, x)


#define LSBLK_EXIT_SOMEOK 64
#define LSBLK_EXIT_ALLFAILED 32

/* column IDs */
enum {
	COL_NAME = 0,
	COL_KNAME,
	COL_MAJMIN,
	COL_FSTYPE,
	COL_TARGET,
	COL_LABEL,
	COL_UUID,
	COL_PARTTYPE,
	COL_PARTLABEL,
	COL_PARTUUID,
	COL_PARTFLAGS,
	COL_RA,
	COL_RO,
	COL_RM,
	COL_HOTPLUG,
	COL_MODEL,
	COL_SERIAL,
	COL_SIZE,
	COL_STATE,
	COL_OWNER,
	COL_GROUP,
	COL_MODE,
	COL_ALIOFF,
	COL_MINIO,
	COL_OPTIO,
	COL_PHYSEC,
	COL_LOGSEC,
	COL_ROTA,
	COL_SCHED,
	COL_RQ_SIZE,
	COL_TYPE,
	COL_DALIGN,
	COL_DGRAN,
	COL_DMAX,
	COL_DZERO,
	COL_WSAME,
	COL_WWN,
	COL_RAND,
	COL_PKNAME,
	COL_HCTL,
	COL_TRANSPORT,
	COL_SUBSYS,
	COL_REV,
	COL_VENDOR
};

/* basic table settings */
enum {
	LSBLK_ASCII =		(1 << 0),
	LSBLK_RAW =		(1 << 1),
	LSBLK_NOHEADINGS =	(1 << 2),
	LSBLK_EXPORT =		(1 << 3),
	LSBLK_TREE =		(1 << 4),
	LSBLK_JSON =		(1 << 5),
};

enum {
	SORT_STRING	= 0,	/* default is to use scols_cell_get_data() */
	SORT_U64	= 1	/* use private pointer from scols_cell_get_userdata() */
};

/* column names */
struct colinfo {
	const char	*name;		/* header */
	double		whint;		/* width hint (N < 1 is in percent of termwidth) */
	int		flags;		/* SCOLS_FL_* */
	const char      *help;

	int	sort_type;		/* SORT_* */
};

/* columns descriptions */
static struct colinfo infos[] = {
	[COL_NAME]   = { "NAME",    0.25, SCOLS_FL_TREE | SCOLS_FL_NOEXTREMES, N_("device name") },
	[COL_KNAME]  = { "KNAME",   0.3, 0, N_("internal kernel device name") },
	[COL_PKNAME] = { "PKNAME",   0.3, 0, N_("internal parent kernel device name") },
	[COL_MAJMIN] = { "MAJ:MIN", 6, 0, N_("major:minor device number"), SORT_U64 },
	[COL_FSTYPE] = { "FSTYPE",  0.1, SCOLS_FL_TRUNC, N_("filesystem type") },
	[COL_TARGET] = { "MOUNTPOINT", 0.10, SCOLS_FL_TRUNC, N_("where the device is mounted") },
	[COL_LABEL]  = { "LABEL",   0.1, 0, N_("filesystem LABEL") },
	[COL_UUID]   = { "UUID",    36,  0, N_("filesystem UUID") },

	[COL_PARTTYPE]  = { "PARTTYPE",  36,  0, N_("partition type UUID") },
	[COL_PARTLABEL] = { "PARTLABEL", 0.1, 0, N_("partition LABEL") },
	[COL_PARTUUID]  = { "PARTUUID",  36,  0, N_("partition UUID") },
	[COL_PARTFLAGS] = { "PARTFLAGS",  36,  0, N_("partition flags") },

	[COL_RA]     = { "RA",      3, SCOLS_FL_RIGHT, N_("read-ahead of the device"), SORT_U64 },
	[COL_RO]     = { "RO",      1, SCOLS_FL_RIGHT, N_("read-only device") },
	[COL_RM]     = { "RM",      1, SCOLS_FL_RIGHT, N_("removable device") },
	[COL_HOTPLUG]= { "HOTPLUG", 1, SCOLS_FL_RIGHT, N_("removable or hotplug device (usb, pcmcia, ...)") },
	[COL_ROTA]   = { "ROTA",    1, SCOLS_FL_RIGHT, N_("rotational device") },
	[COL_RAND]   = { "RAND",    1, SCOLS_FL_RIGHT, N_("adds randomness") },
	[COL_MODEL]  = { "MODEL",   0.1, SCOLS_FL_TRUNC, N_("device identifier") },
	[COL_SERIAL] = { "SERIAL",  0.1, SCOLS_FL_TRUNC, N_("disk serial number") },
	[COL_SIZE]   = { "SIZE",    5, SCOLS_FL_RIGHT, N_("size of the device"), SORT_U64 },
	[COL_STATE]  = { "STATE",   7, SCOLS_FL_TRUNC, N_("state of the device") },
	[COL_OWNER]  = { "OWNER",   0.1, SCOLS_FL_TRUNC, N_("user name"), },
	[COL_GROUP]  = { "GROUP",   0.1, SCOLS_FL_TRUNC, N_("group name") },
	[COL_MODE]   = { "MODE",    10,   0, N_("device node permissions") },
	[COL_ALIOFF] = { "ALIGNMENT", 6, SCOLS_FL_RIGHT, N_("alignment offset"), SORT_U64 },
	[COL_MINIO]  = { "MIN-IO",  6, SCOLS_FL_RIGHT, N_("minimum I/O size"), SORT_U64 },
	[COL_OPTIO]  = { "OPT-IO",  6, SCOLS_FL_RIGHT, N_("optimal I/O size"), SORT_U64 },
	[COL_PHYSEC] = { "PHY-SEC", 7, SCOLS_FL_RIGHT, N_("physical sector size"), SORT_U64 },
	[COL_LOGSEC] = { "LOG-SEC", 7, SCOLS_FL_RIGHT, N_("logical sector size"), SORT_U64 },
	[COL_SCHED]  = { "SCHED",   0.1, 0, N_("I/O scheduler name") },
	[COL_RQ_SIZE]= { "RQ-SIZE", 5, SCOLS_FL_RIGHT, N_("request queue size"), SORT_U64 },
	[COL_TYPE]   = { "TYPE",    4, 0, N_("device type") },
	[COL_DALIGN] = { "DISC-ALN", 6, SCOLS_FL_RIGHT, N_("discard alignment offset"), SORT_U64 },
	[COL_DGRAN]  = { "DISC-GRAN", 6, SCOLS_FL_RIGHT, N_("discard granularity"), SORT_U64 },
	[COL_DMAX]   = { "DISC-MAX", 6, SCOLS_FL_RIGHT, N_("discard max bytes"), SORT_U64 },
	[COL_DZERO]  = { "DISC-ZERO", 1, SCOLS_FL_RIGHT, N_("discard zeroes data") },
	[COL_WSAME]  = { "WSAME",   6, SCOLS_FL_RIGHT, N_("write same max bytes"), SORT_U64 },
	[COL_WWN]    = { "WWN",     18, 0, N_("unique storage identifier") },
	[COL_HCTL]   = { "HCTL", 10, 0, N_("Host:Channel:Target:Lun for SCSI") },
	[COL_TRANSPORT] = { "TRAN", 6, 0, N_("device transport type") },
	[COL_SUBSYS] = { "SUBSYSTEMS", 0.1, SCOLS_FL_NOEXTREMES, N_("de-duplicated chain of subsystems") },
	[COL_REV]    = { "REV",   4, SCOLS_FL_RIGHT, N_("device revision") },
	[COL_VENDOR] = { "VENDOR", 0.1, SCOLS_FL_TRUNC, N_("device vendor") },
};

struct lsblk {
	struct libscols_table *table;	/* output table */
	struct libscols_column *sort_col;/* sort output by this column */
	int sort_id;

	unsigned int all_devices:1;	/* print all devices, including empty */
	unsigned int bytes:1;		/* print SIZE in bytes */
	unsigned int inverse:1;		/* print inverse dependencies */
	unsigned int nodeps:1;		/* don't print slaves/holders */
	unsigned int scsi:1;		/* print only device with HCTL (SCSI) */
	unsigned int paths:1;		/* print devnames with "/dev" prefix */
	unsigned int sort_hidden:1;	/* sort column not between output columns */
};

struct lsblk *lsblk;	/* global handler */

/* columns[] array specifies all currently wanted output column. The columns
 * are defined by infos[] array and you can specify (on command line) each
 * column twice. That's enough, dynamically allocated array of the columns is
 * unnecessary overkill and over-engineering in this case */
static int columns[ARRAY_SIZE(infos) * 2];
static size_t ncolumns;

static inline size_t err_columns_index(size_t arysz, size_t idx)
{
	if (idx >= arysz)
		errx(EXIT_FAILURE, _("too many columns specified, "
				     "the limit is %zu columns"),
				arysz - 1);
	return idx;
}

#define add_column(ary, n, id)	\
		((ary)[ err_columns_index(ARRAY_SIZE(ary), (n)) ] = (id))

static int excludes[256];
static size_t nexcludes;

static int includes[256];
static size_t nincludes;

static struct libmnt_table *mtab, *swaps;
static struct libmnt_cache *mntcache;

#ifdef HAVE_LIBUDEV
struct udev *udev;
#endif

struct blkdev_cxt {
	struct blkdev_cxt *parent;

	struct libscols_line *scols_line;
	struct stat	st;

	char *name;		/* kernel name in /sys/block */
	char *dm_name;		/* DM name (dm/block) */

	char *filename;		/* path to device node */

	struct sysfs_cxt  sysfs;

	int partition;		/* is partition? TRUE/FALSE */

	int probed;		/* already probed */
	char *fstype;		/* detected fs, NULL or "?" if cannot detect */
	char *uuid;		/* filesystem UUID (or stack uuid) */
	char *label;		/* filesystem label */
	char *parttype;		/* partition type UUID */
	char *partuuid;		/* partition UUID */
	char *partlabel;	/* partition label */
	char *partflags;	/* partition flags */
	char *wwn;		/* storage WWN */
	char *serial;		/* disk serial number */

	int npartitions;	/* # of partitions this device has */
	int nholders;		/* # of devices mapped directly to this device
				 * /sys/block/.../holders */
	int nslaves;		/* # of devices this device maps to */
	int maj, min;		/* devno */
	int discard;		/* supports discard */

	uint64_t size;		/* device size */
};

static void lsblk_init_debug(void)
{
	__UL_INIT_DEBUG(lsblk, LSBLK_DEBUG_, 0, LSBLK_DEBUG);
}

static int is_maj_excluded(int maj)
{
	size_t i;

	assert(ARRAY_SIZE(excludes) > nexcludes);

	if (!nexcludes)
		return 0;	/* filter not enabled, device not excluded */

	for (i = 0; i < nexcludes; i++) {
		if (excludes[i] == maj) {
			DBG(FILTER, ul_debug("exclude: maj=%d", maj));
			return 1;
		}
	}
	return 0;
}

static int is_maj_included(int maj)
{
	size_t i;

	assert(ARRAY_SIZE(includes) > nincludes);

	if (!nincludes)
		return 1;	/* filter not enabled, device is included */

	for (i = 0; i < nincludes; i++) {
		if (includes[i] == maj) {
			DBG(FILTER, ul_debug("include: maj=%d", maj));
			return 1;
		}
	}
	return 0;
}

/* array with IDs of enabled columns */
static int get_column_id(int num)
{
	assert(num >= 0);
	assert((size_t) num < ncolumns);
	assert(columns[num] < (int) ARRAY_SIZE(infos));
	return columns[num];
}

static struct colinfo *get_column_info(int num)
{
	return &infos[ get_column_id(num) ];
}

static int column_name_to_id(const char *name, size_t namesz)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(infos); i++) {
		const char *cn = infos[i].name;

		if (!strncasecmp(name, cn, namesz) && !*(cn + namesz))
			return i;
	}
	warnx(_("unknown column: %s"), name);
	return -1;
}

static int column_id_to_number(int id)
{
	size_t i;

	for (i = 0; i < ncolumns; i++)
		if (columns[i] == id)
			return i;
	return -1;
}

static void reset_blkdev_cxt(struct blkdev_cxt *cxt)
{
	if (!cxt)
		return;

	DBG(CXT, ul_debugobj(cxt, "reset"));

	free(cxt->name);
	free(cxt->dm_name);
	free(cxt->filename);
	free(cxt->fstype);
	free(cxt->uuid);
	free(cxt->label);
	free(cxt->parttype);
	free(cxt->partuuid);
	free(cxt->partlabel);
	free(cxt->wwn);
	free(cxt->serial);

	sysfs_deinit(&cxt->sysfs);

	memset(cxt, 0, sizeof(*cxt));
}

static int is_dm(const char *name)
{
	return strncmp(name, "dm-", 3) ? 0 : 1;
}

static struct dirent *xreaddir(DIR *dp)
{
	struct dirent *d;

	assert(dp);

	while ((d = readdir(dp))) {
		if (!strcmp(d->d_name, ".") ||
		    !strcmp(d->d_name, ".."))
			continue;

		/* blacklist here? */
		break;
	}
	return d;
}

static char *get_device_path(struct blkdev_cxt *cxt)
{
	char path[PATH_MAX];

	assert(cxt);
	assert(cxt->name);

	if (is_dm(cxt->name))
		return canonicalize_dm_name(cxt->name);

	snprintf(path, sizeof(path), "/dev/%s", cxt->name);
	sysfs_devname_sys_to_dev(path);
	return xstrdup(path);
}

static int table_parser_errcb(struct libmnt_table *tb __attribute__((__unused__)),
			const char *filename, int line)
{
	if (filename)
		warnx(_("%s: parse error at line %d -- ignored"), filename, line);
	return 1;
}

static int is_active_swap(const char *filename)
{
	if (!swaps) {
		swaps = mnt_new_table();
		if (!swaps)
			return 0;
		if (!mntcache)
			mntcache = mnt_new_cache();

		mnt_table_set_parser_errcb(swaps, table_parser_errcb);
		mnt_table_set_cache(swaps, mntcache);
		mnt_table_parse_swaps(swaps, NULL);
	}

	return mnt_table_find_srcpath(swaps, filename, MNT_ITER_BACKWARD) != 0;
}

static char *get_device_mountpoint(struct blkdev_cxt *cxt)
{
	struct libmnt_fs *fs;
	const char *fsroot;

	assert(cxt);
	assert(cxt->filename);

	if (!mtab) {
		mtab = mnt_new_table();
		if (!mtab)
			return NULL;
		if (!mntcache)
			mntcache = mnt_new_cache();

		mnt_table_set_parser_errcb(mtab, table_parser_errcb);
		mnt_table_set_cache(mtab, mntcache);
		mnt_table_parse_mtab(mtab, NULL);
	}

	/* Note that maj:min in /proc/self/mountinfo does not have to match with
	 * devno as returned by stat(), so we have to try devname too
	 */
	fs = mnt_table_find_devno(mtab, makedev(cxt->maj, cxt->min), MNT_ITER_BACKWARD);
	if (!fs)
		fs = mnt_table_find_srcpath(mtab, cxt->filename, MNT_ITER_BACKWARD);
	if (!fs)
		return is_active_swap(cxt->filename) ? xstrdup("[SWAP]") : NULL;

	/* found */
	fsroot = mnt_fs_get_root(fs);
	if (fsroot && strcmp(fsroot, "/") != 0) {
		/* hmm.. we found bind mount or btrfs subvolume, let's try to
		 * get real FS root mountpoint */
		struct libmnt_fs *rfs;
		struct libmnt_iter *itr = mnt_new_iter(MNT_ITER_BACKWARD);

		mnt_table_set_iter(mtab, itr, fs);
		while (mnt_table_next_fs(mtab, itr, &rfs) == 0) {
			fsroot = mnt_fs_get_root(rfs);
			if ((!fsroot || strcmp(fsroot, "/") == 0)
			    && mnt_fs_match_source(rfs, cxt->filename, mntcache)) {
				fs = rfs;
				break;
			}
		}
		mnt_free_iter(itr);
	}

	DBG(DEV, ul_debugobj(cxt, "mountpoint: %s", mnt_fs_get_target(fs)));
	return xstrdup(mnt_fs_get_target(fs));
}

#ifndef HAVE_LIBUDEV
static int get_udev_properties(struct blkdev_cxt *cxt
				__attribute__((__unused__)))
{
	return -1;
}
#else
static int get_udev_properties(struct blkdev_cxt *cxt)
{
	struct udev_device *dev;

	if (cxt->probed)
		return 0;		/* already done */

	if (!udev)
		udev = udev_new();
	if (!udev)
		return -1;

	dev = udev_device_new_from_subsystem_sysname(udev, "block", cxt->name);
	if (dev) {
		const char *data;

		if ((data = udev_device_get_property_value(dev, "ID_FS_LABEL_ENC"))) {
			cxt->label = xstrdup(data);
			unhexmangle_string(cxt->label);
		}
		if ((data = udev_device_get_property_value(dev, "ID_FS_UUID_ENC"))) {
			cxt->uuid = xstrdup(data);
			unhexmangle_string(cxt->uuid);
		}
		if ((data = udev_device_get_property_value(dev, "ID_PART_ENTRY_NAME"))) {
			cxt->partlabel = xstrdup(data);
			unhexmangle_string(cxt->partlabel);
		}
		if ((data = udev_device_get_property_value(dev, "ID_FS_TYPE")))
			cxt->fstype = xstrdup(data);
		if ((data = udev_device_get_property_value(dev, "ID_PART_ENTRY_TYPE")))
			cxt->parttype = xstrdup(data);
		if ((data = udev_device_get_property_value(dev, "ID_PART_ENTRY_UUID")))
			cxt->partuuid = xstrdup(data);
		if ((data = udev_device_get_property_value(dev, "ID_PART_ENTRY_FLAGS")))
			cxt->partflags = xstrdup(data);

		data = udev_device_get_property_value(dev, "ID_WWN_WITH_EXTENSION");
		if (!data)
			data = udev_device_get_property_value(dev, "ID_WWN");
		if (data)
			cxt->wwn = xstrdup(data);

		if ((data = udev_device_get_property_value(dev, "ID_SERIAL_SHORT")))
			cxt->serial = xstrdup(data);
		udev_device_unref(dev);
		cxt->probed = 1;
		DBG(DEV, ul_debugobj(cxt, "%s: found udev properties", cxt->name));
	}

	return cxt->probed == 1 ? 0 : -1;

}
#endif /* HAVE_LIBUDEV */

static void probe_device(struct blkdev_cxt *cxt)
{
	blkid_probe pr = NULL;

	if (cxt->probed)
		return;

	if (!cxt->size)
		return;

	/* try udev DB */
	if (get_udev_properties(cxt) == 0)
		return;				/* success */

	cxt->probed = 1;

	/* try libblkid (fallback) */
	if (getuid() != 0)
		return;				/* no permissions to read from the device */

	pr = blkid_new_probe_from_filename(cxt->filename);
	if (!pr)
		return;

	blkid_probe_enable_superblocks(pr, 1);
	blkid_probe_set_superblocks_flags(pr, BLKID_SUBLKS_LABEL |
					      BLKID_SUBLKS_UUID |
					      BLKID_SUBLKS_TYPE);
	blkid_probe_enable_partitions(pr, 1);
	blkid_probe_set_partitions_flags(pr, BLKID_PARTS_ENTRY_DETAILS);

	if (!blkid_do_safeprobe(pr)) {
		const char *data = NULL;

		if (!blkid_probe_lookup_value(pr, "TYPE", &data, NULL))
			cxt->fstype = xstrdup(data);
		if (!blkid_probe_lookup_value(pr, "UUID", &data, NULL))
			cxt->uuid = xstrdup(data);
		if (!blkid_probe_lookup_value(pr, "LABEL", &data, NULL))
			cxt->label = xstrdup(data);
		if (!blkid_probe_lookup_value(pr, "PART_ENTRY_TYPE", &data, NULL))
			cxt->parttype = xstrdup(data);
		if (!blkid_probe_lookup_value(pr, "PART_ENTRY_UUID", &data, NULL))
			cxt->partuuid = xstrdup(data);
		if (!blkid_probe_lookup_value(pr, "PART_ENTRY_NAME", &data, NULL))
			cxt->partlabel = xstrdup(data);
		if (!blkid_probe_lookup_value(pr, "PART_ENTRY_FLAGS", &data, NULL))
			cxt->partflags = xstrdup(data);
		DBG(DEV, ul_debugobj(cxt, "%s: found blkid properties", cxt->name));
	}

	blkid_free_probe(pr);
	return;
}

static int is_readonly_device(struct blkdev_cxt *cxt)
{
	int fd, ro = 0;

	if (sysfs_scanf(&cxt->sysfs, "ro", "%d", &ro) == 1)
		return ro;

	/* fallback if "ro" attribute does not exist */
	fd = open(cxt->filename, O_RDONLY);
	if (fd != -1) {
		if (ioctl(fd, BLKROGET, &ro) != 0)
			ro = 0;
		close(fd);
	}
	return ro;
}

static char *get_scheduler(struct blkdev_cxt *cxt)
{
	char *str = sysfs_strdup(&cxt->sysfs, "queue/scheduler");
	char *p, *res = NULL;

	if (!str)
		return NULL;
	p = strchr(str, '[');
	if (p) {
		res = p + 1;
		p = strchr(res, ']');
		if (p) {
			*p = '\0';
			res = xstrdup(res);
		} else
			res = NULL;
	}
	free(str);
	return res;
}

static char *get_type(struct blkdev_cxt *cxt)
{
	char *res = NULL, *p;

	if (is_dm(cxt->name)) {
		char *dm_uuid = sysfs_strdup(&cxt->sysfs, "dm/uuid");

		/* The DM_UUID prefix should be set to subsystem owning
		 * the device - LVM, CRYPT, DMRAID, MPATH, PART */
		if (dm_uuid) {
			char *tmp = dm_uuid;
			char *dm_uuid_prefix = strsep(&tmp, "-");

			if (dm_uuid_prefix) {
				/* kpartx hack to remove partition number */
				if (strncasecmp(dm_uuid_prefix, "part", 4) == 0)
					dm_uuid_prefix[4] = '\0';

				res = xstrdup(dm_uuid_prefix);
			}
		}

		free(dm_uuid);
		if (!res)
			/* No UUID or no prefix - just mark it as DM device */
			res = xstrdup("dm");

	} else if (!strncmp(cxt->name, "loop", 4)) {
		res = xstrdup("loop");

	} else if (!strncmp(cxt->name, "md", 2)) {
		char *md_level = sysfs_strdup(&cxt->sysfs, "md/level");
		res = md_level ? md_level : xstrdup("md");

	} else {
		const char *type = NULL;
		int x = 0;

		if (!sysfs_read_int(&cxt->sysfs, "device/type", &x))
			type = blkdev_scsi_type_to_name(x);

		if (!type)
			type = cxt->partition ? "part" : "disk";
		res = xstrdup(type);
	}

	for (p = res; p && *p; p++)
		*p = tolower((unsigned char) *p);
	return res;
}

/* Thanks to lsscsi code for idea of detection logic used here */
static char *get_transport(struct blkdev_cxt *cxt)
{
	struct sysfs_cxt *sysfs = &cxt->sysfs;
	char *attr = NULL;
	const char *trans = NULL;

	/* SCSI - Serial Peripheral Interface */
	if (sysfs_scsi_host_is(sysfs, "spi"))
		trans = "spi";

	/* FC/FCoE - Fibre Channel / Fibre Channel over Ethernet */
	else if (sysfs_scsi_host_is(sysfs, "fc")) {
		attr = sysfs_scsi_host_strdup_attribute(sysfs, "fc", "symbolic_name");
		if (!attr)
			return NULL;
		trans = strstr(attr, " over ") ? "fcoe" : "fc";
		free(attr);
	}

	/* SAS - Serial Attached SCSI */
	else if (sysfs_scsi_host_is(sysfs, "sas") ||
		 sysfs_scsi_has_attribute(sysfs, "sas_device"))
		trans = "sas";


	/* SBP - Serial Bus Protocol (FireWire) */
	else if (sysfs_scsi_has_attribute(sysfs, "ieee1394_id"))
		trans = "sbp";

	/* iSCSI */
	else if (sysfs_scsi_host_is(sysfs, "iscsi"))
		trans ="iscsi";

	/* USB - Universal Serial Bus */
	else if (sysfs_scsi_path_contains(sysfs, "usb"))
		trans = "usb";

	/* ATA, SATA */
	else if (sysfs_scsi_host_is(sysfs, "scsi")) {
		attr = sysfs_scsi_host_strdup_attribute(sysfs, "scsi", "proc_name");
		if (!attr)
			return NULL;
		if (!strncmp(attr, "ahci", 4) || !strncmp(attr, "sata", 4))
			trans = "sata";
		else if (strstr(attr, "ata"))
			trans = "ata";
		free(attr);

	} else if (strncmp(cxt->name, "nvme", 4) == 0)
		trans = "nvme";

	return trans ? xstrdup(trans) : NULL;
}

static char *get_subsystems(struct blkdev_cxt *cxt)
{
	char path[PATH_MAX];
	char *sub, *chain, *res = NULL;
	size_t len = 0, last = 0;

	chain = sysfs_get_devchain(&cxt->sysfs, path, sizeof(path));
	if (!chain)
		return NULL;

	while (sysfs_next_subsystem(&cxt->sysfs, chain, &sub) == 0) {
		size_t sz;

		/* don't create "block:scsi:scsi", but "block:scsi" */
		if (len && strcmp(res + last, sub) == 0) {
			free(sub);
			continue;
		}

		sz = strlen(sub);
		res = xrealloc(res, len + sz + 2);
		if (len)
			res[len++] = ':';

		memcpy(res + len, sub, sz + 1);
		last = len;
		len += sz;
		free(sub);
	}

	return res;
}


#define is_parsable(_l)	(scols_table_is_raw((_l)->table) || \
			 scols_table_is_export((_l)->table) || \
			 scols_table_is_json((_l)->table))

static char *mk_name(const char *name)
{
	char *p;
	if (!name)
		return NULL;
	if (lsblk->paths)
		xasprintf(&p, "/dev/%s", name);
	else
		p = xstrdup(name);
	if (p)
		sysfs_devname_sys_to_dev(p);
	return p;
}

static char *mk_dm_name(const char *name)
{
	char *p;
	if (!name)
		return NULL;
	if (lsblk->paths)
		xasprintf(&p, "/dev/mapper/%s", name);
	else
		p = xstrdup(name);
	return p;
}

/* stores data to scols cell userdata (invisible and independent on output)
 * to make the original values accessible for sort functions
 */
static void set_sortdata_u64(struct libscols_line *ln, int col, uint64_t x)
{
	struct libscols_cell *ce = scols_line_get_cell(ln, col);
	uint64_t *data;

	if (!ce)
		return;
	data = xmalloc(sizeof(uint64_t));
	*data = x;
	scols_cell_set_userdata(ce, data);
}

static void set_sortdata_u64_from_string(struct libscols_line *ln, int col, const char *str)
{
	uint64_t x;

	if (!str || sscanf(str, "%"SCNu64, &x) != 1)
		return;

	set_sortdata_u64(ln, col, x);
}

static void unref_sortdata(struct libscols_table *tb)
{
	struct libscols_iter *itr;
	struct libscols_line *ln;

	if (!tb || !lsblk->sort_col)
		return;
	itr = scols_new_iter(SCOLS_ITER_FORWARD);
	if (!itr)
		return;
	while (scols_table_next_line(tb, itr, &ln) == 0) {
		struct libscols_cell *ce = scols_line_get_column_cell(ln,
								lsblk->sort_col);
		void *data = scols_cell_get_userdata(ce);
		free(data);
	}

	scols_free_iter(itr);
}

static void set_scols_data(struct blkdev_cxt *cxt, int col, int id, struct libscols_line *ln)
{
	int sort = 0, st_rc = 0;
	char *str = NULL;

	if (!cxt->st.st_rdev && (id == COL_OWNER || id == COL_GROUP ||
				 id == COL_MODE))
		st_rc = stat(cxt->filename, &cxt->st);

	if (lsblk->sort_id == id)
		sort = 1;

	switch(id) {
	case COL_NAME:
		str = cxt->dm_name ? mk_dm_name(cxt->dm_name) :	mk_name(cxt->name);
		break;
	case COL_KNAME:
		str = mk_name(cxt->name);
		break;
	case COL_PKNAME:
		if (cxt->parent)
			str = mk_name(cxt->parent->name);
		break;
	case COL_OWNER:
	{
		struct passwd *pw = st_rc ? NULL : getpwuid(cxt->st.st_uid);
		if (pw)
			str = xstrdup(pw->pw_name);
		break;
	}
	case COL_GROUP:
	{
		struct group *gr = st_rc ? NULL : getgrgid(cxt->st.st_gid);
		if (gr)
			str = xstrdup(gr->gr_name);
		break;
	}
	case COL_MODE:
	{
		char md[11];

		if (!st_rc) {
			xstrmode(cxt->st.st_mode, md);
			str = xstrdup(md);
		}
		break;
	}
	case COL_MAJMIN:
		if (is_parsable(lsblk))
			xasprintf(&str, "%u:%u", cxt->maj, cxt->min);
		else
			xasprintf(&str, "%3u:%-3u", cxt->maj, cxt->min);
		if (sort)
			set_sortdata_u64(ln, col, makedev(cxt->maj, cxt->min));
		break;
	case COL_FSTYPE:
		probe_device(cxt);
		if (cxt->fstype)
			str = xstrdup(cxt->fstype);
		break;
	case COL_TARGET:
		str = get_device_mountpoint(cxt);
		break;
	case COL_LABEL:
		probe_device(cxt);
		if (cxt->label)
			str = xstrdup(cxt->label);
		break;
	case COL_UUID:
		probe_device(cxt);
		if (cxt->uuid)
			str = xstrdup(cxt->uuid);
		break;
	case COL_PARTTYPE:
		probe_device(cxt);
		if (cxt->parttype)
			str = xstrdup(cxt->parttype);
		break;
	case COL_PARTLABEL:
		probe_device(cxt);
		if (cxt->partlabel)
			str = xstrdup(cxt->partlabel);
		break;
	case COL_PARTUUID:
		probe_device(cxt);
		if (cxt->partuuid)
			str = xstrdup(cxt->partuuid);
		break;
	case COL_PARTFLAGS:
		probe_device(cxt);
		if (cxt->partflags)
			str = xstrdup(cxt->partflags);
		break;
	case COL_WWN:
		get_udev_properties(cxt);
		if (cxt->wwn)
			str = xstrdup(cxt->wwn);
		break;
	case COL_RA:
		str = sysfs_strdup(&cxt->sysfs, "queue/read_ahead_kb");
		if (sort)
			set_sortdata_u64_from_string(ln, col, str);
		break;
	case COL_RO:
		str = xstrdup(is_readonly_device(cxt) ? "1" : "0");
		break;
	case COL_RM:
		str = sysfs_strdup(&cxt->sysfs, "removable");
		if (!str && cxt->sysfs.parent)
			str = sysfs_strdup(cxt->sysfs.parent, "removable");
		break;
	case COL_HOTPLUG:
		str = sysfs_is_hotpluggable(&cxt->sysfs) ? xstrdup("1") : xstrdup("0");
		break;
	case COL_ROTA:
		str = sysfs_strdup(&cxt->sysfs, "queue/rotational");
		break;
	case COL_RAND:
		str = sysfs_strdup(&cxt->sysfs, "queue/add_random");
		break;
	case COL_MODEL:
		if (!cxt->partition && cxt->nslaves == 0)
			str = sysfs_strdup(&cxt->sysfs, "device/model");
		break;
	case COL_SERIAL:
		if (!cxt->partition && cxt->nslaves == 0) {
			get_udev_properties(cxt);
			if (cxt->serial)
				str = xstrdup(cxt->serial);
			else
				str = sysfs_strdup(&cxt->sysfs, "device/serial");
		}
		break;
	case COL_REV:
		if (!cxt->partition && cxt->nslaves == 0)
			str = sysfs_strdup(&cxt->sysfs, "device/rev");
		break;
	case COL_VENDOR:
		if (!cxt->partition && cxt->nslaves == 0)
			str = sysfs_strdup(&cxt->sysfs, "device/vendor");
		break;
	case COL_SIZE:
		if (!cxt->size)
			break;
		if (lsblk->bytes)
			xasprintf(&str, "%ju", cxt->size);
		else
			str = size_to_human_string(SIZE_SUFFIX_1LETTER, cxt->size);
		if (sort)
			set_sortdata_u64(ln, col, cxt->size);
		break;
	case COL_STATE:
		if (!cxt->partition && !cxt->dm_name)
			str = sysfs_strdup(&cxt->sysfs, "device/state");
		else if (cxt->dm_name) {
			int x = 0;
			if (sysfs_read_int(&cxt->sysfs, "dm/suspended", &x) == 0)
				str = xstrdup(x ? "suspended" : "running");
		}
		break;
	case COL_ALIOFF:
		str = sysfs_strdup(&cxt->sysfs, "alignment_offset");
		if (sort)
			set_sortdata_u64_from_string(ln, col, str);
		break;
	case COL_MINIO:
		str = sysfs_strdup(&cxt->sysfs, "queue/minimum_io_size");
		if (sort)
			set_sortdata_u64_from_string(ln, col, str);
		break;
	case COL_OPTIO:
		str = sysfs_strdup(&cxt->sysfs, "queue/optimal_io_size");
		if (sort)
			set_sortdata_u64_from_string(ln, col, str);
		break;
	case COL_PHYSEC:
		str = sysfs_strdup(&cxt->sysfs, "queue/physical_block_size");
		if (sort)
			set_sortdata_u64_from_string(ln, col, str);
		break;
	case COL_LOGSEC:
		str = sysfs_strdup(&cxt->sysfs, "queue/logical_block_size");
		if (sort)
			set_sortdata_u64_from_string(ln, col, str);
		break;
	case COL_SCHED:
		str = get_scheduler(cxt);
		break;
	case COL_RQ_SIZE:
		str = sysfs_strdup(&cxt->sysfs, "queue/nr_requests");
		if (sort)
			set_sortdata_u64_from_string(ln, col, str);
		break;
	case COL_TYPE:
		str = get_type(cxt);
		break;
	case COL_HCTL:
	{
		int h, c, t, l;
		if (sysfs_scsi_get_hctl(&cxt->sysfs, &h, &c, &t, &l) == 0)
			xasprintf(&str, "%d:%d:%d:%d", h, c, t, l);
		break;
	}
	case COL_TRANSPORT:
		str = get_transport(cxt);
		break;
	case COL_SUBSYS:
		str = get_subsystems(cxt);
		break;
	case COL_DALIGN:
		if (cxt->discard)
			str = sysfs_strdup(&cxt->sysfs, "discard_alignment");
		if (!str)
			str = xstrdup("0");
		if (sort)
			set_sortdata_u64_from_string(ln, col, str);
		break;
	case COL_DGRAN:
		if (lsblk->bytes) {
			str = sysfs_strdup(&cxt->sysfs, "queue/discard_granularity");
			if (sort)
				set_sortdata_u64_from_string(ln, col, str);
		} else {
			uint64_t x;
			if (sysfs_read_u64(&cxt->sysfs,
					   "queue/discard_granularity", &x) == 0) {
				str = size_to_human_string(SIZE_SUFFIX_1LETTER, x);
				if (sort)
					set_sortdata_u64(ln, col, x);
			}
		}
		break;
	case COL_DMAX:
		if (lsblk->bytes) {
			str = sysfs_strdup(&cxt->sysfs, "queue/discard_max_bytes");
			if (sort)
				set_sortdata_u64_from_string(ln, col, str);
		} else {
			uint64_t x;
			if (sysfs_read_u64(&cxt->sysfs,
					   "queue/discard_max_bytes", &x) == 0) {
				str = size_to_human_string(SIZE_SUFFIX_1LETTER, x);
				if (sort)
					set_sortdata_u64(ln, col, x);
			}
		}
		break;
	case COL_DZERO:
		if (cxt->discard)
			str = sysfs_strdup(&cxt->sysfs, "queue/discard_zeroes_data");
		if (!str)
			str = xstrdup("0");
		break;
	case COL_WSAME:
		if (lsblk->bytes) {
			str = sysfs_strdup(&cxt->sysfs, "queue/write_same_max_bytes");
			if (sort)
				set_sortdata_u64_from_string(ln, col, str);
		} else {
			uint64_t x;

			if (sysfs_read_u64(&cxt->sysfs,
					   "queue/write_same_max_bytes", &x) == 0) {
				str = size_to_human_string(SIZE_SUFFIX_1LETTER, x);
				if (sort)
					set_sortdata_u64(ln, col, x);
			}
		}
		if (!str)
			str = xstrdup("0");
		break;
	};

	if (str)
		scols_line_refer_data(ln, col, str);
}

static void fill_table_line(struct blkdev_cxt *cxt, struct libscols_line *scols_parent)
{
	size_t i;

	cxt->scols_line = scols_table_new_line(lsblk->table, scols_parent);
	if (!cxt->scols_line)
		return;

	for (i = 0; i < ncolumns; i++)
		set_scols_data(cxt, i, get_column_id(i), cxt->scols_line);
}

static int set_cxt(struct blkdev_cxt *cxt,
		    struct blkdev_cxt *parent,
		    struct blkdev_cxt *wholedisk,
		    const char *name)
{
	dev_t devno;

	DBG(CXT, ul_debugobj(cxt, "setting context for %s [parent=%p, wholedisk=%p]",
				name, parent, wholedisk));

	cxt->parent = parent;
	cxt->name = xstrdup(name);
	cxt->partition = wholedisk != NULL;

	cxt->filename = get_device_path(cxt);
	if (!cxt->filename) {
		DBG(CXT, ul_debugobj(cxt, "%s: failed to get device path", cxt->name));
		return -1;
	}
	DBG(CXT, ul_debugobj(cxt, "%s: filename=%s", cxt->name, cxt->filename));

	devno = sysfs_devname_to_devno(cxt->name, wholedisk ? wholedisk->name : NULL);

	if (!devno) {
		DBG(CXT, ul_debugobj(cxt, "%s: unknown device name", cxt->name));
		return -1;
	}

	if (lsblk->inverse) {
		if (sysfs_init(&cxt->sysfs, devno, wholedisk ? &wholedisk->sysfs : NULL)) {
			DBG(CXT, ul_debugobj(cxt, "%s: failed to initialize sysfs handler", cxt->name));
			return -1;
		}
		if (parent)
			parent->sysfs.parent = &cxt->sysfs;
	} else {
		if (sysfs_init(&cxt->sysfs, devno, parent ? &parent->sysfs : NULL)) {
			DBG(CXT, ul_debugobj(cxt, "%s: failed to initialize sysfs handler", cxt->name));
			return -1;
		}
	}

	cxt->maj = major(devno);
	cxt->min = minor(devno);
	cxt->size = 0;

	if (sysfs_read_u64(&cxt->sysfs, "size", &cxt->size) == 0)	/* in sectors */
		cxt->size <<= 9;					/* in bytes */

	if (sysfs_read_int(&cxt->sysfs,
			   "queue/discard_granularity", &cxt->discard) != 0)
		cxt->discard = 0;

	/* Ignore devices of zero size */
	if (!lsblk->all_devices && cxt->size == 0) {
		DBG(CXT, ul_debugobj(cxt, "zero size device -- ignore"));
		return -1;
	}
	if (is_dm(cxt->name)) {
		cxt->dm_name = sysfs_strdup(&cxt->sysfs, "dm/name");
		if (!cxt->dm_name) {
			DBG(CXT, ul_debugobj(cxt, "%s: failed to get dm name", cxt->name));
			return -1;
		}
	}

	cxt->npartitions = sysfs_count_partitions(&cxt->sysfs, cxt->name);
	cxt->nholders = sysfs_count_dirents(&cxt->sysfs, "holders");
	cxt->nslaves = sysfs_count_dirents(&cxt->sysfs, "slaves");

	DBG(CXT, ul_debugobj(cxt, "%s: npartitions=%d, nholders=%d, nslaves=%d",
			cxt->name, cxt->npartitions, cxt->nholders, cxt->nslaves));

	/* ignore non-SCSI devices */
	if (lsblk->scsi && sysfs_scsi_get_hctl(&cxt->sysfs, NULL, NULL, NULL, NULL)) {
		DBG(CXT, ul_debugobj(cxt, "non-scsi device -- ignore"));
		return -1;
	}

	DBG(CXT, ul_debugobj(cxt, "%s: context successfully initialized", cxt->name));
	return 0;
}

static int process_blkdev(struct blkdev_cxt *cxt, struct blkdev_cxt *parent,
			  int do_partitions, const char *part_name);

/*
 * List device partitions if any.
 */
static int list_partitions(struct blkdev_cxt *wholedisk_cxt, struct blkdev_cxt *parent_cxt,
			   const char *part_name)
{
	DIR *dir;
	struct dirent *d;
	struct blkdev_cxt part_cxt = { 0 };
	int r = -1;

	assert(wholedisk_cxt);

	/*
	 * Do not process further if there are no partitions for
	 * this device or the device itself is a partition.
	 */
	if (!wholedisk_cxt->npartitions || wholedisk_cxt->partition)
		return -1;

	DBG(CXT, ul_debugobj(wholedisk_cxt, "probe whole-disk for partitions"));

	dir = sysfs_opendir(&wholedisk_cxt->sysfs, NULL);
	if (!dir)
		err(EXIT_FAILURE, _("failed to open device directory in sysfs"));

	while ((d = xreaddir(dir))) {
		/* Process particular partition only? */
		if (part_name && strcmp(part_name, d->d_name))
			continue;

		if (!(sysfs_is_partition_dirent(dir, d, wholedisk_cxt->name)))
			continue;

		DBG(CXT, ul_debugobj(wholedisk_cxt, "  checking %s", d->d_name));

		if (lsblk->inverse) {
			/*
			 * <parent_cxt>
			 * `-<part_cxt>
			 *   `-<wholedisk_cxt>
			 *    `-...
			 */
			if (set_cxt(&part_cxt, parent_cxt, wholedisk_cxt, d->d_name))
				goto next;

			if (!parent_cxt && part_cxt.nholders)
				goto next;

			wholedisk_cxt->parent = &part_cxt;
			fill_table_line(&part_cxt, parent_cxt ? parent_cxt->scols_line : NULL);
			if (!lsblk->nodeps)
				process_blkdev(wholedisk_cxt, &part_cxt, 0, NULL);
		} else {
			/*
			 * <parent_cxt>
			 * `-<wholedisk_cxt>
			 *   `-<part_cxt>
			 *    `-...
			 */
			int ps = set_cxt(&part_cxt, wholedisk_cxt, wholedisk_cxt, d->d_name);

			/* Print whole disk only once */
			if (r)
				fill_table_line(wholedisk_cxt, parent_cxt ? parent_cxt->scols_line : NULL);
			if (ps == 0 && !lsblk->nodeps)
				process_blkdev(&part_cxt, wholedisk_cxt, 0, NULL);
		}
	next:
		reset_blkdev_cxt(&part_cxt);
		r = 0;
	}

	DBG(CXT, ul_debugobj(wholedisk_cxt, "probe whole-disk for partitions -- done"));
	closedir(dir);
	return r;
}

static int get_wholedisk_from_partition_dirent(DIR *dir,
				struct dirent *d, struct blkdev_cxt *cxt)
{
	char path[PATH_MAX];
	char *p;
	int len;

	if ((len = readlinkat(dirfd(dir), d->d_name, path, sizeof(path) - 1)) < 0)
		return 0;

	path[len] = '\0';

	/* The path ends with ".../<device>/<partition>" */
	p = strrchr(path, '/');
	if (!p)
		return 0;
	*p = '\0';

	p = strrchr(path, '/');
	if (!p)
		return 0;
	p++;

	return set_cxt(cxt, NULL, NULL, p);
}

/*
 * List device dependencies: partitions, holders (inverse = 0) or slaves (inverse = 1).
 */
static int list_deps(struct blkdev_cxt *cxt)
{
	DIR *dir;
	struct dirent *d;
	struct blkdev_cxt dep = { 0 };
	const char *depname;

	assert(cxt);

	if (lsblk->nodeps)
		return 0;

	DBG(CXT, ul_debugobj(cxt, "%s: list dependencies", cxt->name));

	if (!(lsblk->inverse ? cxt->nslaves : cxt->nholders))
		return 0;

	depname = lsblk->inverse ? "slaves" : "holders";
	dir = sysfs_opendir(&cxt->sysfs, depname);
	if (!dir)
		return 0;

	DBG(CXT, ul_debugobj(cxt, "%s: checking for '%s' dependence", cxt->name, depname));

	while ((d = xreaddir(dir))) {
		/* Is the dependency a partition? */
		if (sysfs_is_partition_dirent(dir, d, NULL)) {
			if (!get_wholedisk_from_partition_dirent(dir, d, &dep)) {
				DBG(CXT, ul_debugobj(cxt, "%s: %s: dependence is partition",
								cxt->name, d->d_name));
				process_blkdev(&dep, cxt, 1, d->d_name);
			}
		}
		/* The dependency is a whole device. */
		else if (!set_cxt(&dep, cxt, NULL, d->d_name)) {
			DBG(CXT, ul_debugobj(cxt, "%s: %s: dependence is whole-disk",
								cxt->name, d->d_name));
			/* For inverse tree we don't want to show partitions
			 * if the dependence is on whole-disk */
			process_blkdev(&dep, cxt, lsblk->inverse ? 0 : 1, NULL);
		}
		reset_blkdev_cxt(&dep);
	}
	closedir(dir);

	DBG(CXT, ul_debugobj(cxt, "%s: checking for '%s' -- done", cxt->name, depname));
	return 0;
}

static int process_blkdev(struct blkdev_cxt *cxt, struct blkdev_cxt *parent,
			  int do_partitions, const char *part_name)
{
	if (do_partitions && cxt->npartitions)
		list_partitions(cxt, parent, part_name);		/* partitions + whole-disk */
	else
		fill_table_line(cxt, parent ? parent->scols_line : NULL); /* whole-disk only */

	return list_deps(cxt);
}

/* Iterate devices in sysfs */
static int iterate_block_devices(void)
{
	DIR *dir;
	struct dirent *d;
	struct blkdev_cxt cxt = { 0 };

	if (!(dir = opendir(_PATH_SYS_BLOCK)))
		return -errno;

	DBG(DEV, ul_debug("iterate on " _PATH_SYS_BLOCK));

	while ((d = xreaddir(dir))) {

		DBG(DEV, ul_debug(" %s dentry", d->d_name));

		if (set_cxt(&cxt, NULL, NULL, d->d_name))
			goto next;

		if (is_maj_excluded(cxt.maj) || !is_maj_included(cxt.maj))
			goto next;

		/* Skip devices in the middle of dependency tree. */
		if ((lsblk->inverse ? cxt.nholders : cxt.nslaves) > 0)
			goto next;

		process_blkdev(&cxt, NULL, 1, NULL);
	next:
		reset_blkdev_cxt(&cxt);
	}

	closedir(dir);

	DBG(DEV, ul_debug("iterate on " _PATH_SYS_BLOCK " -- done"));
	return 0;
}

static char *devno_to_sysfs_name(dev_t devno, char *devname, char *buf, size_t buf_size)
{
	char path[PATH_MAX];
	ssize_t len;

	if (!sysfs_devno_path(devno, path, sizeof(path))) {
		warn(_("%s: failed to compose sysfs path"), devname);
		return NULL;
	}

	len = readlink(path, buf, buf_size - 1);
	if (len < 0) {
		warn(_("%s: failed to read link"), path);
		return NULL;
	}
	buf[len] = '\0';

	return xstrdup(strrchr(buf, '/') + 1);
}

static int process_one_device(char *devname)
{
	struct blkdev_cxt parent = { 0 }, cxt = { 0 };
	struct stat st;
	char buf[PATH_MAX + 1], *name = NULL, *diskname = NULL;
	dev_t disk = 0;
	int real_part = 0, rc = -EINVAL;

	if (stat(devname, &st) || !S_ISBLK(st.st_mode)) {
		warnx(_("%s: not a block device"), devname);
		goto leave;
	}

	if (!(name = devno_to_sysfs_name(st.st_rdev, devname, buf, PATH_MAX))) {
		warn(_("%s: failed to get sysfs name"), devname);
		goto leave;
	}

	if (!strncmp(name, "dm-", 3)) {
		/* dm mapping is never a real partition! */
		real_part = 0;
	} else {
		if (blkid_devno_to_wholedisk(st.st_rdev, buf, sizeof(buf), &disk)) {
			warn(_("%s: failed to get whole-disk device number"), devname);
			goto leave;
		}
		diskname = buf;
		real_part = st.st_rdev != disk;
	}

	if (!real_part) {
		/*
		 * Device is not a partition.
		 */
		if (set_cxt(&cxt, NULL, NULL, name))
			goto leave;
		process_blkdev(&cxt, NULL, !lsblk->inverse, NULL);
	} else {
		/*
		 * Partition, read sysfs name of the device.
		 */
		if (set_cxt(&parent, NULL, NULL, diskname))
			goto leave;
		if (set_cxt(&cxt, &parent, &parent, name))
			goto leave;

		if (lsblk->inverse)
			process_blkdev(&parent, &cxt, 1, cxt.name);
		else
			process_blkdev(&cxt, &parent, 1, NULL);
	}

	rc = 0;
leave:
	free(name);
	reset_blkdev_cxt(&cxt);

	if (real_part)
		reset_blkdev_cxt(&parent);

	return rc;
}

static void parse_excludes(const char *str0)
{
	const char *str = str0;

	while (str && *str) {
		char *end = NULL;
		unsigned long n;

		errno = 0;
		n = strtoul(str, &end, 10);

		if (end == str || (end && *end && *end != ','))
			errx(EXIT_FAILURE, _("failed to parse list '%s'"), str0);
		if (errno != 0 && (n == ULONG_MAX || n == 0))
			err(EXIT_FAILURE, _("failed to parse list '%s'"), str0);
		excludes[nexcludes++] = n;

		if (nexcludes == ARRAY_SIZE(excludes))
			/* TRANSLATORS: The standard value for %d is 256. */
			errx(EXIT_FAILURE, _("the list of excluded devices is "
					"too large (limit is %d devices)"),
					(int)ARRAY_SIZE(excludes));

		str = end && *end ? end + 1 : NULL;
	}
}

static void parse_includes(const char *str0)
{
	const char *str = str0;

	while (str && *str) {
		char *end = NULL;
		unsigned long n;

		errno = 0;
		n = strtoul(str, &end, 10);

		if (end == str || (end && *end && *end != ','))
			errx(EXIT_FAILURE, _("failed to parse list '%s'"), str0);
		if (errno != 0 && (n == ULONG_MAX || n == 0))
			err(EXIT_FAILURE, _("failed to parse list '%s'"), str0);
		includes[nincludes++] = n;

		if (nincludes == ARRAY_SIZE(includes))
			/* TRANSLATORS: The standard value for %d is 256. */
			errx(EXIT_FAILURE, _("the list of included devices is "
					"too large (limit is %d devices)"),
					(int)ARRAY_SIZE(includes));
		str = end && *end ? end + 1 : NULL;
	}
}

/*
 * see set_sortdata_u64() and columns initialization in main()
 */
static int cmp_u64_cells(struct libscols_cell *a,
			 struct libscols_cell *b,
			 __attribute__((__unused__)) void *data)
{
	uint64_t *adata = (uint64_t *) scols_cell_get_userdata(a),
		 *bdata = (uint64_t *) scols_cell_get_userdata(b);

	if (adata == NULL && bdata == NULL)
		return 0;
	if (adata == NULL)
		return -1;
	if (bdata == NULL)
		return 1;
	return *adata == *bdata ? 0 : *adata >= *bdata ? 1 : -1;
}

static void __attribute__((__noreturn__)) help(FILE *out)
{
	size_t i;

	fputs(USAGE_HEADER, out);
	fprintf(out, _(" %s [options] [<device> ...]\n"), program_invocation_short_name);

	fputs(USAGE_SEPARATOR, out);
	fputs(_("List information about block devices.\n"), out);

	fputs(USAGE_OPTIONS, out);
	fputs(_(" -a, --all            print all devices\n"), out);
	fputs(_(" -b, --bytes          print SIZE in bytes rather than in human readable format\n"), out);
	fputs(_(" -d, --nodeps         don't print slaves or holders\n"), out);
	fputs(_(" -D, --discard        print discard capabilities\n"), out);
	fputs(_(" -e, --exclude <list> exclude devices by major number (default: RAM disks)\n"), out);
	fputs(_(" -f, --fs             output info about filesystems\n"), out);
	fputs(_(" -i, --ascii          use ascii characters only\n"), out);
	fputs(_(" -I, --include <list> show only devices with specified major numbers\n"), out);
	fputs(_(" -J, --json           use JSON output format\n"), out);
	fputs(_(" -l, --list           use list format output\n"), out);
	fputs(_(" -m, --perms          output info about permissions\n"), out);
	fputs(_(" -n, --noheadings     don't print headings\n"), out);
	fputs(_(" -o, --output <list>  output columns\n"), out);
	fputs(_(" -O, --output-all     output all columns\n"), out);
	fputs(_(" -p, --paths          print complete device path\n"), out);
	fputs(_(" -P, --pairs          use key=\"value\" output format\n"), out);
	fputs(_(" -r, --raw            use raw output format\n"), out);
	fputs(_(" -s, --inverse        inverse dependencies\n"), out);
	fputs(_(" -S, --scsi           output info about SCSI devices\n"), out);
	fputs(_(" -t, --topology       output info about topology\n"), out);
	fputs(_(" -x, --sort <column>  sort output by <column>\n"), out);
	fputs(USAGE_SEPARATOR, out);
	fputs(USAGE_HELP, out);
	fputs(USAGE_VERSION, out);

	fprintf(out, _("\nAvailable columns (for --output):\n"));

	for (i = 0; i < ARRAY_SIZE(infos); i++)
		fprintf(out, " %11s  %s\n", infos[i].name, _(infos[i].help));

	fprintf(out, USAGE_MAN_TAIL("lsblk(8)"));

	exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}

static void check_sysdevblock(void)
{
	if (access(_PATH_SYS_DEVBLOCK, R_OK) != 0)
		err(EXIT_FAILURE, _("failed to access sysfs directory: %s"),
		    _PATH_SYS_DEVBLOCK);
}

int main(int argc, char *argv[])
{
	struct lsblk _ls = { .sort_id = -1 };
	int scols_flags = LSBLK_TREE;
	int c, status = EXIT_FAILURE;
	char *outarg = NULL;
	size_t i;

	static const struct option longopts[] = {
		{ "all",	0, 0, 'a' },
		{ "bytes",      0, 0, 'b' },
		{ "nodeps",     0, 0, 'd' },
		{ "discard",    0, 0, 'D' },
		{ "help",	0, 0, 'h' },
		{ "json",       0, 0, 'J' },
		{ "output",     1, 0, 'o' },
		{ "output-all", 0, 0, 'O' },
		{ "perms",      0, 0, 'm' },
		{ "noheadings",	0, 0, 'n' },
		{ "list",       0, 0, 'l' },
		{ "ascii",	0, 0, 'i' },
		{ "raw",        0, 0, 'r' },
		{ "inverse",	0, 0, 's' },
		{ "fs",         0, 0, 'f' },
		{ "exclude",    1, 0, 'e' },
		{ "include",    1, 0, 'I' },
		{ "topology",   0, 0, 't' },
		{ "paths",      0, 0, 'p' },
		{ "pairs",      0, 0, 'P' },
		{ "scsi",       0, 0, 'S' },
		{ "sort",	1, 0, 'x' },
		{ "version",    0, 0, 'V' },
		{ NULL, 0, 0, 0 },
	};

	static const ul_excl_t excl[] = {       /* rows and cols in in ASCII order */
		{ 'D','O' },
		{ 'I','e' },
		{ 'J', 'P', 'r' },
		{ 'O','S' },
		{ 'O','f' },
		{ 'O','m' },
		{ 'O','t' },
		{ 'P','l','r' },
		{ 0 }
	};
	int excl_st[ARRAY_SIZE(excl)] = UL_EXCL_STATUS_INIT;

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	lsblk = &_ls;

	lsblk_init_debug();

	while((c = getopt_long(argc, argv,
			       "abdDe:fhJlnmo:OpPiI:rstVSx:", longopts, NULL)) != -1) {

		err_exclusive_options(c, longopts, excl, excl_st);

		switch(c) {
		case 'a':
			lsblk->all_devices = 1;
			break;
		case 'b':
			lsblk->bytes = 1;
			break;
		case 'd':
			lsblk->nodeps = 1;
			break;
		case 'D':
			add_column(columns, ncolumns++, COL_NAME);
			add_column(columns, ncolumns++, COL_DALIGN);
			add_column(columns, ncolumns++, COL_DGRAN);
			add_column(columns, ncolumns++, COL_DMAX);
			add_column(columns, ncolumns++, COL_DZERO);
			break;
		case 'e':
			parse_excludes(optarg);
			break;
		case 'h':
			help(stdout);
			break;
		case 'J':
			scols_flags |= LSBLK_JSON;
			break;
		case 'l':
			scols_flags &= ~LSBLK_TREE; /* disable the default */
			break;
		case 'n':
			scols_flags |= LSBLK_NOHEADINGS;
			break;
		case 'o':
			outarg = optarg;
			break;
		case 'O':
			for (ncolumns = 0 ; ncolumns < ARRAY_SIZE(infos); ncolumns++)
				columns[ncolumns] = ncolumns;
			break;
		case 'p':
			lsblk->paths = 1;
			break;
		case 'P':
			scols_flags |= LSBLK_EXPORT;
			scols_flags &= ~LSBLK_TREE;	/* disable the default */
			break;
		case 'i':
			scols_flags |= LSBLK_ASCII;
			break;
		case 'I':
			parse_includes(optarg);
			break;
		case 'r':
			scols_flags &= ~LSBLK_TREE;	/* disable the default */
			scols_flags |= LSBLK_RAW;		/* enable raw */
			break;
		case 's':
			lsblk->inverse = 1;
			break;
		case 'f':
			add_column(columns, ncolumns++, COL_NAME);
			add_column(columns, ncolumns++, COL_FSTYPE);
			add_column(columns, ncolumns++, COL_LABEL);
			add_column(columns, ncolumns++, COL_UUID);
			add_column(columns, ncolumns++, COL_TARGET);
			break;
		case 'm':
			add_column(columns, ncolumns++, COL_NAME);
			add_column(columns, ncolumns++, COL_SIZE);
			add_column(columns, ncolumns++, COL_OWNER);
			add_column(columns, ncolumns++, COL_GROUP);
			add_column(columns, ncolumns++, COL_MODE);
			break;
		case 't':
			add_column(columns, ncolumns++, COL_NAME);
			add_column(columns, ncolumns++, COL_ALIOFF);
			add_column(columns, ncolumns++, COL_MINIO);
			add_column(columns, ncolumns++, COL_OPTIO);
			add_column(columns, ncolumns++, COL_PHYSEC);
			add_column(columns, ncolumns++, COL_LOGSEC);
			add_column(columns, ncolumns++, COL_ROTA);
			add_column(columns, ncolumns++, COL_SCHED);
			add_column(columns, ncolumns++, COL_RQ_SIZE);
			add_column(columns, ncolumns++, COL_RA);
			add_column(columns, ncolumns++, COL_WSAME);
			break;
		case 'S':
			lsblk->nodeps = 1;
			lsblk->scsi = 1;
			add_column(columns, ncolumns++, COL_NAME);
			add_column(columns, ncolumns++, COL_HCTL);
			add_column(columns, ncolumns++, COL_TYPE);
			add_column(columns, ncolumns++, COL_VENDOR);
			add_column(columns, ncolumns++, COL_MODEL);
			add_column(columns, ncolumns++, COL_REV);
			add_column(columns, ncolumns++, COL_TRANSPORT);
			break;
		case 'V':
			printf(UTIL_LINUX_VERSION);
			return EXIT_SUCCESS;
		case 'x':
			scols_flags &= ~LSBLK_TREE; /* disable the default */
			lsblk->sort_id = column_name_to_id(optarg, strlen(optarg));
			if (lsblk->sort_id >= 0)
				break;
			/* fallthrough */
		default:
			help(stderr);
		}
	}

	check_sysdevblock();

	if (!ncolumns) {
		add_column(columns, ncolumns++, COL_NAME);
		add_column(columns, ncolumns++, COL_MAJMIN);
		add_column(columns, ncolumns++, COL_RM);
		add_column(columns, ncolumns++, COL_SIZE);
		add_column(columns, ncolumns++, COL_RO);
		add_column(columns, ncolumns++, COL_TYPE);
		add_column(columns, ncolumns++, COL_TARGET);
	}

	if (outarg && string_add_to_idarray(outarg, columns, ARRAY_SIZE(columns),
					 &ncolumns, column_name_to_id) < 0)
		return EXIT_FAILURE;

	if (nexcludes == 0 && nincludes == 0)
		excludes[nexcludes++] = 1;	/* default: ignore RAM disks */

	if (lsblk->sort_id < 0)
		/* Since Linux 4.8 we have sort devices by default, because
		 * /sys is no more sorted */
		lsblk->sort_id = COL_MAJMIN;

	if (lsblk->sort_id >= 0 && column_id_to_number(lsblk->sort_id) < 0) {
		/* the sort column is not between output columns -- add as hidden */
		add_column(columns, ncolumns++, lsblk->sort_id);
		lsblk->sort_hidden = 1;
	}

	mnt_init_debug(0);
	scols_init_debug(0);

	/*
	 * initialize output columns
	 */
	if (!(lsblk->table = scols_new_table()))
		errx(EXIT_FAILURE, _("failed to initialize output table"));
	scols_table_enable_raw(lsblk->table, !!(scols_flags & LSBLK_RAW));
	scols_table_enable_export(lsblk->table, !!(scols_flags & LSBLK_EXPORT));
	scols_table_enable_ascii(lsblk->table, !!(scols_flags & LSBLK_ASCII));
	scols_table_enable_json(lsblk->table, !!(scols_flags & LSBLK_JSON));
	scols_table_enable_noheadings(lsblk->table, !!(scols_flags & LSBLK_NOHEADINGS));

	if (scols_flags & LSBLK_JSON)
		scols_table_set_name(lsblk->table, "blockdevices");

	for (i = 0; i < ncolumns; i++) {
		struct colinfo *ci = get_column_info(i);
		struct libscols_column *cl;
		int id = get_column_id(i), fl = ci->flags;

		if (!(scols_flags & LSBLK_TREE) && id == COL_NAME)
			fl &= ~SCOLS_FL_TREE;
		if (lsblk->sort_hidden && lsblk->sort_id == id)
			fl |= SCOLS_FL_HIDDEN;

		cl = scols_table_new_column(lsblk->table, ci->name, ci->whint, fl);
		if (!cl) {
			warn(_("failed to initialize output column"));
			goto leave;
		}
		if (!lsblk->sort_col && lsblk->sort_id == id) {
			lsblk->sort_col = cl;
			scols_column_set_cmpfunc(cl,
				ci->sort_type == SORT_STRING ?
				scols_cmpstr_cells : cmp_u64_cells, NULL);
		}
	}

	if (optind == argc)
		status = iterate_block_devices() == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
	else {
		int cnt = 0, cnt_err = 0;

		while (optind < argc) {
			if (process_one_device(argv[optind++]) != 0)
				cnt_err++;
			cnt++;
		}
		status = cnt == 0	? EXIT_FAILURE :	/* nothing */
			 cnt == cnt_err	? LSBLK_EXIT_ALLFAILED :/* all failed */
			 cnt_err	? LSBLK_EXIT_SOMEOK :	/* some ok */
					  EXIT_SUCCESS;		/* all success */
	}

	if (lsblk->sort_col)
		scols_sort_table(lsblk->table, lsblk->sort_col);

	scols_print_table(lsblk->table);

leave:
	if (lsblk->sort_col)
		unref_sortdata(lsblk->table);

	scols_unref_table(lsblk->table);

	mnt_unref_table(mtab);
	mnt_unref_table(swaps);
	mnt_unref_cache(mntcache);
#ifdef HAVE_LIBUDEV
	udev_unref(udev);
#endif
	return status;
}
