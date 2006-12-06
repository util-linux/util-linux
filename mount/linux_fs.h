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
struct ext2_super_block {
	u_char   s_dummy1[56];
	u_char   s_magic[2];
	u_char   s_dummy2[46];
	u_char   s_uuid[16];
	u_char   s_volume_name[16];
};
#define ext2magic(s)	((uint) s.s_magic[0] + (((uint) s.s_magic[1]) << 8))

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
#define XFS_SUPER_MAGIC2 "BSFX"
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
