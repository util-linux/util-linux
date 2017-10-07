
/*
 * No copyright is claimed.  This code is in the public domain; do with
 * it what you wish.
 *
 * Written by Karel Zak <kzak@redhat.com>
 *
 * -- based on mount/losetup.c
 *
 * Simple library for work with loop devices.
 *
 *  - requires kernel 2.6.x
 *  - reads info from /sys/block/loop<N>/loop/<attr> (new kernels)
 *  - reads info by ioctl
 *  - supports *unlimited* number of loop devices
 *  - supports /dev/loop<N> as well as /dev/loop/<N>
 *  - minimize overhead (fd, loopinfo, ... are shared for all operations)
 *  - setup (associate device and backing file)
 *  - delete (dis-associate file)
 *  - old LOOP_{SET,GET}_STATUS (32bit) ioctls are unsupported
 *  - extendible
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <inttypes.h>
#include <dirent.h>

#include "linux_version.h"
#include "c.h"
#include "sysfs.h"
#include "pathnames.h"
#include "loopdev.h"
#include "canonicalize.h"
#include "blkdev.h"
#include "debug.h"

/*
 * Debug stuff (based on include/debug.h)
 */
static UL_DEBUG_DEFINE_MASK(loopdev);
UL_DEBUG_DEFINE_MASKNAMES(loopdev) = UL_DEBUG_EMPTY_MASKNAMES;

#define LOOPDEV_DEBUG_INIT	(1 << 1)
#define LOOPDEV_DEBUG_CXT	(1 << 2)
#define LOOPDEV_DEBUG_ITER	(1 << 3)
#define LOOPDEV_DEBUG_SETUP	(1 << 4)
#define SFDISKPROG_DEBUG_ALL	0xFFFF

#define DBG(m, x)       __UL_DBG(loopdev, LOOPDEV_DEBUG_, m, x)
#define ON_DBG(m, x)    __UL_DBG_CALL(loopdev, LOOPDEV_DEBUG_, m, x)

static void loopdev_init_debug(void)
{
	if (loopdev_debug_mask)
		return;
	__UL_INIT_DEBUG(loopdev, LOOPDEV_DEBUG_, 0, LOOPDEV_DEBUG);
}

/*
 * see loopcxt_init()
 */
#define loopcxt_ioctl_enabled(_lc)	(!((_lc)->flags & LOOPDEV_FL_NOIOCTL))
#define loopcxt_sysfs_available(_lc)	(!((_lc)->flags & LOOPDEV_FL_NOSYSFS)) \
					 && !loopcxt_ioctl_enabled(_lc)

/*
 * @lc: context
 * @device: device name, absolute device path or NULL to reset the current setting
 *
 * Sets device, absolute paths (e.g. "/dev/loop<N>") are unchanged, device
 * names ("loop<N>") are converted to the path (/dev/loop<N> or to
 * /dev/loop/<N>)
 *
 * This sets the device name, but does not check if the device exists!
 *
 * Returns: <0 on error, 0 on success
 */
int loopcxt_set_device(struct loopdev_cxt *lc, const char *device)
{
	if (!lc)
		return -EINVAL;

	if (lc->fd >= 0) {
		close(lc->fd);
		DBG(CXT, ul_debugobj(lc, "closing old open fd"));
	}
	lc->fd = -1;
	lc->mode = 0;
	lc->has_info = 0;
	lc->info_failed = 0;
	*lc->device = '\0';
	memset(&lc->info, 0, sizeof(lc->info));

	/* set new */
	if (device) {
		if (*device != '/') {
			const char *dir = _PATH_DEV;

			/* compose device name for /dev/loop<n> or /dev/loop/<n> */
			if (lc->flags & LOOPDEV_FL_DEVSUBDIR) {
				if (strlen(device) < 5)
					return -1;
				device += 4;
				dir = _PATH_DEV_LOOP "/";	/* _PATH_DEV uses tailing slash */
			}
			snprintf(lc->device, sizeof(lc->device), "%s%s",
				dir, device);
		} else {
			strncpy(lc->device, device, sizeof(lc->device));
			lc->device[sizeof(lc->device) - 1] = '\0';
		}
		DBG(CXT, ul_debugobj(lc, "%s name assigned", device));
	}

	sysfs_deinit(&lc->sysfs);
	return 0;
}

int loopcxt_has_device(struct loopdev_cxt *lc)
{
	return lc && *lc->device;
}

/*
 * @lc: context
 * @flags: LOOPDEV_FL_* flags
 *
 * Initialize loop handler.
 *
 * We have two sets of the flags:
 *
 *	* LOOPDEV_FL_* flags control loopcxt_* API behavior
 *
 *	* LO_FLAGS_* are kernel flags used for LOOP_{SET,GET}_STAT64 ioctls
 *
 * Note about LOOPDEV_FL_{RDONLY,RDWR} flags. These flags are used for open(2)
 * syscall to open loop device. By default is the device open read-only.
 *
 * The expection is loopcxt_setup_device(), where the device is open read-write
 * if LO_FLAGS_READ_ONLY flags is not set (see loopcxt_set_flags()).
 *
 * Returns: <0 on error, 0 on success.
 */
int loopcxt_init(struct loopdev_cxt *lc, int flags)
{
	int rc;
	struct stat st;
	struct loopdev_cxt dummy = UL_LOOPDEVCXT_EMPTY;

	if (!lc)
		return -EINVAL;

	loopdev_init_debug();
	DBG(CXT, ul_debugobj(lc, "initialize context"));

	memcpy(lc, &dummy, sizeof(dummy));
	lc->flags = flags;

	rc = loopcxt_set_device(lc, NULL);
	if (rc)
		return rc;

	if (stat(_PATH_SYS_BLOCK, &st) || !S_ISDIR(st.st_mode)) {
		lc->flags |= LOOPDEV_FL_NOSYSFS;
		lc->flags &= ~LOOPDEV_FL_NOIOCTL;
		DBG(CXT, ul_debugobj(lc, "init: disable /sys usage"));
	}

	if (!(lc->flags & LOOPDEV_FL_NOSYSFS) &&
	    get_linux_version() >= KERNEL_VERSION(2,6,37)) {
		/*
		 * Use only sysfs for basic information about loop devices
		 */
		lc->flags |= LOOPDEV_FL_NOIOCTL;
		DBG(CXT, ul_debugobj(lc, "init: ignore ioctls"));
	}

	if (!(lc->flags & LOOPDEV_FL_CONTROL) && !stat(_PATH_DEV_LOOPCTL, &st)) {
		lc->flags |= LOOPDEV_FL_CONTROL;
		DBG(CXT, ul_debugobj(lc, "init: loop-control detected "));
	}

	return 0;
}

/*
 * @lc: context
 *
 * Deinitialize loop context
 */
