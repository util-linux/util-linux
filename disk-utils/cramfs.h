#ifndef __CRAMFS_H
#define __CRAMFS_H

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

#define CRAMFS_MAGIC		0x28cd3d45	/* some random number */
#define CRAMFS_SIGNATURE	"Compressed ROMFS"

/*
 * Width of various bitfields in struct cramfs_inode.
 * Primarily used to generate warnings in mkcramfs.
 */
#define CRAMFS_MODE_WIDTH 16
#define CRAMFS_UID_WIDTH 16
#define CRAMFS_SIZE_WIDTH 24
#define CRAMFS_GID_WIDTH 8
#define CRAMFS_NAMELEN_WIDTH 6
#define CRAMFS_OFFSET_WIDTH 26


/*
 * Reasonably terse representation of the inode data.
 */
struct cramfs_inode {
	u32 mode:16, uid:16;
	/* SIZE for device files is i_rdev */
	u32 size:24, gid:8;
	/* NAMELEN is the length of the file name, divided by 4 and
           rounded up.  (cramfs doesn't support hard links.) */
	/* OFFSET: For symlinks and non-empty regular files, this
	   contains the offset (divided by 4) of the file data in
	   compressed form (starting with an array of block pointers;
	   see README).  For non-empty directories it is the offset
	   (divided by 4) of the inode of the first file in that
	   directory.  For anything else, offset is zero. */
	u32 namelen:6, offset:26;
};

struct cramfs_info {
        u32 crc;
        u32 edition;
        u32 blocks;
        u32 files;
};

/*
 * Superblock information at the beginning of the FS.
 */
struct cramfs_super {
	u32 magic;		/* 0x28cd3d45 - random number */
	u32 size;		/* Not used.  mkcramfs currently
                                   writes a constant 1<<16 here. */
	u32 flags;		/* 0 */
	u32 future;		/* 0 */
	u8 signature[16];	/* "Compressed ROMFS" */
	struct cramfs_info fsid;/* unique filesystem info */
	u8 name[16];		/* user-defined name */
	struct cramfs_inode root;	/* Root inode data */
};

#define CRAMFS_FLAG_FSID_VERSION_2      0x00000001      /* fsid version #2 */
#define CRAMFS_FLAG_SORTED_DIRS         0x00000002      /* sorted dirs */
#define CRAMFS_FLAG_HOLES               0x00000100      /* support for holes */
#define CRAMFS_FLAG_WRONG_SIGNATURE     0x00000200      /* reserved */
#define CRAMFS_FLAG_SHIFTED_ROOT_OFFSET 0x00000400      /* shifted root fs */

/*
 * Valid values in super.flags.  Currently we refuse to mount
 * if (flags & ~CRAMFS_SUPPORTED_FLAGS).  Maybe that should be
 * changed to test super.future instead.
 */
#define CRAMFS_SUPPORTED_FLAGS (0xff)

/* Uncompression interfaces to the underlying zlib */
int cramfs_uncompress_block(void *dst, int dstlen, void *src, int srclen);
int cramfs_uncompress_init(void);
int cramfs_uncompress_exit(void);

#endif
