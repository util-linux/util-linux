/*
 * Copyright (C) 2009 Karel Zak <kzak@redhat.com>
 *
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <inttypes.h>

#ifdef HAVE_LINUX_BLKZONED_H
#include <linux/blkzoned.h>
#endif

#include "superblocks.h"

struct btrfs_super_block {
	uint8_t csum[32];
	uint8_t fsid[16];
	uint64_t bytenr;
	uint64_t flags;
	uint8_t magic[8];
	uint64_t generation;
	uint64_t root;
	uint64_t chunk_root;
	uint64_t log_root;
	uint64_t log_root_transid;
	uint64_t total_bytes;
	uint64_t bytes_used;
	uint64_t root_dir_objectid;
	uint64_t num_devices;
	uint32_t sectorsize;
	uint32_t nodesize;
	uint32_t leafsize;
	uint32_t stripesize;
	uint32_t sys_chunk_array_size;
	uint64_t chunk_root_generation;
	uint64_t compat_flags;
	uint64_t compat_ro_flags;
	uint64_t incompat_flags;
	uint16_t csum_type;
	uint8_t root_level;
	uint8_t chunk_root_level;
	uint8_t log_root_level;
	struct btrfs_dev_item {
		uint64_t devid;
		uint64_t total_bytes;
		uint64_t bytes_used;
		uint32_t io_align;
		uint32_t io_width;
		uint32_t sector_size;
		uint64_t type;
		uint64_t generation;
		uint64_t start_offset;
		uint32_t dev_group;
		uint8_t seek_speed;
		uint8_t bandwidth;
		uint8_t uuid[16];
		uint8_t fsid[16];
	} __attribute__ ((__packed__)) dev_item;
	uint8_t label[256];
} __attribute__ ((__packed__));

#define BTRFS_SUPER_INFO_SIZE 4096

/* Number of superblock log zones */
#define BTRFS_NR_SB_LOG_ZONES 2

/* Introduce some macros and types to unify the code with kernel side */
#define SECTOR_SHIFT 9

typedef uint64_t sector_t;

#ifdef HAVE_LINUX_BLKZONED_H
static int sb_write_pointer(blkid_probe pr, struct blk_zone *zones, uint64_t *wp_ret)
{
	bool empty[BTRFS_NR_SB_LOG_ZONES];
	bool full[BTRFS_NR_SB_LOG_ZONES];
	sector_t sector;

	assert(zones[0].type != BLK_ZONE_TYPE_CONVENTIONAL &&
	       zones[1].type != BLK_ZONE_TYPE_CONVENTIONAL);

	empty[0] = zones[0].cond == BLK_ZONE_COND_EMPTY;
	empty[1] = zones[1].cond == BLK_ZONE_COND_EMPTY;
	full[0] = zones[0].cond == BLK_ZONE_COND_FULL;
	full[1] = zones[1].cond == BLK_ZONE_COND_FULL;

	/*
	 * Possible states of log buffer zones
	 *
	 *           Empty[0]  In use[0]  Full[0]
	 * Empty[1]         *          x        0
	 * In use[1]        0          x        0
	 * Full[1]          1          1        C
	 *
	 * Log position:
	 *   *: Special case, no superblock is written
	 *   0: Use write pointer of zones[0]
	 *   1: Use write pointer of zones[1]
	 *   C: Compare super blocks from zones[0] and zones[1], use the latest
	 *      one determined by generation
	 *   x: Invalid state
	 */

	if (empty[0] && empty[1]) {
		/* Special case to distinguish no superblock to read */
		*wp_ret = zones[0].start << SECTOR_SHIFT;
		return -ENOENT;
	} else if (full[0] && full[1]) {
		/* Compare two super blocks */
		struct btrfs_super_block *super[BTRFS_NR_SB_LOG_ZONES];
		int i;

		for (i = 0; i < BTRFS_NR_SB_LOG_ZONES; i++) {
			uint64_t bytenr;

			bytenr = ((zones[i].start + zones[i].len)
				   << SECTOR_SHIFT) - BTRFS_SUPER_INFO_SIZE;

			super[i] = (struct btrfs_super_block *)
				blkid_probe_get_buffer(pr, bytenr, BTRFS_SUPER_INFO_SIZE);
			if (!super[i])
				return -EIO;
			DBG(LOWPROBE, ul_debug("(btrfs) checking #%d zone "
						"[start=%" PRIu64", len=%" PRIu64", sb-offset=%" PRIu64"]",
						i, (uint64_t) zones[i].start,
						(uint64_t) zones[i].len, bytenr));
		}

		if (super[0]->generation > super[1]->generation)
			sector = zones[1].start;
		else
			sector = zones[0].start;
	} else if (!full[0] && (empty[1] || full[1])) {
		sector = zones[0].wp;
	} else if (full[0]) {
		sector = zones[1].wp;
	} else {
		return -EUCLEAN;
	}
	*wp_ret = sector << SECTOR_SHIFT;

	DBG(LOWPROBE, ul_debug("(btrfs) write pointer: %" PRIu64" sector", sector));
	return 0;
}

