/*
 * No copyright is claimed.  This code is in the public domain; do with
 * it what you wish.
 *
 * Written by Karel Zak <kzak@redhat.com>
 */
#include <ctype.h>

#include "c.h"
#include "at.h"
#include "pathnames.h"
#include "sysfs.h"
#include "fileutils.h"

char *sysfs_devno_attribute_path(dev_t devno, char *buf,
				 size_t bufsiz, const char *attr)
{
	int len;

	if (attr)
		len = snprintf(buf, bufsiz, _PATH_SYS_DEVBLOCK "/%d:%d/%s",
			major(devno), minor(devno), attr);
	else
		len = snprintf(buf, bufsiz, _PATH_SYS_DEVBLOCK "/%d:%d",
			major(devno), minor(devno));

	return (len < 0 || (size_t) len + 1 > bufsiz) ? NULL : buf;
}

int sysfs_devno_has_attribute(dev_t devno, const char *attr)
{
	char path[PATH_MAX];
	struct stat info;

	if (!sysfs_devno_attribute_path(devno, path, sizeof(path), attr))
		return 0;
	if (stat(path, &info) == 0)
		return 1;
	return 0;
}

char *sysfs_devno_path(dev_t devno, char *buf, size_t bufsiz)
{
	return sysfs_devno_attribute_path(devno, buf, bufsiz, NULL);
}

dev_t sysfs_devname_to_devno(const char *name, const char *parent)
{
	char buf[PATH_MAX], *path = NULL;
	dev_t dev = 0;

	if (strncmp("/dev/", name, 5) == 0) {
		/*
		 * Read from /dev
		 */
		struct stat st;

		if (stat(name, &st) == 0)
			dev = st.st_rdev;
		else
			name += 5;	/* unaccesible, or not node in /dev */
	}

	if (!dev && parent && strncmp("dm-", name, 3)) {
		/*
		 * Create path to /sys/block/<parent>/<name>/dev
		 */
		int len = snprintf(buf, sizeof(buf),
				_PATH_SYS_BLOCK "/%s/%s/dev", parent, name);
		if (len < 0 || (size_t) len + 1 > sizeof(buf))
			return 0;
		path = buf;

	} else if (!dev) {
		/*
		 * Create path to /sys/block/<name>/dev
		 */
		int len = snprintf(buf, sizeof(buf),
				_PATH_SYS_BLOCK "/%s/dev", name);
		if (len < 0 || (size_t) len + 1 > sizeof(buf))
			return 0;
		path = buf;
	}

	if (path) {
		/*
		 * read devno from sysfs
		 */
		FILE *f;
		int maj = 0, min = 0;

		f = fopen(path, "r" UL_CLOEXECSTR);
		if (!f)
			return 0;

		if (fscanf(f, "%d:%d", &maj, &min) == 2)
			dev = makedev(maj, min);
		fclose(f);
	}
	return dev;
}

/*
 * Returns devname (e.g. "/dev/sda1") for the given devno.
 *
 * Note that the @buf has to be large enough to store /sys/dev/block/<maj:min>
 * symlinks.
 *
 * Please, use more robust blkid_devno_to_devname() in your applications.
 */
char *sysfs_devno_to_devpath(dev_t devno, char *buf, size_t bufsiz)
{
	struct sysfs_cxt cxt;
	char *name;
	size_t sz;
	struct stat st;

	if (sysfs_init(&cxt, devno, NULL))
		return NULL;

	name = sysfs_get_devname(&cxt, buf, bufsiz);
	sysfs_deinit(&cxt);

	if (!name)
		return NULL;

	sz = strlen(name);

	if (sz + sizeof("/dev/") > bufsiz)
		return NULL;

	/* create the final "/dev/<name>" string */
	memmove(buf + 5, name, sz + 1);
	memcpy(buf, "/dev/", 5);

	if (!stat(buf, &st) && S_ISBLK(st.st_mode) && st.st_rdev == devno)
		return buf;

	return NULL;
}

