/*
 * Portable xxxat() functions.
 *
 * No copyright is claimed.  This code is in the public domain; do with
 * it what you wish.
 *
 * Written by Karel Zak <kzak@redhat.com>
 */
#include <stdio.h>
#include <fcntl.h>

#include "at.h"
#include "c.h"

FILE *fopen_at(int dir, const char *filename, int flags,
			const char *mode)
{
	int fd = openat(dir, filename, flags);

	if (fd < 0)
		return NULL;

	return fdopen(fd, mode);
}

