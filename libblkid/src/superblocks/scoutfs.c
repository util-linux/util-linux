/*
 * Copyright (C) 2025 Versity, Inc.
 *
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 */

#include <inttypes.h>

#include "superblocks.h"
#include "crc32c.h"

enum {
	SCOUTFS_TYPE_DATA,
	SCOUTFS_TYPE_METADATA,
};

#define SCOUTFS_UUID_BYTES 16

#define SCOUTFS_BLOCK_SM_SHIFT 12
#define SCOUTFS_BLOCK_SM_SIZE  (1 << SCOUTFS_BLOCK_SM_SHIFT)
#define SCOUTFS_BLOCK_LG_SHIFT 16
#define SCOUTFS_BLOCK_LG_SIZE  (1 << SCOUTFS_BLOCK_LG_SHIFT)

struct scoutfs_block_header {
	uint32_t crc;
	uint32_t magic;
	uint64_t fsid;
	uint64_t seq;
	uint64_t blkno;
};

#define SCOUTFS_FLAG_IS_META_BDEV 0x01

struct scoutfs_super_block {
	struct scoutfs_block_header hdr;
	uint64_t id;
	uint64_t fmt_vers;
	uint64_t flags;
	uint8_t uuid[SCOUTFS_UUID_BYTES];
	/* rest omitted */
};

static int probe_scoutfs(blkid_probe pr, const struct blkid_idmag *mag)
{
	const struct scoutfs_super_block *sb;
	const unsigned char *buf;
	uint32_t crc;

	/* scoutfs_super_block is always in a SCOUTFS_BLOCK_SM_SIZE block */
	buf = blkid_probe_get_sb_buffer(pr, mag, SCOUTFS_BLOCK_SM_SIZE);
	if (buf == NULL)
		return errno ? -errno : 1;
	sb = (struct scoutfs_super_block *)buf;

	crc = crc32c(~0, (char *)buf + sizeof(sb->hdr.crc), SCOUTFS_BLOCK_SM_SIZE - sizeof(sb->hdr.crc));
	if (!blkid_probe_verify_csum(pr, crc, le32_to_cpu(sb->hdr.crc)))
		return BLKID_PROBE_NONE;

	blkid_probe_sprintf_version(pr, "%"PRIu64, le64_to_cpu(sb->fmt_vers));
	blkid_probe_set_uuid(pr, sb->uuid);
	blkid_probe_sprintf_value(pr, "FSID", "%016"PRIx64, le64_to_cpu(sb->hdr.fsid));
	blkid_probe_set_wiper(pr, 0, 0x10000);

	if (mag->hint == SCOUTFS_TYPE_METADATA) {
		/* meta blocksize is 64k blocks */
		blkid_probe_set_fsblocksize(pr, SCOUTFS_BLOCK_LG_SIZE);
		blkid_probe_set_block_size(pr, SCOUTFS_BLOCK_LG_SIZE);

		if (!(le64_to_cpu(sb->flags) & SCOUTFS_FLAG_IS_META_BDEV))
			return BLKID_PROBE_NONE;
	} else {
		/* data blocksize is 4k blocks */
		blkid_probe_set_fsblocksize(pr, SCOUTFS_BLOCK_SM_SIZE);
		blkid_probe_set_block_size(pr, SCOUTFS_BLOCK_SM_SIZE);

		if (le64_to_cpu(sb->flags) & SCOUTFS_FLAG_IS_META_BDEV)
			return BLKID_PROBE_NONE;
	}

	return 0;
}

/*
 * Scoutfs has the same magic value for the data and the meta devices,
 * and the superblock format used in them is identical, except for the
 * flag used to indicate the meta device superblock.
 */
const struct blkid_idinfo scoutfs_meta_idinfo =
{
	.name		= "scoutfs_meta",
	.usage		= BLKID_USAGE_FILESYSTEM,
	.probefunc	= probe_scoutfs,
	.minsz		= 0x20000,
	.magics		= {
		{
		  .magic	= "\x8b\x42\x3c\x10",
		  .hint         = SCOUTFS_TYPE_METADATA,
		  .kboff	= 64,
		  .sboff	= 4,
		  .len		= 4,
		},
		{ NULL }
	}
};

const struct blkid_idinfo scoutfs_data_idinfo =
{
	.name		= "scoutfs_data",
	.usage		= BLKID_USAGE_FILESYSTEM,
	.probefunc	= probe_scoutfs,
	.minsz		= 0x20000,
	.magics		= {
		{
		  .magic	= "\x8b\x42\x3c\x10",
		  .hint         = SCOUTFS_TYPE_DATA,
		  .kboff	= 64,
		  .sboff	= 4,
		  .len		= 4,
		},
		{ NULL }
	}
};