static int sb_log_offset(blkid_probe pr, uint64_t *bytenr_ret)
{
	uint32_t zone_num = 0;
	uint32_t zone_size_sector;
	struct blk_zone_report *rep;
	struct blk_zone *zones;
	int ret;
	int i;
	uint64_t wp;


	zone_size_sector = pr->zone_size >> SECTOR_SHIFT;
	rep = blkdev_get_zonereport(pr->fd, zone_num * zone_size_sector, 2);
	if (!rep) {
		ret = -errno;
		goto out;
	}
	zones = (struct blk_zone *)(rep + 1);

	/*
	 * Use the head of the first conventional zone, if the zones
	 * contain one.
	 */
	for (i = 0; i < BTRFS_NR_SB_LOG_ZONES; i++) {
		if (zones[i].type == BLK_ZONE_TYPE_CONVENTIONAL) {
			DBG(LOWPROBE, ul_debug("(btrfs) checking conventional zone"));
			*bytenr_ret = zones[i].start << SECTOR_SHIFT;
			ret = 0;
			goto out;
		}
	}

	ret = sb_write_pointer(pr, zones, &wp);
	if (ret != -ENOENT && ret) {
		ret = 1;
		goto out;
	}
	if (ret != -ENOENT) {
		if (wp == zones[0].start << SECTOR_SHIFT)
			wp = (zones[1].start + zones[1].len) << SECTOR_SHIFT;
		wp -= BTRFS_SUPER_INFO_SIZE;
	}
	*bytenr_ret = wp;

	ret = 0;
out:
	free(rep);

	return ret;
}
#endif

static int probe_btrfs(blkid_probe pr, const struct blkid_idmag *mag)
{
	struct btrfs_super_block *bfs;

	if (pr->zone_size) {
#ifdef HAVE_LINUX_BLKZONED_H
		uint64_t offset = 0;
		int ret;

		ret = sb_log_offset(pr, &offset);
		if (ret)
			return ret;
		bfs = (struct btrfs_super_block *)
			blkid_probe_get_buffer(pr, offset,
					       sizeof(struct btrfs_super_block));
#else
		/* Nothing can be done */
		return 1;
#endif
	} else {
		bfs = blkid_probe_get_sb(pr, mag, struct btrfs_super_block);
	}
	if (!bfs)
		return errno ? -errno : 1;

	if (*bfs->label)
		blkid_probe_set_label(pr,
				(unsigned char *) bfs->label,
				sizeof(bfs->label));

	blkid_probe_set_uuid(pr, bfs->fsid);
	blkid_probe_set_uuid_as(pr, bfs->dev_item.uuid, "UUID_SUB");
	blkid_probe_set_block_size(pr, le32_to_cpu(bfs->sectorsize));

	return 0;
}

const struct blkid_idinfo btrfs_idinfo =
{
	.name		= "btrfs",
	.usage		= BLKID_USAGE_FILESYSTEM,
	.probefunc	= probe_btrfs,
	.minsz		= 1024 * 1024,
	.magics		=
	{
	  { .magic = "_BHRfS_M", .len = 8, .sboff = 0x40, .kboff = 64 },
	  /* For zoned btrfs */
	  { .magic = "_BHRfS_M", .len = 8, .sboff = 0x40,
	    .is_zoned = 1, .zonenum = 0, .kboff_inzone = 0 },
	  { .magic = "_BHRfS_M", .len = 8, .sboff = 0x40,
	    .is_zoned = 1, .zonenum = 1, .kboff_inzone = 0 },
	  { NULL }
	}
};

