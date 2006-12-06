#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "common.h"

/*
 * return partition name - uses static storage unless buf is supplied
 */
static char *
partnamebf(char *dev, int pno, int lth, int bufsiz, char *bufp) {
	static char buffer[80];
	char *p;
	int w, wp;

	if (!bufp) {
		bufp = buffer;
		bufsiz = sizeof(buffer);
	}

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

	wp = strlen(p);
		
	if (lth) {
		sprintf(bufp, "%*.*s%s%-2u", lth-wp-2, w, dev, p, pno);
	} else {
		sprintf(bufp, "%.*s%s%-2u", w, dev, p, pno);
	}
	return bufp;
}

char *
partname(char *dev, int pno, int lth) {
	return partnamebf(dev, pno, lth, 0, NULL);
}
