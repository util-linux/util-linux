/*
 * Copyright (C) 1999, 2001 by Andries Brouwer
 * Copyright (C) 1999, 2000, 2003 by Theodore Ts'o
 * Copyright (C) 2008 Karel Zak <kzak@redhat.com>
 *
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>

#include "blkidP.h"

struct ocfs_volume_header {
	unsigned char	minor_version[4];
	unsigned char	major_version[4];
	unsigned char	signature[128];
	char		mount[128];
	unsigned char   mount_len[2];
};

struct ocfs_volume_label {
	unsigned char	disk_lock[48];
	char		label[64];
	unsigned char	label_len[2];
	unsigned char   vol_id[16];
	unsigned char   vol_id_len[2];
};

#define ocfsmajor(o) ( (uint32_t) o.major_version[0] \
                   + (((uint32_t) o.major_version[1]) << 8) \
                   + (((uint32_t) o.major_version[2]) << 16) \
                   + (((uint32_t) o.major_version[3]) << 24))

#define ocfsminor(o) ( (uint32_t) o.minor_version[0] \
                   + (((uint32_t) o.minor_version[1]) << 8) \
                   + (((uint32_t) o.minor_version[2]) << 16) \
                   + (((uint32_t) o.minor_version[3]) << 24))

#define ocfslabellen(o)	((uint32_t)o.label_len[0] + (((uint32_t) o.label_len[1]) << 8))
#define ocfsmountlen(o)	((uint32_t)o.mount_len[0] + (((uint32_t) o.mount_len[1]) << 8))

struct ocfs2_super_block {
	unsigned char  signature[8];
	unsigned char  s_dummy1[184];
	unsigned char  s_dummy2[80];
	char	       s_label[64];
	unsigned char  s_uuid[16];
};

struct oracle_asm_disk_label {
	char dummy[32];
	char dl_tag[8];
	char dl_id[24];
};

static int probe_ocfs(blkid_probe pr, const struct blkid_idmag *mag)
{
	unsigned char *buf;
	struct ocfs_volume_header ovh;
	struct ocfs_volume_label ovl;
	uint32_t maj, min;

	/* header */
	buf = blkid_probe_get_buffer(pr, mag->kboff << 10,
			sizeof(struct ocfs_volume_header));
	if (!buf)
		return -1;
	memcpy(&ovh, buf, sizeof(ovh));

	/* label */
	buf = blkid_probe_get_buffer(pr, (mag->kboff << 10) + 512,
			sizeof(struct ocfs_volume_label));
	if (!buf)
		return -1;
	memcpy(&ovl, buf, sizeof(ovl));

	maj = ocfsmajor(ovh);
	min = ocfsminor(ovh);

	if (maj == 1)
		blkid_probe_set_value(pr, "SEC_TYPE",
				(unsigned char *) "ocfs1", sizeof("ocfs1"));
	else if (maj >= 9)
		blkid_probe_set_value(pr, "SEC_TYPE",
				(unsigned char *) "ntocfs", sizeof("ntocfs"));

	blkid_probe_set_label(pr, (unsigned char *) ovl.label,
				ocfslabellen(ovl));
	blkid_probe_set_value(pr, "MOUNT", (unsigned char *) ovh.mount,
				ocfsmountlen(ovh));
	blkid_probe_set_uuid(pr, ovl.vol_id);
	blkid_probe_sprintf_version(pr, "%u.%u", maj, min);
	return 0;
}

static int probe_ocfs2(blkid_probe pr, const struct blkid_idmag *mag)
{
	struct ocfs2_super_block *osb;

	osb = blkid_probe_get_sb(pr, mag, struct ocfs2_super_block);
	if (!osb)
		return -1;

	blkid_probe_set_label(pr, (unsigned char *) osb->s_label, sizeof(osb->s_label));
	blkid_probe_set_uuid(pr, osb->s_uuid);
	return 0;
}

static int probe_oracleasm(blkid_probe pr, const struct blkid_idmag *mag)
{
	struct oracle_asm_disk_label *dl;

	dl = blkid_probe_get_sb(pr, mag, struct oracle_asm_disk_label);
	if (!dl)
		return -1;

	blkid_probe_set_label(pr, (unsigned char *) dl->dl_id, sizeof(dl->dl_id));
	return 0;
}


const struct blkid_idinfo ocfs_idinfo =
{
	.name		= "ocfs",
	.usage		= BLKID_USAGE_FILESYSTEM,
	.probefunc	= probe_ocfs,
	.magics		=
	{
		{ .magic = "OracleCFS", .len = 9, .kboff = 8 },
		{ NULL }
	}
};

const struct blkid_idinfo ocfs2_idinfo =
{
	.name		= "ocfs2",
	.usage		= BLKID_USAGE_FILESYSTEM,
	.probefunc	= probe_ocfs2,
	.magics		=
	{
		{ .magic = "OCFSV2", .len = 6, .kboff = 1 },
		{ .magic = "OCFSV2", .len = 6, .kboff = 2 },
		{ .magic = "OCFSV2", .len = 6, .kboff = 4 },
		{ .magic = "OCFSV2", .len = 6, .kboff = 8 },
		{ NULL }
	}
};

const struct blkid_idinfo oracleasm_idinfo =
{
	.name		= "oracleasm",
	.usage		= BLKID_USAGE_FILESYSTEM,
	.probefunc	= probe_oracleasm,
	.magics		=
	{
		{ .magic = "ORCLDISK", .len = 8, .sboff = 32 },
		{ NULL }
	}
};

