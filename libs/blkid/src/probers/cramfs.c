/*
 * Copyright (C) 2008 Karel Zak <kzak@redhat.com>
 * Copyright (C) 2004-2007 Kay Sievers <kay.sievers@vrfy.org>
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
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <stdint.h>

#include "blkidP.h"

struct cramfs_super
{
	uint8_t		magic[4];
	uint32_t	size;
	uint32_t	flags;
	uint32_t	future;
	uint8_t		signature[16];
	struct cramfs_info
	{
		uint32_t	crc;
		uint32_t	edition;
		uint32_t	blocks;
		uint32_t	files;
	} info;
	uint8_t		name[16];
};

static int probe_cramfs(blkid_probe pr, const struct blkid_idmag *mag)
{
	struct cramfs_super *cs;

	cs = blkid_probe_get_sb(pr, mag, struct cramfs_super);
	if (!cs)
		return -1;

	blkid_probe_set_label(pr, cs->name, sizeof(cs->name));
	return 0;
}

const struct blkid_idinfo cramfs_idinfo =
{
	.name		= "cramfs",
	.usage		= BLKID_USAGE_FILESYSTEM,
	.probefunc	= probe_cramfs,
	.magics		=
	{
		{ "\x45\x3d\xcd\x28", 4, 0, 0 },
		{ "\x28\xcd\x3d\x45", 4, 0, 0 },
		{ NULL }
	}
};


