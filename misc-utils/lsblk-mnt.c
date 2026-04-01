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
 * Multi-device filesystem groups (btrfs, etc.)
 *
 * /proc/self/mountinfo lists only one member device per mount, so other
 * devices appear unmounted in lsblk. We use /sys/fs/<type>/ to discover
 * multi-device groups and propagate mount entries to all members.
 */
struct fs_devs_group {
	char **members;		/* kernel device names (e.g. "sdc1") */
	size_t nmembers;
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

/* Add a new member device name to the group */
static void fs_devs_add_member(struct fs_devs_group *grp, const char *name)
{
	grp->members = xreallocarray(grp->members,
			grp->nmembers + 1, sizeof(char *));
	grp->members[grp->nmembers] = xstrdup(name);
	grp->nmembers++;
}

/* Free all members in the group */
static void fs_devs_free_members(struct fs_devs_group *grp)
{
	size_t i;

	for (i = 0; i < grp->nmembers; i++)
		free(grp->members[i]);
	free(grp->members);
	grp->members = NULL;
	grp->nmembers = 0;
}

/* Register a new multi-device group */
static void fs_devs_add_group(struct fs_devs_group *grp)
{
	fs_devs_groups = xreallocarray(fs_devs_groups,
			fs_devs_ngroups + 1, sizeof(struct fs_devs_group));
	fs_devs_groups[fs_devs_ngroups] = *grp;
	fs_devs_ngroups++;
}

/* Find the group that contains the given device name */
static struct fs_devs_group *fs_devs_find_group(const char *name)
{
	size_t i, j;

	for (i = 0; i < fs_devs_ngroups; i++) {
		for (j = 0; j < fs_devs_groups[i].nmembers; j++) {
			if (strcmp(fs_devs_groups[i].members[j], name) == 0)
				return &fs_devs_groups[i];
		}
	}
	return NULL;
}

/* Search mountinfo for peer devices in the group and add their
 * mount entries to the device. This extends add_filesystem() for
 * multi-device setups where only one member appears in mountinfo. */
static void fs_devs_add_filesystem(struct lsblk_device *dev,
				   struct fs_devs_group *grp)
{
	struct libmnt_iter *itr;
	size_t j;

	itr = mnt_new_iter(MNT_ITER_BACKWARD);
	if (!itr)
		return;

	for (j = 0; j < grp->nmembers; j++) {
		struct libmnt_fs *fs;
		char devpath[PATH_MAX];

		if (strcmp(grp->members[j], dev->name) == 0)
			continue;

		snprintf(devpath, sizeof(devpath), "/dev/%s",
				grp->members[j]);
		mnt_reset_iter(itr, MNT_ITER_BACKWARD);

		while (mnt_table_next_fs(mtab, itr, &fs) == 0) {
			if (mnt_fs_streq_srcpath(fs, devpath))
				add_filesystem(dev, fs);
		}
	}
	mnt_free_iter(itr);
}

/* Scan /sys/fs/btrfs/<uuid>/devices/ and register multi-device groups */
static void fs_devs_scan_btrfs(void)
{
	DIR *dir, *devdir;
	struct dirent *d, *dd;
	char path[PATH_MAX];
	const char *prefix = lsblk->sysroot ? lsblk->sysroot : "";

	snprintf(path, sizeof(path), "%s/sys/fs/btrfs", prefix);
	dir = opendir(path);
	if (!dir)
		return;

	while ((d = xreaddir(dir)) != NULL) {
		struct fs_devs_group grp = { .members = NULL, .nmembers = 0 };

		snprintf(path, sizeof(path), "%s/sys/fs/btrfs/%s/devices",
				prefix, d->d_name);
		devdir = opendir(path);
		if (!devdir)
			continue;

		while ((dd = xreaddir(devdir)) != NULL)
			fs_devs_add_member(&grp, dd->d_name);
		closedir(devdir);

		if (grp.nmembers > 1)
			fs_devs_add_group(&grp);
		else
			fs_devs_free_members(&grp);
	}
	closedir(dir);
}

/* Scan all supported multi-device filesystem types */
static void fs_devs_scan(void)
{
	fs_devs_scanned = 1;
	fs_devs_scan_btrfs();
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

	grp = fs_devs_find_group(dev->name);
	if (grp)
		fs_devs_add_filesystem(dev, grp);
}

/* Free all cached multi-device groups */
static void fs_devs_free(void)
{
	size_t i;

	for (i = 0; i < fs_devs_ngroups; i++)
		fs_devs_free_members(&fs_devs_groups[i]);
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
