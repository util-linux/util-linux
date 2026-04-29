#include <dirent.h>

#include "c.h"
#include "fileutils.h"
#include "pathnames.h"
#include "xalloc.h"
#include "nls.h"

#include "lsblk.h"

static struct libmnt_table *mtab, *swaps;
static struct libmnt_cache *mntcache;

/*
 * Multi-device filesystem groups (btrfs, zfs, etc.)
 *
 * /proc/self/mountinfo lists only one member device per mount, so other
 * devices appear unmounted in lsblk. We use /sys/fs/<type>/ or mountinfo
 * to discover multi-device groups and propagate mount entries to all members.
 */
enum fs_devs_type {
	FSTYPE_BTRFS = 1,
	FSTYPE_ZFS = 2,
};

struct fs_devs_group {
	char **devnames;	/* device names (e.g. "sdc1") */
	size_t ndevnames;

	struct libmnt_fs **fss;	/* cached mount entries from mountinfo */
	size_t nfss;

	int fstype;		/* FSTYPE_BTRFS or FSTYPE_ZFS */
	char *identifier;	/* UUID or pool name */
};

static struct fs_devs_group *fs_devs_groups;
static size_t fs_devs_ngroups;
static int fs_devs_scanned;

static int table_parser_errcb(struct libmnt_table *tb __attribute__((__unused__)),
			const char *filename, int line)
{
	if (filename)
		warnx(_("%s: parse error at line %d -- ignored"), filename, line);
	return 1;
}

static struct libmnt_fs *get_active_swap(const char *filename)
{
	assert(filename);

	if (!swaps) {
		swaps = mnt_new_table();
		if (!swaps)
			return 0;
		if (!mntcache)
			mntcache = mnt_new_cache();

		mnt_table_set_parser_errcb(swaps, table_parser_errcb);
		mnt_table_set_cache(swaps, mntcache);

		if (!lsblk->sysroot)
			mnt_table_parse_swaps(swaps, NULL);
		else {
			char buf[PATH_MAX];
			snprintf(buf, sizeof(buf), "%s" _PATH_PROC_SWAPS, lsblk->sysroot);
			mnt_table_parse_swaps(swaps, buf);
		}
	}

	return mnt_table_find_srcpath(swaps, filename, MNT_ITER_BACKWARD);
}

void lsblk_device_free_filesystems(struct lsblk_device *dev)
{
	if (!dev)
		return;

	free(dev->fss);

	dev->fss = NULL;
	dev->nfss = 0;
	dev->is_mounted = 0;
	dev->is_swap = 0;
}

static void add_filesystem(struct lsblk_device *dev, struct libmnt_fs *fs)
{
	assert(dev);
	assert(fs);

	dev->fss = xreallocarray(dev->fss, dev->nfss + 1, sizeof(struct libmnt_fs *));
	dev->fss[dev->nfss] = fs;
	dev->nfss++;
	dev->is_mounted = 1;

	if (mnt_fs_is_swaparea(fs))
		dev->is_swap = 1;
}

/* Add a device name to the group */
static void fs_devs_add_devname(struct fs_devs_group *grp, const char *name)
{
	grp->devnames = xreallocarray(grp->devnames,
			grp->ndevnames + 1, sizeof(char *));
	grp->devnames[grp->ndevnames] = xstrdup(name);
	grp->ndevnames++;
}

/* Cache a mount entry in the group */
static void fs_devs_add_filesystem(struct fs_devs_group *grp, struct libmnt_fs *fs)
{
	grp->fss = xreallocarray(grp->fss,
			grp->nfss + 1, sizeof(struct libmnt_fs *));
	mnt_ref_fs(fs);
	grp->fss[grp->nfss] = fs;
	grp->nfss++;
}

/* Free all device names in the group */
static void fs_devs_free_devnames(struct fs_devs_group *grp)
{
	size_t i;

	for (i = 0; i < grp->ndevnames; i++)
		free(grp->devnames[i]);
	free(grp->devnames);
	grp->devnames = NULL;
	grp->ndevnames = 0;
}

/* Free all cached mount entries in the group */
static void fs_devs_free_filesystems(struct fs_devs_group *grp)
{
	size_t i;

	for (i = 0; i < grp->nfss; i++)
		mnt_unref_fs(grp->fss[i]);
	free(grp->fss);
	grp->fss = NULL;
	grp->nfss = 0;
}

/* Register a new multi-device group */
static struct fs_devs_group *fs_devs_add_group(struct fs_devs_group *grp)
{
	fs_devs_groups = xreallocarray(fs_devs_groups,
			fs_devs_ngroups + 1, sizeof(struct fs_devs_group));
	fs_devs_groups[fs_devs_ngroups] = *grp;
	return &fs_devs_groups[fs_devs_ngroups++];
}

/* Find group by fstype and identifier */
static struct fs_devs_group *fs_devs_get_group(int fstype, const char *identifier)
{
	size_t i;

	for (i = 0; i < fs_devs_ngroups; i++) {
		if (fs_devs_groups[i].fstype == fstype
		    && fs_devs_groups[i].identifier
		    && strcmp(fs_devs_groups[i].identifier, identifier) == 0)
			return &fs_devs_groups[i];
	}
	return NULL;
}

