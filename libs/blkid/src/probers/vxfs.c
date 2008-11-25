/*
 * Copyright (C) 1999, 2000, 2003 by Theodore Ts'o
 * Copyright (C) 2001 by Andreas Dilger
 * Copyright (C) 2004 Kay Sievers <kay.sievers@vrfy.org>
 * Copyright (C) 2008 Karel Zak <kzak@redhat.com>
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This file is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <stdio.h>

#include "blkidP.h"

struct vxfs_super {
	uint32_t		vs_magic;
	int32_t			vs_version;
};

static int probe_vxfs(blkid_probe pr, const struct blkid_idmag *mag)
{
	struct vxfs_super *vxs;

	vxs = blkid_probe_get_sb(pr, mag, struct vxfs_super);
	if (!vxs)
		return -1;

	blkid_probe_sprintf_version(pr, "%u", (unsigned int) vxs->vs_version);
	return 0;
}


const struct blkid_idinfo vxfs_idinfo =
{
	.name		= "vxfs",
	.usage		= BLKID_USAGE_FILESYSTEM,
	.probefunc	= probe_vxfs,
	.magics		=
	{
		{ .magic = "\365\374\001\245", .len = 4, .kboff = 1 },
		{ NULL }
	}
};