void loopcxt_deinit(struct loopdev_cxt *lc)
{
	int errsv = errno;

	if (!lc)
		return;

	DBG(CXT, ul_debugobj(lc, "de-initialize"));

	free(lc->filename);
	lc->filename = NULL;

	ignore_result( loopcxt_set_device(lc, NULL) );
	loopcxt_deinit_iterator(lc);

	errno = errsv;
}

/*
 * @lc: context
 *
 * Returns newly allocated device path.
 */
char *loopcxt_strdup_device(struct loopdev_cxt *lc)
{
	if (!lc || !*lc->device)
		return NULL;
	return strdup(lc->device);
}

/*
 * @lc: context
 *
 * Returns pointer device name in the @lc struct.
 */
const char *loopcxt_get_device(struct loopdev_cxt *lc)
{
	return lc && *lc->device ? lc->device : NULL;
}

/*
 * @lc: context
 *
 * Returns pointer to the sysfs context (see lib/sysfs.c)
 */
struct sysfs_cxt *loopcxt_get_sysfs(struct loopdev_cxt *lc)
{
	if (!lc || !*lc->device || (lc->flags & LOOPDEV_FL_NOSYSFS))
		return NULL;

	if (!lc->sysfs.devno) {
		dev_t devno = sysfs_devname_to_devno(lc->device, NULL);
		if (!devno) {
			DBG(CXT, ul_debugobj(lc, "sysfs: failed devname to devno"));
			return NULL;
		}
		if (sysfs_init(&lc->sysfs, devno, NULL)) {
			DBG(CXT, ul_debugobj(lc, "sysfs: init failed"));
			return NULL;
		}
	}

	return &lc->sysfs;
}

/*
 * @lc: context
 *
 * Returns: file descriptor to the open loop device or <0 on error. The mode
 *          depends on LOOPDEV_FL_{RDWR,RDONLY} context flags. Default is
 *          read-only.
 */
int loopcxt_get_fd(struct loopdev_cxt *lc)
{
	if (!lc || !*lc->device)
		return -EINVAL;

	if (lc->fd < 0) {
		lc->mode = lc->flags & LOOPDEV_FL_RDWR ? O_RDWR : O_RDONLY;
		lc->fd = open(lc->device, lc->mode | O_CLOEXEC);
		DBG(CXT, ul_debugobj(lc, "open %s [%s]: %m", lc->device,
				lc->flags & LOOPDEV_FL_RDWR ? "rw" : "ro"));
	}
	return lc->fd;
}

int loopcxt_set_fd(struct loopdev_cxt *lc, int fd, int mode)
{
	if (!lc)
		return -EINVAL;

	lc->fd = fd;
	lc->mode = mode;
	return 0;
}

/*
 * @lc: context
 * @flags: LOOPITER_FL_* flags
 *
 * Iterator allows to scan list of the free or used loop devices.
 *
 * Returns: <0 on error, 0 on success
 */
int loopcxt_init_iterator(struct loopdev_cxt *lc, int flags)
{
	struct loopdev_iter *iter;
	struct stat st;

	if (!lc)
		return -EINVAL;


	iter = &lc->iter;
	DBG(ITER, ul_debugobj(iter, "initialize"));

	/* always zeroize
	 */
	memset(iter, 0, sizeof(*iter));
	iter->ncur = -1;
	iter->flags = flags;
	iter->default_check = 1;

	if (!lc->extra_check) {
		/*
		 * Check for /dev/loop/<N> subdirectory
		 */
		if (!(lc->flags & LOOPDEV_FL_DEVSUBDIR) &&
		    stat(_PATH_DEV_LOOP, &st) == 0 && S_ISDIR(st.st_mode))
			lc->flags |= LOOPDEV_FL_DEVSUBDIR;

		lc->extra_check = 1;
	}
	return 0;
}

/*
 * @lc: context
 *
 * Returns: <0 on error, 0 on success
 */
int loopcxt_deinit_iterator(struct loopdev_cxt *lc)
{
	struct loopdev_iter *iter;

	if (!lc)
		return -EINVAL;

	iter = &lc->iter;
	DBG(ITER, ul_debugobj(iter, "de-initialize"));

	free(iter->minors);
	if (iter->proc)
		fclose(iter->proc);
	if (iter->sysblock)
		closedir(iter->sysblock);

	memset(iter, 0, sizeof(*iter));
	return 0;
}

/*
 * Same as loopcxt_set_device, but also checks if the device is
 * associeted with any file.
 *
 * Returns: <0 on error, 0 on success, 1 device does not match with
 *         LOOPITER_FL_{USED,FREE} flags.
 */
static int loopiter_set_device(struct loopdev_cxt *lc, const char *device)
{
	int rc = loopcxt_set_device(lc, device);
	int used;

	if (rc)
		return rc;

	if (!(lc->iter.flags & LOOPITER_FL_USED) &&
	    !(lc->iter.flags & LOOPITER_FL_FREE))
		return 0;	/* caller does not care about device status */

	if (!is_loopdev(lc->device)) {
		DBG(ITER, ul_debugobj(&lc->iter, "%s does not exist", lc->device));
		return -errno;
	}

	DBG(ITER, ul_debugobj(&lc->iter, "%s exist", lc->device));

	used = loopcxt_get_offset(lc, NULL) == 0;

	if ((lc->iter.flags & LOOPITER_FL_USED) && used)
		return 0;

	if ((lc->iter.flags & LOOPITER_FL_FREE) && !used)
		return 0;

	DBG(ITER, ul_debugobj(&lc->iter, "failed to use %s device", lc->device));

	ignore_result( loopcxt_set_device(lc, NULL) );
	return 1;
}

static int cmpnum(const void *p1, const void *p2)
{
	return (((* (int *) p1) > (* (int *) p2)) -
			((* (int *) p1) < (* (int *) p2)));
}

/*
 * The classic scandir() is more expensive and less portable.
 * We needn't full loop device names -- loop numbers (loop<N>)
 * are enough.
 */
static int loop_scandir(const char *dirname, int **ary, int hasprefix)
{
	DIR *dir;
	struct dirent *d;
	unsigned int n, count = 0, arylen = 0;

	if (!dirname || !ary)
		return 0;

	DBG(ITER, ul_debug("scan dir: %s", dirname));

	dir = opendir(dirname);
	if (!dir)
		return 0;
	free(*ary);
	*ary = NULL;

	while((d = readdir(dir))) {
#ifdef _DIRENT_HAVE_D_TYPE
		if (d->d_type != DT_BLK && d->d_type != DT_UNKNOWN &&
		    d->d_type != DT_LNK)
			continue;
#endif
		if (!strcmp(d->d_name, ".") || !strcmp(d->d_name, ".."))
			continue;

		if (hasprefix) {
			/* /dev/loop<N> */
			if (sscanf(d->d_name, "loop%u", &n) != 1)
				continue;
		} else {
			/* /dev/loop/<N> */
			char *end = NULL;

			errno = 0;
			n = strtol(d->d_name, &end, 10);
			if (d->d_name == end || (end && *end) || errno)
				continue;
		}
		if (n < LOOPDEV_DEFAULT_NNODES)
			continue;			/* ignore loop<0..7> */

		if (count + 1 > arylen) {
			int *tmp;

			arylen += 1;

			tmp = realloc(*ary, arylen * sizeof(int));
			if (!tmp) {
				free(*ary);
				*ary = NULL;
				closedir(dir);
				return -1;
			}
			*ary = tmp;
		}
		if (*ary)
			(*ary)[count++] = n;
	}
	if (count && *ary)
		qsort(*ary, count, sizeof(int), cmpnum);

	closedir(dir);
	return count;
}