/* Find the group that contains the given device */
static struct fs_devs_group *fs_devs_find_group(struct lsblk_device *dev)
{
	size_t i, j;

	for (i = 0; i < fs_devs_ngroups; i++) {
		struct fs_devs_group *grp = &fs_devs_groups[i];

		switch (grp->fstype) {
		case FSTYPE_BTRFS:
			for (j = 0; j < grp->ndevnames; j++) {
				if (strcmp(grp->devnames[j], dev->name) == 0)
					return grp;
			}
			break;
		case FSTYPE_ZFS:
		{
			struct lsblk_devprop *prop = lsblk_device_get_properties(dev);

			if (prop && prop->fstype && prop->label
			    && strcmp(prop->fstype, "zfs_member") == 0
			    && strcmp(prop->label, grp->identifier) == 0)
				return grp;
			break;
		}
		}
	}
	return NULL;
}

/* Propagate cached mount entries from the group to the device */
static void fs_devs_apply_group(struct lsblk_device *dev,
				struct fs_devs_group *grp)
{
	size_t i;

	for (i = 0; i < grp->nfss; i++)
		add_filesystem(dev, grp->fss[i]);
}

/* Scan /sys/fs/btrfs/<uuid>/devices/ and register multi-device groups */
static void fs_devs_scan_btrfs(void)
{
	DIR *dir, *devdir;
	struct dirent *d, *dd;
	char path[PATH_MAX];
	const char *prefix = lsblk->sysroot ? lsblk->sysroot : "";
	size_t i;

	snprintf(path, sizeof(path), "%s/sys/fs/btrfs", prefix);
	dir = opendir(path);
	if (!dir)
		return;

	while ((d = xreaddir(dir)) != NULL) {
		struct fs_devs_group *grp;

		snprintf(path, sizeof(path), "%s/sys/fs/btrfs/%s/devices",
				prefix, d->d_name);
		devdir = opendir(path);
		if (!devdir)
			continue;

		/* Skip single-device filesystems */
		if (!xreaddir(devdir) || !xreaddir(devdir)) {
			closedir(devdir);
			continue;
		}
		rewinddir(devdir);

		grp = fs_devs_get_group(FSTYPE_BTRFS, d->d_name);
		if (!grp) {
			struct fs_devs_group new_grp = {
				.fstype = FSTYPE_BTRFS,
				.identifier = xstrdup(d->d_name)
			};
			grp = fs_devs_add_group(&new_grp);
		}

		while ((dd = xreaddir(devdir)) != NULL)
			fs_devs_add_devname(grp, dd->d_name);
		closedir(devdir);
	}
	closedir(dir);

	/* Cache mount entries for all btrfs groups. Search mountinfo
	 * for entries where source matches any member device. */
	assert(mtab);

	for (i = 0; i < fs_devs_ngroups; i++) {
		struct fs_devs_group *grp = &fs_devs_groups[i];
		struct libmnt_iter *itr;
		struct libmnt_fs *fs;
		size_t j;

		if (grp->fstype != FSTYPE_BTRFS)
			continue;

		itr = mnt_new_iter(MNT_ITER_BACKWARD);
		if (!itr)
			return;

		for (j = 0; j < grp->ndevnames; j++) {
			char devpath[PATH_MAX];

			snprintf(devpath, sizeof(devpath), "/dev/%s",
					grp->devnames[j]);
			mnt_reset_iter(itr, MNT_ITER_BACKWARD);

			while (mnt_table_next_fs(mtab, itr, &fs) == 0) {
				if (mnt_fs_streq_srcpath(fs, devpath))
					fs_devs_add_filesystem(grp, fs);
			}
		}
		mnt_free_iter(itr);
	}
}

/* Scan mountinfo for ZFS pools and register multi-device groups.
 * ZFS has no sysfs device enumeration, so we collect unique pool
 * names from mounted ZFS filesystems. Device membership is resolved
 * later in fs_devs_find_group() via blkid properties. */
static void fs_devs_scan_zfs(void)
{
	struct libmnt_iter *itr;
	struct libmnt_fs *fs;

	assert(mtab);

	itr = mnt_new_iter(MNT_ITER_BACKWARD);
	if (!itr)
		return;

	while (mnt_table_next_fs(mtab, itr, &fs) == 0) {
		const char *fstype = mnt_fs_get_fstype(fs);
		const char *source;
		char *pool_name;
		struct fs_devs_group *grp;

		if (!fstype || strcmp(fstype, "zfs") != 0)
			continue;
		source = mnt_fs_get_source(fs);
		if (!source || !*source)
			continue;

		/* Extract pool name (source is "pool" or "pool/dataset") */
		pool_name = xstrndup(source, strcspn(source, "/"));

		/* Get or create group for this pool, cache mount entry */
		grp = fs_devs_get_group(FSTYPE_ZFS, pool_name);
		if (!grp) {
			struct fs_devs_group new_grp = {
				.fstype = FSTYPE_ZFS,
				.identifier = pool_name
			};
			grp = fs_devs_add_group(&new_grp);
		} else {
			free(pool_name);
		}
		fs_devs_add_filesystem(grp, fs);
	}
	mnt_free_iter(itr);
}

