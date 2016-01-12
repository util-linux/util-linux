/*
 * Copyright (C) 1999 by Andries Brouwer
 * Copyright (C) 1999, 2000, 2003 by Theodore Ts'o
 * Copyright (C) 2001 by Andreas Dilger
 * Copyright (C) 2004 Kay Sievers <kay.sievers@vrfy.org>
 * Copyright (C) 2008 Karel Zak <kzak@redhat.com>
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
#include "iso9660.h"

struct dstring128 {
	uint8_t	clen;
	uint8_t	c[127];
} __attribute__((packed));

struct dstring32 {
	uint8_t	clen;
	uint8_t	c[31];
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
		} __attribute__((packed)) logical;
	} __attribute__((packed)) type;

} __attribute__((packed));

struct volume_structure_descriptor {
	uint8_t		type;
	uint8_t		id[5];
	uint8_t		version;
} __attribute__((packed));

#define UDF_VSD_OFFSET			0x8000LL

static inline int gen_uuid_from_volset_id(unsigned char uuid[17], struct dstring128 *volset_id)
{
	size_t i;
	size_t len;
	size_t binpos;
	unsigned char buf[128];

	if (volset_id->clen == 8)
		memcpy(buf, volset_id->c, 16);
	else if (volset_id->clen == 16)
		blkid_encode_to_utf8(BLKID_ENC_UTF16BE, buf, sizeof(buf), volset_id->c, 127);
	else
		return -1;

	buf[16] = 0;
	len = strlen((char *) buf);

	if (len < 8)
		return -1;

	for (i = len; i < 16; ++i)
		buf[i] = 0;

	binpos = 16;
	for (i = 0; i < len; ++i) {
		if (!isalnum(buf[i])) {
			binpos = i;
			break;
		}
	}

	if (binpos < 8) {
		for (i = 0; i < 8; ++i)
			snprintf((char *) uuid + 2 * i, 3, "%02x", buf[i]);
	} else if (binpos < 16) {
		memcpy(uuid, buf, 8);
		for (i = 0; i < 4; ++i)
			snprintf((char *) uuid + 8 + 2 * i, 3, "%02x", buf[8+i]);
	} else {
		memcpy(uuid, buf, 16);
		uuid[16] = 0;
	}

	return 0;
}

static int probe_udf(blkid_probe pr,
		const struct blkid_idmag *mag __attribute__((__unused__)))
{
	struct volume_descriptor *vd;
	struct volume_structure_descriptor *vsd;
	unsigned int bs;
	unsigned int pbs[2];
	unsigned int b;
	unsigned int type;
	unsigned int count;
	unsigned int loc;
	unsigned int i;
	int have_label = 0;
	int have_uuid = 0;
	int have_logvolid = 0;
	int have_volid = 0;
	int have_volsetid = 0;

	/* The block size of a UDF filesystem is that of the underlying
	 * storage; we check later on for the special case of image files,
	 * which may have the 2048-byte block size of optical media. */
	pbs[0] = blkid_probe_get_sectorsize(pr);
	pbs[1] = 0x800;

	/* check for a Volume Structure Descriptor (VSD); each is
	 * 2048 bytes long */
	for (b = 0; b < 0x8000; b += 0x800) {
		vsd = (struct volume_structure_descriptor *)
			blkid_probe_get_buffer(pr,
					UDF_VSD_OFFSET + b,
					sizeof(*vsd));
		if (!vsd)
			return errno ? -errno : 1;
		if (vsd->id[0] != '\0')
			goto nsr;
	}
	return 1;

nsr:
	/* search the list of VSDs for a NSR descriptor */
	for (b = 0; b < 64; b++) {
		vsd = (struct volume_structure_descriptor *)
			blkid_probe_get_buffer(pr,
					UDF_VSD_OFFSET + ((uint64_t) b * 0x800),
					sizeof(*vsd));
		if (!vsd)
			return errno ? -errno : 1;
		if (vsd->id[0] == '\0')
			return 1;
		if (memcmp(vsd->id, "NSR02", 5) == 0)
			goto anchor;
		if (memcmp(vsd->id, "NSR03", 5) == 0)
			goto anchor;
	}
	return 1;

