/*
 * Copyright (C) 1999 by Andries Brouwer
 * Copyright (C) 1999, 2000, 2003 by Theodore Ts'o
 * Copyright (C) 2001 by Andreas Dilger
 * Copyright (C) 2004 Kay Sievers <kay.sievers@vrfy.org>
 * Copyright (C) 2008 Karel Zak <kzak@redhat.com>
 * Copyright (C) 2014-2017 Pali Rohár <pali.rohar@gmail.com>
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

#define udf_cid_to_enc(cid) ((cid) == 8 ? UL_ENCODE_LATIN1 : (cid) == 16 ? UL_ENCODE_UTF16BE : -1)

struct dstring128 {
	uint8_t	cid;
	uint8_t	c[126];
	uint8_t	clen;
} __attribute__((packed));

struct dstring32 {
	uint8_t	cid;
	uint8_t	c[30];
	uint8_t	clen;
} __attribute__((packed));

struct volume_descriptor {
	struct descriptor_tag {
		uint16_t	id;
		uint16_t	version;
		uint8_t		checksum;
		uint8_t		reserved;
		uint16_t	serial;
		uint16_t	crc;
		uint16_t	crc_len;
		uint32_t	location;
	} __attribute__((packed)) tag;

	union {
		struct anchor_descriptor {
			uint32_t	length;
			uint32_t	location;
		} __attribute__((packed)) anchor;

		struct primary_descriptor {
			uint32_t	seq_num;
			uint32_t	desc_num;
			struct dstring32 ident;
			uint16_t	vds_num;
			uint16_t	max_vol_seq;
			uint16_t	ichg_lvl;
			uint16_t	max_ichg_lvl;
			uint32_t	charset_list;
			uint32_t	max_charset_list;
			struct dstring128 volset_id;
		} __attribute__((packed)) primary;

		struct logical_descriptor {
			uint32_t	seq_num;
			uint8_t		desc_charset[64];
			struct dstring128 logvol_id;
			uint32_t	logical_blocksize;
			uint8_t		domain_id_flags;
			char		domain_id[23];
			uint16_t	udf_rev;
			uint8_t		domain_suffix_flags;
			uint8_t		reserved[5];
			uint8_t		logical_contents_use[16];
			uint32_t	map_table_length;
			uint32_t	num_partition_maps;
			uint8_t		imp_id[32];
			uint8_t		imp_use[128];
			uint32_t	lvid_length;
			uint32_t	lvid_location;
		} __attribute__((packed)) logical;

		struct logical_vol_integ_descriptor {
			uint8_t		recording_date[12];
			uint32_t	type;
			uint32_t	next_lvid_length;
			uint32_t	next_lvid_location;
			uint8_t		logical_contents_use[32];
			uint32_t	num_partitions;
			uint32_t	imp_use_length;
		} __attribute__((packed)) logical_vol_integ;
	} __attribute__((packed)) type;

} __attribute__((packed));

#define TAG_ID_PVD  1
#define TAG_ID_AVDP 2
#define TAG_ID_LVD  6
#define TAG_ID_TD   8
#define TAG_ID_LVID 9

struct volume_structure_descriptor {
	uint8_t		type;
	uint8_t		id[5];
	uint8_t		version;
} __attribute__((packed));

#define UDF_VSD_OFFSET			0x8000LL

struct logical_vol_integ_descriptor_imp_use
{
	uint8_t		imp_id[32];
	uint32_t	num_files;
	uint32_t	num_dirs;
	uint16_t	min_udf_read_rev;
	uint16_t	min_udf_write_rev;
	uint16_t	max_udf_write_rev;
} __attribute__ ((packed));

#define UDF_LVIDIU_OFFSET(vd) (sizeof((vd).tag) + sizeof((vd).type.logical_vol_integ) + 2 * 4 * le32_to_cpu((vd).type.logical_vol_integ.num_partitions))
#define UDF_LVIDIU_LENGTH(vd) (le32_to_cpu((vd).type.logical_vol_integ.imp_use_length))

static inline int gen_uuid_from_volset_id(unsigned char uuid[17], struct dstring128 *volset_id)
{
	int enc;
	size_t i;
	size_t len;
	size_t clen;
	size_t nonhexpos;
	unsigned char buf[17];

	memset(buf, 0, sizeof(buf));

	clen = volset_id->clen;
	if (clen > 0)
		--clen;
	if (clen > sizeof(volset_id->c))
		clen = sizeof(volset_id->c);

	enc = udf_cid_to_enc(volset_id->cid);
	if (enc == -1)
		return -1;

	len = ul_encode_to_utf8(enc, buf, sizeof(buf), volset_id->c, clen);
	if (len < 8)
		return -1;

	nonhexpos = 16;
	for (i = 0; i < 16; ++i) {
		if (!isxdigit(buf[i])) {
			nonhexpos = i;
			break;
		}
	}

	if (nonhexpos < 8) {
		snprintf((char *) uuid, 17, "%02x%02x%02x%02x%02x%02x%02x%02x",
			buf[0], buf[1], buf[2], buf[3],
			buf[4], buf[5], buf[6], buf[7]);
	} else if (nonhexpos < 16) {
		for (i = 0; i < 8; ++i)
			uuid[i] = tolower(buf[i]);
		snprintf((char *) uuid + 8, 9, "%02x%02x%02x%02x",
			buf[8], buf[9], buf[10], buf[11]);
	} else {
		for (i = 0; i < 16; ++i)
			uuid[i] = tolower(buf[i]);
		uuid[16] = 0;
	}

	return 0;
}

static int probe_udf(blkid_probe pr,
		const struct blkid_idmag *mag __attribute__((__unused__)))
{
	struct volume_descriptor *vd;
	struct volume_structure_descriptor *vsd;
	struct logical_vol_integ_descriptor_imp_use *lvidiu;
	uint32_t lvid_len = 0;
	uint32_t lvid_loc = 0;
	uint32_t bs;
	uint32_t b;
	uint16_t type;
	uint32_t count;
	uint32_t loc;
	size_t i;
	uint32_t vsd_len;
	uint16_t udf_rev = 0;
	int vsd_2048_valid = -1;
	int have_label = 0;
	int have_uuid = 0;
	int have_logvolid = 0;
	int have_volid = 0;
	int have_volsetid = 0;

	/* The block size of a UDF filesystem is that of the underlying
	 * storage; we check later on for the special case of image files,
	 * which may have any block size valid for UDF filesystem */
	uint32_t pbs[] = { 0, 512, 1024, 2048, 4096 };
	pbs[0] = blkid_probe_get_sectorsize(pr);

	for (i = 0; i < ARRAY_SIZE(pbs); i++) {
		/* Do not try with block size same as sector size two times */
		if (i != 0 && pbs[0] == pbs[i])
			continue;

		/* ECMA-167 2/8.4, 2/9.1: Each VSD is either 2048 bytes long or
		 * its size is same as blocksize (for blocksize > 2048 bytes)
		 * plus padded with zeros */
		vsd_len = pbs[i] > 2048 ? pbs[i] : 2048;

		/* Process 2048 bytes long VSD only once */
		if (vsd_len == 2048) {
			if (vsd_2048_valid == 0)
				continue;
			if (vsd_2048_valid == 1)
				goto anchor;
		}

		/* Check for a Volume Structure Descriptor (VSD) */
		for (b = 0; b < 64; b++) {
			vsd = (struct volume_structure_descriptor *)
				blkid_probe_get_buffer(pr,
						UDF_VSD_OFFSET + b * vsd_len,
						sizeof(*vsd));
			if (!vsd)
				return errno ? -errno : 1;
			if (vsd->id[0] == '\0')
				break;
			if (memcmp(vsd->id, "NSR02", 5) == 0 ||
			    memcmp(vsd->id, "NSR03", 5) == 0)
				goto anchor;
			else if (memcmp(vsd->id, "BEA01", 5) != 0 &&
			         memcmp(vsd->id, "BOOT2", 5) != 0 &&
			         memcmp(vsd->id, "CD001", 5) != 0 &&
			         memcmp(vsd->id, "CDW02", 5) != 0 &&
			         memcmp(vsd->id, "TEA01", 5) != 0)
				/* ECMA-167 2/8.3.1: The volume recognition sequence is
				 * terminated by the first sector which is not a valid
				 * descriptor.
				 * UDF-2.60 2.1.7: UDF 2.00 and lower revisions do not
				 * have requirement that NSR descriptor is in Extended Area
				 * (between BEA01 and TEA01) and that there is only one
				 * Extended Area. So do not stop scanning after TEA01. */
				break;
		}

		if (vsd_len == 2048)
			vsd_2048_valid = 0;

		/* NSR was not found, try with next block size */
		continue;

anchor:
		if (vsd_len == 2048)
			vsd_2048_valid = 1;

		/* Read Anchor Volume Descriptor (AVDP), detect block size */
		vd = (struct volume_descriptor *)
			blkid_probe_get_buffer(pr, 256 * pbs[i], sizeof(*vd));
		if (!vd)
			return errno ? -errno : 1;

		/* Check that we read correct sector and detected correct block size */
		if (le32_to_cpu(vd->tag.location) != 256)
			continue;

		type = le16_to_cpu(vd->tag.id);
		if (type == TAG_ID_AVDP)
			goto real_blksz;

	}
	return 1;

