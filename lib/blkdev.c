
#include <sys/types.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "blkdev.h"
#include "linux_version.h"

static long
blkdev_valid_offset (int fd, off_t offset) {
	char ch;

	if (lseek (fd, offset, 0) < 0)
		return 0;
	if (read (fd, &ch, 1) < 1)
		return 0;
	return 1;
}

off_t
blkdev_find_size (int fd) {
	off_t high, low;

	low = 0;
	for (high = 1; high > 0 && blkdev_valid_offset (fd, high); high *= 2)
		low = high;
	while (low < high - 1)
	{
		const off_t mid = (low + high) / 2;

		if (blkdev_valid_offset (fd, mid))
			low = mid;
		else
			high = mid;
	}
	blkdev_valid_offset (fd, 0);
	return (low + 1);
}

/* get size in bytes */
int
blkdev_get_size(int fd, unsigned long long *bytes)
{
	/* TODO: use stat as well */

#ifdef BLKGETSIZE64
#ifdef __linux__
	int ver = get_linux_version();
	/* kernels 2.4.15-2.4.17, had a broken BLKGETSIZE64 */
	if (ver >= KERNEL_VERSION (2,6,0) ||
	   (ver >= KERNEL_VERSION (2,4,18) && ver < KERNEL_VERSION (2,5,0)))
#endif
		if (ioctl(fd, BLKGETSIZE64, bytes) >= 0)
			return 0;
#endif /* BLKGETSIZE64 */

#ifdef BLKGETSIZE
	{
		unsigned long size;

		if (ioctl(fd, BLKGETSIZE, &size) >= 0) {
			*bytes = ((unsigned long long)size << 9);
			return 0;
		}
	}

	return -1;
#endif /* BLKGETSIZE */

	*bytes = blkdev_find_size(fd);
	return 0;
}

/* get 512-byte sector count */
int
blkdev_get_sectors(int fd, unsigned long long *sectors)
{
	unsigned long long bytes;

	if (blkdev_get_size(fd, &bytes) == 0) {
		*sectors = (bytes >> 9);
		return 0;
	}

	return -1;
}

/* get logical sector size (default is 512)
 *
 * This is the smallest unit the storage device can
 * address. It is typically 512 bytes.
 */
int
blkdev_get_sector_size(int fd, int *sector_size)
{
#ifdef BLKSSZGET
#ifdef __linux__
	if (get_linux_version() < KERNEL_VERSION(2,3,3)) {
		*sector_size = DEFAULT_SECTOR_SIZE;
		return 0;
	}
#endif
	if (ioctl(fd, BLKSSZGET, sector_size) >= 0)
		return 0;

	return -1;
#else
	*sector_size = DEFAULT_SECTOR_SIZE;
	return 0;
#endif
}


#ifdef TEST_PROGRAM
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <err.h>
int
main(int argc, char **argv)
{
	unsigned long long bytes;
	unsigned long long sectors;
	int sector_size;
	int fd;

	if (argc != 2) {
		fprintf(stderr, "usage: %s device\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	if ((fd = open(argv[1], O_RDONLY)) < 0)
		err(EXIT_FAILURE, "open %s failed", argv[1]);

	if (blkdev_get_size(fd, &bytes) < 0)
		err(EXIT_FAILURE, "blkdev_get_size() failed");
	if (blkdev_get_sectors(fd, &sectors) < 0)
		err(EXIT_FAILURE, "blkdev_get_sectors() failed");
	if (blkdev_get_sector_size(fd, &sector_size) < 0)
		err(EXIT_FAILURE, "blkdev_get_sector_size() failed");

	printf("bytes %llu\n", bytes);
	printf("sectors %llu\n", sectors);
	printf("sectorsize %d\n", sector_size);

	return EXIT_SUCCESS;
}
#endif /* TEST_PROGRAM */
