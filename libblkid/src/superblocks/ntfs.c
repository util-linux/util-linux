/*
 * Copyright (C) 2004 Kay Sievers <kay.sievers@vrfy.org>
 * Copyright (C) 2008 Karel Zak <kzak@redhat.com>
 *
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <inttypes.h>

#include "superblocks.h"

struct ntfs_bios_parameters {
	uint16_t	sector_size;	/* Size of a sector in bytes. */
	uint8_t		sectors_per_cluster;	/* Size of a cluster in sectors. */
	uint16_t	reserved_sectors;	/* zero */
	uint8_t		fats;			/* zero */
	uint16_t	root_entries;		/* zero */
	uint16_t	sectors;		/* zero */
	uint8_t		media_type;		/* 0xf8 = hard disk */
	uint16_t	sectors_per_fat;	/* zero */
	uint16_t	sectors_per_track;	/* irrelevant */
	uint16_t	heads;			/* irrelevant */
	uint32_t	hidden_sectors;		/* zero */
	uint32_t	large_sectors;		/* zero */
} __attribute__ ((__packed__));

struct ntfs_super_block {
	uint8_t		jump[3];
	uint8_t		oem_id[8];	/* magic string */

	struct ntfs_bios_parameters	bpb;

	uint16_t	unused[2];
	uint64_t	number_of_sectors;
	uint64_t	mft_cluster_location;
	uint64_t	mft_mirror_cluster_location;
	int8_t		clusters_per_mft_record;
	uint8_t		reserved1[3];
	int8_t		cluster_per_index_record;
	uint8_t		reserved2[3];
	uint64_t	volume_serial;
	uint32_t	checksum;
} __attribute__((packed));

struct master_file_table_record {
	uint32_t	magic;
	uint16_t	usa_ofs;
	uint16_t	usa_count;
	uint64_t	lsn;
	uint16_t	sequence_number;
	uint16_t	link_count;
	uint16_t	attrs_offset;
	uint16_t	flags;
	uint32_t	bytes_in_use;
	uint32_t	bytes_allocated;
} __attribute__((__packed__));

struct file_attribute {
	uint32_t	type;
	uint32_t	len;
	uint8_t		non_resident;
	uint8_t		name_len;
	uint16_t	name_offset;
	uint16_t	flags;
	uint16_t	instance;
	uint32_t	value_len;
	uint16_t	value_offset;
} __attribute__((__packed__));

#define MFT_RECORD_VOLUME	3
#define NTFS_MAX_CLUSTER_SIZE	(64 * 1024)

enum {
	MFT_RECORD_ATTR_VOLUME_NAME		= cpu_to_le32(0x60),
	MFT_RECORD_ATTR_END			= cpu_to_le32(0xffffffff)
};

static int probe_ntfs(blkid_probe pr, const struct blkid_idmag *mag)
{
	struct ntfs_super_block *ns;
	struct master_file_table_record *mft;

	uint32_t sectors_per_cluster, mft_record_size, attr_off;
	uint16_t sector_size;
	uint64_t nr_clusters, off;
	unsigned char *buf_mft;

	ns = blkid_probe_get_sb(pr, mag, struct ntfs_super_block);
	if (!ns)
		return -1;

	/*
	 * Check bios parameters block
	 */
	sector_size = le16_to_cpu(ns->bpb.sector_size);
	sectors_per_cluster = ns->bpb.sectors_per_cluster;

	if (sector_size < 256 || sector_size > 4096)
		return 1;

	switch (sectors_per_cluster) {
	case 1: case 2: case 4: case 8: case 16: case 32: case 64: case 128:
		break;
	default:
		return 1;
	}

	if ((uint16_t) le16_to_cpu(ns->bpb.sector_size) *
			ns->bpb.sectors_per_cluster > NTFS_MAX_CLUSTER_SIZE)
		return 1;

	/* Unused fields must be zero */
	if (le16_to_cpu(ns->bpb.reserved_sectors)
	    || le16_to_cpu(ns->bpb.root_entries)
	    || le16_to_cpu(ns->bpb.sectors)
	    || le16_to_cpu(ns->bpb.sectors_per_fat)
	    || le32_to_cpu(ns->bpb.large_sectors)
	    || ns->bpb.fats)
		return 1;

	if ((uint8_t) ns->clusters_per_mft_record < 0xe1
	    || (uint8_t) ns->clusters_per_mft_record > 0xf7) {

		switch (ns->clusters_per_mft_record) {
		case 1: case 2: case 4: case 8: case 16: case 32: case 64:
			break;
		default:
			return 1;
		}
	}

	if (ns->clusters_per_mft_record > 0)
		mft_record_size = ns->clusters_per_mft_record *
				  sectors_per_cluster * sector_size;
	else
		mft_record_size = 1 << (0 - ns->clusters_per_mft_record);

	nr_clusters = le64_to_cpu(ns->number_of_sectors) / sectors_per_cluster;

	if ((le64_to_cpu(ns->mft_cluster_location) > nr_clusters) ||
	    (le64_to_cpu(ns->mft_mirror_cluster_location) > nr_clusters))
		return 1;


	off = le64_to_cpu(ns->mft_cluster_location) * sector_size *
		sectors_per_cluster;

	DBG(DEBUG_LOWPROBE, printf("NTFS: sector_size=%d, mft_record_size=%d, "
			"sectors_per_cluster=%d, nr_clusters=%ju "
			"cluster_offset=%jd\n",
			(int) sector_size, mft_record_size,
			sectors_per_cluster, nr_clusters,
			(intmax_t)off));

	buf_mft = blkid_probe_get_buffer(pr, off, mft_record_size);
	if (!buf_mft)
		return 1;

	if (memcmp(buf_mft, "FILE", 4))
		return 1;

	off += MFT_RECORD_VOLUME * mft_record_size;

	buf_mft = blkid_probe_get_buffer(pr, off, mft_record_size);
	if (!buf_mft)
		return 1;

	if (memcmp(buf_mft, "FILE", 4))
		return 1;

	mft = (struct master_file_table_record *) buf_mft;
	attr_off = le16_to_cpu(mft->attrs_offset);

	while (attr_off < mft_record_size &&
	       attr_off <= le32_to_cpu(mft->bytes_allocated)) {

		uint32_t attr_len;
		struct file_attribute *attr;

		attr = (struct file_attribute *) (buf_mft + attr_off);
		attr_len = le32_to_cpu(attr->len);
		if (!attr_len)
			break;

		if (attr->type == MFT_RECORD_ATTR_END)
			break;
		if (attr->type == MFT_RECORD_ATTR_VOLUME_NAME) {
			unsigned int val_off = le16_to_cpu(attr->value_offset);
			unsigned int val_len = le32_to_cpu(attr->value_len);
			unsigned char *val = ((uint8_t *) attr) + val_off;

			blkid_probe_set_utf8label(pr, val, val_len, BLKID_ENC_UTF16LE);
			break;
		}

		if (UINT_MAX - attr_len < attr_off)
			break;
		attr_off += attr_len;
	}

	blkid_probe_sprintf_uuid(pr,
			(unsigned char *) &ns->volume_serial,
			sizeof(ns->volume_serial),
			"%016" PRIX64, le64_to_cpu(ns->volume_serial));
	return 0;
}


const struct blkid_idinfo ntfs_idinfo =
{
	.name		= "ntfs",
	.usage		= BLKID_USAGE_FILESYSTEM,
	.probefunc	= probe_ntfs,
	.magics		=
	{
		{ .magic = "NTFS    ", .len = 8, .sboff = 3 },
		{ NULL }
	}
};

