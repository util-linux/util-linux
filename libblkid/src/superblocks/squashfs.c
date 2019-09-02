/*
 * Copyright (C) 2008 Karel Zak <kzak@redhat.com>
 *
 * Inspired by libvolume_id by
 *     Kay Sievers <kay.sievers@vrfy.org>
 *
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>

#include "bitops.h"	/* swab16() */
#include "superblocks.h"

struct sqsh_super_block {
	uint32_t	s_magic;
	uint32_t	inodes;
	uint32_t	bytes_used_2;
	uint32_t	uid_start_2;
	uint32_t	guid_start_2;
	uint32_t	inode_table_start_2;
	uint32_t	directory_table_start_2;
	uint16_t	s_major;
	uint16_t	s_minor;
} __attribute__((packed));

static int probe_squashfs(blkid_probe pr, const struct blkid_idmag *mag)
{
	struct sqsh_super_block *sq;
	uint16_t vermaj;
	uint16_t vermin;

	sq = blkid_probe_get_sb(pr, mag, struct sqsh_super_block);
	if (!sq)
		return errno ? -errno : 1;

	vermaj = le16_to_cpu(sq->s_major);
	vermin = le16_to_cpu(sq->s_minor);
	if (vermaj < 4)
		return 1;

	blkid_probe_sprintf_version(pr, "%u.%u", vermaj, vermin);

	return 0;
}

static int probe_squashfs3(blkid_probe pr, const struct blkid_idmag *mag)
{
	struct sqsh_super_block *sq;
	uint16_t vermaj;
	uint16_t vermin;

	sq = blkid_probe_get_sb(pr, mag, struct sqsh_super_block);
	if (!sq)
		return errno ? -errno : 1;

	if (strcmp(mag->magic, "sqsh") == 0) {
		vermaj = be16_to_cpu(sq->s_major);
		vermin = be16_to_cpu(sq->s_minor);
	} else {
		vermaj = le16_to_cpu(sq->s_major);
		vermin = le16_to_cpu(sq->s_minor);
	}

	if (vermaj > 3)
		return 1;

	blkid_probe_sprintf_version(pr, "%u.%u", vermaj, vermin);

	blkid_probe_set_block_size(pr, 1024);

	return 0;
}

const struct blkid_idinfo squashfs_idinfo =
{
	.name		= "squashfs",
	.usage		= BLKID_USAGE_FILESYSTEM,
	.probefunc	= probe_squashfs,
	.magics		=
	{
		{ .magic = "hsqs", .len = 4 },
		{ NULL }
	}
};

const struct blkid_idinfo squashfs3_idinfo =
{
	.name		= "squashfs3",
	.usage		= BLKID_USAGE_FILESYSTEM,
	.probefunc	= probe_squashfs3,
	.magics		=
	{
		{ .magic = "sqsh", .len = 4 }, /* big endian */
		{ .magic = "hsqs", .len = 4 }, /* little endian */
		{ NULL }
	}
};

