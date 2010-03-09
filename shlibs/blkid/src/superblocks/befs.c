/*
 * Copyright (C) 2010 Jeroen Oortwijn <oortwijn@gmail.com>
 *
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <inttypes.h>

#include "superblocks.h"

#define B_OS_NAME_LENGTH	0x20
#define SUPER_BLOCK_MAGIC1	0x42465331	/* BFS1 */
#define SUPER_BLOCK_MAGIC2	0xdd121031
#define SUPER_BLOCK_MAGIC3	0x15b6830e
#define SUPER_BLOCK_FS_ENDIAN	0x42494745	/* BIGE */
#define INODE_MAGIC1		0x3bbe0ad9
#define B_UINT64_TYPE		0x554C4C47	/* ULLG */

#define FS16_TO_CPU(value, fs_is_le) (fs_is_le ? le16_to_cpu(value) \
							: be16_to_cpu(value))
#define FS32_TO_CPU(value, fs_is_le) (fs_is_le ? le32_to_cpu(value) \
							: be32_to_cpu(value))
#define FS64_TO_CPU(value, fs_is_le) (fs_is_le ? le64_to_cpu(value) \
							: be64_to_cpu(value))

typedef struct block_run {
	int32_t		allocation_group;
	uint16_t	start;
	uint16_t	len;
} __attribute__((packed)) block_run, inode_addr;

struct befs_super_block {
	char		name[B_OS_NAME_LENGTH];
	int32_t		magic1;
	int32_t		fs_byte_order;
	uint32_t	block_size;
	uint32_t	block_shift;
	int64_t		num_blocks;
	int64_t		used_blocks;
	int32_t		inode_size;
	int32_t		magic2;
	int32_t		blocks_per_ag;
	int32_t		ag_shift;
	int32_t		num_ags;
	int32_t		flags;
	block_run	log_blocks;
	int64_t		log_start;
	int64_t		log_end;
	int32_t		magic3;
	inode_addr	root_dir;
	inode_addr	indices;
	int32_t		pad[8];
} __attribute__((packed));

typedef struct data_stream {
	block_run	direct[12];
	int64_t		max_direct_range;
	block_run	indirect;
	int64_t		max_indirect_range;
	block_run	double_indirect;
	int64_t		max_double_indirect_range;
	int64_t		size;
} __attribute__((packed)) data_stream;

struct befs_inode {
	int32_t		magic1;
	inode_addr	inode_num;
	int32_t		uid;
	int32_t		gid;
	int32_t		mode;
	int32_t		flags;
	int64_t		create_time;
	int64_t		last_modified_time;
	inode_addr	parent;
	inode_addr	attributes;
	uint32_t	type;
	int32_t		inode_size;
	uint32_t	etc;
	data_stream	data;
	int32_t		pad[4];
	int32_t		small_data[1];
} __attribute__((packed));

struct small_data {
	uint32_t	type;
	uint16_t	name_size;
	uint16_t	data_size;
	char		name[0];
} __attribute__((packed));

