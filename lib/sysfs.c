/*
 * Copyright (C) 2011 Karel Zak <kzak@redhat.com>
 */

#include "c.h"
#include "at.h"
#include "pathnames.h"
#include "sysfs.h"

char *sysfs_devno_attribute_path(dev_t devno, char *buf,
				 size_t buflen, const char *attr)
{
	int len;

	if (attr)
		len = snprintf(buf, buflen, _PATH_SYS_DEVBLOCK "/%d:%d/%s",
			major(devno), minor(devno), attr);
	else
		len = snprintf(buf, buflen, _PATH_SYS_DEVBLOCK "/%d:%d",
			major(devno), minor(devno));

	return (len < 0 || len + 1 > buflen) ? NULL : buf;
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

char *sysfs_devno_path(dev_t devno, char *buf, size_t buflen)
{
	return sysfs_devno_attribute_path(devno, buf, buflen, NULL);
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

	if (!dev && parent) {
		/*
		 * Create path to /sys/block/<parent>/<name>/dev
		 */
		int len = snprintf(buf, sizeof(buf),
				_PATH_SYS_BLOCK "/%s/%s/dev", parent, name);
		if (len < 0 || len + 1 > sizeof(buf))
			return 0;
		path = buf;

	} else if (!dev) {
		/*
		 * Create path to /sys/block/<name>/dev
		 */
		int len = snprintf(buf, sizeof(buf),
				_PATH_SYS_BLOCK "/%s/dev", name);
		if (len < 0 || len + 1 > sizeof(buf))
			return 0;
		path = buf;
	}

	if (path) {
		/*
		 * read devno from sysfs
		 */
		FILE *f;
		int maj = 0, min = 0;

		f = fopen(path, "r");
		if (!f)
			return 0;

		if (fscanf(f, "%u:%u", &maj, &min) == 2)
			dev = makedev(maj, min);
		fclose(f);
	}
	return dev;
}

int sysfs_init(struct sysfs_cxt *cxt, dev_t devno, struct sysfs_cxt *parent)
{
	char path[PATH_MAX];
	int fd, rc = 0;

	if (!sysfs_devno_path(devno, path, sizeof(path)))
		goto err;

	fd = open(path, O_RDONLY);
	if (fd < 0)
		goto err;
#ifndef HAVE_FSTATAT
	cxt->dir_path = strdup(path);
	if (!cxt->dir_path)
		goto err;
#endif
	cxt->devno = devno;
	cxt->dir_fd = fd;
	cxt->parent = parent;
	return 0;
err:
	rc = -errno;
	sysfs_deinit(cxt);
	return rc;
}

void sysfs_deinit(struct sysfs_cxt *cxt)
{
	if (!cxt)
		return;

	if (cxt->dir_fd >= 0)
	       close(cxt->dir_fd);
	cxt->devno = 0;
	cxt->dir_fd = -1;
	cxt->parent = NULL;
#ifndef HAVE_FSTATAT
	free(cxt->dir_path);
#endif
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
	int fd = open_at(cxt->dir_fd, cxt->dir_path, attr, O_RDONLY);

	if (fd == -1 && errno == ENOENT &&
	    strncmp(attr, "queue/", 6) == 0 && cxt->parent) {

		/* Exception for "queue/<attr>". These attributes are available
		 * for parental devices only
		 */
		fd = open_at(cxt->parent->dir_fd, cxt->dir_path, attr, O_RDONLY);
	}
	return fd;
}

DIR *sysfs_opendir(struct sysfs_cxt *cxt, const char *attr)
{
	DIR *dir;
	int fd;

	if (attr)
		fd = sysfs_open(cxt, attr);
	else {
		/* request to open root of device in sysfs (/sys/block/<dev>)
		 * -- we cannot use cxt->sysfs_fd directly, because closedir()
		 * will close this our persistent file descriptor.
		 */
		fd = dup(cxt->dir_fd);
	}

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

	return fd < 0 ? NULL : fdopen(fd, "r");
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
	if (d->d_type != DT_DIR)
		return 0;
#endif
	if (strncmp(parent_name, d->d_name, strlen(parent_name)))
		return 0;

	/* Cannot use /partition file, not supported on old sysfs */
	snprintf(path, sizeof(path), "%s/start", d->d_name);

	return faccessat(dirfd(dir), path, R_OK, 0) == 0;
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

int64_t sysfs_read_s64(struct sysfs_cxt *cxt, const char *attr)
{
	uint64_t x;
	return sysfs_scanf(cxt, attr, "%"SCNd64, &x) == 1 ? x : 0;
}

uint64_t sysfs_read_u64(struct sysfs_cxt *cxt, const char *attr)
{
	uint64_t x;
	return sysfs_scanf(cxt, attr, "%"SCNu64, &x) == 1 ? x : 0;
}

int sysfs_read_int(struct sysfs_cxt *cxt, const char *attr)
{
	int x;
	return sysfs_scanf(cxt, attr, "%d", &x) == 1 ? x : 0;
}

char *sysfs_strdup(struct sysfs_cxt *cxt, const char *attr)
{
	char buf[1024];
	return sysfs_scanf(cxt, attr, "%1024[^\n]", buf) == 1 ?
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


#ifdef TEST_PROGRAM
#include <errno.h>
#include <err.h>
#include <stdlib.h>

int main(int argc, char *argv[])
{
	struct sysfs_cxt cxt;
	char *devname;
	dev_t devno;
	char path[PATH_MAX];

	if (argc != 2)
		errx(EXIT_FAILURE, "usage: %s <devname>", argv[0]);

	devname = argv[1];
	devno = sysfs_devname_to_devno(devname, NULL);

	if (!devno)
		err(EXIT_FAILURE, "failed to read devno");

	printf("NAME: %s\n", devname);
	printf("DEVNO: %u\n", (unsigned int) devno);
	printf("DEVNOPATH: %s\n", sysfs_devno_path(devno, path, sizeof(path)));
	printf("PARTITION: %s\n",
		sysfs_devno_has_attribute(devno, "partition") ? "YES" : "NOT");

	sysfs_init(&cxt, devno, NULL);

	printf("SLAVES: %d\n", sysfs_count_dirents(&cxt, "slaves"));
	printf("SIZE: %jd\n", sysfs_read_u64(&cxt, "size"));
	printf("SECTOR: %d\n", sysfs_read_int(&cxt, "queue/hw_sector_size"));

	return EXIT_SUCCESS;
}
#endif
