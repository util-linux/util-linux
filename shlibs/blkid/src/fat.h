#ifndef _BLKID_FAT_H
#define _BLKID_FAT_H

/* This FAT superblock is required for:
 *
 *	superblocks/vfat.c
 *	partitions/dos.c
 */

/* Yucky misaligned values */
struct vfat_super_block {
/* 00*/	unsigned char	vs_ignored[3];
/* 03*/	unsigned char	vs_sysid[8];
/* 0b*/	unsigned char	vs_sector_size[2];
/* 0d*/	uint8_t		vs_cluster_size;
/* 0e*/	uint16_t	vs_reserved;
/* 10*/	uint8_t		vs_fats;
/* 11*/	unsigned char	vs_dir_entries[2];
/* 13*/	unsigned char	vs_sectors[2];
/* 15*/	unsigned char	vs_media;
/* 16*/	uint16_t	vs_fat_length;
/* 18*/	uint16_t	vs_secs_track;
/* 1a*/	uint16_t	vs_heads;
/* 1c*/	uint32_t	vs_hidden;
/* 20*/	uint32_t	vs_total_sect;
/* 24*/	uint32_t	vs_fat32_length;
/* 28*/	uint16_t	vs_flags;
/* 2a*/	uint8_t		vs_version[2];
/* 2c*/	uint32_t	vs_root_cluster;
/* 30*/	uint16_t	vs_fsinfo_sector;
/* 32*/	uint16_t	vs_backup_boot;
/* 34*/	uint16_t	vs_reserved2[6];
/* 40*/	unsigned char	vs_unknown[3];
/* 43*/	unsigned char	vs_serno[4];
/* 47*/	unsigned char	vs_label[11];
/* 52*/	unsigned char   vs_magic[8];
/* 5a*/	unsigned char	vs_dummy2[0x1fe - 0x5a];
/*1fe*/	unsigned char	vs_pmagic[2];
} __attribute__((packed));

/* Yucky misaligned values */
struct msdos_super_block {
/* 00*/	unsigned char	ms_ignored[3];
/* 03*/	unsigned char	ms_sysid[8];
/* 0b*/	unsigned char	ms_sector_size[2];
/* 0d*/	uint8_t		ms_cluster_size;
/* 0e*/	uint16_t	ms_reserved;
/* 10*/	uint8_t		ms_fats;
/* 11*/	unsigned char	ms_dir_entries[2];
/* 13*/	unsigned char	ms_sectors[2]; /* =0 iff V3 or later */
/* 15*/	unsigned char	ms_media;
/* 16*/	uint16_t	ms_fat_length; /* Sectors per FAT */
/* 18*/	uint16_t	ms_secs_track;
/* 1a*/	uint16_t	ms_heads;
/* 1c*/	uint32_t	ms_hidden;
/* V3 BPB */
/* 20*/	uint32_t	ms_total_sect; /* iff ms_sectors == 0 */
/* V4 BPB */
/* 24*/	unsigned char	ms_unknown[3]; /* Phys drive no., resvd, V4 sig (0x29) */
/* 27*/	unsigned char	ms_serno[4];
/* 2b*/	unsigned char	ms_label[11];
/* 36*/	unsigned char   ms_magic[8];
/* 3e*/	unsigned char	ms_dummy2[0x1fe - 0x3e];
/*1fe*/	unsigned char	ms_pmagic[2];
} __attribute__((packed));


static inline int blkid_fat_valid_media(struct msdos_super_block *ms)
{
	return 0xf8 <= ms->ms_media || ms->ms_media == 0xf0;
}

static inline int blkid_fat_valid_sectorsize(
			struct msdos_super_block *ms,
			uint16_t *sector_size)
{
	unsigned char *tmp = (unsigned char *) &ms->ms_sector_size;
	uint16_t ssz;

	ssz = tmp[0] + (tmp[1] << 8);

	if (ssz != 0x200 && ssz != 0x400 && ssz != 0x800 && ssz != 0x1000)
		return 0;
	if (sector_size)
		*sector_size = ssz;
	return 1;
}


#endif /* _BLKID_FAT_H */
