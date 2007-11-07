
#include <sys/types.h>
#include <sys/ioctl.h>

#include "blkdev.h"
#include "linux_version.h"

/* get size in bytes */
int
blkdev_get_size(int fd, unsigned long long *bytes)
{
	unsigned long size;
	int ver = get_linux_version();

	/* kernels 2.4.15-2.4.17, had a broken BLKGETSIZE64 */
	if (ver >= KERNEL_VERSION (2,6,0) ||
	   (ver >= KERNEL_VERSION (2,4,18) && ver < KERNEL_VERSION (2,5,0))) {

		if (ioctl(fd, BLKGETSIZE64, bytes) >= 0)
			return 0;
	}
	if (ioctl(fd, BLKGETSIZE, &size) >= 0) {
		*bytes = ((unsigned long long)size << 9);
		return 0;
	}

	return -1;
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

/* get hardware sector size */
int
blkdev_get_sector_size(int fd, int *sector_size)
{
	if (get_linux_version() < KERNEL_VERSION(2,3,3)) {
		*sector_size = DEFAULT_SECTOR_SIZE;
		return 0;
	}
	if (ioctl(fd, BLKSSZGET, sector_size) >= 0)
		return 0;

	return -1;
}


#ifdef MAIN_TEST_BLKDEV
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
