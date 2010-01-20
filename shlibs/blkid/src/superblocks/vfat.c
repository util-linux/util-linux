/*
 * Copyright (C) 1999 by Andries Brouwer
 * Copyright (C) 1999, 2000, 2003 by Theodore Ts'o
 * Copyright (C) 2001 by Andreas Dilger
 * Copyright (C) 2004 Kay Sievers <kay.sievers@vrfy.org>
 * Copyright (C) 2008 Karel Zak <kzak@redhat.com>
 *
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <stdint.h>

#include "superblocks.h"

/* {msdos,vfat}_super_block is defined in ../fat.h */
#include "fat.h"

struct vfat_dir_entry {
	uint8_t		name[11];
	uint8_t		attr;
	uint16_t	time_creat;
	uint16_t	date_creat;
	uint16_t	time_acc;
	uint16_t	date_acc;
	uint16_t	cluster_high;
	uint16_t	time_write;
	uint16_t	date_write;
	uint16_t	cluster_low;
	uint32_t	size;
} __attribute__((packed));

struct fat32_fsinfo {
	uint8_t signature1[4];
	uint32_t reserved1[120];
	uint8_t signature2[4];
	uint32_t free_clusters;
	uint32_t next_cluster;
	uint32_t reserved2[4];
} __attribute__((packed));

/* maximum number of clusters */
#define FAT12_MAX 0xFF4
#define FAT16_MAX 0xFFF4
#define FAT32_MAX 0x0FFFFFF6

#define FAT_ATTR_VOLUME_ID		0x08
#define FAT_ATTR_DIR			0x10
#define FAT_ATTR_LONG_NAME		0x0f
#define FAT_ATTR_MASK			0x3f
#define FAT_ENTRY_FREE			0xe5

static const char *no_name = "NO NAME    ";

/*
 * Look for LABEL (name) in the FAT root directory.
 */
static unsigned char *search_fat_label(blkid_probe pr,
				uint32_t offset, uint32_t entries)
{
	struct vfat_dir_entry *ent, *dir = NULL;
	int i;

	DBG(DEBUG_LOWPROBE,
		printf("\tlook for label in root-dir "
			"(entries: %d, offset: %d)\n", entries, offset));

	if (!blkid_probe_is_tiny(pr)) {
		/* large disk, read whole root directory */
		dir = (struct vfat_dir_entry *)
			blkid_probe_get_buffer(pr,
					offset,
					entries * sizeof(struct vfat_dir_entry));
		if (!dir)
			return NULL;
	}

	for (i = 0; i < entries; i++) {
		/*
		 * The root directory could be relatively large (4-16kB).
		 * Fortunately, the LABEL is usually the first entry in the
		 * directory. On tiny disks we call read() per entry.
		 */
		if (!dir)
			ent = (struct vfat_dir_entry *)
				blkid_probe_get_buffer(pr,
					offset + (i * sizeof(struct vfat_dir_entry)),
					sizeof(struct vfat_dir_entry));
		else
			ent = &dir[i];

		if (!ent || ent->name[0] == 0x00)
			break;

		if ((ent->name[0] == FAT_ENTRY_FREE) ||
		    (ent->cluster_high != 0 || ent->cluster_low != 0) ||
		    ((ent->attr & FAT_ATTR_MASK) == FAT_ATTR_LONG_NAME))
			continue;

		if ((ent->attr & (FAT_ATTR_VOLUME_ID | FAT_ATTR_DIR)) ==
		    FAT_ATTR_VOLUME_ID) {
			DBG(DEBUG_LOWPROBE,
				printf("\tfound fs LABEL at entry %d\n", i));
			return ent->name;
		}
	}
	return NULL;
}

/*
 * The FAT filesystem could be without a magic string in superblock
 * (e.g. old floppies).  This heuristic for FAT detection is inspired
 * by libvolume_id and the Linux kernel.
 */
