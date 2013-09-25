/*
 * Copyright (C) 2008 Karel Zak <kzak@redhat.com>
 *
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 */

/**
 * SECTION: init
 * @title: Library initialization
 * @short_description: initialize debugging
 */

#include <stdarg.h>

#include "mountP.h"

int libmount_debug_mask;

/**
 * mnt_init_debug:
 * @mask: debug mask (0xffff to enable full debugging)
 *
 * If the @mask is not specified, then this function reads
 * the LIBMOUNT_DEBUG environment variable to get the mask.
 *
 * Already initialized debugging stuff cannot be changed. Calling
 * this function twice has no effect.
 */
void mnt_init_debug(int mask)
{
	if (libmount_debug_mask & MNT_DEBUG_INIT)
		return;
	if (!mask) {
		char *str = getenv("LIBMOUNT_DEBUG");
		if (str)
			libmount_debug_mask = strtoul(str, 0, 0);
	} else
		libmount_debug_mask = mask;

	libmount_debug_mask |= MNT_DEBUG_INIT;

	if (libmount_debug_mask != MNT_DEBUG_INIT) {
		const char *ver = NULL;
		const char **features = NULL, **p;

		DBG(INIT, mnt_debug("library debug mask: 0x%04x",
				libmount_debug_mask));

		mnt_get_library_version(&ver);
		mnt_get_library_features(&features);

		DBG(INIT, mnt_debug("library version: %s", ver));
		p = features;
		while (p && *p)
			DBG(INIT, mnt_debug("    feature: %s", *p++));
	}
}
