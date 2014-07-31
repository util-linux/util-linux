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

static const struct dbg_mask libblkid_masknames [] = {
	{ "all", BLKID_DEBUG_ALL },
	{ "cache", BLKID_DEBUG_CACHE },
	{ "dump", BLKID_DEBUG_DUMP },
	{ "dev", BLKID_DEBUG_DEV },
	{ "devname", BLKID_DEBUG_DEVNAME },
	{ "devno", BLKID_DEBUG_DEVNO },
	{ "probe", BLKID_DEBUG_PROBE },
	{ "read", BLKID_DEBUG_READ },
	{ "resolve", BLKID_DEBUG_RESOLVE },
	{ "save", BLKID_DEBUG_SAVE },
	{ "tag", BLKID_DEBUG_TAG },
	{ "lowprobe", BLKID_DEBUG_LOWPROBE },
	{ "config", BLKID_DEBUG_CONFIG },
	{ "evaluate", BLKID_DEBUG_EVALUATE },
	{ "init", BLKID_DEBUG_INIT },
	{ NULL, 0 }
};

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