static int probe_fat_nomagic(blkid_probe pr, const struct blkid_idmag *mag)
{
	struct msdos_super_block *ms;

	ms = blkid_probe_get_sb(pr, mag, struct msdos_super_block);
	if (!ms)
		return -1;

	/* Old floppies have a valid MBR signature */
	if (ms->ms_pmagic[0] != 0x55 || ms->ms_pmagic[1] != 0xAA)
		return 1;

	/* heads check */
	if (ms->ms_heads == 0)
		return 1;

	/* cluster size check*/
	if (ms->ms_cluster_size == 0 ||
	    (ms->ms_cluster_size & (ms->ms_cluster_size-1)))
		return 1;

	/* media check */
	if (!blkid_fat_valid_media(ms))
		return 1;

	/* fat counts(Linux kernel expects at least 1 FAT table) */
	if (!ms->ms_fats)
		return 1;

	/*
	 * OS/2 and apparently DFSee will place a FAT12/16-like
	 * pseudo-superblock in the first 512 bytes of non-FAT
	 * filesystems --- at least JFS and HPFS, and possibly others.
	 * So we explicitly check for those filesystems at the
	 * FAT12/16 filesystem magic field identifier, and if they are
	 * present, we rule this out as a FAT filesystem, despite the
	 * FAT-like pseudo-header.
         */
	if ((memcmp(ms->ms_magic, "JFS     ", 8) == 0) ||
	    (memcmp(ms->ms_magic, "HPFS    ", 8) == 0))
		return 1;

	return 0;
}

/* FAT label extraction from the root directory taken from Kay
 * Sievers's volume_id library */
