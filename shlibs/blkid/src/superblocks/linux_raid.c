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

#include "superblocks.h"

struct mdp0_super_block {
	uint32_t	md_magic;
	uint32_t	major_version;
	uint32_t	minor_version;
	uint32_t	patch_version;
	uint32_t	gvalid_words;
	uint32_t	set_uuid0;
	uint32_t	ctime;
	uint32_t	level;
	uint32_t	size;
	uint32_t	nr_disks;
	uint32_t	raid_disks;
	uint32_t	md_minor;
	uint32_t	not_persistent;
	uint32_t	set_uuid1;
	uint32_t	set_uuid2;
	uint32_t	set_uuid3;
};

struct mdp1_super_block {
	uint32_t	magic;
	uint32_t	major_version;
	uint32_t	feature_map;
	uint32_t	pad0;
	uint8_t		set_uuid[16];
	uint8_t		set_name[32];
};

#define MD_RESERVED_BYTES		0x10000
#define MD_SB_MAGIC			0xa92b4efc

static int probe_raid0(blkid_probe pr, off_t off)
{
	struct mdp0_super_block *mdp0;
	union {
		uint32_t ints[4];
		uint8_t bytes[16];
	} uuid;

	if (pr->size < 0x10000)
		return -1;
	mdp0 = (struct mdp0_super_block *)
			blkid_probe_get_buffer(pr,
				off,
				sizeof(struct mdp0_super_block));
	if (!mdp0)
		return -1;

	memset(uuid.ints, 0, sizeof(uuid.ints));

	if (le32_to_cpu(mdp0->md_magic) == MD_SB_MAGIC) {
		uuid.ints[0] = swab32(mdp0->set_uuid0);
		if (le32_to_cpu(mdp0->minor_version >= 90)) {
			uuid.ints[1] = swab32(mdp0->set_uuid1);
			uuid.ints[2] = swab32(mdp0->set_uuid2);
			uuid.ints[3] = swab32(mdp0->set_uuid3);
		}
		if (blkid_probe_sprintf_version(pr, "%u.%u.%u",
				le32_to_cpu(mdp0->major_version),
				le32_to_cpu(mdp0->minor_version),
				le32_to_cpu(mdp0->patch_version)) != 0)
			return -1;

	} else if (be32_to_cpu(mdp0->md_magic) == MD_SB_MAGIC) {
		uuid.ints[0] = mdp0->set_uuid0;
		if (be32_to_cpu(mdp0->minor_version >= 90)) {
			uuid.ints[1] = mdp0->set_uuid1;
			uuid.ints[2] = mdp0->set_uuid2;
			uuid.ints[3] = mdp0->set_uuid3;
		}
		if (blkid_probe_sprintf_version(pr, "%u.%u.%u",
				be32_to_cpu(mdp0->major_version),
				be32_to_cpu(mdp0->minor_version),
				be32_to_cpu(mdp0->patch_version)) != 0)
			return -1;
	} else
		return -1;

	if (blkid_probe_set_uuid(pr, (unsigned char *) uuid.bytes) != 0)
		return -1;

	return 0;
}

static int probe_raid1(blkid_probe pr, off_t off)
{
	struct mdp1_super_block *mdp1;

	mdp1 = (struct mdp1_super_block *)
			blkid_probe_get_buffer(pr,
				off,
				sizeof(struct mdp1_super_block));
	if (!mdp1)
		return -1;
	if (le32_to_cpu(mdp1->magic) != MD_SB_MAGIC)
		return -1;
	if (le32_to_cpu(mdp1->major_version) != 1)
		return -1;
	if (blkid_probe_set_uuid(pr, (unsigned char *) mdp1->set_uuid) != 0)
		return -1;
	if (blkid_probe_set_label(pr, mdp1->set_name,
				sizeof(mdp1->set_name)) != 0)
		return -1;

	return 0;
}

int probe_raid(blkid_probe pr, const struct blkid_idmag *mag)
{
	const char *ver = NULL;

	if (pr->size > MD_RESERVED_BYTES) {
		/* version 0 at the end of the device */
		uint64_t sboff = (pr->size & ~(MD_RESERVED_BYTES - 1))
			         - MD_RESERVED_BYTES;
		if (probe_raid0(pr, sboff) == 0)
			return 0;

		/* version 1.0 at the end of the device */
		sboff = (pr->size & ~(0x1000 - 1)) - 0x2000;
		if (probe_raid1(pr, sboff) == 0)
			ver = "1.0";
	}

	if (!ver) {
		/* version 1.1 at the start of the device */
		if (probe_raid1(pr, 0) == 0)
			ver = "1.1";

		/* version 1.2 at 4k offset from the start */
		else if (probe_raid1(pr, 0x1000) == 0)
			ver = "1.2";
	}

	if (ver) {
		blkid_probe_set_version(pr, ver);
		return 0;
	}
	return -1;
}


const struct blkid_idinfo linuxraid_idinfo = {
	.name		= "linux_raid_member",
	.usage		= BLKID_USAGE_RAID,
	.probefunc	= probe_raid,
	.magics		= BLKID_NONE_MAGIC
};