anchor:
	/* read Anchor Volume Descriptor (AVDP), checking block size */
	for (i = 0; i < 2; i++) {
		vd = (struct volume_descriptor *)
			blkid_probe_get_buffer(pr, 256 * pbs[i], sizeof(*vd));
		if (!vd)
			return errno ? -errno : 1;

		type = le16_to_cpu(vd->tag.id);
		if (type == 2) /* TAG_ID_AVDP */
			goto real_blksz;
	}
	return 0;

real_blksz:
	/* Use the actual block size from here on out */
	bs = pbs[i];

	/* get descriptor list address and block count */
	count = le32_to_cpu(vd->type.anchor.length) / bs;
	loc = le32_to_cpu(vd->type.anchor.location);

	/* check if the list is usable */
	for (b = 0; b < count; b++) {
		vd = (struct volume_descriptor *)
			blkid_probe_get_buffer(pr,
					(uint64_t) (loc + b) * bs,
					sizeof(*vd));
		if (!vd)
			return errno ? -errno : 1;
	}

	/* Try extract all possible ISO9660 information -- if there is
	 * usable LABEL and UUID in ISO header then use it, otherwise
	 * read UDF specific LABEL and UUID */
	if (probe_iso9660(pr, mag) == 0) {
		if (__blkid_probe_lookup_value(pr, "LABEL") != NULL)
			have_label = 1;
		if (__blkid_probe_lookup_value(pr, "UUID") != NULL)
			have_uuid = 1;
	}

	/* Read UDF identifiers */
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
		if (type == 1) { /* TAG_ID_PVD */
			if (!have_volid) {
				uint8_t clen = vd->type.primary.ident.clen;
				if (clen == 8)
					have_volid = !blkid_probe_set_id_label(pr, "VOLUME_ID",
							vd->type.primary.ident.c, 31);
				else if (clen == 16)
					have_volid = !blkid_probe_set_utf8_id_label(pr, "VOLUME_ID",
							vd->type.primary.ident.c, 31,
							BLKID_ENC_UTF16BE);
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
				 * The first 16 characters of VolumeSetIdentifier are used to generate UUID.
				 * If all 16 characters are alphanumeric then they are used unchanged as UUID.
				 * If one of first 8 characters (time value) is not alphanumeric then first
				 * 8 characters are encoded to their hexadecimal values in 16 characters and
				 * set as UUID. If all first 8 characters (time value) are alphanumeric but
				 * some other remaining character is not then first 8 characters are unchanged
				 * (set as first part of UUID string), next 4 characters are encoded to their
				 * hexadecimal values (in 8 characters) and set as second part of UUID string.
				 */
				unsigned char uuid[17];
				if (gen_uuid_from_volset_id(uuid, &vd->type.primary.volset_id) == 0)
					have_uuid = !blkid_probe_strncpy_uuid(pr, uuid, sizeof(uuid));
			}
			if (!have_volsetid) {
				uint8_t clen = vd->type.primary.volset_id.clen;
				if (clen == 8)
					have_volsetid = !blkid_probe_set_id_label(pr, "VOLUME_SET_ID",
							vd->type.primary.volset_id.c, 127);
				else if (clen == 16)
					have_volsetid = !blkid_probe_set_utf8_id_label(pr, "VOLUME_SET_ID",
							vd->type.primary.volset_id.c, 127,
							BLKID_ENC_UTF16BE);
			}
		} else if (type == 6) { /* TAG_ID_LVD */
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
				uint8_t clen = vd->type.logical.logvol_id.clen;
				if (clen == 8) {
					if (!have_label)
						have_label = !blkid_probe_set_label(pr,
								vd->type.logical.logvol_id.c, 127);
					if (!have_logvolid)
						have_logvolid = !blkid_probe_set_id_label(pr, "LOGICAL_VOLUME_ID",
								vd->type.logical.logvol_id.c, 127);
				} else if (clen == 16) {
					if (!have_label)
						have_label = !blkid_probe_set_utf8label(pr,
								vd->type.logical.logvol_id.c,
								127, BLKID_ENC_UTF16BE);
					if (!have_logvolid)
						have_logvolid = !blkid_probe_set_utf8_id_label(pr, "LOGICAL_VOLUME_ID",
								vd->type.logical.logvol_id.c, 127,
								BLKID_ENC_UTF16BE);
				}
			}
		}
		if (have_logvolid && have_volid && have_volsetid)
			break;
	}

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
