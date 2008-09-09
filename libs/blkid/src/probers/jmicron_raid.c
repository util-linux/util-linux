/*
 * Copyright (C) 2008 Karel Zak <kzak@redhat.com>
 * Copyright (C) 2006 Kay Sievers <kay.sievers@vrfy.org>
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


struct jmicron_meta {
	int8_t		signature[2];
	uint8_t		minor_version;
	uint8_t		major_version;
	uint16_t	checksum;
};

#define JM_SIGNATURE		"JM"

static int probe_jmraid(blkid_probe pr, const struct blkid_idmag *mag)
{
	uint64_t meta_off;
	struct jmicron_meta *jm;

	if (pr->size < 0x10000)
		return -1;

	meta_off = ((pr->size / 0x200) - 1) * 0x200;
	jm = (struct jmicron_meta *) blkid_probe_get_buffer(pr, meta_off, 0x200);
	if (!jm)
		return -1;

	if (memcmp(jm->signature, JM_SIGNATURE, sizeof(JM_SIGNATURE) - 1) != 0)
		return -1;


	if (blkid_probe_sprintf_version(pr, "%u.%u",
				jm->major_version, jm->minor_version) != 0)
		return -1;

	return 0;
}

const struct blkid_idinfo jmraid_idinfo = {
	.name		= "jmicron_raid_member",
	.usage		= BLKID_USAGE_RAID,
	.probefunc	= probe_jmraid,
	.magics		= BLKID_NONE_MAGIC
};


