/*
 * version.c - Return the version of the blkid library
 *
 * Copyright (C) 2008 Karel Zak <kzak@redhat.com>
 * [Based on libblkid/version.c by Theodore Ts'o]
 *
 * See COPYING.libmount for the License of this software.
 */

#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

#include "mountP.h"

static const char *lib_version = LIBMOUNT_VERSION;

/**
 * mnt_parse_version_string:
 * @ver_string: version string (e.g "2.18.0")
 *
 * Returns: release version code.
 */
int mnt_parse_version_string(const char *ver_string)
{
	const char *cp;
	int version = 0;

	for (cp = ver_string; *cp; cp++) {
		if (*cp == '.')
			continue;
		if (!isdigit(*cp))
			break;
		version = (version * 10) + (*cp - '0');
	}
	return version;
}

/**
 * mnt_get_library_version:
 * @ver_string: return pointer to the static library version string
 *
 * Returns: release version number.
 */
int mnt_get_library_version(const char **ver_string)
{
	if (ver_string)
		*ver_string = lib_version;

	return mnt_parse_version_string(lib_version);
}
