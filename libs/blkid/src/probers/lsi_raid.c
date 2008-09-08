/*
 * Copyright (C) 2008 Karel Zak <kzak@redhat.com>
 * Copyright (C) 2005 Kay Sievers <kay.sievers@vrfy.org>
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

struct lsi_meta {
	uint8_t		sig[6];
};


#define LSI_SIGNATURE		"$XIDE$"

static int probe_lsiraid(blkid_probe pr, const struct blkid_idmag *mag)
{
	uint64_t meta_off;
	struct lsi_meta *lsi;

	if (pr->size < 0x10000)
		return -1;

	meta_off = ((pr->size / 0x200) - 1) * 0x200;
	lsi = (struct lsi_meta *) blkid_probe_get_buffer(pr, meta_off, 0x200);
	if (!lsi)
		return -1;

	if (memcmp(lsi->sig, LSI_SIGNATURE, sizeof(LSI_SIGNATURE)-1) != 0)
		return -1;

	return 0;
}

const struct blkid_idinfo lsiraid_idinfo = {
	.name		= "lsi_mega_raid_member",
	.usage		= BLKID_USAGE_RAID,
	.probefunc	= probe_lsiraid
};


