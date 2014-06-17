/*
 * Copyright (C) 2011 Karel Zak <kzak@redhat.com>
 */
#ifndef UTIL_LINUX_SYSFS_H
#define UTIL_LINUX_SYSFS_H


#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <inttypes.h>
#include <dirent.h>

struct sysfs_cxt {
	dev_t	devno;
	int	dir_fd;		/* /sys/block/<name> */
	char	*dir_path;
	struct sysfs_cxt *parent;

	unsigned int	scsi_host,
			scsi_channel,
			scsi_target,
			scsi_lun;

	unsigned int	has_hctl : 1;
};

#define UL_SYSFSCXT_EMPTY { 0, -1, NULL, NULL, 0, 0, 0, 0, 0 }

extern char *sysfs_devno_attribute_path(dev_t devno, char *buf,
                                 size_t bufsiz, const char *attr);
extern int sysfs_devno_has_attribute(dev_t devno, const char *attr);
extern char *sysfs_devno_path(dev_t devno, char *buf, size_t bufsiz);
extern char *sysfs_devno_to_devpath(dev_t devno, char *buf, size_t bufsiz);
extern dev_t sysfs_devname_to_devno(const char *name, const char *parent);

extern int sysfs_init(struct sysfs_cxt *cxt, dev_t devno, struct sysfs_cxt *parent)
					__attribute__ ((warn_unused_result));
extern void sysfs_deinit(struct sysfs_cxt *cxt);

extern DIR *sysfs_opendir(struct sysfs_cxt *cxt, const char *attr);

extern int sysfs_stat(struct sysfs_cxt *cxt, const char *attr, struct stat *st);
extern ssize_t sysfs_readlink(struct sysfs_cxt *cxt, const char *attr,
	                   char *buf, size_t bufsiz);
extern int sysfs_has_attribute(struct sysfs_cxt *cxt, const char *attr);

extern int sysfs_scanf(struct sysfs_cxt *cxt,  const char *attr,
		       const char *fmt, ...)
		        __attribute__ ((format (scanf, 3, 4)));

extern int sysfs_read_s64(struct sysfs_cxt *cxt, const char *attr, int64_t *res);
extern int sysfs_read_u64(struct sysfs_cxt *cxt, const char *attr, uint64_t *res);
extern int sysfs_read_int(struct sysfs_cxt *cxt, const char *attr, int *res);

extern char *sysfs_get_devname(struct sysfs_cxt *cxt, char *buf, size_t bufsiz);

extern char *sysfs_strdup(struct sysfs_cxt *cxt, const char *attr);

extern int sysfs_count_dirents(struct sysfs_cxt *cxt, const char *attr);
extern int sysfs_count_partitions(struct sysfs_cxt *cxt, const char *devname);
extern dev_t sysfs_partno_to_devno(struct sysfs_cxt *cxt, int partno);
extern char *sysfs_get_slave(struct sysfs_cxt *cxt);

extern int sysfs_is_partition_dirent(DIR *dir, struct dirent *d,
			const char *parent_name);

extern int sysfs_devno_to_wholedisk(dev_t dev, char *diskname,
            size_t len, dev_t *diskdevno);

extern int sysfs_devno_is_lvm_private(dev_t devno);
extern int sysfs_devno_is_wholedisk(dev_t devno);

extern int sysfs_scsi_get_hctl(struct sysfs_cxt *cxt, int *h,
			       int *c, int *t, int *l);
extern char *sysfs_scsi_host_strdup_attribute(struct sysfs_cxt *cxt,
                const char *type, const char *attr);
extern int sysfs_scsi_host_is(struct sysfs_cxt *cxt, const char *type);
extern int sysfs_scsi_has_attribute(struct sysfs_cxt *cxt, const char *attr);
extern int sysfs_scsi_path_contains(struct sysfs_cxt *cxt, const char *pattern);

#endif /* UTIL_LINUX_SYSFS_H */
