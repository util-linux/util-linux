#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "common.h"

/*
 * return partition name - uses static storage unless buf is supplied
 */
char *
partname(char *dev, int pno, int lth) {
	static char bufp[80];
	char *p;
	int w, wp;

	w = strlen(dev);
	p = "";

	if (isdigit(dev[w-1]))
		p = "p";

	/* devfs kludge - note: fdisk partition names are not supposed
	   to equal kernel names, so there is no reason to do this */
	if (strcmp (dev + w - 4, "disc") == 0) {
		w -= 4;
		p = "part";
	}

	/* udev names partitions by appending -partN
	   e.g. ata-SAMSUNG_SV8004H_0357J1FT712448-part1 */
	if ((strncmp(dev, PATH_DEV_BYID, strlen(PATH_DEV_BYID)) == 0) ||
	     strncmp(dev, PATH_DEV_BYPATH, strlen(PATH_DEV_BYPATH)) == 0) {
	       p = "-part";
	}

	wp = strlen(p);

	if (lth) {
		snprintf(bufp, sizeof(bufp), "%*.*s%s%-2u",
			 lth-wp-2, w, dev, p, pno);
	} else {
		snprintf(bufp, sizeof(bufp), "%.*s%s%-2u", w, dev, p, pno);
	}
	return bufp;
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
