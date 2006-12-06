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