/*
 * Set the next *used* loop device according to /proc/partitions.
 *
 * Loop devices smaller than 512 bytes are invisible for this function.
 */
static int loopcxt_next_from_proc(struct loopdev_cxt *lc)
{
	struct loopdev_iter *iter = &lc->iter;
	char buf[BUFSIZ];

	DBG(ITER, ul_debugobj(iter, "scan /proc/partitions"));

	if (!iter->proc)
		iter->proc = fopen(_PATH_PROC_PARTITIONS, "r" UL_CLOEXECSTR);
	if (!iter->proc)
		return 1;

	while (fgets(buf, sizeof(buf), iter->proc)) {
		unsigned int m;
		char name[128 + 1];


		if (sscanf(buf, " %u %*s %*s %128[^\n ]",
			   &m, name) != 2 || m != LOOPDEV_MAJOR)
			continue;

		DBG(ITER, ul_debugobj(iter, "checking %s", name));

		if (loopiter_set_device(lc, name) == 0)
			return 0;
	}

	return 1;
}

/*
 * Set the next *used* loop device according to
 * /sys/block/loopN/loop/backing_file (kernel >= 2.6.37 is required).
 *
 * This is preferred method.
 */
static int loopcxt_next_from_sysfs(struct loopdev_cxt *lc)
{
	struct loopdev_iter *iter = &lc->iter;
	struct dirent *d;
	int fd;

	DBG(ITER, ul_debugobj(iter, "scanning /sys/block"));

	if (!iter->sysblock)
		iter->sysblock = opendir(_PATH_SYS_BLOCK);

	if (!iter->sysblock)
		return 1;

	fd = dirfd(iter->sysblock);

	while ((d = readdir(iter->sysblock))) {
		char name[NAME_MAX + 18 + 1];
		struct stat st;

		DBG(ITER, ul_debugobj(iter, "check %s", d->d_name));

		if (strcmp(d->d_name, ".") == 0
		    || strcmp(d->d_name, "..") == 0
		    || strncmp(d->d_name, "loop", 4) != 0)
			continue;

		snprintf(name, sizeof(name), "%s/loop/backing_file", d->d_name);
		if (fstatat(fd, name, &st, 0) != 0)
			continue;

		if (loopiter_set_device(lc, d->d_name) == 0)
			return 0;
	}

	return 1;
}

/*
 * @lc: context, has to initialized by loopcxt_init_iterator()
 *
 * Returns: 0 on success, -1 on error, 1 at the end of scanning. The details
 *          about the current loop device are available by
 *          loopcxt_get_{fd,backing_file,device,offset, ...} functions.
 */
int loopcxt_next(struct loopdev_cxt *lc)
{
	struct loopdev_iter *iter;

	if (!lc)
		return -EINVAL;


	iter = &lc->iter;
	if (iter->done)
		return 1;

	DBG(ITER, ul_debugobj(iter, "next"));

	/* A) Look for used loop devices in /proc/partitions ("losetup -a" only)
	 */
	if (iter->flags & LOOPITER_FL_USED) {
		int rc;

		if (loopcxt_sysfs_available(lc))
			rc = loopcxt_next_from_sysfs(lc);
		else
			rc = loopcxt_next_from_proc(lc);
		if (rc == 0)
			return 0;
		goto done;
	}

	/* B) Classic way, try first eight loop devices (default number
	 *    of loop devices). This is enough for 99% of all cases.
	 */
	if (iter->default_check) {
		DBG(ITER, ul_debugobj(iter, "next: default check"));
		for (++iter->ncur; iter->ncur < LOOPDEV_DEFAULT_NNODES;
							iter->ncur++) {
			char name[16];
			snprintf(name, sizeof(name), "loop%d", iter->ncur);

			if (loopiter_set_device(lc, name) == 0)
				return 0;
		}
		iter->default_check = 0;
	}

	/* C) the worst possibility, scan whole /dev or /dev/loop/<N>
	 */
	if (!iter->minors) {
		DBG(ITER, ul_debugobj(iter, "next: scanning /dev"));
		iter->nminors = (lc->flags & LOOPDEV_FL_DEVSUBDIR) ?
			loop_scandir(_PATH_DEV_LOOP, &iter->minors, 0) :
			loop_scandir(_PATH_DEV, &iter->minors, 1);
		iter->ncur = -1;
	}
	for (++iter->ncur; iter->ncur < iter->nminors; iter->ncur++) {
		char name[16];
		snprintf(name, sizeof(name), "loop%d", iter->minors[iter->ncur]);

		if (loopiter_set_device(lc, name) == 0)
			return 0;
	}
done:
	loopcxt_deinit_iterator(lc);
	return 1;
}

/*
 * @device: path to device
 */
int is_loopdev(const char *device)
{
	struct stat st;

	if (device && stat(device, &st) == 0 &&
		S_ISBLK(st.st_mode) &&
		major(st.st_rdev) == LOOPDEV_MAJOR)
		return 1;

	errno = ENODEV;
	return 0;
}

/*
 * @lc: context
 *
 * Returns result from LOOP_GET_STAT64 ioctl or NULL on error.
 */
struct loop_info64 *loopcxt_get_info(struct loopdev_cxt *lc)
{
	int fd;

	if (!lc || lc->info_failed) {
		errno = EINVAL;
		return NULL;
	}
	errno = 0;
	if (lc->has_info)
		return &lc->info;

	fd = loopcxt_get_fd(lc);
	if (fd < 0)
		return NULL;

	if (ioctl(fd, LOOP_GET_STATUS64, &lc->info) == 0) {
		lc->has_info = 1;
		lc->info_failed = 0;
		DBG(CXT, ul_debugobj(lc, "reading loop_info64 OK"));
		return &lc->info;
	}

	lc->info_failed = 1;
	DBG(CXT, ul_debugobj(lc, "reading loop_info64 FAILED"));

	return NULL;
}

/*
 * @lc: context
 *
 * Returns (allocated) string with path to the file assicieted
 * with the current loop device.
 */
