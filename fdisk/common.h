#ifndef FDISK_COMMON_H
#define FDISK_COMMON_H

/* common stuff for fdisk, cfdisk, sfdisk */

/* udev paths */
#define PATH_DEV_BYID   "/dev/disk/by-id"
#define PATH_DEV_BYPATH "/dev/disk/by-path"

struct systypes {
	unsigned char type;
	char *name;
};

extern struct systypes i386_sys_types[];

extern char *partname(char *dev, int pno, int lth);
extern int is_probably_full_disk(char *name);

#endif /* FDISK_COMMON_H */
