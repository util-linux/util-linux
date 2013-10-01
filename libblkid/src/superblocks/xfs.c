/*
 * Copyright (C) 1999 by Andries Brouwer
 * Copyright (C) 1999, 2000, 2003 by Theodore Ts'o
 * Copyright (C) 2001 by Andreas Dilger
 * Copyright (C) 2004 Kay Sievers <kay.sievers@vrfy.org>
 * Copyright (C) 2008 Karel Zak <kzak@redhat.com>
 * Copyright (C) 2013 Eric Sandeen <sandeen@redhat.com>
 *
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <stdint.h>

#include "superblocks.h"

struct xfs_super_block {
	unsigned char	xs_magic[4];
	uint32_t	xs_blocksize;
	uint64_t	xs_dblocks;
	uint64_t	xs_rblocks;
	uint32_t	xs_dummy1[2];
	unsigned char	xs_uuid[16];
	uint32_t	xs_dummy2[15];
	char		xs_fname[12];
	uint32_t	xs_dummy3[2];
	uint64_t	xs_icount;
	uint64_t	xs_ifree;
	uint64_t	xs_fdblocks;
} __attribute__((packed));

static int probe_xfs(blkid_probe pr, const struct blkid_idmag *mag)
{
	struct xfs_super_block *xs;

	xs = blkid_probe_get_sb(pr, mag, struct xfs_super_block);
	if (!xs)
		return -1;

	if (strlen(xs->xs_fname))
		blkid_probe_set_label(pr, (unsigned char *) xs->xs_fname,
				sizeof(xs->xs_fname));
	blkid_probe_set_uuid(pr, xs->xs_uuid);
	return 0;
}

const struct blkid_idinfo xfs_idinfo =
{
	.name		= "xfs",
	.usage		= BLKID_USAGE_FILESYSTEM,
	.probefunc	= probe_xfs,
	.magics		=
	{
		{ .magic = "XFSB", .len = 4 },
		{ NULL }
	}
};

struct xlog_rec_header {
	uint32_t	h_magicno;
	uint32_t	h_dummy1[1];
	uint32_t	h_version;
	uint32_t	h_len;
	uint32_t	h_dummy2[71];
	uint32_t	h_fmt;
	unsigned char	h_uuid[16];
} __attribute__((packed));

#define XLOG_HEADER_MAGIC_NUM 0xFEEDbabe

/*
 * For very small filesystems, the minimum log size
 * can be smaller, but that seems vanishingly unlikely
 * when used with an external log (which is used for
 * performance reasons; tiny conflicts with that goal).
 */
#define XFS_MIN_LOG_BYTES	(10 * 1024 * 1024)

#define XLOG_FMT_LINUX_LE	1
#define XLOG_FMT_LINUX_BE	2
#define XLOG_FMT_IRIX_BE	3

#define XLOG_VERSION_1		1
#define XLOG_VERSION_2		2	/* Large IClogs, Log sunit */
#define XLOG_VERSION_OKBITS	(XLOG_VERSION_1 | XLOG_VERSION_2)

static int xlog_valid_rec_header(struct xlog_rec_header *rhead)
{
	uint32_t hlen;

	if (rhead->h_magicno != cpu_to_be32(XLOG_HEADER_MAGIC_NUM))
		return 0;

	if (!rhead->h_version ||
            (be32_to_cpu(rhead->h_version) & (~XLOG_VERSION_OKBITS)))
		return 0;

	/* LR body must have data or it wouldn't have been written */
	hlen = be32_to_cpu(rhead->h_len);
	if (hlen <= 0 || hlen > INT_MAX)
		return 0;

	if (rhead->h_fmt != cpu_to_be32(XLOG_FMT_LINUX_LE) &&
	    rhead->h_fmt != cpu_to_be32(XLOG_FMT_LINUX_BE) &&
	    rhead->h_fmt != cpu_to_be32(XLOG_FMT_IRIX_BE))
		return 0;

	return 1;
}

/* xlog record header will be in some sector in the first 256k */
static int probe_xfs_log(blkid_probe pr, const struct blkid_idmag *mag)
{
	int i;
	struct xlog_rec_header *rhead;
	unsigned char *buf;

	buf = blkid_probe_get_buffer(pr, 0, 256*1024);
	if (!buf)
		return -1;

	if (memcmp(buf, "XFSB", 4) == 0)
		return 1;			/* this is regular XFS, ignore */

	/* check the first 512 512-byte sectors */
	for (i = 0; i < 512; i++) {
		rhead = (struct xlog_rec_header *)&buf[i*512];

		if (xlog_valid_rec_header(rhead)) {
			blkid_probe_set_uuid_as(pr, rhead->h_uuid, "LOGUUID");
			return 0;
		}
	}

	return -1;
}

const struct blkid_idinfo xfs_log_idinfo =
{
	.name		= "xfs_external_log",
	.usage		= BLKID_USAGE_OTHER,
	.probefunc	= probe_xfs_log,
	.magics		= BLKID_NONE_MAGIC,
	.minsz		= XFS_MIN_LOG_BYTES,
};
