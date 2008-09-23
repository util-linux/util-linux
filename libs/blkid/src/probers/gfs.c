/*
 * Copyright (C) 1999, 2001 by Andries Brouwer
 * Copyright (C) 1999, 2000, 2003 by Theodore Ts'o
 * Copyright (C) 2008 Karel Zak <kzak@redhat.com>
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This file is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <stdint.h>

#include "blkidP.h"

/* Common gfs/gfs2 constants: */
#define GFS_MAGIC               0x01161970
#define GFS_DEFAULT_BSIZE       4096
#define GFS_SUPERBLOCK_OFFSET	(0x10 * GFS_DEFAULT_BSIZE)
#define GFS_METATYPE_SB         1
#define GFS_FORMAT_SB           100
#define GFS_LOCKNAME_LEN        64

/* gfs1 constants: */
#define GFS_FORMAT_FS           1309
#define GFS_FORMAT_MULTI        1401
/* gfs2 constants: */
#define GFS2_FORMAT_FS          1801
#define GFS2_FORMAT_MULTI       1900

struct gfs2_meta_header {
	uint32_t mh_magic;
	uint32_t mh_type;
	uint64_t __pad0;          /* Was generation number in gfs1 */
	uint32_t mh_format;
	uint32_t __pad1;          /* Was incarnation number in gfs1 */
};

struct gfs2_inum {
	uint64_t no_formal_ino;
	uint64_t no_addr;
};

struct gfs2_sb {
	struct gfs2_meta_header sb_header;

	uint32_t sb_fs_format;
	uint32_t sb_multihost_format;
	uint32_t  __pad0;  /* Was superblock flags in gfs1 */

	uint32_t sb_bsize;
	uint32_t sb_bsize_shift;
	uint32_t __pad1;   /* Was journal segment size in gfs1 */

	struct gfs2_inum sb_master_dir; /* Was jindex dinode in gfs1 */
	struct gfs2_inum __pad2; /* Was rindex dinode in gfs1 */
	struct gfs2_inum sb_root_dir;

	char sb_lockproto[GFS_LOCKNAME_LEN];
	char sb_locktable[GFS_LOCKNAME_LEN];
	/* In gfs1, quota and license dinodes followed */
};

static int probe_gfs(blkid_probe pr, const struct blkid_idmag *mag)
{
	struct gfs2_sb *sbd;

	sbd = blkid_probe_get_sb(pr, mag, struct gfs2_sb);
	if (!sbd)
		return -1;

	if (be32_to_cpu(sbd->sb_fs_format) == GFS_FORMAT_FS &&
	    be32_to_cpu(sbd->sb_multihost_format) == GFS_FORMAT_MULTI)
	{
		if (strlen(sbd->sb_locktable))
			blkid_probe_set_label(pr,
				(unsigned char *) sbd->sb_locktable,
				sizeof(sbd->sb_locktable));
		return 0;
	}
	return -1;
}

static int probe_gfs2(blkid_probe pr, const struct blkid_idmag *mag)
{
	struct gfs2_sb *sbd;

	sbd = blkid_probe_get_sb(pr, mag, struct gfs2_sb);
	if (!sbd)
		return -1;

	if (be32_to_cpu(sbd->sb_fs_format) == GFS2_FORMAT_FS &&
	    be32_to_cpu(sbd->sb_multihost_format) == GFS2_FORMAT_MULTI)
	{
		if (strlen(sbd->sb_locktable))
			blkid_probe_set_label(pr,
				(unsigned char *) sbd->sb_locktable,
				sizeof(sbd->sb_locktable));
		return 0;
	}
	return -1;
}

const struct blkid_idinfo gfs_idinfo =
{
	.name		= "gfs",
	.usage		= BLKID_USAGE_FILESYSTEM,
	.probefunc	= probe_gfs,
	.magics		=
	{
		{ .magic = "\x01\x16\x19\x70", .len = 4, .kboff = 64 },
		{ NULL }
	}
};

const struct blkid_idinfo gfs2_idinfo =
{
	.name		= "gfs2",
	.usage		= BLKID_USAGE_FILESYSTEM,
	.probefunc	= probe_gfs2,
	.magics		=
	{
		{ .magic = "\x01\x16\x19\x70", .len = 4, .kboff = 64 },
		{ NULL }
	}
};

