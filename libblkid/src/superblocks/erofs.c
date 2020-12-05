/*
 * Copyright (C) 2020 Gao Xiang
 *
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License
 */
#include <stddef.h>
#include <string.h>

#include "superblocks.h"

#define EROFS_SUPER_OFFSET      1024
#define EROFS_SB_KBOFF		(EROFS_SUPER_OFFSET >> 10)

#define EROFS_SUPER_MAGIC_V1	"\xe2\xe1\xf5\xe0"
#define EROFS_MAGIC_OFF		0

/* All in little-endian */
struct erofs_super_block {
	uint32_t	magic;
	uint32_t	checksum;
	uint32_t	feature_compat;
	uint8_t		blkszbits;
	uint8_t		reserved;

	uint16_t	root_nid;
	uint64_t	inos;

	uint64_t	build_time;
	uint32_t	build_time_nsec;
	uint32_t	blocks;
	uint32_t	meta_blkaddr;
	uint32_t	xattr_blkaddr;
	uint8_t		uuid[16];
	uint8_t		volume_name[16];
	uint32_t	feature_incompat;
	uint8_t		reserved2[44];
};

static int probe_erofs(blkid_probe pr, const struct blkid_idmag *mag)
{
	struct erofs_super_block *sb;

	sb = blkid_probe_get_sb(pr, mag, struct erofs_super_block);
	if (!sb)
		return errno ? -errno : BLKID_PROBE_NONE;

	if (sb->volume_name[0])
		blkid_probe_set_label(pr, (unsigned char *)sb->volume_name,
				      sizeof(sb->volume_name));

	blkid_probe_set_uuid(pr, sb->uuid);

	if (sb->blkszbits < 32)
		blkid_probe_set_block_size(pr, 1U << sb->blkszbits);
	return BLKID_PROBE_OK;
}

const struct blkid_idinfo erofs_idinfo =
{
	.name           = "erofs",
	.usage          = BLKID_USAGE_FILESYSTEM,
	.probefunc      = probe_erofs,
	.magics         =
        {
		{
			.magic = EROFS_SUPER_MAGIC_V1,
			.len = 4,
			.kboff = EROFS_SB_KBOFF,
			.sboff = EROFS_MAGIC_OFF,
		}, { NULL }
	}
};
