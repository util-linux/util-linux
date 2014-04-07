/*
 * Copyright (C) 2014 Karel Zak <kzak@redhat.com>
 *
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 */

/**
 * SECTION: init
 * @title: Library initialization
 * @short_description: initialize debugging
 *
 * The library debug stuff.
 */

#include <stdarg.h>

#include "smartcolsP.h"

UL_DEBUG_DEFINE_MASK(libsmartcols);

/**
 * scols_init_debug:
 * @mask: debug mask (0xffff to enable full debugging)
 *
 * If the @mask is not specified, then this function reads
 * the LIBSMARTCOLS_DEBUG environment variable to get the mask.
 *
 * Already initialized debugging stuff cannot be changed. Calling
 * this function twice has no effect.
 */
void scols_init_debug(int mask)
{
	__UL_INIT_DEBUG(libsmartcols, SCOLS_DEBUG_, mask, LIBSMARTCOLS_DEBUG);

	if (libsmartcols_debug_mask != SCOLS_DEBUG_INIT) {
		const char *ver = NULL;

		scols_get_library_version(&ver);

		DBG(INIT, ul_debug("library version: %s", ver));
	}
}
