/* common stuff for fdisk, cfdisk, sfdisk */

/* including <linux/fs.h> fails */
#include <sys/ioctl.h>
#define BLKRRPART  _IO(0x12,95)    /* re-read partition table */
#define BLKGETSIZE _IO(0x12,96)    /* return device size */
#define BLKFLSBUF  _IO(0x12,97)    /* flush buffer cache */

struct systypes {
	unsigned char type;
	char *name;
};

extern struct systypes i386_sys_types[];

extern char *partname(char *dev, int pno, int lth);
