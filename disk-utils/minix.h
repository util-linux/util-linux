#ifndef UTIL_LINUX_MINIX_H
#define UTIL_LINUX_MINIX_H

#include <stdint.h>

struct minix_inode {
	uint16_t i_mode;
	uint16_t i_uid;
	uint32_t i_size;
	uint32_t i_time;
	uint8_t  i_gid;
	uint8_t  i_nlinks;
	uint16_t i_zone[9];
};

struct minix2_inode {
	uint16_t i_mode;
	uint16_t i_nlinks;
	uint16_t i_uid;
	uint16_t i_gid;
	uint32_t i_size;
	uint32_t i_atime;
	uint32_t i_mtime;
	uint32_t i_ctime;
	uint32_t i_zone[10];
};

struct minix_super_block {
	uint16_t s_ninodes;
	uint16_t s_nzones;
	uint16_t s_imap_blocks;
	uint16_t s_zmap_blocks;
	uint16_t s_firstdatazone;
	uint16_t s_log_zone_size;
	uint32_t s_max_size;
	uint16_t s_magic;
	uint16_t s_state;
	uint32_t s_zones;
};

/* V3 minix super-block data on disk */
struct minix3_super_block {
	uint32_t s_ninodes;
	uint16_t s_pad0;
	uint16_t s_imap_blocks;
	uint16_t s_zmap_blocks;
	uint16_t s_firstdatazone;
	uint16_t s_log_zone_size;
	uint16_t s_pad1;
	uint32_t s_max_size;
	uint32_t s_zones;
	uint16_t s_magic;
	uint16_t s_pad2;
	uint16_t s_blocksize;
	uint8_t  s_disk_version;
};

#define BLOCK_SIZE_BITS 10
#define BLOCK_SIZE (1<<BLOCK_SIZE_BITS)

#define NAME_MAX   255   /* # chars in a file name */
#define MAX_INODES 65535

#define MINIX_INODES_PER_BLOCK ((BLOCK_SIZE)/(sizeof (struct minix_inode)))
#define MINIX2_INODES_PER_BLOCK ((BLOCK_SIZE)/(sizeof (struct minix2_inode)))

#define MINIX_VALID_FS               0x0001          /* Clean fs. */
#define MINIX_ERROR_FS               0x0002          /* fs has errors. */

#define MINIX_SUPER_MAGIC    0x137F          /* original minix fs */
#define MINIX_SUPER_MAGIC2   0x138F          /* minix fs, 30 char names */
#define MINIX2_SUPER_MAGIC   0x2468	     /* minix V2 fs */
#define MINIX2_SUPER_MAGIC2  0x2478	     /* minix V2 fs, 30 char names */
#define MINIX3_SUPER_MAGIC   0x4d5a          /* minix V3 fs (60 char names) */

#define Inode (((struct minix_inode *) inode_buffer)-1)
#define Inode2 (((struct minix2_inode *) inode_buffer)-1)

#define INODE_SIZE (sizeof(struct minix_inode))
#define INODE2_SIZE (sizeof(struct minix2_inode))

int fs_version = 1; /* this default value needs to change in a near future */
char *super_block_buffer, *inode_buffer = NULL;

static char *inode_map;
static char *zone_map;

#define BITS_PER_BLOCK (BLOCK_SIZE<<3)

#define UPPER(size,n) ((size+((n)-1))/(n))

/*
 * wrappers to different superblock attributes
 */
#define Super (*(struct minix_super_block *)super_block_buffer)
#define Super3 (*(struct minix3_super_block *)super_block_buffer)

static inline unsigned long get_ninodes(void)
{
	switch (fs_version) {
	case 3:
		return Super3.s_ninodes;
	default:
		return (unsigned long) Super.s_ninodes;
	}
}

static inline unsigned long get_nzones(void)
{
	switch (fs_version) {
	case 3:
		return (unsigned long) Super3.s_zones;
	case 2:
		return (unsigned long) Super.s_zones;
	default:
		return (unsigned long) Super.s_nzones;
	}
}

static inline unsigned long get_nimaps(void)
{
	switch (fs_version) {
	case 3:
		return (unsigned long) Super3.s_imap_blocks;
	default:
		return (unsigned long) Super.s_imap_blocks;
	}
}

static inline unsigned long get_nzmaps(void)
{
	switch (fs_version) {
	case 3:
		return (unsigned long) Super3.s_zmap_blocks;
	default:
		return (unsigned long) Super.s_zmap_blocks;
	}
}

static inline unsigned long get_first_zone(void)
{
	switch (fs_version) {
	case 3:
		return (unsigned long) Super3.s_firstdatazone;
	default:
		return (unsigned long) Super.s_firstdatazone;
	}
}

static inline unsigned long get_zone_size(void)
{
	switch (fs_version) {
	case 3:
		return (unsigned long) Super3.s_log_zone_size;
	default:
		return (unsigned long) Super.s_log_zone_size;
	}
}

static inline unsigned long get_max_size(void)
{
	switch (fs_version) {
	case 3:
		return (unsigned long) Super3.s_max_size;
	default:
		return (unsigned long) Super.s_max_size;
	}
}

static unsigned long inode_blocks(void)
{
	switch (fs_version) {
	case 3:
	case 2:
		return UPPER(get_ninodes(), MINIX2_INODES_PER_BLOCK);
	default:
		return UPPER(get_ninodes(), MINIX_INODES_PER_BLOCK);
	}
}

static inline unsigned long first_zone_data(void)
{
	return 2 + get_nimaps() + get_nzmaps() + inode_blocks();
}

static inline unsigned long get_inode_buffer_size(void)
{
	return inode_blocks() * BLOCK_SIZE;
}

#endif /* UTIL_LINUX_MINIX_H */