int sysfs_init(struct sysfs_cxt *cxt, dev_t devno, struct sysfs_cxt *parent)
{
	char path[PATH_MAX];
	int fd, rc;

	memset(cxt, 0, sizeof(*cxt));
	cxt->dir_fd = -1;

	if (!sysfs_devno_path(devno, path, sizeof(path)))
		goto err;

	fd = open(path, O_RDONLY|O_CLOEXEC);
	if (fd < 0)
		goto err;
	cxt->dir_fd = fd;

	cxt->dir_path = strdup(path);
	if (!cxt->dir_path)
		goto err;
	cxt->devno = devno;
	cxt->parent = parent;
	return 0;
err:
	rc = errno > 0 ? -errno : -1;
	sysfs_deinit(cxt);
	return rc;
}

void sysfs_deinit(struct sysfs_cxt *cxt)
{
	if (!cxt)
		return;

	if (cxt->dir_fd >= 0)
	       close(cxt->dir_fd);
	free(cxt->dir_path);

	memset(cxt, 0, sizeof(*cxt));

	cxt->dir_fd = -1;
}

int sysfs_stat(struct sysfs_cxt *cxt, const char *attr, struct stat *st)
{
	int rc = fstat_at(cxt->dir_fd, cxt->dir_path, attr, st, 0);

	if (rc != 0 && errno == ENOENT &&
	    strncmp(attr, "queue/", 6) == 0 && cxt->parent) {

		/* Exception for "queue/<attr>". These attributes are available
		 * for parental devices only
		 */
		return fstat_at(cxt->parent->dir_fd,
				cxt->parent->dir_path, attr, st, 0);
	}
	return rc;
}

int sysfs_has_attribute(struct sysfs_cxt *cxt, const char *attr)
{
	struct stat st;

	return sysfs_stat(cxt, attr, &st) == 0;
}

static int sysfs_open(struct sysfs_cxt *cxt, const char *attr)
{
	int fd = open_at(cxt->dir_fd, cxt->dir_path, attr, O_RDONLY|O_CLOEXEC);

	if (fd == -1 && errno == ENOENT &&
	    strncmp(attr, "queue/", 6) == 0 && cxt->parent) {

		/* Exception for "queue/<attr>". These attributes are available
		 * for parental devices only
		 */
		fd = open_at(cxt->parent->dir_fd, cxt->dir_path, attr,
				O_RDONLY|O_CLOEXEC);
	}
	return fd;
}

ssize_t sysfs_readlink(struct sysfs_cxt *cxt, const char *attr,
		   char *buf, size_t bufsiz)
{
	if (!cxt->dir_path)
		return -1;

	if (attr)
		return readlink_at(cxt->dir_fd, cxt->dir_path, attr, buf, bufsiz);

	/* read /sys/dev/block/<maj:min> link */
	return readlink(cxt->dir_path, buf, bufsiz);
}

DIR *sysfs_opendir(struct sysfs_cxt *cxt, const char *attr)
{
	DIR *dir;
	int fd = -1;

	if (attr)
		fd = sysfs_open(cxt, attr);

	else if (cxt->dir_fd >= 0)
		/* request to open root of device in sysfs (/sys/block/<dev>)
		 * -- we cannot use cxt->sysfs_fd directly, because closedir()
		 * will close this our persistent file descriptor.
		 */
		fd = dup(cxt->dir_fd);

	if (fd < 0)
		return NULL;

	dir = fdopendir(fd);
	if (!dir) {
		close(fd);
		return NULL;
	}
	if (!attr)
		 rewinddir(dir);
	return dir;
}


static FILE *sysfs_fopen(struct sysfs_cxt *cxt, const char *attr)
{
	int fd = sysfs_open(cxt, attr);

	return fd < 0 ? NULL : fdopen(fd, "r" UL_CLOEXECSTR);
}


static struct dirent *xreaddir(DIR *dp)
{
	struct dirent *d;

	while ((d = readdir(dp))) {
		if (!strcmp(d->d_name, ".") ||
		    !strcmp(d->d_name, ".."))
			continue;

		/* blacklist here? */
		break;
	}
	return d;
}

