/*
 * Copyright (C) 2009 by Andreas Dilger <adilger@sun.com>
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
#include <inttypes.h>

#include "blkidP.h"

/* #include <sys/uberblock_impl.h> */
#define UBERBLOCK_MAGIC         0x00bab10c              /* oo-ba-bloc!  */
struct zfs_uberblock {
	uint64_t	ub_magic;	/* UBERBLOCK_MAGIC		*/
	uint64_t	ub_version;	/* SPA_VERSION			*/
	uint64_t	ub_txg;		/* txg of last sync		*/
	uint64_t	ub_guid_sum;	/* sum of all vdev guids	*/
	uint64_t	ub_timestamp;	/* UTC time of last sync	*/
	/*blkptr_t	ub_rootbp;*/	/* MOS objset_phys_t		*/
} __attribute__((packed));

static int probe_zfs(blkid_probe pr, const struct blkid_idmag *mag)
{
	struct zfs_uberblock *ub;
	int swab_endian;
	uint64_t spa_version;

	ub = blkid_probe_get_sb(pr, mag, struct zfs_uberblock);
	if (!ub)
		return -1;

	swab_endian = (ub->ub_magic == swab64(UBERBLOCK_MAGIC));
	spa_version = swab_endian ? swab64(ub->ub_version) : ub->ub_version;

	blkid_probe_sprintf_version(pr, "%" PRIu64, spa_version);
#if 0
	/* read nvpair data for pool name, pool GUID from the MOS, but
	 * unfortunately this is more complex than it could be */
	blkid_probe_set_label(pr, pool_name, pool_len));
	blkid_probe_set_uuid(pr, pool_guid);
#endif
	return 0;
}

const struct blkid_idinfo zfs_idinfo =
{
	.name		= "zfs",
	.usage		= BLKID_USAGE_FILESYSTEM,
	.probefunc	= probe_zfs,
	.magics		=
	{
		{ .magic = "\0\0\x02\xf5\xb0\x07\xb1\x0c", .len = 8, .kboff = 8 },
		{ .magic = "\x1c\xb1\x07\xb0\xf5\x02\0\0", .len = 8, .kboff = 8 },
		{ .magic = "\0\0\x02\xf5\xb0\x07\xb1\x0c", .len = 8, .kboff = 264 },
		{ .magic = "\x0c\xb1\x07\xb0\xf5\x02\0\0", .len = 8, .kboff = 264 },
		{ NULL }
	}
};

