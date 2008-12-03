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

#include "blkidP.h"

/* http://www.snia.org/standards/home */
#define DDF_HEADER			0xDE11DE11
#define DDF_GUID_LENGTH			24
#define DDF_REV_LENGTH			8

struct ddf_header {
	uint32_t	signature;
	uint32_t	crc;
	uint8_t		guid[DDF_GUID_LENGTH];
	uint8_t		ddf_rev[DDF_REV_LENGTH];
};

static int probe_ddf(blkid_probe pr, const struct blkid_idmag *mag)
{
	uint64_t off;
	struct ddf_header *ddf;

	if (pr->size < 0x10000)
		return -1;

	off = ((pr->size / 0x200) - 1) * 0x200;
	ddf = (struct ddf_header *)
			blkid_probe_get_buffer(pr,
					off,
					sizeof(struct ddf_header));
	if (!ddf)
		return -1;
	if (ddf->signature != cpu_to_be32(DDF_HEADER))
		return -1;
	if (blkid_probe_sprintf_uuid(pr, ddf->guid,
				sizeof(ddf->guid), "%s", ddf->guid) != 0)
		return -1;
	if (blkid_probe_set_version(pr, (char *) ddf->ddf_rev) != 0)
		return -1;
	return 0;
}

const struct blkid_idinfo ddfraid_idinfo = {
	.name		= "ddf_raid_member",
	.usage		= BLKID_USAGE_RAID,
	.probefunc	= probe_ddf,
	.magics		= BLKID_NONE_MAGIC
};