int sysfs_is_partition_dirent(DIR *dir, struct dirent *d, const char *parent_name)
{
	char path[256];

#ifdef _DIRENT_HAVE_D_TYPE
	if (d->d_type != DT_DIR &&
	    d->d_type != DT_LNK &&
	    d->d_type != DT_UNKNOWN)
		return 0;
#endif
	if (parent_name) {
		const char *p = parent_name;
		size_t len;

		/* /dev/sda --> "sda" */
		if (*parent_name == '/') {
			p = strrchr(parent_name, '/');
			if (!p)
				return 0;
			p++;
		}

		len = strlen(p);
		if (strlen(d->d_name) <= len)
			return 0;

		/* partitions subdir name is
		 *	"<parent>[:digit:]" or "<parent>p[:digit:]"
		 */
		return strncmp(p, d->d_name, len) == 0 &&
		       ((*(d->d_name + len) == 'p' && isdigit(*(d->d_name + len + 1)))
			|| isdigit(*(d->d_name + len)));
	}

	/* Cannot use /partition file, not supported on old sysfs */
	snprintf(path, sizeof(path), "%s/start", d->d_name);

	return faccessat(dirfd(dir), path, R_OK, 0) == 0;
}

/*
 * Converts @partno (partition number) to devno of the partition.
 * The @cxt handles wholedisk device.
 *
 * Note that this code does not expect any special format of the
 * partitions devnames.
 */
dev_t sysfs_partno_to_devno(struct sysfs_cxt *cxt, int partno)
{
	DIR *dir;
	struct dirent *d;
	char path[256];
	dev_t devno = 0;

	dir = sysfs_opendir(cxt, NULL);
	if (!dir)
		return 0;

	while ((d = xreaddir(dir))) {
		int n, maj, min;

		if (!sysfs_is_partition_dirent(dir, d, NULL))
			continue;

		snprintf(path, sizeof(path), "%s/partition", d->d_name);
		if (sysfs_read_int(cxt, path, &n))
			continue;

		if (n == partno) {
			snprintf(path, sizeof(path), "%s/dev", d->d_name);
			if (sysfs_scanf(cxt, path, "%d:%d", &maj, &min) == 2)
				devno = makedev(maj, min);
			break;
		}
	}

	closedir(dir);
	return devno;
}


int sysfs_scanf(struct sysfs_cxt *cxt,  const char *attr, const char *fmt, ...)
{
	FILE *f = sysfs_fopen(cxt, attr);
	va_list ap;
	int rc;

	if (!f)
		return -EINVAL;
	va_start(ap, fmt);
	rc = vfscanf(f, fmt, ap);
	va_end(ap);

	fclose(f);
	return rc;
}


int sysfs_read_s64(struct sysfs_cxt *cxt, const char *attr, int64_t *res)
{
	int64_t x = 0;

	if (sysfs_scanf(cxt, attr, "%"SCNd64, &x) == 1) {
		if (res)
			*res = x;
		return 0;
	}
	return -1;
}

int sysfs_read_u64(struct sysfs_cxt *cxt, const char *attr, uint64_t *res)
{
	uint64_t x = 0;

	if (sysfs_scanf(cxt, attr, "%"SCNu64, &x) == 1) {
		if (res)
			*res = x;
		return 0;
	}
	return -1;
}

int sysfs_read_int(struct sysfs_cxt *cxt, const char *attr, int *res)
{
	int x = 0;

	if (sysfs_scanf(cxt, attr, "%d", &x) == 1) {
		if (res)
			*res = x;
		return 0;
	}
	return -1;
}

char *sysfs_strdup(struct sysfs_cxt *cxt, const char *attr)
{
	char buf[1024];
	return sysfs_scanf(cxt, attr, "%1023[^\n]", buf) == 1 ?
						strdup(buf) : NULL;
}

int sysfs_count_dirents(struct sysfs_cxt *cxt, const char *attr)
{
	DIR *dir;
	int r = 0;

	if (!(dir = sysfs_opendir(cxt, attr)))
		return 0;

	while (xreaddir(dir)) r++;

	closedir(dir);
	return r;
}

int sysfs_count_partitions(struct sysfs_cxt *cxt, const char *devname)
{
	DIR *dir;
	struct dirent *d;
	int r = 0;

	if (!(dir = sysfs_opendir(cxt, NULL)))
		return 0;

	while ((d = xreaddir(dir))) {
		if (sysfs_is_partition_dirent(dir, d, devname))
			r++;
	}

	closedir(dir);
	return r;
}

