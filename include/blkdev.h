#ifndef BLKDEV_H
#define BLKDEV_H

#include <sys/types.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>

#define DEFAULT_SECTOR_SIZE       512

/* open() retries when errno is ENOMEDIUM */
#define CRDOM_NOMEDIUM_RETRIES    5

#if !defined(BLKROSET) && defined(__linux__)

#define BLKROSET   _IO(0x12,93)	/* set device read-only (0 = read-write) */
#define BLKROGET   _IO(0x12,94)	/* get read-only status (0 = read_write) */
#define BLKRRPART  _IO(0x12,95)	/* re-read partition table */
#define BLKGETSIZE _IO(0x12,96)	/* return device size /512 (long *arg) */
#define BLKFLSBUF  _IO(0x12,97)	/* flush buffer cache */
#define BLKRASET   _IO(0x12,98)	/* set read ahead for block device */
#define BLKRAGET   _IO(0x12,99)	/* get current read ahead setting */
#define BLKFRASET  _IO(0x12,100)/* set filesystem (mm/filemap.c) read-ahead */
#define BLKFRAGET  _IO(0x12,101)/* get filesystem (mm/filemap.c) read-ahead */
#define BLKSECTSET _IO(0x12,102)/* set max sectors per request (ll_rw_blk.c) */
#define BLKSECTGET _IO(0x12,103)/* get max sectors per request (ll_rw_blk.c) */
#define BLKSSZGET  _IO(0x12,104)/* get block device sector size */

/* ioctls introduced in 2.2.16, removed in 2.5.58 */
#define BLKELVGET  _IOR(0x12,106,size_t) /* elevator get */
#define BLKELVSET  _IOW(0x12,107,size_t) /* elevator set */

#define BLKBSZGET  _IOR(0x12,112,size_t)
#define BLKBSZSET  _IOW(0x12,113,size_t)
#define BLKGETSIZE64 _IOR(0x12,114,size_t) /* return device size in bytes (u64 *arg) */

#endif /* BLKROSET */

/* block device topology ioctls, introduced in 2.6.32 */
#ifndef BLKIOMIN
#define BLKIOMIN   _IO(0x12,120)
#define BLKIOOPT   _IO(0x12,121)
#define BLKALIGNOFF _IO(0x12,122)
#define BLKPBSZGET _IO(0x12,123)
#endif

#ifndef HDIO_GETGEO
# ifdef __linux__
#  define HDIO_GETGEO 0x0301
# endif
struct hd_geometry {
	unsigned char heads;
	unsigned char sectors;
	unsigned short cylinders;	/* truncated */
	unsigned long start;
};
#endif

/* Determine size in bytes */
off_t blkdev_find_size (int fd);

/* get size in bytes */
int blkdev_get_size(int fd, unsigned long long *bytes);

/* get 512-byte sector count */
int blkdev_get_sectors(int fd, unsigned long long *sectors);

/* get hardware sector size */
int blkdev_get_sector_size(int fd, int *sector_size);

#endif /* BLKDEV_H */
