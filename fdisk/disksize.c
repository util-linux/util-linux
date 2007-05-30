#include <stdio.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include "common.h"

int disksize(int fd, unsigned long long *sectors) {
	int err;
	long sz;
	long long b;

	err = ioctl(fd, BLKGETSIZE, &sz);
	if (err) {
		sz = 0;
		if (errno != EFBIG)
			return err;
	}
	err = ioctl(fd, BLKGETSIZE64, &b);
	if (err || b == 0 || b == sz)
		*sectors = sz;
	else
		*sectors = (b >> 9);
	return 0;
}

int
is_probably_full_disk(char *name) {
#ifdef HDIO_GETGEO
	struct hd_geometry geometry;
	int fd, i = 0;

	fd = open(name, O_RDONLY);
	if (fd >= 0) {
		i = ioctl(fd, HDIO_GETGEO, &geometry);
		close(fd);
	}
	if (i==0)
		return (fd >= 0 && geometry.start == 0);
#endif
	/*
	 * The "silly heuristic" is still sexy for us, because
	 * for example Xen doesn't implement HDIO_GETGEO for virtual
	 * block devices (/dev/xvda).
	 *
	 * -- kzak@redhat.com (23-Feb-2006)
	 */
	while (*name)
		name++;
	return !isdigit(name[-1]);
}