/*
 * Returns slave name if there is only one slave, otherwise returns NULL.
 * The result should be deallocated by free().
 */
char *sysfs_get_slave(struct sysfs_cxt *cxt)
{
	DIR *dir;
	struct dirent *d;
	char *name = NULL;

	if (!(dir = sysfs_opendir(cxt, "slaves")))
		return NULL;

	while ((d = xreaddir(dir))) {
		if (name)
			goto err;	/* more slaves */

		name = strdup(d->d_name);
	}

	closedir(dir);
	return name;
err:
	free(name);
	closedir(dir);
	return NULL;
}

/*
 * Note that the @buf has to be large enough to store /sys/dev/block/<maj:min>
 * symlinks.
 */
char *sysfs_get_devname(struct sysfs_cxt *cxt, char *buf, size_t bufsiz)
{
	char *name = NULL;
	ssize_t sz;

	sz = sysfs_readlink(cxt, NULL, buf, bufsiz - 1);
	if (sz < 0)
		return NULL;

	buf[sz] = '\0';
	name = strrchr(buf, '/');
	if (!name)
		return NULL;

	name++;
	sz = strlen(name);

	memmove(buf, name, sz + 1);
	return buf;
}

static int get_dm_wholedisk(struct sysfs_cxt *cxt, char *diskname,
                size_t len, dev_t *diskdevno)
{
    int rc = 0;
    char *name;

    /* Note, sysfs_get_slave() returns the first slave only,
     * if there is more slaves, then return NULL
     */
    name = sysfs_get_slave(cxt);
    if (!name)
        return -1;

    if (diskname && len) {
        strncpy(diskname, name, len);
        diskname[len - 1] = '\0';
    }

    if (diskdevno) {
        *diskdevno = sysfs_devname_to_devno(name, NULL);
        if (!*diskdevno)
            rc = -1;
    }

    free(name);
    return rc;
}

/*
 * Returns by @diskdevno whole disk device devno and (optionaly) by
 * @diskname the whole disk device name.
 */
int sysfs_devno_to_wholedisk(dev_t dev, char *diskname,
            size_t len, dev_t *diskdevno)
{
    struct sysfs_cxt cxt;
    int is_part = 0;

    if (!dev || sysfs_init(&cxt, dev, NULL) != 0)
        return -1;

    is_part = sysfs_has_attribute(&cxt, "partition");
    if (!is_part) {
        /*
         * Extra case for partitions mapped by device-mapper.
         *
         * All regualar partitions (added by BLKPG ioctl or kernel PT
         * parser) have the /sys/.../partition file. The partitions
         * mapped by DM don't have such file, but they have "part"
         * prefix in DM UUID.
         */
        char *uuid = sysfs_strdup(&cxt, "dm/uuid");
        char *tmp = uuid;
        char *prefix = uuid ? strsep(&tmp, "-") : NULL;

        if (prefix && strncasecmp(prefix, "part", 4) == 0)
            is_part = 1;
        free(uuid);

        if (is_part &&
            get_dm_wholedisk(&cxt, diskname, len, diskdevno) == 0)
            /*
             * partitioned device, mapped by DM
             */
            goto done;

        is_part = 0;
    }

    if (!is_part) {
        /*
         * unpartitioned device
         */
        if (diskname && len) {
            if (!sysfs_get_devname(&cxt, diskname, len))
                goto err;
        }
        if (diskdevno)
            *diskdevno = dev;

    } else {
        /*
         * partitioned device
         *  - readlink /sys/dev/block/8:1   = ../../block/sda/sda1
         *  - dirname  ../../block/sda/sda1 = ../../block/sda
         *  - basename ../../block/sda      = sda
         */
        char linkpath[PATH_MAX];
        char *name;
        int linklen;

        linklen = sysfs_readlink(&cxt, NULL,
                linkpath, sizeof(linkpath) - 1);
        if (linklen < 0)
            goto err;
        linkpath[linklen] = '\0';

        stripoff_last_component(linkpath);      /* dirname */
        name = stripoff_last_component(linkpath);   /* basename */
        if (!name)
            goto err;

        if (diskname && len) {
            strncpy(diskname, name, len);
            diskname[len - 1] = '\0';
        }

        if (diskdevno) {
            *diskdevno = sysfs_devname_to_devno(name, NULL);
            if (!*diskdevno)
                goto err;
        }
    }

done:
    sysfs_deinit(&cxt);
    return 0;
err:
    sysfs_deinit(&cxt);
    return -1;
}

