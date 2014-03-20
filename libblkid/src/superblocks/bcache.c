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
#define SB_JOURNAL_BUCKETS 256U

#define node(i, j)         ((i)->d + (j))
#define end(i)             node(i, (i)->keys)

static const char bcache_magic[] = {
	0xc6, 0x85, 0x73, 0xf6, 0x4e, 0x1a, 0x45, 0xca,
	0x82, 0x65, 0xf5, 0x7f, 0x48, 0xba, 0x6d, 0x81
};

struct bcache_super_block {
	uint64_t		csum;
	uint64_t		offset;	/* sector where this sb was written */
	uint64_t		version;

	uint8_t			magic[16];

	uint8_t			uuid[16];
	union {
		uint8_t		set_uuid[16];
		uint64_t	set_magic;
	};
	uint8_t			label[SB_LABEL_SIZE];

	uint64_t		flags;
	uint64_t		seq;
	uint64_t		pad[8];

	union {
	struct {
		/* Cache devices */
		uint64_t	nbuckets;	/* device size */

		uint16_t	block_size;	/* sectors */
		uint16_t	bucket_size;	/* sectors */

		uint16_t	nr_in_set;
		uint16_t	nr_this_dev;
	};
	struct {
		/* Backing devices */
		uint64_t	data_offset;

		/*
		 * block_size from the cache device section is still used by
		 * backing devices, so don't add anything here until we fix
		 * things to not need it for backing devices anymore
		 */
	};
	};

	uint32_t		last_mount;	/* time_t */

	uint16_t		first_bucket;
	union {
		uint16_t	njournal_buckets;
		uint16_t	keys;
	};
	uint64_t		d[SB_JOURNAL_BUCKETS];	/* journal buckets */
};

/* magic string */
#define BCACHE_SB_MAGIC     bcache_magic
/* magic string len */
#define BCACHE_SB_MAGIC_LEN sizeof (bcache_magic)
/* super block offset */
#define BCACHE_SB_OFF       0x1000
/* supper block offset in kB */
#define BCACHE_SB_KBOFF     (BCACHE_SB_OFF >> 10)
/* magic string offset within super block */
#define BCACHE_SB_MAGIC_OFF offsetof (struct bcache_super_block, magic)

static uint64_t bcache_crc64(struct bcache_super_block *bcs)
{
	unsigned char *data = (unsigned char *) bcs;
	size_t sz;

	data += 8;		/* skip csum field */
	sz = (unsigned char *) end(bcs) - data;

	return crc64(0xFFFFFFFFFFFFFFFFULL, data, sz) ^ 0xFFFFFFFFFFFFFFFFULL;
}

static int probe_bcache (blkid_probe pr, const struct blkid_idmag *mag)
{
	struct bcache_super_block *bcs;

	bcs = blkid_probe_get_sb(pr, mag, struct bcache_super_block);
	if (!bcs)
		return errno ? -errno : BLKID_PROBE_NONE;

	if (le64_to_cpu(bcs->offset) != BCACHE_SB_OFF / 512)
		return BLKID_PROBE_NONE;
	if (!blkid_probe_verify_csum(pr, bcache_crc64(bcs), le64_to_cpu(bcs->csum)))
		return BLKID_PROBE_NONE;

	if (blkid_probe_set_uuid(pr, bcs->uuid) < 0)
		return BLKID_PROBE_NONE;

	return BLKID_PROBE_OK;
};

const struct blkid_idinfo bcache_idinfo =
{
	.name		= "bcache",
	.usage		= BLKID_USAGE_OTHER,
	.probefunc	= probe_bcache,
	.minsz		= 8192,
	.magics		=
	{
		{ .magic = BCACHE_SB_MAGIC
		, .len   = BCACHE_SB_MAGIC_LEN
		, .kboff = BCACHE_SB_KBOFF
		, .sboff = BCACHE_SB_MAGIC_OFF
		} ,
		{ NULL }
	}
};