real_blksz:
	/* Use the actual block size from here on out */
	bs = pbs[i];

	/* get descriptor list address and block count */
	count = le32_to_cpu(vd->type.anchor.length) / bs;
	loc = le32_to_cpu(vd->type.anchor.location);

	/* pick the primary descriptor from the list and read UDF identifiers */
	for (b = 0; b < count; b++) {
		vd = (struct volume_descriptor *)
			blkid_probe_get_buffer(pr,
					(uint64_t) (loc + b) * bs,
					sizeof(*vd));
		if (!vd)
			return errno ? -errno : 1;
		type = le16_to_cpu(vd->tag.id);
		if (type == 0)
			break;
		if (le32_to_cpu(vd->tag.location) != loc + b)
			break;
		if (type == TAG_ID_TD)
			break;
		if (type == TAG_ID_PVD) {
			if (!have_volid) {
				int enc = udf_cid_to_enc(vd->type.primary.ident.cid);
				uint8_t clen = vd->type.primary.ident.clen;
				if (clen > 0)
					--clen;
				if (clen > sizeof(vd->type.primary.ident.c))
					clen = sizeof(vd->type.primary.ident.c);
				if (enc != -1)
					have_volid = !blkid_probe_set_utf8_id_label(pr, "VOLUME_ID",
							vd->type.primary.ident.c, clen, enc);
			}
			if (!have_uuid) {
				/* VolumeSetIdentifier in UDF 2.01 specification:
				 * =================================================================================
				 * 2.2.2.5 dstring VolumeSetIdentifier
				 *
				 * Interpreted as specifying the identifier for the volume set.
				 *
				 * The first 16 characters of this field should be set to a unique value. The
				 * remainder of the field may be set to any allowed value. Specifically, software
				 * generating volumes conforming to this specification shall not set this field to a
				 * fixed or trivial value. Duplicate disks which are intended to be identical may
				 * contain the same value in this field.
				 *
				 * NOTE: The intended purpose of this is to guarantee Volume Sets with unique
				 * identifiers. The first 8 characters of the unique part should come from a CS0
				 * hexadecimal representation of a 32-bit time value. The remaining 8 characters
				 * are free for implementation use.
				 * =================================================================================
				 *
				 * Implementation in libblkid:
				 * The first 16 (Unicode) characters of VolumeSetIdentifier are encoded to UTF-8
				 * and then first 16 UTF-8 bytes are used to generate UUID. If all 16 bytes are
				 * hexadecimal digits then their lowercase variants are used as UUID. If one of
				 * the first 8 bytes (time value) is not hexadecimal digit then first 8 bytes are
				 * encoded to their hexadecimal representations, resulting in 16 characters and
				 * set as UUID. If all first 8 bytes (time value) are hexadecimal digits but some
				 * remaining not then lowercase variant of the first 8 bytes are used as first
				 * part of UUID and next 4 bytes encoded in hexadecimal representations (resulting
				 * in 8 characters) are used as second part of UUID string.
				 */
				unsigned char uuid[17];
				if (gen_uuid_from_volset_id(uuid, &vd->type.primary.volset_id) == 0)
					have_uuid = !blkid_probe_strncpy_uuid(pr, uuid, sizeof(uuid));
			}
			if (!have_volsetid) {
				int enc = udf_cid_to_enc(vd->type.primary.volset_id.cid);
				uint8_t clen = vd->type.primary.volset_id.clen;
				if (clen > 0)
					--clen;
				if (clen > sizeof(vd->type.primary.volset_id.c))
					clen = sizeof(vd->type.primary.volset_id.c);
				if (enc != -1)
					have_volsetid = !blkid_probe_set_utf8_id_label(pr, "VOLUME_SET_ID",
							vd->type.primary.volset_id.c, clen, enc);
			}
		} else if (type == TAG_ID_LVD) {
			if (!lvid_len || !lvid_loc) {
				uint32_t num_partition_maps = le32_to_cpu(vd->type.logical.num_partition_maps);
				/* ECMA-167 3/10.6.12: If num_partition_maps is 0, then no LVID is specified */
				if (num_partition_maps) {
					lvid_len = le32_to_cpu(vd->type.logical.lvid_length);
					lvid_loc = le32_to_cpu(vd->type.logical.lvid_location);
				}
			}
			if (!udf_rev) {
				/* UDF-2.60: 2.1.5.3: UDF revision field shall indicate revision of UDF document
				 * We use maximal value from this field and from LVIDIU fields for ID_FS_VERSION */
				if (strncmp(vd->type.logical.domain_id, "*OSTA UDF Compliant", sizeof(vd->type.logical.domain_id)) == 0)
					udf_rev = le16_to_cpu(vd->type.logical.udf_rev);
			}
			if (!have_logvolid || !have_label) {
				/* LogicalVolumeIdentifier in UDF 2.01 specification:
				 * ===============================================================
				 * 2. Basic Restrictions & Requirements
				 *
				 * Logical Volume Descriptor
				 *
				 * There shall be exactly one prevailing Logical Volume
				 * Descriptor recorded per Volume Set.
				 *
				 * The LogicalVolumeIdentifier field shall not be null and
				 * should contain an identifier that aids in the identification of
				 * the logical volume. Specifically, software generating
				 * volumes conforming to this specification shall not set this
				 * field to a fixed or trivial value. Duplicate disks, which are
				 * intended to be identical, may contain the same value in this
				 * field. This field is extremely important in logical volume
				 * identification when multiple media are present within a
				 * jukebox. This name is typically what is displayed to the user.
				 * ===============================================================
				 *
				 * Implementation in libblkid:
				 * The LogicalVolumeIdentifier field is used for LABEL. MS Windows
				 * read Volume Label also from LogicalVolumeIdentifier. Grub2 read
				 * LABEL also from this field. Program newfs_udf (from UDFclient)
				 * when formatting disk set this field from user option Disc Name.
				 */
				int enc = udf_cid_to_enc(vd->type.logical.logvol_id.cid);
				uint8_t clen = vd->type.logical.logvol_id.clen;
				if (clen > 0)
					--clen;
				if (clen > sizeof(vd->type.logical.logvol_id.c))
					clen = sizeof(vd->type.logical.logvol_id.c);
				if (enc != -1) {
					if (!have_label)
						have_label = !blkid_probe_set_utf8label(pr,
								vd->type.logical.logvol_id.c, clen, enc);
					if (!have_logvolid)
						have_logvolid = !blkid_probe_set_utf8_id_label(pr, "LOGICAL_VOLUME_ID",
								vd->type.logical.logvol_id.c, clen, enc);
				}
			}
		}
		if (have_volid && have_uuid && have_volsetid && have_logvolid && have_label && lvid_len && lvid_loc)
			break;
	}

	/* Pick the first logical volume integrity descriptor and read UDF revision */
	if (lvid_loc && lvid_len >= sizeof(*vd)) {
		vd = (struct volume_descriptor *)
			blkid_probe_get_buffer(pr,
					(uint64_t) lvid_loc * bs,
					sizeof(*vd));
		if (!vd)
			return errno ? -errno : 1;
		type = le16_to_cpu(vd->tag.id);
		if (type == TAG_ID_LVID &&
		    le32_to_cpu(vd->tag.location) == lvid_loc &&
		    UDF_LVIDIU_LENGTH(*vd) >= sizeof(*lvidiu)) {
			/* ECMA-167 3/8.8.2: There is stored sequence of LVIDs and valid is just last
			 * one. So correctly we should jump to next_lvid_location and read next LVID
			 * until we find last one. This could be time consuming process and could
			 * lead to scanning lot of disk blocks. Because we use LVID only for UDF
			 * version, in the worst case we would report only wrong ID_FS_VERSION. */
			uint16_t lvidiu_udf_rev;
			lvidiu = (struct logical_vol_integ_descriptor_imp_use *)
				blkid_probe_get_buffer(pr,
						(uint64_t) lvid_loc * bs + UDF_LVIDIU_OFFSET(*vd),
						sizeof(*lvidiu));
			if (!lvidiu)
				return errno ? -errno : 1;
			/* UDF-2.60: 2. Basic Restrictions & Requirements:
			 * The Minimum UDF Read Revision value shall be at most #0250
			 * for all media with a UDF 2.60 file system.
			 * Because some 2.60 implementations put 2.50 into both LVIDIU
			 * fields and 2.60 into LVD, use maximal value from LVD,
			 * Minimum UDF Read Revision and Minimum UDF Write Revision for
			 * ID_FS_VERSION to distinguish between UDF 2.50 and UDF 2.60 discs. */
			lvidiu_udf_rev = le16_to_cpu(lvidiu->min_udf_read_rev);
			if (lvidiu_udf_rev && udf_rev < lvidiu_udf_rev)
				udf_rev = lvidiu_udf_rev;
			lvidiu_udf_rev = le16_to_cpu(lvidiu->min_udf_write_rev);
			if (lvidiu_udf_rev && udf_rev < lvidiu_udf_rev)
				udf_rev = lvidiu_udf_rev;
		}
	}

	if (udf_rev)
		/* UDF revision is stored as decimal number in hexadecimal format.
		 * E.g. number 0x0150 is revision 1.50, number 0x0201 is revision 2.01. */
		blkid_probe_sprintf_version(pr, "%x.%02x", (unsigned int)(udf_rev >> 8), (unsigned int)(udf_rev & 0xFF));

	blkid_probe_set_block_size(pr, bs);

	return 0;
}


const struct blkid_idinfo udf_idinfo =
{
	.name		= "udf",
	.usage		= BLKID_USAGE_FILESYSTEM,
	.probefunc	= probe_udf,
	.flags		= BLKID_IDINFO_TOLERANT,
	.magics		=
	{
		{ .magic = "BEA01", .len = 5, .kboff = 32, .sboff = 1 },
		{ .magic = "BOOT2", .len = 5, .kboff = 32, .sboff = 1 },
		{ .magic = "CD001", .len = 5, .kboff = 32, .sboff = 1 },
		{ .magic = "CDW02", .len = 5, .kboff = 32, .sboff = 1 },
		{ .magic = "NSR02", .len = 5, .kboff = 32, .sboff = 1 },
		{ .magic = "NSR03", .len = 5, .kboff = 32, .sboff = 1 },
		{ .magic = "TEA01", .len = 5, .kboff = 32, .sboff = 1 },
		{ NULL }
	}
};
