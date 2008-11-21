/*
 * Copyright (C) 2008 Karel Zak <kzak@redhat.com>
 * Copyright (C) 2007 Kay Sievers <kay.sievers@vrfy.org>
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
#include <stdint.h>

#include "blkidP.h"

struct hpt45x_meta {
	uint32_t	magic;
};

#define HPT37X_CONFIG_OFF		0x1200
#define HPT37X_MAGIC_OK			0x5a7816f0
#define HPT37X_MAGIC_BAD		0x5a7816fd

#define HPT45X_MAGIC_OK			0x5a7816f3
#define HPT45X_MAGIC_BAD		0x5a7816fd

static int probe_highpoint45x(blkid_probe pr, const struct blkid_idmag *mag)
{
	const uint8_t *buf;
	struct hpt45x_meta *hpt;
	uint64_t meta_off;
	uint32_t magic;

	if (pr->size < 0x10000)
		return -1;

	meta_off = ((pr->size / 0x200) - 11) * 0x200;
	buf = blkid_probe_get_buffer(pr, meta_off, 0x200);
	if (buf == NULL)
		return -1;

	hpt = (struct hpt45x_meta *) buf;
	magic = le32_to_cpu(hpt->magic);
	if (magic != HPT45X_MAGIC_OK && magic != HPT45X_MAGIC_BAD)
		return -1;
	return 0;
}

const struct blkid_idinfo highpoint45x_idinfo = {
	.name		= "highpoint_raid_member",
	.usage		= BLKID_USAGE_RAID,
	.probefunc	= probe_highpoint45x,
	.magics		= BLKID_NONE_MAGIC
};

const struct blkid_idinfo highpoint37x_idinfo = {
	.name		= "highpoint_raid_member",
	.usage		= BLKID_USAGE_RAID,
	.magics		= {
		{ .magic = "\xf0\x16\x78\x5a", .len = 4, .sboff = 32 },
		{ .magic = "\xfd\x16\x78\x5a", .len = 4, .sboff = 32 },
		{ NULL }
	}
};


