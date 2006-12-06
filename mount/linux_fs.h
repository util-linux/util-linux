/* Including <linux/fs.h> became more and more painful.
   Below a very abbreviated version of some declarations,
   only designed to be able to check a magic number
   in case no filesystem type was given. */

#ifndef BLKGETSIZE
#ifndef _IO
/* pre-1.3.45 */
#define BLKGETSIZE 0x1260		   /* return device size */
#else
/* same on i386, m68k, arm; different on alpha, mips, sparc, ppc */
#define BLKGETSIZE _IO(0x12,96)
#endif
#endif

#define MINIX_SUPER_MAGIC   0x137F         /* original minix fs */
#define MINIX_SUPER_MAGIC2  0x138F         /* minix fs, 30 char names */
struct minix_super_block {
	u_char   s_dummy[16];
	u_char   s_magic[2];
};
#define minixmagic(s)	((uint) s.s_magic[0] + (((uint) s.s_magic[1]) << 8))

#define ISODCL(from, to) (to - from + 1)
#define ISO_STANDARD_ID "CD001"
struct iso_volume_descriptor {
	char type[ISODCL(1,1)]; /* 711 */
	char id[ISODCL(2,6)];
	char version[ISODCL(7,7)];
	char data[ISODCL(8,2048)];
};

#define HS_STANDARD_ID "CDROM"
struct  hs_volume_descriptor {
	char foo[ISODCL (  1,   8)]; /* 733 */
	char type[ISODCL (  9,   9)]; /* 711 */
	char id[ISODCL ( 10,  14)];
	char version[ISODCL ( 15,  15)]; /* 711 */
	char data[ISODCL(16,2048)];
};

#define EXT_SUPER_MAGIC 0x137D
struct ext_super_block {
	u_char   s_dummy[56];
	u_char   s_magic[2];
};
#define extmagic(s)	((uint) s.s_magic[0] + (((uint) s.s_magic[1]) << 8))

#define EXT2_PRE_02B_MAGIC  0xEF51
#define EXT2_SUPER_MAGIC    0xEF53
#define EXT3_FEATURE_COMPAT_HAS_JOURNAL 0x0004
struct ext2_super_block {
	u_char 	s_dummy1[56];
	u_char 	s_magic[2];
	u_char	s_dummy2[34];
	u_char	s_feature_compat[4];
	u_char	s_feature_incompat[4];
	u_char	s_feature_ro_compat[4];
	u_char	s_uuid[16];
	u_char 	s_volume_name[16];
	u_char	s_dummy3[88];
	u_char	s_journal_inum[4];	/* ext3 only */
};
#define ext2magic(s)	((uint) s.s_magic[0] + (((uint) s.s_magic[1]) << 8))

struct reiserfs_super_block
{
	u_char		s_block_count[4];
	u_char		s_free_blocks[4];
	u_char		s_root_block[4];
	u_char		s_journal_block[4];
	u_char		s_journal_dev[4];
	u_char		s_orig_journal_size[4];
	u_char		s_journal_trans_max[4];
	u_char		s_journal_block_count[4];
	u_char		s_journal_max_batch[4];
	u_char		s_journal_max_commit_age[4];
	u_char		s_journal_max_trans_age[4];
	u_char		s_blocksize[2];
	u_char		s_oid_maxsize[2];
	u_char		s_oid_cursize[2];
	u_char		s_state[2];
	u_char		s_magic[12];
};
#define REISERFS_SUPER_MAGIC_STRING "ReIsErFs"
#define REISER2FS_SUPER_MAGIC_STRING "ReIsEr2Fs"
#define REISERFS_DISK_OFFSET_IN_BYTES (64 * 1024)
/* the spot for the super in versions 3.5 - 3.5.10 (inclusive) */
#define REISERFS_OLD_DISK_OFFSET_IN_BYTES (8 * 1024)

#define _XIAFS_SUPER_MAGIC 0x012FD16D
struct xiafs_super_block {
    u_char     s_boot_segment[512];     /*  1st sector reserved for boot */
    u_char     s_dummy[60];
    u_char     s_magic[4];
};
#define xiafsmagic(s)	((uint) s.s_magic[0] + (((uint) s.s_magic[1]) << 8) + \
			(((uint) s.s_magic[2]) << 16) + \
			(((uint) s.s_magic[3]) << 24))

