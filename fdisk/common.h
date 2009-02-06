#ifndef FDISK_COMMON_H
#define FDISK_COMMON_H

/* common stuff for fdisk, cfdisk, sfdisk */

struct systypes {
	unsigned char type;
	char *name;
};

extern struct systypes i386_sys_types[];

extern char *partname(char *dev, int pno, int lth);
extern int is_probably_full_disk(char *name);

#endif /* FDISK_COMMON_H */
