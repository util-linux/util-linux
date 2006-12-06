/* common stuff for fdisk, cfdisk, sfdisk */

/* including <linux/fs.h> fails */
#include <sys/types.h>
#include <sys/ioctl.h>
#define BLKRRPART    _IO(0x12,95)    /* re-read partition table */
#define BLKGETSIZE   _IO(0x12,96)    /* return device size */
#define BLKFLSBUF    _IO(0x12,97)    /* flush buffer cache */
#define BLKSSZGET    _IO(0x12,104)   /* get block device sector size */
#define BLKGETSIZE64 _IOR(0x12,114,size_t)	/* size in bytes */

/* including <linux/hdreg.h> also fails */
struct hd_geometry {
      unsigned char heads;
      unsigned char sectors;
      unsigned short cylinders;
      unsigned long start;
};

#define HDIO_GETGEO		0x0301	/* get device geometry */


struct systypes {
	unsigned char type;
	char *name;
};

extern struct systypes i386_sys_types[];

extern char *partname(char *dev, int pno, int lth);

int disksize(int fd, unsigned long long *sectors);
