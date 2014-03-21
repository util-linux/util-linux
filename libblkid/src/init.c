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

UL_DEBUG_DEFINE_MASK(libblkid);

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
	__UL_INIT_DEBUG(libblkid, BLKID_DEBUG_, mask, LIBBLKID_DEBUG);

	if (libblkid_debug_mask != BLKID_DEBUG_INIT) {
		const char *ver = NULL;
		const char *date = NULL;

		blkid_get_library_version(&ver, &date);
		DBG(INIT, ul_debug("library version: %s [%s]", ver, date));
	}
}