static int probe_befs(blkid_probe pr, const struct blkid_idmag *mag)
{
	struct befs_super_block *bs;
	struct befs_inode *bi;
	struct small_data *sd;
	int fs_le;
	uint64_t volume_id = 0;
	const char *version = NULL;

	bs = (struct befs_super_block *) blkid_probe_get_buffer(pr,
					mag->sboff - B_OS_NAME_LENGTH,
					sizeof(struct befs_super_block));
	if (!bs)
		return -1;

	if (le32_to_cpu(bs->magic1) == SUPER_BLOCK_MAGIC1
		&& le32_to_cpu(bs->magic2) == SUPER_BLOCK_MAGIC2
		&& le32_to_cpu(bs->magic3) == SUPER_BLOCK_MAGIC3
		&& le32_to_cpu(bs->fs_byte_order) == SUPER_BLOCK_FS_ENDIAN) {
		fs_le = 1;
		version = "little-endian";
	} else if (be32_to_cpu(bs->magic1) == SUPER_BLOCK_MAGIC1
		&& be32_to_cpu(bs->magic2) == SUPER_BLOCK_MAGIC2
		&& be32_to_cpu(bs->magic3) == SUPER_BLOCK_MAGIC3
		&& be32_to_cpu(bs->fs_byte_order) == SUPER_BLOCK_FS_ENDIAN) {
		fs_le = 0;
		version = "big-endian";
	} else
		return -1;

	bi = (struct befs_inode *) blkid_probe_get_buffer(pr,
		((blkid_loff_t)FS32_TO_CPU(bs->root_dir.allocation_group, fs_le)
			<< FS32_TO_CPU(bs->ag_shift, fs_le)
			<< FS32_TO_CPU(bs->block_shift, fs_le))
		+ ((blkid_loff_t)FS16_TO_CPU(bs->root_dir.start, fs_le)
			<< FS32_TO_CPU(bs->block_shift, fs_le)),
		(blkid_loff_t)FS16_TO_CPU(bs->root_dir.len, fs_le)
			<< FS32_TO_CPU(bs->block_shift, fs_le));
	if (!bi)
		return -1;

	if (FS32_TO_CPU(bi->magic1, fs_le) != INODE_MAGIC1)
		return -1;

	/*
	 * all checks pass, set LABEL and VERSION
	 */
	if (strlen(bs->name))
		blkid_probe_set_label(pr, (unsigned char *) bs->name,
							sizeof(bs->name));
	if (version)
		blkid_probe_set_version(pr, version);

	/*
	 * search for UUID
	 */
	sd = (struct small_data *) bi->small_data;

	do {
		if (FS32_TO_CPU(sd->type, fs_le) == B_UINT64_TYPE
				&& FS16_TO_CPU(sd->name_size, fs_le) == 12
				&& FS16_TO_CPU(sd->data_size, fs_le) == 8
				&& strcmp(sd->name, "be:volume_id") == 0) {
			volume_id = *(uint64_t *) ((uint8_t *) sd->name
					+ FS16_TO_CPU(sd->name_size, fs_le)
					+ 3);
			blkid_probe_sprintf_uuid(pr,
					(unsigned char *) &volume_id,
					sizeof(volume_id),
					"%016" PRIx64,
					FS64_TO_CPU(volume_id, fs_le));
			break;
		} else if (FS32_TO_CPU(sd->type, fs_le) == 0
				&& FS16_TO_CPU(sd->name_size, fs_le) == 0
				&& FS16_TO_CPU(sd->data_size, fs_le) == 0) {
			break;
		}

		sd = (struct small_data *) ((uint8_t *) sd
				+ sizeof(struct small_data)
				+ FS16_TO_CPU(sd->name_size, fs_le) + 3
				+ FS16_TO_CPU(sd->data_size, fs_le) + 1);

	} while ((intptr_t) sd < (intptr_t) bi
					+ FS32_TO_CPU(bi->inode_size, fs_le)
					- sizeof(struct small_data));

	if (volume_id == 0) {
		/*
		 * TODO: Search for the be:volume_id attribute in the
		 *       attributes directory of the root directory.
		 */
	}

	return 0;
}

const struct blkid_idinfo befs_idinfo =
{
	.name		= "befs",
	.usage		= BLKID_USAGE_FILESYSTEM,
	.probefunc	= probe_befs,
	.minsz		= 1024 * 1440,
	.magics		= {
		{ .magic = "BFS1", .len = 4, .sboff = B_OS_NAME_LENGTH },
		{ .magic = "1SFB", .len = 4, .sboff = B_OS_NAME_LENGTH },
		{ .magic = "BFS1", .len = 4, .sboff = 0x200 +
							B_OS_NAME_LENGTH },
		{ .magic = "1SFB", .len = 4, .sboff = 0x200 +
							B_OS_NAME_LENGTH },
		{ NULL }
	}
};