/* From jj@sunsite.ms.mff.cuni.cz Mon Mar 23 15:19:05 1998 */
#define UFS_SUPER_MAGIC 0x00011954
struct ufs_super_block {
    u_char     s_dummy[0x55c];
    u_char     s_magic[4];
};
#define ufsmagic(s)	((uint) s.s_magic[0] + (((uint) s.s_magic[1]) << 8) + \
			 (((uint) s.s_magic[2]) << 16) + \
			 (((uint) s.s_magic[3]) << 24))

/* From Richard.Russon@ait.co.uk Wed Feb 24 08:05:27 1999 */
#define NTFS_SUPER_MAGIC "NTFS"
struct ntfs_super_block {
    u_char    s_dummy[3];
    u_char    s_magic[4];
};

/* From inspection of a few FAT filesystems - aeb */
/* Unfortunately I find almost the same thing on an extended partition;
   it looks like a primary has some directory entries where the extended
   has a partition table: IO.SYS, MSDOS.SYS, WINBOOT.SYS */
struct fat_super_block {
    u_char    s_dummy[3];
    u_char    s_os[8];		/* "MSDOS5.0" or "MSWIN4.0" or "MSWIN4.1" */
				/* mtools-3.9.4 writes "MTOOL394" */
    u_char    s_dummy2[32];
    u_char    s_label[11];	/* for DOS? */
    u_char    s_fs[8];		/* "FAT12   " or "FAT16   " or all zero   */
                                /* OS/2 BM has "FAT     " here. */
    u_char    s_dummy3[9];
    u_char    s_label2[11];	/* for Windows? */
    u_char    s_fs2[8];	        /* garbage or "FAT32   " */
};

#define XFS_SUPER_MAGIC "XFSB"
struct xfs_super_block {
    u_char    s_magic[4];
    u_char    s_dummy[28];
    u_char    s_uuid[16];
    u_char    s_dummy2[60];
    u_char    s_fname[12];
};

#define CRAMFS_SUPER_MAGIC 0x28cd3d45
struct cramfs_super_block {
	u_char    s_magic[4];
	u_char    s_dummy[12];
	u_char    s_id[16];
};
#define cramfsmagic(s)	((uint) s.s_magic[0] + (((uint) s.s_magic[1]) << 8) + \
			 (((uint) s.s_magic[2]) << 16) + \
			 (((uint) s.s_magic[3]) << 24))

#define HFS_SUPER_MAGIC 0x4244
struct hfs_super_block {
	u_char    s_magic[2];
	u_char    s_dummy[18];
	u_char    s_blksize[4];
};
#define hfsmagic(s)	((uint) s.s_magic[0] + (((uint) s.s_magic[1]) << 8))
#define hfsblksize(s)	((uint) s.s_blksize[0] + \
			 (((uint) s.s_blksize[1]) << 8) + \
			 (((uint) s.s_blksize[2]) << 16) + \
			 (((uint) s.s_blksize[3]) << 24))

#define HPFS_SUPER_MAGIC 0xf995e849
struct hpfs_super_block {
	u_char    s_magic[4];
	u_char    s_magic2[4];
};
#define hpfsmagic(s)	((uint) s.s_magic[0] + (((uint) s.s_magic[1]) << 8) + \
			 (((uint) s.s_magic[2]) << 16) + \
			 (((uint) s.s_magic[3]) << 24))

struct adfs_super_block {
	u_char    s_dummy[448];
	u_char    s_blksize[1];
	u_char    s_dummy2[62];
	u_char    s_checksum[1];
};
#define adfsblksize(s)	((uint) s.s_blksize[0])

/* found in first 4 bytes of block 1 */
struct vxfs_super_block {
	u_char	s_magic[4];
};
#define vxfsmagic(s)	((uint) s.s_magic[0] + (((uint) s.s_magic[1]) << 8) + \
			 (((uint) s.s_magic[2]) << 16) + \
			 (((uint) s.s_magic[3]) << 24))
#define VXFS_SUPER_MAGIC 0xa501FCF5

struct jfs_super_block {
	char s_magic[4];
};
#define JFS_SUPER1_OFF 0x8000
#define JFS_MAGIC "JFS1"

#define SYSV_SUPER_MAGIC	0xfd187e20

struct sysv_super_block {
	u_char  s_dummy1[504];
	u_char  s_magic[4];
	u_char  type[4];
};

#define sysvmagic(s)	((uint) s.s_magic[0] + (((uint) s.s_magic[1]) << 8) + \
			 (((uint) s.s_magic[2]) << 16) + \
			 (((uint) s.s_magic[3]) << 24))