char *loopcxt_get_backing_file(struct loopdev_cxt *lc)
{
	struct sysfs_cxt *sysfs = loopcxt_get_sysfs(lc);
	char *res = NULL;

	if (sysfs)
		/*
		 * This is always preffered, the loop_info64
		 * has too small buffer for the filename.
		 */
		res = sysfs_strdup(sysfs, "loop/backing_file");

	if (!res && loopcxt_ioctl_enabled(lc)) {
		struct loop_info64 *lo = loopcxt_get_info(lc);

		if (lo) {
			lo->lo_file_name[LO_NAME_SIZE - 2] = '*';
			lo->lo_file_name[LO_NAME_SIZE - 1] = '\0';
			res = strdup((char *) lo->lo_file_name);
		}
	}

	DBG(CXT, ul_debugobj(lc, "get_backing_file [%s]", res));
	return res;
}

/*
 * @lc: context
 * @offset: returns offset number for the given device
 *
 * Returns: <0 on error, 0 on success
 */
int loopcxt_get_offset(struct loopdev_cxt *lc, uint64_t *offset)
{
	struct sysfs_cxt *sysfs = loopcxt_get_sysfs(lc);
	int rc = -EINVAL;

	if (sysfs)
		rc = sysfs_read_u64(sysfs, "loop/offset", offset);

	if (rc && loopcxt_ioctl_enabled(lc)) {
		struct loop_info64 *lo = loopcxt_get_info(lc);
		if (lo) {
			if (offset)
				*offset = lo->lo_offset;
			rc = 0;
		} else
			rc = -errno;
	}

	DBG(CXT, ul_debugobj(lc, "get_offset [rc=%d]", rc));
	return rc;
}

/*
 * @lc: context
 * @blocksize: returns logical blocksize for the given device
 *
 * Returns: <0 on error, 0 on success
 */
int loopcxt_get_blocksize(struct loopdev_cxt *lc, uint64_t *blocksize)
{
	struct sysfs_cxt *sysfs = loopcxt_get_sysfs(lc);
	int rc = -EINVAL;

	if (sysfs)
		rc = sysfs_read_u64(sysfs, "queue/logical_block_size", blocksize);

	/* Fallback based on BLKSSZGET ioctl */
	if (rc) {
		int fd = loopcxt_get_fd(lc);
		int sz = 0;

		if (fd < 0)
			return -EINVAL;
		rc = blkdev_get_sector_size(fd, &sz);
		if (rc)
			return rc;

		*blocksize = sz;
	}

	DBG(CXT, ul_debugobj(lc, "get_blocksize [rc=%d]", rc));
	return rc;
}

/*
 * @lc: context
 * @sizelimit: returns size limit for the given device
 *
 * Returns: <0 on error, 0 on success
 */
int loopcxt_get_sizelimit(struct loopdev_cxt *lc, uint64_t *size)
{
	struct sysfs_cxt *sysfs = loopcxt_get_sysfs(lc);
	int rc = -EINVAL;

	if (sysfs)
		rc = sysfs_read_u64(sysfs, "loop/sizelimit", size);

	if (rc && loopcxt_ioctl_enabled(lc)) {
		struct loop_info64 *lo = loopcxt_get_info(lc);
		if (lo) {
			if (size)
				*size = lo->lo_sizelimit;
			rc = 0;
		} else
			rc = -errno;
	}

	DBG(CXT, ul_debugobj(lc, "get_sizelimit [rc=%d]", rc));
	return rc;
}

/*
 * @lc: context
 * @devno: returns encryption type
 *
 * Cryptoloop is DEPRECATED!
 *
 * Returns: <0 on error, 0 on success
 */
int loopcxt_get_encrypt_type(struct loopdev_cxt *lc, uint32_t *type)
{
	struct loop_info64 *lo = loopcxt_get_info(lc);
	int rc;

	/* not provided by sysfs */
	if (lo) {
		if (type)
			*type = lo->lo_encrypt_type;
		rc = 0;
	} else
		rc = -errno;

	DBG(CXT, ul_debugobj(lc, "get_encrypt_type [rc=%d]", rc));
	return rc;
}

/*
 * @lc: context
 * @devno: returns crypt name
 *
 * Cryptoloop is DEPRECATED!
 *
 * Returns: <0 on error, 0 on success
 */
const char *loopcxt_get_crypt_name(struct loopdev_cxt *lc)
{
	struct loop_info64 *lo = loopcxt_get_info(lc);

	if (lo)
		return (char *) lo->lo_crypt_name;

	DBG(CXT, ul_debugobj(lc, "get_crypt_name failed"));
	return NULL;
}

/*
 * @lc: context
 * @devno: returns backing file devno
 *
 * Returns: <0 on error, 0 on success
 */
int loopcxt_get_backing_devno(struct loopdev_cxt *lc, dev_t *devno)
{
	struct loop_info64 *lo = loopcxt_get_info(lc);
	int rc;

	if (lo) {
		if (devno)
			*devno = lo->lo_device;
		rc = 0;
	} else
		rc = -errno;

	DBG(CXT, ul_debugobj(lc, "get_backing_devno [rc=%d]", rc));
	return rc;
}

/*
 * @lc: context
 * @ino: returns backing file inode
 *
 * Returns: <0 on error, 0 on success
 */
int loopcxt_get_backing_inode(struct loopdev_cxt *lc, ino_t *ino)
{
	struct loop_info64 *lo = loopcxt_get_info(lc);
	int rc;

	if (lo) {
		if (ino)
			*ino = lo->lo_inode;
		rc = 0;
	} else
		rc = -errno;

	DBG(CXT, ul_debugobj(lc, "get_backing_inode [rc=%d]", rc));
	return rc;
}

/*
 * Check if the kernel supports partitioned loop devices.
 *
 * Notes:
 *   - kernels < 3.2 support partitioned loop devices and PT scanning
 *     only if max_part= module parameter is non-zero
 *
 *   - kernels >= 3.2 always support partitioned loop devices
 *
 *   - kernels >= 3.2 always support BLKPG_{ADD,DEL}_PARTITION ioctls
 *
 *   - kernels >= 3.2 enable PT scanner only if max_part= is non-zero or if the
 *     LO_FLAGS_PARTSCAN flag is set for the device. The PT scanner is disabled
 *     by default.
 *
 *  See kernel commit e03c8dd14915fabc101aa495828d58598dc5af98.
 */
int loopmod_supports_partscan(void)
{
	int rc, ret = 0;
	FILE *f;

	if (get_linux_version() >= KERNEL_VERSION(3,2,0))
		return 1;

	f = fopen("/sys/module/loop/parameters/max_part", "r" UL_CLOEXECSTR);
	if (!f)
		return 0;
	rc = fscanf(f, "%d", &ret);
	fclose(f);
	return rc == 1 ? ret : 0;
}

