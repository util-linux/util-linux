/*
 * Copyright (C) 2026 OpenSVC
 *
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 */

#include <stddef.h>

#include "superblocks.h"
#include "crc32c.h"

#define OPENSVC_HB_VERSION		1
#define OPENSVC_HB_BLOCK_SIZE		4096
#define OPENSVC_HB_SLOT_SIZE		(1024 * 1024)
#define OPENSVC_HB_MIN_BLOCK_SIZE	512
#define OPENSVC_HB_MAX_BLOCK_SIZE	(64 * 1024)
#define OPENSVC_HB_UUID_SIZE		16

struct opensvc_hb_header {
	uint32_t crc;
	uint8_t  magic[8];
	uint32_t version;
	uint32_t page_size;
	uint32_t slot_size;
	uint8_t  uuid[OPENSVC_HB_UUID_SIZE];
} __attribute__((packed));

static int opensvc_hb_verify_csum(blkid_probe pr, const struct opensvc_hb_header *header)
{
	uint32_t checksum;

	checksum = ul_crc32c_exclude_offset(~0U, (const unsigned char *) header, sizeof(struct opensvc_hb_header),
		offsetof(struct opensvc_hb_header, crc),
		sizeof(header->crc));
	checksum ^= ~0U;

	return blkid_probe_verify_csum(pr, checksum, le32_to_cpu(header->crc));
}

static int probe_opensvc_hb(blkid_probe pr, const struct blkid_idmag *mag)
{
	const struct opensvc_hb_header *header;
	uint32_t version;
	uint32_t page_size;

	header = blkid_probe_get_sb(pr, mag, struct opensvc_hb_header);
	if (!header)
		return errno ? -errno : BLKID_PROBE_NONE;

	if (!opensvc_hb_verify_csum(pr, header))
		return BLKID_PROBE_NONE;

	version = le32_to_cpu(header->version);
	page_size = le32_to_cpu(header->page_size);

	if (version != OPENSVC_HB_VERSION)
		return BLKID_PROBE_NONE;

	if (page_size < OPENSVC_HB_MIN_BLOCK_SIZE ||
		page_size > OPENSVC_HB_MAX_BLOCK_SIZE ||
		!is_power_of_2(page_size))
		return BLKID_PROBE_NONE;

	blkid_probe_sprintf_version(pr, "%u", version);
	blkid_probe_set_uuid(pr, header->uuid);
	blkid_probe_set_fsblocksize(pr, page_size);
	blkid_probe_set_block_size(pr, page_size);

	return BLKID_PROBE_OK;
}

const struct blkid_idinfo opensvc_hb_idinfo =
{
	.name      = "opensvc_hb",
	.usage     = BLKID_USAGE_OTHER,
	.probefunc = probe_opensvc_hb,
	.minsz     = sizeof(struct opensvc_hb_header),
	.magics    = {
	        {
	        	.magic = "\x3d\xc1\x3c\x87\xc0\x5b\xe3\xb6",
				.len   = 8,
				.sboff = 4,
			},
			{ NULL }
	}
};
