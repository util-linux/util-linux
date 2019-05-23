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

#define SB_LABEL_SIZE      32
#define SB_JOURNAL_BUCKETS 256U

#define node(i, j)         ((i)->d + (j))
#define end(i)             node(i, le16_to_cpu((i)->keys))

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
};

/* magic string */
#define BCACHE_SB_MAGIC     "\xc6\x85\x73\xf6\x4e\x1a\x45\xca\x82\x65\xf5\x7f\x48\xba\x6d\x81"
/* magic string len */
#define BCACHE_SB_MAGIC_LEN (sizeof(BCACHE_SB_MAGIC) - 1)
/* super block offset */
#define BCACHE_SB_OFF       0x1000
/* supper block offset in kB */
#define BCACHE_SB_KBOFF     (BCACHE_SB_OFF >> 10)
/* magic string offset within super block */
#define BCACHE_SB_MAGIC_OFF offsetof (struct bcache_super_block, magic)

static int probe_bcache (blkid_probe pr, const struct blkid_idmag *mag)
{
	struct bcache_super_block *bcs;

	bcs = blkid_probe_get_sb(pr, mag, struct bcache_super_block);
	if (!bcs)
		return errno ? -errno : BLKID_PROBE_NONE;

	if (le64_to_cpu(bcs->offset) != BCACHE_SB_OFF / 512)
		return BLKID_PROBE_NONE;

	if (blkid_probe_set_uuid(pr, bcs->uuid) < 0)
		return BLKID_PROBE_NONE;

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