/*
 * @lc: context
 *
 * Returns: 1 if the partscan flags is set *or* (for old kernels) partitions
 * scanning is enabled for all loop devices.
 */
int loopcxt_is_partscan(struct loopdev_cxt *lc)
{
	struct sysfs_cxt *sysfs = loopcxt_get_sysfs(lc);

	if (sysfs) {
		/* kernel >= 3.2 */
		int fl;
		if (sysfs_read_int(sysfs, "loop/partscan", &fl) == 0)
			return fl;
	}

	/* old kernels (including kernels without loopN/loop/<flags> directory */
	return loopmod_supports_partscan();
}

/*
 * @lc: context
 *
 * Returns: 1 if the autoclear flags is set.
 */
int loopcxt_is_autoclear(struct loopdev_cxt *lc)
{
	struct sysfs_cxt *sysfs = loopcxt_get_sysfs(lc);

	if (sysfs) {
		int fl;
		if (sysfs_read_int(sysfs, "loop/autoclear", &fl) == 0)
			return fl;
	}

	if (loopcxt_ioctl_enabled(lc)) {
		struct loop_info64 *lo = loopcxt_get_info(lc);
		if (lo)
			return lo->lo_flags & LO_FLAGS_AUTOCLEAR;
	}
	return 0;
}

/*
 * @lc: context
 *
 * Returns: 1 if the readonly flags is set.
 */
int loopcxt_is_readonly(struct loopdev_cxt *lc)
{
	struct sysfs_cxt *sysfs = loopcxt_get_sysfs(lc);

	if (sysfs) {
		int fl;
		if (sysfs_read_int(sysfs, "ro", &fl) == 0)
			return fl;
	}

	if (loopcxt_ioctl_enabled(lc)) {
		struct loop_info64 *lo = loopcxt_get_info(lc);
		if (lo)
			return lo->lo_flags & LO_FLAGS_READ_ONLY;
	}
	return 0;
}

/*
 * @lc: context
 *
 * Returns: 1 if the dio flags is set.
 */
int loopcxt_is_dio(struct loopdev_cxt *lc)
{
	struct sysfs_cxt *sysfs = loopcxt_get_sysfs(lc);

	if (sysfs) {
		int fl;
		if (sysfs_read_int(sysfs, "loop/dio", &fl) == 0)
			return fl;
	}
	if (loopcxt_ioctl_enabled(lc)) {
		struct loop_info64 *lo = loopcxt_get_info(lc);
		if (lo)
			return lo->lo_flags & LO_FLAGS_DIRECT_IO;
	}
	return 0;
}

/*
 * @lc: context
 * @st: backing file stat or NULL
 * @backing_file: filename
 * @offset: offset (use LOOPDEV_FL_OFFSET if specified)
 * @sizelimit: size limit (use LOOPDEV_FL_SIZELIMIT if specified)
 * @flags: LOOPDEV_FL_{OFFSET,SIZELIMIT}
 *
 * Returns 1 if the current @lc loopdev is associated with the given backing
 * file. Note that the preferred way is to use devno and inode number rather
 * than filename. The @backing_file filename is poor solution usable in case
 * that you don't have rights to call stat().
 *
 * LOOPDEV_FL_SIZELIMIT requires LOOPDEV_FL_OFFSET being set as well.
 *
 * Don't forget that old kernels provide very restricted (in size) backing
 * filename by LOOP_GET_STAT64 ioctl only.
 */
int loopcxt_is_used(struct loopdev_cxt *lc,
		    struct stat *st,
		    const char *backing_file,
		    uint64_t offset,
		    uint64_t sizelimit,
		    int flags)
{
	ino_t ino;
	dev_t dev;

	if (!lc)
		return 0;

	DBG(CXT, ul_debugobj(lc, "checking %s vs. %s",
				loopcxt_get_device(lc),
				backing_file));

	if (st && loopcxt_get_backing_inode(lc, &ino) == 0 &&
		  loopcxt_get_backing_devno(lc, &dev) == 0) {

		if (ino == st->st_ino && dev == st->st_dev)
			goto found;

		/* don't use filename if we have devno and inode */
		return 0;
	}

	/* poor man's solution */
	if (backing_file) {
		char *name = loopcxt_get_backing_file(lc);
		int rc = name && strcmp(name, backing_file) == 0;

		free(name);
		if (rc)
			goto found;
	}

	return 0;
found:
	if (flags & LOOPDEV_FL_OFFSET) {
		uint64_t off;

		int rc = loopcxt_get_offset(lc, &off) == 0 && off == offset;

		if (rc && flags & LOOPDEV_FL_SIZELIMIT) {
			uint64_t sz;

			return loopcxt_get_sizelimit(lc, &sz) == 0 && sz == sizelimit;
		} else
			return rc;
	}
	return 1;
}

/*
 * The setting is removed by loopcxt_set_device() loopcxt_next()!
 */
int loopcxt_set_offset(struct loopdev_cxt *lc, uint64_t offset)
{
	if (!lc)
		return -EINVAL;
	lc->info.lo_offset = offset;

	DBG(CXT, ul_debugobj(lc, "set offset=%jd", offset));
	return 0;
}

/*
 * The setting is removed by loopcxt_set_device() loopcxt_next()!
 */
int loopcxt_set_sizelimit(struct loopdev_cxt *lc, uint64_t sizelimit)
{
	if (!lc)
		return -EINVAL;
	lc->info.lo_sizelimit = sizelimit;

	DBG(CXT, ul_debugobj(lc, "set sizelimit=%jd", sizelimit));
	return 0;
}

/*
 * @lc: context
 * @flags: kernel LO_FLAGS_{READ_ONLY,USE_AOPS,AUTOCLEAR} flags
 *
 * The setting is removed by loopcxt_set_device() loopcxt_next()!
 *
 * Returns: 0 on success, <0 on error.
 */
int loopcxt_set_flags(struct loopdev_cxt *lc, uint32_t flags)
{
	if (!lc)
		return -EINVAL;
	lc->info.lo_flags = flags;

	DBG(CXT, ul_debugobj(lc, "set flags=%u", (unsigned) flags));
	return 0;
}

/*
 * @lc: context
 * @filename: backing file path (the path will be canonicalized)
 *
 * The setting is removed by loopcxt_set_device() loopcxt_next()!
 *
 * Returns: 0 on success, <0 on error.
 */
int loopcxt_set_backing_file(struct loopdev_cxt *lc, const char *filename)
{
	if (!lc)
		return -EINVAL;

	lc->filename = canonicalize_path(filename);
	if (!lc->filename)
		return -errno;

	strncpy((char *)lc->info.lo_file_name, lc->filename, LO_NAME_SIZE);
	lc->info.lo_file_name[LO_NAME_SIZE- 1] = '\0';

	DBG(CXT, ul_debugobj(lc, "set backing file=%s", lc->info.lo_file_name));
	return 0;
}

