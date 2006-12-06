#include "common.h"

int disksize(int fd, unsigned long long *sectors) {
	int err;
	long sz;
	long long b;

	err = ioctl(fd, BLKGETSIZE, &sz);
	if (err)
		return err;
	err = ioctl(fd, BLKGETSIZE64, &b);
	if (err || b == 0 || b == sz)
		*sectors = sz;
	else
		*sectors = (b >> 9);
	return 0;
}
