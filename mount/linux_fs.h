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

#define MINIX_SUPER_MAGIC   0x137F         /* minix v1, 14 char names */
#define MINIX_SUPER_MAGIC2  0x138F         /* minix v1, 30 char names */
#define MINIX2_SUPER_MAGIC  0x2468	   /* minix v2, 14 char names */
#define MINIX2_SUPER_MAGIC2 0x2478         /* minix v2, 30 char names */
struct minix_super_block {
	u_char   s_dummy[16];
	u_char   s_magic[2];
};
#define minixmagic(s)	assemble2le(s.s_magic)

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
#define extmagic(s)	assemble2le(s.s_magic)

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
#define ext2magic(s)	assemble2le(s.s_magic)

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
	u_char		s_magic[10];
	u_char		s_dummy1[10];
	u_char		s_version[2]; /* only valid with relocated journal */

	/* only valid in 3.6.x format --mason@suse.com */
	u_char		s_dummy2[10];
	u_char		s_uuid[16];
	u_char		s_label[16];
};
#define REISERFS_SUPER_MAGIC_STRING "ReIsErFs"
#define REISER2FS_SUPER_MAGIC_STRING "ReIsEr2Fs"
/* also known as REISER2FS_JR_SUPER_MAGIC_STRING */
#define REISER3FS_SUPER_MAGIC_STRING "ReIsEr3Fs"
#define REISERFS_DISK_OFFSET_IN_BYTES (64 * 1024)
/* the spot for the super in versions 3.5 - 3.5.10 (inclusive) */
#define REISERFS_OLD_DISK_OFFSET_IN_BYTES (8 * 1024)

/* values of s_version when REISER3FS_SUPER_MAGIC_STRING is found */
#define REISERFS_VERSION_1 0 /* 3.5.x disk format */
#define REISERFS_VERSION_2 2 /* 3.6.x disk format */

extern int reiserfs_magic_version(const char *magic);

#define _XIAFS_SUPER_MAGIC 0x012FD16D
struct xiafs_super_block {
    u_char     s_boot_segment[512];     /*  1st sector reserved for boot */
    u_char     s_dummy[60];
    u_char     s_magic[4];
};
#define xiafsmagic(s)	assemble4le(s.s_magic)

/* From jj@sunsite.ms.mff.cuni.cz Mon Mar 23 15:19:05 1998 */
#define UFS_SUPER_MAGIC_LE 0x00011954
#define UFS_SUPER_MAGIC_BE 0x54190100
struct ufs_super_block {
    u_char     s_dummy[0x55c];
    u_char     s_magic[4];
};
#define ufsmagic(s)	assemble4le(s.s_magic)

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
#define CRAMFS_SUPER_MAGIC_BE 0x453dcd28
struct cramfs_super_block {
	u_char    s_magic[4];
	u_char    s_dummy[12];
	u_char    s_id[16];
};
#define cramfsmagic(s)	assemble4le(s.s_magic)

#define HFS_SUPER_MAGIC 0x4244
struct hfs_super_block {
	u_char    s_magic[2];		/* drSigWord */
	u_char    s_dummy[18];
	u_char    s_blksize[4];		/* drAlBlkSiz */
};
#define hfsmagic(s)	assemble2be(s.s_magic)
#define hfsblksize(s)	assemble4be(s.s_blksize)

#define HPFS_SUPER_MAGIC 0xf995e849
struct hpfs_super_block {
	u_char    s_magic[4];
	u_char    s_magic2[4];
};
#define hpfsmagic(s)	assemble4le(s.s_magic)

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
#define vxfsmagic(s)	assemble4le(s.s_magic)
#define VXFS_SUPER_MAGIC 0xa501FCF5

struct jfs_super_block {
	char	s_magic[4];
	u_char	s_version[4];
	u_char	s_dummy1[93];
	char	s_fpack[11];
	u_char	s_dummy2[24];
	u_char	s_uuid[16];
	char	s_label[16];
};
#define JFS_SUPER1_OFF 0x8000
#define JFS_MAGIC "JFS1"

struct sysv_super_block {
	u_char  s_dummy1[504];
	u_char  s_magic[4];
	u_char  type[4];
};
#define sysvmagic(s)		assemble4le(s.s_magic)
#define SYSV_SUPER_MAGIC	0xfd187e20

struct mdp_super_block {
	u_char	md_magic[4];
};
#define MD_SB_MAGIC	0xa92b4efc
#define mdsbmagic(s)	assemble4le(s.md_magic)

struct ocfs_volume_header {
	u_char  minor_version[4];
	u_char  major_version[4];
	u_char  signature[128];
};

struct ocfs_volume_label {
	u_char  disk_lock[48];
	u_char  label[64];
	u_char  label_len[2];
};

#define ocfslabellen(o)	assemble2le(o.label_len)
#define OCFS_MAGIC	"OracleCFS"

struct ocfs2_super_block {
	u_char  signature[8];
	u_char  s_dummy1[184];
	u_char  s_dummy2[80];
	u_char  s_label[64];
	u_char  s_uuid[16];
};

#define OCFS2_MIN_BLOCKSIZE		512
#define OCFS2_MAX_BLOCKSIZE		4096
#define OCFS2_SUPER_BLOCK_BLKNO		2
#define OCFS2_SUPER_BLOCK_SIGNATURE	"OCFSV2"


struct efs_volume_directory {	/* size 16 */
	char    vd_name[8];
	char    vd_lbn[4];
	char    vd_nbytes[4];
};

struct efs_partition_table {	/* size 12 */
	char    pt_nblks[4];
	char    pt_firstlbn[4];
	char    pt_type[4];
};

struct efs_volume_header {	/* size 512 */
	char    vh_magic[4];
	short   vh_rootpt;
	short   vh_swappt;
	char    vh_bootfile[16];
	char    pad[48];
	struct efs_volume_directory vh_vd[15];
	struct efs_partition_table  vh_pt[16];
	int     vh_csum;
	int     vh_fill;
};

struct efs_super {
	char     fs_stuff[512+28];
	char     fs_magic[4];
	char     fs_fname[6];
	char     fs_fpack[6];
	/* ... */
};

#define EFS_VHMAGIC	0x0be5a941	/* big endian */
#define EFS_SBMAGIC	0x00072959	/* idem */
#define EFS_SBMAGIC2	0x0007295a	/* idem */

static inline int
assemble2le(unsigned char *p) {
	return (p[0] | (p[1] << 8));
}

static inline int
assemble2be(unsigned char *p) {
	return (p[1] | (p[0] << 8));
}

static inline int
assemble4le(unsigned char *p) {
	return (p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
}

static inline int
assemble4be(unsigned char *p) {
	return (p[3] | (p[2] << 8) | (p[1] << 16) | (p[0] << 24));
}