/*
 * In kernels prior to v3.9, if the offset or sizelimit options
 * are used, the block device's size won't be synced automatically.
 * blockdev --getsize64 and filesystems will use the backing
 * file size until the block device has been re-opened or the
 * LOOP_SET_CAPACITY ioctl is called to sync the sizes.
 *
 * Since mount -oloop uses the LO_FLAGS_AUTOCLEAR option and passes
 * the open file descriptor to the mount system call, we need to use
 * the ioctl. Calling losetup directly doesn't have this problem since
 * it closes the device when it exits and whatever consumes the device
 * next will re-open it, causing the resync.
 */
static int loopcxt_check_size(struct loopdev_cxt *lc, int file_fd)
{
	uint64_t size, expected_size;
	int dev_fd;
	struct stat st;

	if (!lc->info.lo_offset && !lc->info.lo_sizelimit)
		return 0;

	if (fstat(file_fd, &st)) {
		DBG(CXT, ul_debugobj(lc, "failed to fstat backing file"));
		return -errno;
	}
	if (S_ISBLK(st.st_mode)) {
		if (blkdev_get_size(file_fd,
				(unsigned long long *) &expected_size)) {
			DBG(CXT, ul_debugobj(lc, "failed to determine device size"));
			return -errno;
		}
	} else
		expected_size = st.st_size;

	if (expected_size == 0 || expected_size <= lc->info.lo_offset) {
		DBG(CXT, ul_debugobj(lc, "failed to determine expected size"));
		return 0;	/* ignore this error */
	}

	if (lc->info.lo_offset > 0)
		expected_size -= lc->info.lo_offset;

	if (lc->info.lo_sizelimit > 0 && lc->info.lo_sizelimit < expected_size)
		expected_size = lc->info.lo_sizelimit;

	dev_fd = loopcxt_get_fd(lc);
	if (dev_fd < 0) {
		DBG(CXT, ul_debugobj(lc, "failed to get loop FD"));
		return -errno;
	}

	if (blkdev_get_size(dev_fd, (unsigned long long *) &size)) {
		DBG(CXT, ul_debugobj(lc, "failed to determine loopdev size"));
		return -errno;
	}

	/* It's block device, so, align to 512-byte sectors */
	if (expected_size % 512) {
		DBG(CXT, ul_debugobj(lc, "expected size misaligned to 512-byte sectors"));
		expected_size = (expected_size >> 9) << 9;
	}

	if (expected_size != size) {
		DBG(CXT, ul_debugobj(lc, "warning: loopdev and expected "
				      "size mismatch (%ju/%ju)",
				      size, expected_size));

		if (loopcxt_set_capacity(lc)) {
			/* ioctl not available */
			if (errno == ENOTTY || errno == EINVAL)
				errno = ERANGE;
			return -errno;
		}

		if (blkdev_get_size(dev_fd, (unsigned long long *) &size))
			return -errno;

		if (expected_size != size) {
			errno = ERANGE;
			DBG(CXT, ul_debugobj(lc, "failed to set loopdev size, "
					"size: %ju, expected: %ju",
					size, expected_size));
			return -errno;
		}
	}

	return 0;
}

/*
 * @lc: context
 *
 * Associate the current device (see loopcxt_{set,get}_device()) with
 * a file (see loopcxt_set_backing_file()).
 *
 * The device is initialized read-write by default. If you want read-only
 * device then set LO_FLAGS_READ_ONLY by loopcxt_set_flags(). The LOOPDEV_FL_*
 * flags are ignored and modified according to LO_FLAGS_*.
 *
 * If the device is already open by loopcxt_get_fd() then this setup device
 * function will re-open the device to fix read/write mode.
 *
 * The device is also initialized read-only if the backing file is not
 * possible to open read-write (e.g. read-only FS).
 *
 * Returns: <0 on error, 0 on success.
 */
int loopcxt_setup_device(struct loopdev_cxt *lc)
{
	int file_fd, dev_fd, mode = O_RDWR, rc = -1, cnt = 0;
	int errsv = 0;

	if (!lc || !*lc->device || !lc->filename)
		return -EINVAL;

	DBG(SETUP, ul_debugobj(lc, "device setup requested"));

	/*
	 * Open backing file and device
	 */
	if (lc->info.lo_flags & LO_FLAGS_READ_ONLY)
		mode = O_RDONLY;

	if ((file_fd = open(lc->filename, mode | O_CLOEXEC)) < 0) {
		if (mode != O_RDONLY && (errno == EROFS || errno == EACCES))
			file_fd = open(lc->filename, mode = O_RDONLY);

		if (file_fd < 0) {
			DBG(SETUP, ul_debugobj(lc, "open backing file failed: %m"));
			return -errno;
		}
	}
	DBG(SETUP, ul_debugobj(lc, "backing file open: OK"));

	if (lc->fd != -1 && lc->mode != mode) {
		DBG(SETUP, ul_debugobj(lc, "closing already open device (mode mismatch)"));
		close(lc->fd);
		lc->fd = -1;
		lc->mode = 0;
	}

	if (mode == O_RDONLY) {
		lc->flags |= LOOPDEV_FL_RDONLY;			/* open() mode */
		lc->info.lo_flags |= LO_FLAGS_READ_ONLY;	/* kernel loopdev mode */
	} else {
		lc->flags |= LOOPDEV_FL_RDWR;			/* open() mode */
		lc->info.lo_flags &= ~LO_FLAGS_READ_ONLY;
		lc->flags &= ~LOOPDEV_FL_RDONLY;
	}

	do {
		errno = 0;
		dev_fd = loopcxt_get_fd(lc);
		if (dev_fd >= 0 || lc->control_ok == 0)
			break;
		if (errno != EACCES && errno != ENOENT)
			break;
		/* We have permissions to open /dev/loop-control, but open
		 * /dev/loopN failed with EACCES, it's probably because udevd
		 * does not applied chown yet. Let's wait a moment. */
		xusleep(25000);
	} while (cnt++ < 16);

	if (dev_fd < 0) {
		rc = -errno;
		goto err;
	}

	DBG(SETUP, ul_debugobj(lc, "device open: OK"));

	/*
	 * Set FD
	 */
	if (ioctl(dev_fd, LOOP_SET_FD, file_fd) < 0) {
		rc = -errno;
		errsv = errno;
		DBG(SETUP, ul_debugobj(lc, "LOOP_SET_FD failed: %m"));
		goto err;
	}

	DBG(SETUP, ul_debugobj(lc, "LOOP_SET_FD: OK"));

	if (ioctl(dev_fd, LOOP_SET_STATUS64, &lc->info)) {
		rc = -errno;
		errsv = errno;
		DBG(SETUP, ul_debugobj(lc, "LOOP_SET_STATUS64 failed: %m"));
		goto err;
	}

	DBG(SETUP, ul_debugobj(lc, "LOOP_SET_STATUS64: OK"));

	if ((rc = loopcxt_check_size(lc, file_fd)))
		goto err;

	close(file_fd);

	memset(&lc->info, 0, sizeof(lc->info));
	lc->has_info = 0;
	lc->info_failed = 0;

	DBG(SETUP, ul_debugobj(lc, "success [rc=0]"));
	return 0;
err:
	if (file_fd >= 0)
		close(file_fd);
	if (dev_fd >= 0 && rc != -EBUSY)
		ioctl(dev_fd, LOOP_CLR_FD, 0);
	if (errsv)
		errno = errsv;

	DBG(SETUP, ul_debugobj(lc, "failed [rc=%d]", rc));
	return rc;
}

