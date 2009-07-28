/*
 * Copyright (C) 2008 Karel Zak <kzak@redhat.com>
 *
 * Inspired by libvolume_id by
 *     Kay Sievers <kay.sievers@vrfy.org>
 *
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>

#include "superblocks.h"

/* http://www.snia.org/standards/home */
#define DDF_GUID_LENGTH			24
#define DDF_REV_LENGTH			8

struct ddf_header {
	uint8_t		signature[4];
	uint32_t	crc;
	uint8_t		guid[DDF_GUID_LENGTH];
	uint8_t		ddf_rev[DDF_REV_LENGTH];
} __attribute__((packed));

static int probe_ddf(blkid_probe pr, const struct blkid_idmag *mag)
{
	int hdrs[] = { 1, 257 };
	int i;
	struct ddf_header *ddf = NULL;
	char version[DDF_REV_LENGTH + 1];

	if (pr->size < 0x30000)
		return -1;

	for (i = 0; i < ARRAY_SIZE(hdrs); i++) {
		uint64_t off = ((pr->size / 0x200) - hdrs[i]) * 0x200;

		ddf = (struct ddf_header *) blkid_probe_get_buffer(pr,
					off,
					sizeof(struct ddf_header));
		if (!ddf)
			return -1;

		if (memcmp(ddf->signature, "\x11\xde\x11\xde", 4) == 0 ||
	            memcmp(ddf->signature, "\xde\x11\xde\x11", 4) == 0)
			break;
		ddf = NULL;
	}

	if (!ddf)
		return -1;

	blkid_probe_strncpy_uuid(pr, ddf->guid, sizeof(ddf->guid));

	memcpy(version, ddf->ddf_rev, sizeof(ddf->ddf_rev));
	*(version + sizeof(ddf->ddf_rev)) = '\0';

	if (blkid_probe_set_version(pr, version) != 0)
		return -1;
	return 0;
}

const struct blkid_idinfo ddfraid_idinfo = {
	.name		= "ddf_raid_member",
	.usage		= BLKID_USAGE_RAID,
	.probefunc	= probe_ddf,
	.magics		= BLKID_NONE_MAGIC
};


