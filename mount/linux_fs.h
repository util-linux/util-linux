/* Including <linux/fs.h> became more and more painful.
   Below a very abbreviated version of some declarations,
   only designed to be able to check a magic number
   in case no filesystem type was given. */

#ifndef BLKGETSIZE
#define BLKGETSIZE 0x1260		   /* return device size */
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
	u_char   s_dummy[56];
	u_char   s_magic[2];
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