/*
 * @lc: context
 *
 * Update status of the current device (see loopcxt_{set,get}_device()).
 *
 * Note that once initialized, kernel accepts only selected changes:
 * LO_FLAGS_AUTOCLEAR and LO_FLAGS_PARTSCAN
 * For more see linux/drivers/block/loop.c:loop_set_status()
 *
 * Returns: <0 on error, 0 on success.
 */
int loopcxt_set_status(struct loopdev_cxt *lc)
{
	int dev_fd, rc = -1;

	errno = 0;
	dev_fd = loopcxt_get_fd(lc);

	if (dev_fd < 0) {
		rc = -errno;
		return rc;
	}
	DBG(SETUP, ul_debugobj(lc, "device open: OK"));

	if (ioctl(dev_fd, LOOP_SET_STATUS64, &lc->info)) {
		rc = -errno;
		DBG(SETUP, ul_debugobj(lc, "LOOP_SET_STATUS64 failed: %m"));
		return rc;
	}

	DBG(SETUP, ul_debugobj(lc, "LOOP_SET_STATUS64: OK"));
	return 0;
}

int loopcxt_set_capacity(struct loopdev_cxt *lc)
{
	int fd = loopcxt_get_fd(lc);

	if (fd < 0)
		return -EINVAL;

	/* Kernels prior to v2.6.30 don't support this ioctl */
	if (ioctl(fd, LOOP_SET_CAPACITY, 0) < 0) {
		int rc = -errno;
		DBG(CXT, ul_debugobj(lc, "LOOP_SET_CAPACITY failed: %m"));
		return rc;
	}

	DBG(CXT, ul_debugobj(lc, "capacity set"));
	return 0;
}

int loopcxt_set_dio(struct loopdev_cxt *lc, unsigned long use_dio)
{
	int fd = loopcxt_get_fd(lc);

	if (fd < 0)
		return -EINVAL;

	/* Kernels prior to v4.4 don't support this ioctl */
	if (ioctl(fd, LOOP_SET_DIRECT_IO, use_dio) < 0) {
		int rc = -errno;
		DBG(CXT, ul_debugobj(lc, "LOOP_SET_DIRECT_IO failed: %m"));
		return rc;
	}

	DBG(CXT, ul_debugobj(lc, "direct io set"));
	return 0;
}

/*
 * Kernel uses "unsigned long" as ioctl arg, but we use u64 for all sizes to
 * keep loopdev internal API simple.
 */
int loopcxt_set_blocksize(struct loopdev_cxt *lc, uint64_t blocksize)
{
	int fd = loopcxt_get_fd(lc);

	if (fd < 0)
		return -EINVAL;

	/* Kernels prior to v4.14 don't support this ioctl */
	if (ioctl(fd, LOOP_SET_BLOCK_SIZE, (unsigned long) blocksize) < 0) {
		int rc = -errno;
		DBG(CXT, ul_debugobj(lc, "LOOP_SET_BLOCK_SIZE failed: %m"));
		return rc;
	}

	DBG(CXT, ul_debugobj(lc, "logical block size set"));
	return 0;
}

int loopcxt_delete_device(struct loopdev_cxt *lc)
{
	int fd = loopcxt_get_fd(lc);

	if (fd < 0)
		return -EINVAL;

	if (ioctl(fd, LOOP_CLR_FD, 0) < 0) {
		DBG(CXT, ul_debugobj(lc, "LOOP_CLR_FD failed: %m"));
		return -errno;
	}

	DBG(CXT, ul_debugobj(lc, "device removed"));
	return 0;
}

int loopcxt_add_device(struct loopdev_cxt *lc)
{
	int rc = -EINVAL;
	int ctl, nr = -1;
	const char *p, *dev = loopcxt_get_device(lc);

	if (!dev)
		goto done;

	if (!(lc->flags & LOOPDEV_FL_CONTROL)) {
		rc = -ENOSYS;
		goto done;
	}

	p = strrchr(dev, '/');
	if (!p || (sscanf(p, "/loop%d", &nr) != 1 && sscanf(p, "/%d", &nr) != 1)
	       || nr < 0)
		goto done;

	ctl = open(_PATH_DEV_LOOPCTL, O_RDWR|O_CLOEXEC);
	if (ctl >= 0) {
		DBG(CXT, ul_debugobj(lc, "add_device %d", nr));
		rc = ioctl(ctl, LOOP_CTL_ADD, nr);
		close(ctl);
	}
	lc->control_ok = rc >= 0 ? 1 : 0;
done:
	DBG(CXT, ul_debugobj(lc, "add_device done [rc=%d]", rc));
	return rc;
}

/*
 * Note that LOOP_CTL_GET_FREE ioctl is supported since kernel 3.1. In older
 * kernels we have to check all loop devices to found unused one.
 *
 * See kernel commit 770fe30a46a12b6fb6b63fbe1737654d28e8484.
 */
int loopcxt_find_unused(struct loopdev_cxt *lc)
{
	int rc = -1;

	DBG(CXT, ul_debugobj(lc, "find_unused requested"));

	if (lc->flags & LOOPDEV_FL_CONTROL) {
		int ctl;

		DBG(CXT, ul_debugobj(lc, "using loop-control"));

		ctl = open(_PATH_DEV_LOOPCTL, O_RDWR|O_CLOEXEC);
		if (ctl >= 0)
			rc = ioctl(ctl, LOOP_CTL_GET_FREE);
		if (rc >= 0) {
			char name[16];
			snprintf(name, sizeof(name), "loop%d", rc);

			rc = loopiter_set_device(lc, name);
		}
		lc->control_ok = ctl >= 0 && rc == 0 ? 1 : 0;
		if (ctl >= 0)
			close(ctl);
		DBG(CXT, ul_debugobj(lc, "find_unused by loop-control [rc=%d]", rc));
	}

	if (rc < 0) {
		DBG(CXT, ul_debugobj(lc, "using loop scan"));
		rc = loopcxt_init_iterator(lc, LOOPITER_FL_FREE);
		if (rc)
			return rc;

		rc = loopcxt_next(lc);
		loopcxt_deinit_iterator(lc);
		DBG(CXT, ul_debugobj(lc, "find_unused by scan [rc=%d]", rc));
	}
	return rc;
}



