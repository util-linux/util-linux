/*
 * Copyright (C) 2008-2013 Karel Zak <kzak@redhat.com>
 *
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 */

/**
 * SECTION: init
 * @title: Library initialization
 * @short_description: initialize debuging
 */

#include <stdarg.h>

#include "blkidP.h"

int libblkid_debug_mask;

/**
 * blkid_init_debug:
 * @mask: debug mask (0xffff to enable full debuging)
 *
 * If the @mask is not specified then this function reads
 * LIBBLKID_DEBUG environment variable to get the mask.
 *
 * Already initialized debugging stuff cannot be changed. It does not
 * have effect to call this function twice.
 */
void blkid_init_debug(int mask)
{
	if (libblkid_debug_mask & BLKID_DEBUG_INIT)
		return;
	if (!mask) {
		char *str = getenv("LIBBLKID_DEBUG");
		if (str)
			libblkid_debug_mask = strtoul(str, 0, 0);
	} else
		libblkid_debug_mask = mask;

	libblkid_debug_mask |= BLKID_DEBUG_INIT;

	if (libblkid_debug_mask != BLKID_DEBUG_INIT) {
		const char *ver = NULL;
		const char *date = NULL;

		DBG(INIT, blkid_debug("library debug mask: 0x%04x",
				libblkid_debug_mask));

		blkid_get_library_version(&ver, &date);
		DBG(INIT, blkid_debug("library version: %s [%s]", ver, date));
	}
}
