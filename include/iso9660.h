#ifndef UTIL_LINUX_ISO_H
#define UTIL_LINUX_ISO_H

#include <stdbool.h>

#include "c.h"

static inline int isonum_721(unsigned char *p)
{
	return ((p[0] & 0xff)
		| ((p[1] & 0xff) << 8));
}

static inline int isonum_722(unsigned char *p)
{
	return ((p[1] & 0xff)
		| ((p[0] & 0xff) << 8));
}

static inline int isonum_723(unsigned char *p, bool check_match)
{
	int le = isonum_721(p);
	int be = isonum_722(p + 2);

	if (check_match && le != be)
		/* translation is useless */
		warnx("723error: le=%d be=%d", le, be);
	return (le);
}

static inline int isonum_731(unsigned char *p)
{
	return ((p[0] & 0xff)
		| ((p[1] & 0xff) << 8)
		| ((p[2] & 0xff) << 16)
		| ((p[3] & 0xff) << 24));
}

static inline int isonum_732(unsigned char *p)
{
	return ((p[3] & 0xff)
		| ((p[2] & 0xff) << 8)
		| ((p[1] & 0xff) << 16)
		| ((p[0] & 0xff) << 24));
}

static inline int isonum_733(unsigned char *p, bool check_match)
{
	int le = isonum_731(p);
	int be = isonum_732(p + 4);

	if (check_match && le != be)
		/* translation is useless */
		warnx("733error: le=%d be=%d", le, be);
	return(le);
}

#endif