/*
 * Return: TRUE/FALSE
 */
int loopdev_is_autoclear(const char *device)
{
	struct loopdev_cxt lc;
	int rc;

	if (!device)
		return 0;

	rc = loopcxt_init(&lc, 0);
	if (!rc)
		rc = loopcxt_set_device(&lc, device);
	if (!rc)
		rc = loopcxt_is_autoclear(&lc);

	loopcxt_deinit(&lc);
	return rc;
}

char *loopdev_get_backing_file(const char *device)
{
	struct loopdev_cxt lc;
	char *res = NULL;

	if (!device)
		return NULL;
	if (loopcxt_init(&lc, 0))
		return NULL;
	if (loopcxt_set_device(&lc, device) == 0)
		res = loopcxt_get_backing_file(&lc);

	loopcxt_deinit(&lc);
	return res;
}

/*
 * Returns: TRUE/FALSE
 */
int loopdev_is_used(const char *device, const char *filename,
		    uint64_t offset, uint64_t sizelimit, int flags)
{
	struct loopdev_cxt lc;
	struct stat st;
	int rc = 0;

	if (!device || !filename)
		return 0;

	rc = loopcxt_init(&lc, 0);
	if (!rc)
		rc = loopcxt_set_device(&lc, device);
	if (rc)
		return rc;

	rc = !stat(filename, &st);
	rc = loopcxt_is_used(&lc, rc ? &st : NULL, filename, offset, sizelimit, flags);

	loopcxt_deinit(&lc);
	return rc;
}

int loopdev_delete(const char *device)
{
	struct loopdev_cxt lc;
	int rc;

	if (!device)
		return -EINVAL;

	rc = loopcxt_init(&lc, 0);
	if (!rc)
		rc = loopcxt_set_device(&lc, device);
	if (!rc)
		rc = loopcxt_delete_device(&lc);
	loopcxt_deinit(&lc);
	return rc;
}

/*
 * Returns: 0 = success, < 0 error, 1 not found
 */
int loopcxt_find_by_backing_file(struct loopdev_cxt *lc, const char *filename,
				 uint64_t offset, uint64_t sizelimit, int flags)
{
	int rc, hasst;
	struct stat st;

	if (!filename)
		return -EINVAL;

	hasst = !stat(filename, &st);

	rc = loopcxt_init_iterator(lc, LOOPITER_FL_USED);
	if (rc)
		return rc;

	while ((rc = loopcxt_next(lc)) == 0) {

		if (loopcxt_is_used(lc, hasst ? &st : NULL,
				    filename, offset, sizelimit, flags))
			break;
	}

	loopcxt_deinit_iterator(lc);
	return rc;
}

/*
 * Returns: 0 = not found, < 0 error, 1 found, 2 found full size and offset match
 */
int loopcxt_find_overlap(struct loopdev_cxt *lc, const char *filename,
			   uint64_t offset, uint64_t sizelimit)
{
	int rc, hasst;
	struct stat st;

	if (!filename)
		return -EINVAL;

	DBG(CXT, ul_debugobj(lc, "find_overlap requested"));
	hasst = !stat(filename, &st);

	rc = loopcxt_init_iterator(lc, LOOPITER_FL_USED);
	if (rc)
		return rc;

	while ((rc = loopcxt_next(lc)) == 0) {
		uint64_t lc_sizelimit, lc_offset;

		rc = loopcxt_is_used(lc, hasst ? &st : NULL,
				     filename, offset, sizelimit, 0);
		if (!rc)
			continue;	/* unused */
		if (rc < 0)
			break;		/* error */

		DBG(CXT, ul_debugobj(lc, "found %s backed by %s",
			loopcxt_get_device(lc), filename));

		rc = loopcxt_get_offset(lc, &lc_offset);
		if (rc) {
			DBG(CXT, ul_debugobj(lc, "failed to get offset for device %s",
				loopcxt_get_device(lc)));
			break;
		}
		rc = loopcxt_get_sizelimit(lc, &lc_sizelimit);
		if (rc) {
			DBG(CXT, ul_debugobj(lc, "failed to get sizelimit for device %s",
				loopcxt_get_device(lc)));
			break;
		}

		/* full match */
		if (lc_sizelimit == sizelimit && lc_offset == offset) {
			DBG(CXT, ul_debugobj(lc, "overlapping loop device %s (full match)",
						loopcxt_get_device(lc)));
			rc = 2;
			goto found;
		}

		/* overlap */
		if (lc_sizelimit != 0 && offset >= lc_offset + lc_sizelimit)
			continue;
		if (sizelimit != 0 && offset + sizelimit <= lc_offset)
			continue;

		DBG(CXT, ul_debugobj(lc, "overlapping loop device %s",
			loopcxt_get_device(lc)));
			rc = 1;
			goto found;
	}

	if (rc == 1)
		rc = 0;	/* not found */
found:
	loopcxt_deinit_iterator(lc);
	DBG(CXT, ul_debugobj(lc, "find_overlap done [rc=%d]", rc));
	return rc;
}

/*
 * Returns allocated string with device name
 */
char *loopdev_find_by_backing_file(const char *filename, uint64_t offset, uint64_t sizelimit, int flags)
{
	struct loopdev_cxt lc;
	char *res = NULL;

	if (!filename)
		return NULL;

	if (loopcxt_init(&lc, 0))
		return NULL;
	if (loopcxt_find_by_backing_file(&lc, filename, offset, sizelimit, flags) == 0)
		res = loopcxt_strdup_device(&lc);
	loopcxt_deinit(&lc);

	return res;
}

/*
 * Returns number of loop devices associated with @file, if only one loop
 * device is associeted with the given @filename and @loopdev is not NULL then
 * @loopdev returns name of the device.
 */
int loopdev_count_by_backing_file(const char *filename, char **loopdev)
{
	struct loopdev_cxt lc;
	int count = 0, rc;

	if (!filename)
		return -1;

	rc = loopcxt_init(&lc, 0);
	if (rc)
		return rc;
	if (loopcxt_init_iterator(&lc, LOOPITER_FL_USED))
		return -1;

	while(loopcxt_next(&lc) == 0) {
		char *backing = loopcxt_get_backing_file(&lc);

		if (!backing || strcmp(backing, filename)) {
			free(backing);
			continue;
		}

		free(backing);
		if (loopdev && count == 0)
			*loopdev = loopcxt_strdup_device(&lc);
		count++;
	}

	loopcxt_deinit(&lc);

	if (loopdev && count > 1) {
		free(*loopdev);
		*loopdev = NULL;
	}
	return count;
}