static int probe_vfat(blkid_probe pr, const struct blkid_idmag *mag)
{
	struct vfat_super_block *vs;
	struct msdos_super_block *ms;
	const unsigned char *vol_label = 0, *tmp;
	unsigned char *vol_serno, vol_label_buf[11];
	int maxloop = 100;
	uint16_t sector_size, dir_entries, reserved;
	uint32_t sect_count, fat_size, dir_size, cluster_count, fat_length;
	uint32_t buf_size, start_data_sect, next, root_start, root_dir_entries;
	const char *version = NULL;

	/* non-standard magic strings */
	if (mag->len <= 2 && probe_fat_nomagic(pr, mag) != 0)
		return 1;

	vs = blkid_probe_get_sb(pr, mag, struct vfat_super_block);
	if (!vs)
		return -1;

	ms = blkid_probe_get_sb(pr, mag, struct msdos_super_block);
	if (!ms)
		return -1;

	/* sector size check */
	if (!blkid_fat_valid_sectorsize(ms, &sector_size))
		return 1;

	tmp = (unsigned char *) &ms->ms_dir_entries;
	dir_entries = tmp[0] + (tmp[1] << 8);
	reserved =  le16_to_cpu(ms->ms_reserved);
	tmp = (unsigned char *) &ms->ms_sectors;
	sect_count = tmp[0] + (tmp[1] << 8);
	if (sect_count == 0)
		sect_count = le32_to_cpu(ms->ms_total_sect);

	fat_length = le16_to_cpu(ms->ms_fat_length);
	if (fat_length == 0)
		fat_length = le32_to_cpu(vs->vs_fat32_length);

	fat_size = fat_length * ms->ms_fats;
	dir_size = ((dir_entries * sizeof(struct vfat_dir_entry)) +
			(sector_size-1)) / sector_size;

	cluster_count = sect_count - (reserved + fat_size + dir_size);
	if (ms->ms_cluster_size == 0)
		return 1;
	cluster_count /= ms->ms_cluster_size;

	if (cluster_count > FAT32_MAX)
		return 1;

	if (ms->ms_fat_length) {
		/* the label may be an attribute in the root directory */
		root_start = (reserved + fat_size) * sector_size;
		root_dir_entries = vs->vs_dir_entries[0] +
			(vs->vs_dir_entries[1] << 8);

		vol_label = search_fat_label(pr, root_start, root_dir_entries);
		if (vol_label) {
			memcpy(vol_label_buf, vol_label, 11);
			vol_label = vol_label_buf;
		}

		if (!vol_label || !memcmp(vol_label, no_name, 11))
			vol_label = ms->ms_label;
		vol_serno = ms->ms_serno;

		blkid_probe_set_value(pr, "SEC_TYPE", (unsigned char *) "msdos",
                              sizeof("msdos"));

		if (cluster_count < FAT12_MAX)
			version = "FAT12";
		else if (cluster_count < FAT16_MAX)
			version = "FAT16";
	} else {
		unsigned char *buf;
		uint16_t fsinfo_sect;

		/* Search the FAT32 root dir for the label attribute */
		buf_size = vs->vs_cluster_size * sector_size;
		start_data_sect = reserved + fat_size;

		version = "FAT32";

		next = le32_to_cpu(vs->vs_root_cluster);
		while (next && --maxloop) {
			uint32_t next_sect_off;
			uint64_t next_off, fat_entry_off;
			int count;

			next_sect_off = (next - 2) * vs->vs_cluster_size;
			next_off = (start_data_sect + next_sect_off) *
				sector_size;

			count = buf_size / sizeof(struct vfat_dir_entry);

			vol_label = search_fat_label(pr, next_off, count);
			if (vol_label) {
				memcpy(vol_label_buf, vol_label, 11);
				vol_label = vol_label_buf;
				break;
			}

			/* get FAT entry */
			fat_entry_off = (reserved * sector_size) +
				(next * sizeof(uint32_t));
			buf = blkid_probe_get_buffer(pr, fat_entry_off, buf_size);
			if (buf == NULL)
				break;

			/* set next cluster */
			next = le32_to_cpu(*((uint32_t *) buf) & 0x0fffffff);
		}

		if (!vol_label || !memcmp(vol_label, no_name, 11))
			vol_label = vs->vs_label;
		vol_serno = vs->vs_serno;

		/*
		 * FAT32 should have a valid signature in the fsinfo block,
		 * but also allow all bytes set to '\0', because some volumes
		 * do not set the signature at all.
		 */
		fsinfo_sect = le16_to_cpu(vs->vs_fsinfo_sector);
		if (fsinfo_sect) {
			struct fat32_fsinfo *fsinfo;

			buf = blkid_probe_get_buffer(pr,
					fsinfo_sect * sector_size,
					sizeof(struct fat32_fsinfo));
			if (buf == NULL)
				return -1;

			fsinfo = (struct fat32_fsinfo *) buf;
			if (memcmp(fsinfo->signature1, "\x52\x52\x61\x41", 4) != 0 &&
			    memcmp(fsinfo->signature1, "\x00\x00\x00\x00", 4) != 0)
				return -1;
			if (memcmp(fsinfo->signature2, "\x72\x72\x41\x61", 4) != 0 &&
			    memcmp(fsinfo->signature2, "\x00\x00\x00\x00", 4) != 0)
				return -1;
		}
	}

	if (vol_label && memcmp(vol_label, no_name, 11))
		blkid_probe_set_label(pr, (unsigned char *) vol_label, 11);

	/* We can't just print them as %04X, because they are unaligned */
	blkid_probe_sprintf_uuid(pr, vol_serno, 4, "%02X%02X-%02X%02X",
		vol_serno[3], vol_serno[2], vol_serno[1], vol_serno[0]);

	if (version)
		blkid_probe_set_version(pr, version);

	return 0;
}


const struct blkid_idinfo vfat_idinfo =
{
	.name		= "vfat",
	.usage		= BLKID_USAGE_FILESYSTEM,
	.probefunc	= probe_vfat,
	.magics		=
	{
		{ .magic = "MSWIN",    .len = 5, .sboff = 0x52 },
		{ .magic = "FAT32   ", .len = 8, .sboff = 0x52 },
		{ .magic = "MSDOS",    .len = 5, .sboff = 0x36 },
		{ .magic = "FAT16   ", .len = 8, .sboff = 0x36 },
		{ .magic = "FAT12   ", .len = 8, .sboff = 0x36 },
		{ .magic = "\353",     .len = 1, },
		{ .magic = "\351",     .len = 1, },
		{ .magic = "\125\252", .len = 2, .sboff = 0x1fe },
		{ NULL }
	}
};

