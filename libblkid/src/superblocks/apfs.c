/*
 * Copyright (C) 2018 Harry Mallon <hjmallon@gmail.com>
 *
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 */

#include "superblocks.h"

const struct blkid_idinfo apfs_idinfo =
{
	.name		= "apfs",
	.usage		= BLKID_USAGE_FILESYSTEM,
	.magics		=
	{
		{ .magic = "NXSB", .len = 4, .sboff = 32 },
		{ NULL }
	}
};
