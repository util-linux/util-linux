/*
 * Copyright (C) 2010 Karel Zak <kzak@redhat.com>
 *
 * Released under the terms of the GNU General Public License version 2
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <err.h>

#include "nls.h"

long getnum(const char *str, const char *errmesg)
{
	long num;
	char *end = NULL;

	if (str == NULL || *str == '\0')
		goto err;
	errno = 0;
	num = strtol(str, &end, 10);

	if (errno || (end && *end))
		goto err;

	return num;
err:
	if (errno)
		err(EXIT_FAILURE, "%s: '%s'", errmesg, str);
	else
		errx(EXIT_FAILURE, "%s: '%s'", errmesg, str);
	return 0;
}
