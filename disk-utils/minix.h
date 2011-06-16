#ifndef __MINIX_H__
#define __MINIX_H__

#ifdef KERNEL_INCLUDES_ARE_CLEAN

#include <linux/fs.h>
#include <linux/minix_fs.h>

#else

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

struct minix_inode {
        u16 i_mode;
        u16 i_uid;
        u32 i_size;
        u32 i_time;
        u8  i_gid;
        u8  i_nlinks;
        u16 i_zone[9];
};

struct minix2_inode {
        u16 i_mode;
        u16 i_nlinks;
        u16 i_uid;
        u16 i_gid;
        u32 i_size;
        u32 i_atime;
        u32 i_mtime;
        u32 i_ctime;
        u32 i_zone[10];
};

struct minix_super_block {
        u16 s_ninodes;
        u16 s_nzones;
        u16 s_imap_blocks;
        u16 s_zmap_blocks;
        u16 s_firstdatazone;
        u16 s_log_zone_size;
        u32 s_max_size;
        u16 s_magic;
        u16 s_state;
        u32 s_zones;
};

#define BLOCK_SIZE_BITS 10
#define BLOCK_SIZE (1<<BLOCK_SIZE_BITS)

#define NAME_MAX         255   /* # chars in a file name */

#define MINIX_INODES_PER_BLOCK ((BLOCK_SIZE)/(sizeof (struct minix_inode)))
#define MINIX2_INODES_PER_BLOCK ((BLOCK_SIZE)/(sizeof (struct minix2_inode)))

#define MINIX_VALID_FS               0x0001          /* Clean fs. */
#define MINIX_ERROR_FS               0x0002          /* fs has errors. */

#define MINIX_SUPER_MAGIC    0x137F          /* original minix fs */
#define MINIX_SUPER_MAGIC2   0x138F          /* minix fs, 30 char names */
#define MINIX2_SUPER_MAGIC   0x2468	     /* minix V2 fs */
#define MINIX2_SUPER_MAGIC2  0x2478	     /* minix V2 fs, 30 char names */

#endif /* KERNEL_INCLUDES_ARE_CLEAN */

#define Inode (((struct minix_inode *) inode_buffer)-1)
#define Inode2 (((struct minix2_inode *) inode_buffer)-1)

#define INODE_SIZE (sizeof(struct minix_inode))
#define INODE2_SIZE (sizeof(struct minix2_inode))

int fs_version = 1;
char *super_block_buffer, *inode_buffer = NULL;

static char *inode_map;
static char *zone_map;

#define BITS_PER_BLOCK (BLOCK_SIZE<<3)

#define UPPER(size,n) ((size+((n)-1))/(n))

/*
 * wrappers to different superblock attributes
 */
#define Super (*(struct minix_super_block *)super_block_buffer)

static inline unsigned long get_ninodes(void)
{
	return (unsigned long) Super.s_ninodes;
}

static inline unsigned long get_nzones(void)
{
	return (unsigned long) fs_version == 2 ? Super.s_zones : Super.s_nzones;
}

static inline unsigned long get_nimaps(void)
{
	return (unsigned long)Super.s_imap_blocks;
}

static inline unsigned long get_nzmaps(void)
{
	return (unsigned long)Super.s_zmap_blocks;
}

static inline unsigned long get_first_zone(void)
{
	return (unsigned long)Super.s_firstdatazone;
}

static inline unsigned long get_zone_size(void)
{
	return (unsigned long)Super.s_log_zone_size;
}

static inline unsigned long get_max_size(void)
{
	return (unsigned long)Super.s_max_size;
}

static unsigned long inode_blocks(void)
{
	if (fs_version == 2)
		return UPPER(get_ninodes(), MINIX2_INODES_PER_BLOCK);
	else
		return UPPER(get_ninodes(), MINIX_INODES_PER_BLOCK);
}

static inline unsigned long first_zone_data(void)
{
	return 2 + get_nimaps() + get_nzmaps() + inode_blocks();
}

static inline unsigned long get_inode_buffer_size(void)
{
	return inode_blocks() * BLOCK_SIZE;
}

#endif /* __MINIX_H__ */
