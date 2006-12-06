/* common stuff for fdisk, cfdisk, sfdisk */

struct systypes {
	unsigned char type;
	char *name;
};

extern struct systypes i386_sys_types[];