/* Scan all supported multi-device filesystem types */
static void fs_devs_scan(void)
{
	if (!mtab)
		return;
	fs_devs_scanned = 1;
	fs_devs_scan_btrfs();
	fs_devs_scan_zfs();
}

/* Try to find mount entries via multi-device filesystem group membership.
 * Called when normal devno/srcpath matching found nothing in mountinfo. */
static void fs_devs_add_mounts(struct lsblk_device *dev)
{
	struct fs_devs_group *grp;

	if (!fs_devs_scanned)
		fs_devs_scan();
	if (!fs_devs_ngroups)
		return;

	grp = fs_devs_find_group(dev);
	if (grp)
		fs_devs_apply_group(dev, grp);
}

/* Free all cached multi-device groups */
static void fs_devs_free(void)
{
	size_t i;

	for (i = 0; i < fs_devs_ngroups; i++) {
		fs_devs_free_devnames(&fs_devs_groups[i]);
		fs_devs_free_filesystems(&fs_devs_groups[i]);
		free(fs_devs_groups[i].identifier);
	}
	free(fs_devs_groups);
	fs_devs_groups = NULL;
	fs_devs_ngroups = 0;
	fs_devs_scanned = 0;
}

struct libmnt_fs **lsblk_device_get_filesystems(struct lsblk_device *dev, size_t *n)
{
	struct libmnt_fs *fs;
	struct libmnt_iter *itr = NULL;
	dev_t devno;

	assert(dev);
	assert(dev->filename);

	if (dev->is_mounted)
		goto done;

	lsblk_device_free_filesystems(dev);	/* reset */

	if (!mtab) {
		mtab = mnt_new_table();
		if (!mtab)
			return NULL;
		if (!mntcache)
			mntcache = mnt_new_cache();

		mnt_table_set_parser_errcb(mtab, table_parser_errcb);
		mnt_table_set_cache(mtab, mntcache);

		if (!lsblk->sysroot)
			mnt_table_parse_mtab(mtab, NULL);
		else {
			char buf[PATH_MAX];
			snprintf(buf, sizeof(buf), "%s" _PATH_PROC_MOUNTINFO, lsblk->sysroot);
			mnt_table_parse_mtab(mtab, buf);
		}
	}

	devno = makedev(dev->maj, dev->min);

	/* All mountpoint where is used devno or device name
	 */
	itr = mnt_new_iter(MNT_ITER_BACKWARD);
	while (mnt_table_next_fs(mtab, itr, &fs) == 0) {
		if (mnt_fs_get_devno(fs) != devno &&
		    !mnt_fs_streq_srcpath(fs, dev->filename))
			continue;
		add_filesystem(dev, fs);
	}

	/* Try mnt_table_find_srcpath() which also canonicalizes patches, etc.
	 */
	if (!dev->nfss) {
		fs = get_active_swap(dev->filename);
		if (!fs) {
			fs = mnt_table_find_srcpath(mtab, dev->filename, MNT_ITER_BACKWARD);
			if (fs)
				dev->is_swap = 1;
		}
		if (fs)
			add_filesystem(dev, fs);
	}

	/* Try multi-device filesystem group (btrfs RAID, etc.) -- the device
	 * may be a member of a group where only one device is in mountinfo */
	if (!dev->nfss)
		fs_devs_add_mounts(dev);

done:
	mnt_free_iter(itr);
	if (n)
		*n = dev->nfss;
	return dev->fss;
}

/* Returns mountpoint where the device is mounted. If the device is used for
 * more filesystems (subvolumes, ...) than returns the "best" one.
 */
const char *lsblk_device_get_mountpoint(struct lsblk_device *dev)
{
	struct libmnt_fs *fs = NULL;
	const char *root;

	lsblk_device_get_filesystems(dev, NULL);
	if (!dev->nfss)
		return NULL;

	/* lsblk_device_get_filesystems() scans mountinfo/swaps in backward
	 * order. It means the first in fss[] is the last mounted FS. Let's
	 * keep it as default */
	fs = dev->fss[0];
	root = mnt_fs_get_root(fs);

	if (root && strcmp(root, "/") != 0) {
		/* FS is subvolume (or subdirectory bind-mount). Try to get
		 * FS with "/" root */
		size_t i;

		for (i = 1; i < dev->nfss; i++) {
			root = mnt_fs_get_root(dev->fss[i]);
			if (!root || strcmp(root, "/") == 0) {
				fs = dev->fss[i];
				break;
			}
		}
	}
	if (mnt_fs_is_swaparea(fs)) {
		dev->is_swap = 1;
		return "[SWAP]";
	}
	return mnt_fs_get_target(fs);
}

void lsblk_mnt_init(void)
{
	mnt_init_debug(0);
}

void lsblk_mnt_deinit(void)
{
	mnt_unref_table(mtab);
	mnt_unref_table(swaps);
	mnt_unref_cache(mntcache);
	fs_devs_free();
}
