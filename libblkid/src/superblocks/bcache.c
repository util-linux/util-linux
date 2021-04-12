/*
 * Copyright (C) 2013 Rolf Fokkens <rolf@fokkens.nl>
 *
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 *
 * Based on code fragments from bcache-tools by Kent Overstreet:
 * http://evilpiepirate.org/git/bcache-tools.git
 */

#include <stddef.h>
#include <stdio.h>

#include "superblocks.h"
#include "crc64.h"

#define SB_LABEL_SIZE      32

/*
 * The bcache_super_block is heavily simplified version of struct cache_sb in kernel.
 * https://github.com/torvalds/linux/blob/master/include/uapi/linux/bcache.h
 */
struct bcache_super_block {
	uint64_t		csum;
	uint64_t		offset;		/* where this super block was written */
	uint64_t		version;
	uint8_t			magic[16];	/* bcache file system identifier */
	uint8_t			uuid[16];	/* device identifier */
} __attribute__((packed));

struct bcachefs_super_block {
	uint8_t		csum[16];
	uint16_t	version;
	uint16_t	version_min;
	uint16_t	pad[2];
	uint8_t		magic[16];
	uint8_t		uuid[16];
	uint8_t		user_uuid[16];
	uint8_t		label[SB_LABEL_SIZE];
	uint64_t	offset;
	uint64_t	seq;
	uint16_t	block_size;
}  __attribute__((packed));

/* magic string */
#define BCACHE_SB_MAGIC     "\xc6\x85\x73\xf6\x4e\x1a\x45\xca\x82\x65\xf5\x7f\x48\xba\x6d\x81"
#define BCACHEFS_SB_MAGIC   "\x18\x00\x18\x00\x00\x00\x00\x00\xc6\x85\x73\xf6\x4e\x1a\x45\xca"
/* magic string len */
#define BCACHE_SB_MAGIC_LEN (sizeof(BCACHE_SB_MAGIC) - 1)
/* super block offset */
#define BCACHE_SB_OFF       0x1000
/* supper block offset in kB */
#define BCACHE_SB_KBOFF     (BCACHE_SB_OFF >> 10)
/* magic string offset within super block */
#define BCACHE_SB_MAGIC_OFF offsetof (struct bcache_super_block, magic)
/* start of checksummed data within superblock */
#define BCACHE_SB_CSUMMED_START 8
/* end of checksummed data within superblock */
#define BCACHE_SB_CSUMMED_END 208
/* granularity of offset and length fields within superblock */
#define BCACHEFS_SECTOR_SIZE   512

static int bcache_verify_checksum(blkid_probe pr, const struct blkid_idmag *mag,
		const struct bcache_super_block *bcs)
{
	unsigned char *csummed = blkid_probe_get_sb_buffer(pr, mag, BCACHE_SB_CSUMMED_END);
	uint64_t csum = ul_crc64_we(csummed + BCACHE_SB_CSUMMED_START,
			BCACHE_SB_CSUMMED_END - BCACHE_SB_CSUMMED_START);
	return blkid_probe_verify_csum(pr, csum, le64_to_cpu(bcs->csum));
}

static int probe_bcache (blkid_probe pr, const struct blkid_idmag *mag)
{
	struct bcache_super_block *bcs;

	bcs = blkid_probe_get_sb(pr, mag, struct bcache_super_block);
	if (!bcs)
		return errno ? -errno : BLKID_PROBE_NONE;

	if (!bcache_verify_checksum(pr, mag, bcs))
		return BLKID_PROBE_NONE;

	if (le64_to_cpu(bcs->offset) != BCACHE_SB_OFF / 512)
		return BLKID_PROBE_NONE;

	if (blkid_probe_set_uuid(pr, bcs->uuid) < 0)
		return BLKID_PROBE_NONE;

	blkid_probe_set_wiper(pr, 0, BCACHE_SB_OFF);

	return BLKID_PROBE_OK;
}

static int probe_bcachefs(blkid_probe pr, const struct blkid_idmag *mag)
{
	struct bcachefs_super_block *bcs;
	uint64_t blocksize;

	bcs = blkid_probe_get_sb(pr, mag, struct bcachefs_super_block);
	if (!bcs)
		return errno ? -errno : BLKID_PROBE_NONE;

	if (le64_to_cpu(bcs->offset) != BCACHE_SB_OFF / BCACHEFS_SECTOR_SIZE)
		return BLKID_PROBE_NONE;

	blkid_probe_set_uuid(pr, bcs->user_uuid);
	blkid_probe_set_label(pr, bcs->label, sizeof(bcs->label));
	blkid_probe_sprintf_version(pr, "%d", le16_to_cpu(bcs->version));
	blocksize = le16_to_cpu(bcs->block_size);
	blkid_probe_set_block_size(pr, blocksize * BCACHEFS_SECTOR_SIZE);
	blkid_probe_set_wiper(pr, 0, BCACHE_SB_OFF);

	return BLKID_PROBE_OK;
}

const struct blkid_idinfo bcache_idinfo =
{
	.name		= "bcache",
	.usage		= BLKID_USAGE_OTHER,
	.probefunc	= probe_bcache,
	.minsz		= 8192,
	.magics		=
	{
		{
			.magic = BCACHE_SB_MAGIC,
			.len   = BCACHE_SB_MAGIC_LEN,
			.kboff = BCACHE_SB_KBOFF,
			.sboff = BCACHE_SB_MAGIC_OFF
		},
		{ NULL }
	}
};

const struct blkid_idinfo bcachefs_idinfo =
{
	.name		= "bcachefs",
	.usage		= BLKID_USAGE_FILESYSTEM,
	.probefunc	= probe_bcachefs,
	.minsz		= 256 * BCACHEFS_SECTOR_SIZE,
	.magics		= {
		{
			.magic = BCACHE_SB_MAGIC,
			.len   = BCACHE_SB_MAGIC_LEN,
			.kboff = BCACHE_SB_KBOFF,
			.sboff = BCACHE_SB_MAGIC_OFF,
		},
		{
			.magic = BCACHEFS_SB_MAGIC,
			.len   = BCACHE_SB_MAGIC_LEN,
			.kboff = BCACHE_SB_KBOFF,
			.sboff = BCACHE_SB_MAGIC_OFF,
		},
		{ NULL }
	}
};