/*
 * Returns 1 if the device is private LVM device.
 */
int sysfs_devno_is_lvm_private(dev_t devno)
{
	struct sysfs_cxt cxt = UL_SYSFSCXT_EMPTY;
	char *uuid = NULL;
	int rc = 0;

	if (sysfs_init(&cxt, devno, NULL) != 0)
		return 0;

	uuid = sysfs_strdup(&cxt, "dm/uuid");

	/* Private LVM devices use "LVM-<uuid>-<name>" uuid format (important
	 * is the "LVM" prefix and "-<name>" postfix).
	 */
	if (uuid && strncmp(uuid, "LVM-", 4) == 0) {
		char *p = strrchr(uuid + 4, '-');

		if (p && *(p + 1))
			rc = 1;
	}

	sysfs_deinit(&cxt);
	free(uuid);
	return rc;
}

/*
 * Return 0 or 1, or < 0 in case of error
 */
int sysfs_devno_is_wholedisk(dev_t devno)
{
	dev_t disk;

	if (sysfs_devno_to_wholedisk(devno, NULL, 0, &disk) != 0)
		return -1;

	return devno == disk;
}

int sysfs_scsi_get_hctl(struct sysfs_cxt *cxt, int *h, int *c, int *t, int *l)
{
	char buf[PATH_MAX], *hctl;
	ssize_t len;

	if (!cxt)
		return -EINVAL;
	if (cxt->has_hctl)
		goto done;

	len = sysfs_readlink(cxt, "device", buf, sizeof(buf) - 1);
	if (len < 0)
		return len;

	buf[len] = '\0';
	hctl = strrchr(buf, '/');
	if (!hctl)
		return -1;
	hctl++;

	if (sscanf(hctl, "%u:%u:%u:%u", &cxt->scsi_host, &cxt->scsi_channel,
				&cxt->scsi_target, &cxt->scsi_lun) != 4)
		return -1;

	cxt->has_hctl = 1;
done:
	if (h)
		*h = cxt->scsi_host;
	if (c)
		*c = cxt->scsi_channel;
	if (t)
		*t = cxt->scsi_target;
	if (l)
		*l = cxt->scsi_lun;
	return 0;
}


static char *sysfs_scsi_host_attribute_path(struct sysfs_cxt *cxt,
		const char *type, char *buf, size_t bufsz, const char *attr)
{
	int len;
	int host;

	if (sysfs_scsi_get_hctl(cxt, &host, NULL, NULL, NULL))
		return NULL;

	if (attr)
		len = snprintf(buf, bufsz, _PATH_SYS_CLASS "/%s_host/host%d/%s",
				type, host, attr);
	else
		len = snprintf(buf, bufsz, _PATH_SYS_CLASS "/%s_host/host%d",
				type, host);

	return (len < 0 || (size_t) len + 1 > bufsz) ? NULL : buf;
}

char *sysfs_scsi_host_strdup_attribute(struct sysfs_cxt *cxt,
		const char *type, const char *attr)
{
	char buf[1024];
	int rc;
	FILE *f;

	if (!attr || !type ||
	    !sysfs_scsi_host_attribute_path(cxt, type, buf, sizeof(buf), attr))
		return NULL;

	if (!(f = fopen(buf, "r" UL_CLOEXECSTR)))
                return NULL;

	rc = fscanf(f, "%1023[^\n]", buf);
	fclose(f);

	return rc == 1 ? strdup(buf) : NULL;
}

int sysfs_scsi_host_is(struct sysfs_cxt *cxt, const char *type)
{
	char buf[PATH_MAX];
	struct stat st;

	if (!type || !sysfs_scsi_host_attribute_path(cxt, type,
				buf, sizeof(buf), NULL))
		return 0;

	return stat(buf, &st) == 0 && S_ISDIR(st.st_mode);
}

