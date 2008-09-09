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
#include <errno.h>
#include <ctype.h>
#include <stdint.h>

#include "blkidP.h"

struct promise_meta {
	uint8_t	sig[24];
};

#define PDC_CONFIG_OFF		0x1200
#define PDC_SIGNATURE		"Promise Technology, Inc."

static int probe_pdcraid(blkid_probe pr, const struct blkid_idmag *mag)
{
	unsigned int i;
	static unsigned int sectors[] = {
		63, 255, 256, 16, 399, 0
	};

	if (pr->size < 0x40000)
		return -1;

	for (i = 0; sectors[i] != 0; i++) {
		uint64_t meta_off;
		struct promise_meta *pdc;

		meta_off = ((pr->size / 0x200) - sectors[i]) * 0x200;
		pdc = (struct promise_meta *)
				blkid_probe_get_buffer(pr, meta_off, 0x200);
		if (!pdc)
			return -1;

		if (memcmp(pdc->sig, PDC_SIGNATURE,
				sizeof(PDC_SIGNATURE) - 1) == 0)
			return 0;
	}
	return -1;
}

const struct blkid_idinfo pdcraid_idinfo = {
	.name		= "promise_fasttrack_raid_member",
	.usage		= BLKID_USAGE_RAID,
	.probefunc	= probe_pdcraid,
	.magics		= BLKID_NONE_MAGIC
};


