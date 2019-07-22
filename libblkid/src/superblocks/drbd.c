/*
 * Copyright (C) 2009 by Bastian Friedrich <bastian.friedrich@collax.com>
 *
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 *
 * defines, structs taken from drbd source; file names represent drbd source
 * files.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <inttypes.h>
#include <stddef.h>

#include "superblocks.h"

/*
 * drbd/linux/drbd.h
 */
#define DRBD_MAGIC 0x83740267

/*
 * user/drbdmeta.c
 * We support v08 and v09
 */
#define DRBD_MD_MAGIC_08         (DRBD_MAGIC+4)
#define DRBD_MD_MAGIC_84_UNCLEAN (DRBD_MAGIC+5)
#define DRBD_MD_MAGIC_09         (DRBD_MAGIC+6)
/* there is no DRBD_MD_MAGIC_09_UNCLEAN */

/*
 * drbd/linux/drbd.h
 */
enum drbd_uuid_index {
	UI_CURRENT,
	UI_BITMAP,
	UI_HISTORY_START,
	UI_HISTORY_END,
	UI_SIZE,		/* nl-packet: number of dirty bits */
	UI_FLAGS,		/* nl-packet: flags */
	UI_EXTENDED_SIZE	/* Everything. */
};


/*
 * Used by libblkid to avoid unnecessary padding at the end of the structs and
 * too large unused structs in memory.
 */
#define DRBD_MD_OFFSET 4096

/*
 * user/shared/drbdmeta.c
 * Minor modifications wrt. types
 */
struct md_on_disk_08 {
	uint64_t la_sect;         /* last agreed size. */
	uint64_t uuid[UI_SIZE];   /* UUIDs */
	uint64_t device_uuid;
	uint64_t reserved_u64_1;
	uint32_t flags;
	uint32_t magic;
	uint32_t md_size_sect;
	int32_t  al_offset;       /* signed sector offset to this block */
	uint32_t al_nr_extents;   /* important for restoring the AL */
	int32_t  bm_offset;       /* signed sector offset to the bitmap, from here */
	uint32_t bm_bytes_per_bit;
	uint32_t reserved_u32[4];

	/* Unnecessary for libblkid **
	 * char reserved[8 * 512 - (8*(UI_SIZE+3)+4*11)];
	 */
};

/*
 * linux/drbd.h, v9 only
 */
#define DRBD_PEERS_MAX 32
#define HISTORY_UUIDS DRBD_PEERS_MAX

/*
 * drbd-headers/drbd_meta_data.h
 * Minor modifications wrt. types
 */
struct peer_dev_md_on_disk_9 {
	uint64_t bitmap_uuid;
	uint64_t bitmap_dagtag;
	uint32_t flags;
	int32_t bitmap_index;
	uint32_t reserved_u32[2];
} __attribute__((packed));

struct meta_data_on_disk_9 {
	uint64_t effective_size;    /* last agreed size */
	uint64_t current_uuid;
	uint64_t reserved_u64[4];   /* to have the magic at the same position as in v07, and v08 */
	uint64_t device_uuid;
	uint32_t flags;             /* MDF */
	uint32_t magic;
	uint32_t md_size_sect;
	uint32_t al_offset;         /* offset to this block */
	uint32_t al_nr_extents;     /* important for restoring the AL */
	uint32_t bm_offset;         /* offset to the bitmap, from here */
	uint32_t bm_bytes_per_bit;  /* BM_BLOCK_SIZE */
	uint32_t la_peer_max_bio_size;   /* last peer max_bio_size */
	uint32_t bm_max_peers;
	int32_t node_id;

	/* see al_tr_number_to_on_disk_sector() */
	uint32_t al_stripes;
	uint32_t al_stripe_size_4k;

	uint32_t reserved_u32[2];

	struct peer_dev_md_on_disk_9 peers[DRBD_PEERS_MAX];
	uint64_t history_uuids[HISTORY_UUIDS];

	/* Unnecessary for libblkid **
	 * char padding[0] __attribute__((aligned(4096)));
	 */
} __attribute__((packed));


static int probe_drbd_84(blkid_probe pr)
{
	struct md_on_disk_08 *md;
	off_t off;

	off = pr->size - DRBD_MD_OFFSET;

	/* Small devices cannot be drbd (?) */
	if (pr->size < 0x10000)
		return 1;

	md = (struct md_on_disk_08 *)
			blkid_probe_get_buffer(pr,
					off,
					sizeof(struct md_on_disk_08));
	if (!md)
		return errno ? -errno : 1;

	if (be32_to_cpu(md->magic) != DRBD_MD_MAGIC_08 &&
			be32_to_cpu(md->magic) != DRBD_MD_MAGIC_84_UNCLEAN)
		return 1;

	/*
	 * DRBD does not have "real" uuids; the following resembles DRBD's
	 * notion of uuids (64 bit, see struct above)
	 */
	blkid_probe_sprintf_uuid(pr,
		(unsigned char *) &md->device_uuid, sizeof(md->device_uuid),
		"%" PRIx64, be64_to_cpu(md->device_uuid));

	blkid_probe_set_version(pr, "v08");

	if (blkid_probe_set_magic(pr,
				off + offsetof(struct md_on_disk_08, magic),
				sizeof(md->magic),
				(unsigned char *) &md->magic))
		return 1;

	return 0;
}

static int probe_drbd_90(blkid_probe pr)
{
	struct meta_data_on_disk_9 *md;
	off_t off;

	off = pr->size - DRBD_MD_OFFSET;

	/*
	 * Smaller ones are certainly not DRBD9 devices.
	 * Recent utils even refuse to generate larger ones,
	 * keep this as a sufficient lower bound.
	 */
	if (pr->size < 0x10000)
		return 1;

	md = (struct meta_data_on_disk_9 *)
			blkid_probe_get_buffer(pr,
					off,
					sizeof(struct meta_data_on_disk_9));
	if (!md)
		return errno ? -errno : 1;

	if (be32_to_cpu(md->magic) != DRBD_MD_MAGIC_09)
		return 1;

	/*
	 * DRBD does not have "real" uuids; the following resembles DRBD's
	 * notion of uuids (64 bit, see struct above)
	 */
	blkid_probe_sprintf_uuid(pr,
		(unsigned char *) &md->device_uuid, sizeof(md->device_uuid),
		"%" PRIx64, be64_to_cpu(md->device_uuid));

	blkid_probe_set_version(pr, "v09");

	if (blkid_probe_set_magic(pr,
				off + offsetof(struct meta_data_on_disk_9, magic),
				sizeof(md->magic),
				(unsigned char *) &md->magic))
		return 1;

	return 0;
}

static int probe_drbd(blkid_probe pr,
		const struct blkid_idmag *mag __attribute__((__unused__)))
{
	int ret;

	ret = probe_drbd_84(pr);
	if (ret <= 0) /* success or fatal (-errno) */
		return ret;

	return probe_drbd_90(pr);
}

const struct blkid_idinfo drbd_idinfo =
{
	.name		= "drbd",
	.usage		= BLKID_USAGE_RAID,
	.probefunc	= probe_drbd,
	.magics		= BLKID_NONE_MAGIC
};

