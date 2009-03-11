
#include <ctype.h>

#include "blkdev.h"
#include "wholedisk.h"

int is_whole_disk(const char *name)
{
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
