/*
 * Copyright (C) 2010 Jeroen Oortwijn <oortwijn@gmail.com>
 *
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 */

#include "superblocks.h"

const struct blkid_idinfo befs_idinfo =
{
	.name		= "befs",
	.usage		= BLKID_USAGE_FILESYSTEM,
	.magics		= {
		{ .magic = "1SFB", .len = 4, .sboff = 0x220 },
		{ NULL }
	}
};
