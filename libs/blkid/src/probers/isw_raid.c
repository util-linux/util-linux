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

struct isw_meta {
	uint8_t		sig[32];
	uint32_t	check_sum;
	uint32_t	mpb_size;
	uint32_t	family_num;
	uint32_t	generation_num;
};

#define ISW_SIGNATURE		"Intel Raid ISM Cfg Sig. "


static int probe_iswraid(blkid_probe pr, const struct blkid_idmag *mag)
{
	uint64_t meta_off;
	struct isw_meta *isw;

	if (pr->size < 0x10000)
		return -1;

	meta_off = ((pr->size / 0x200) - 2) * 0x200;

	if (pr->size < 0x10000)
		return -1;

	isw = (struct isw_meta *) blkid_probe_get_buffer(pr, meta_off, 0x200);
	if (!isw)
		return -1;

	if (memcmp(isw->sig, ISW_SIGNATURE, sizeof(ISW_SIGNATURE)-1) != 0)
		return -1;

	if (blkid_probe_sprintf_version(pr, "%6s",
			&isw->sig[sizeof(ISW_SIGNATURE)-1]) != 0)
		return -1;

	return 0;
}

const struct blkid_idinfo iswraid_idinfo = {
	.name		= "isw_raid_member",
	.usage		= BLKID_USAGE_RAID,
	.probefunc	= probe_iswraid,
	.magics		= BLKID_NONE_MAGIC
};


