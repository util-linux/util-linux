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

struct nvidia_meta {
	uint8_t		vendor[8];
	uint32_t	size;
	uint32_t	chksum;
	uint16_t	version;
};

#define NVIDIA_SIGNATURE		"NVIDIA"

static int probe_nvraid(blkid_probe pr, const struct blkid_idmag *mag)
{
	uint64_t meta_off;
	struct nvidia_meta *nv;

	if (pr->size < 0x10000)
		return -1;

	meta_off = ((pr->size / 0x200) - 2) * 0x200;
	nv = (struct nvidia_meta *) blkid_probe_get_buffer(pr, meta_off, 0x200);
	if (!nv)
		return -1;

	if (memcmp(nv->vendor, NVIDIA_SIGNATURE, sizeof(NVIDIA_SIGNATURE)-1) != 0)
		return -1;

	if (blkid_probe_sprintf_version(pr, "%u", le16_to_cpu(nv->version)) != 0)
		return -1;

	return 0;
}

const struct blkid_idinfo nvraid_idinfo = {
	.name		= "nvidia_raid_member",
	.usage		= BLKID_USAGE_RAID,
	.probefunc	= probe_nvraid
};


