/*
 * Copyright (C) 2008 Karel Zak <kzak@redhat.com>
 * Copyright (C) 2004-2006 Heinz Mauelshagen, Red Hat GmbH
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

struct via_meta {
	uint16_t	signature;
	uint8_t		version_number;
	struct via_array {
		uint16_t	disk_bit_mask;
		uint8_t		disk_array_ex;
		uint32_t	capacity_low;
		uint32_t	capacity_high;
		uint32_t	serial_checksum;
	} array;
	uint32_t	serial_checksum[8];
	uint8_t		checksum;
};

#define VIA_SIGNATURE		0xAA55

/* 8 bit checksum on first 50 bytes of metadata. */
static uint8_t meta_checksum(struct via_meta *via)
{
	uint8_t i = 50, sum = 0;

	while (i--)
		sum += ((uint8_t*) via)[i];

	return sum == via->checksum;
}

static int probe_viaraid(blkid_probe pr, const struct blkid_idmag *mag)
{
	uint64_t meta_off;
	struct via_meta *via;

	if (pr->size < 0x10000)
		return -1;

	meta_off = ((pr->size / 0x200)-1) * 0x200;

	via = (struct via_meta *) blkid_probe_get_buffer(pr, meta_off, 0x200);
	if (!via)
		return -1;
	if (le16_to_cpu(via->signature) != VIA_SIGNATURE)
		return -1;
	if (via->version_number > 1)
		return -1;
	if (!meta_checksum(via))
		return -1;
	if (blkid_probe_sprintf_version(pr, "%u", via->version_number) != 0)
		return -1;
	return 0;
}

const struct blkid_idinfo viaraid_idinfo = {
	.name		= "via_raid_member",
	.usage		= BLKID_USAGE_RAID,
	.probefunc	= probe_viaraid
};