static char *sysfs_scsi_attribute_path(struct sysfs_cxt *cxt,
		char *buf, size_t bufsz, const char *attr)
{
	int len, h, c, t, l;

	if (sysfs_scsi_get_hctl(cxt, &h, &c, &t, &l) != 0)
		return NULL;

	if (attr)
		len = snprintf(buf, bufsz, _PATH_SYS_SCSI "/devices/%d:%d:%d:%d/%s",
				h,c,t,l, attr);
	else
		len = snprintf(buf, bufsz, _PATH_SYS_SCSI "/devices/%d:%d:%d:%d",
				h,c,t,l);
	return (len < 0 || (size_t) len + 1 > bufsz) ? NULL : buf;
}

int sysfs_scsi_has_attribute(struct sysfs_cxt *cxt, const char *attr)
{
	char path[PATH_MAX];
	struct stat st;

	if (!sysfs_scsi_attribute_path(cxt, path, sizeof(path), attr))
		return 0;

	return stat(path, &st) == 0;
}

int sysfs_scsi_path_contains(struct sysfs_cxt *cxt, const char *pattern)
{
	char path[PATH_MAX], linkc[PATH_MAX];
	struct stat st;
	ssize_t len;

	if (!sysfs_scsi_attribute_path(cxt, path, sizeof(path), NULL))
		return 0;

	if (stat(path, &st) != 0)
		return 0;

	len = readlink(path, linkc, sizeof(linkc) - 1);
	if (len < 0)
		return 0;

	linkc[len] = '\0';
	return strstr(linkc, pattern) != NULL;
}

#ifdef TEST_PROGRAM_SYSFS
#include <errno.h>
#include <err.h>
#include <stdlib.h>

int main(int argc, char *argv[])
{
	struct sysfs_cxt cxt = UL_SYSFSCXT_EMPTY;
	char *devname;
	dev_t devno;
	char path[PATH_MAX];
	int i, is_part;
	uint64_t u64;
	ssize_t len;

	if (argc != 2)
		errx(EXIT_FAILURE, "usage: %s <devname>", argv[0]);

	devname = argv[1];
	devno = sysfs_devname_to_devno(devname, NULL);

	if (!devno)
		err(EXIT_FAILURE, "failed to read devno");

	is_part = sysfs_devno_has_attribute(devno, "partition");

	printf("NAME: %s\n", devname);
	printf("DEVNO: %u (%d:%d)\n", (unsigned int) devno, major(devno), minor(devno));
	printf("DEVNOPATH: %s\n", sysfs_devno_path(devno, path, sizeof(path)));
	printf("DEVPATH: %s\n", sysfs_devno_to_devpath(devno, path, sizeof(path)));
	printf("PARTITION: %s\n", is_part ? "YES" : "NOT");

	if (sysfs_init(&cxt, devno, NULL))
		return EXIT_FAILURE;

	len = sysfs_readlink(&cxt, NULL, path, sizeof(path) - 1);
	if (len > 0) {
		path[len] = '\0';
		printf("DEVNOLINK: %s\n", path);
	}

	if (!is_part) {
		printf("First 5 partitions:\n");
		for (i = 1; i <= 5; i++) {
			dev_t dev = sysfs_partno_to_devno(&cxt, i);
			if (dev)
				printf("\t#%d %d:%d\n", i, major(dev), minor(dev));
		}
	}

	printf("SLAVES: %d\n", sysfs_count_dirents(&cxt, "slaves"));

	if (sysfs_read_u64(&cxt, "size", &u64))
		printf("read SIZE failed\n");
	else
		printf("SIZE: %jd\n", u64);

	if (sysfs_read_int(&cxt, "queue/hw_sector_size", &i))
		printf("read SECTOR failed\n");
	else
		printf("SECTOR: %d\n", i);

	printf("DEVNAME: %s\n", sysfs_get_devname(&cxt, path, sizeof(path)));

	sysfs_deinit(&cxt);
	return EXIT_SUCCESS;
}
#endif
