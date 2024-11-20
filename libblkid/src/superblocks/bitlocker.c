/*
 * Copyright (C) 2018 Karel Zak <kzak@redhat.com>
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

#define BDE_HDR_SIZE	512
#define BDE_HDR_OFFSET	0

struct bde_header_win7 {
/*   0 */ unsigned char	boot_entry_point[3];
/*   3 */ unsigned char	fs_signature[8];
/*  11 */ unsigned char	__dummy1[67 - 11];
/*  67 */ uint32_t      volume_serial;		/* NTFS uses 64bit serial number */
/*  71 */ unsigned char volume_label[11];	/* "NO NAME\x20\x20\x20\x20" only */
/*  82 */ unsigned char __dummy2[160 - 82];
/* 160 */ unsigned char guid[16];		/* BitLocker specific GUID */
/* 176 */ uint64_t      fve_metadata_offset;
} __attribute__((packed));


struct bde_header_togo {
/*   0 */ unsigned char	boot_entry_point[3];
/*   3 */ unsigned char	fs_signature[8];
/*  11 */ unsigned char	__dummy[424 - 11];
/* 424 */ unsigned char guid[16];
/* 440 */ uint64_t      fve_metadata_offset;
} __attribute__((packed));


struct bde_fve_metadata_block_header {
/*   0 */ unsigned char  signature[8];
/*   8 */ unsigned char  __dummy1[10 - 8];
/*  10 */ uint16_t       version;
/*  12 */ unsigned char  __dummy2[64 - 12];
} __attribute__((packed));

struct bde_fve_metadata_header {
/*   0 */ uint32_t      size;
/*   4 */ uint32_t      version;
/*   8 */ uint32_t      header_size;
/*  12 */ uint32_t      size_copy;
/*  16 */ unsigned char volume_identifier[16];
/*  32 */ unsigned char __dummy[48 - 32];
} __attribute__((packed));

struct bde_fve_metadata_entry {
/*   0 */ uint16_t      size;
/*   2 */ uint16_t      entry_type;
/*   4 */ uint16_t      value_type;
/*   6 */ uint16_t      version;
/*   8 */ unsigned char data[];
} __attribute__((packed));

struct bde_fve_metadata {
	struct bde_fve_metadata_block_header block_header;
	struct bde_fve_metadata_header header;
} __attribute__((packed));

enum {
	BDE_VERSION_VISTA = 0,
	BDE_VERSION_WIN7,
	BDE_VERSION_TOGO
};

#define BDE_MAGIC_VISTA		"\xeb\x52\x90-FVE-FS-"
#define BDE_MAGIC_WIN7		"\xeb\x58\x90-FVE-FS-"
#define BDE_MAGIC_TOGO		"\xeb\x58\x90MSWIN4.1"

#define BDE_MAGIC_FVE		"-FVE-FS-"

#define BDE_METADATA_ENTRY_TYPE_DESCRIPTION 0x0007
#define BDE_METADATA_VALUE_TYPE_STRING      0x0002

static int get_bitlocker_type(const unsigned char *buf)
{
	size_t i;
	static const char *const map[] = {
		[BDE_VERSION_VISTA] = BDE_MAGIC_VISTA,
		[BDE_VERSION_WIN7]  = BDE_MAGIC_WIN7,
		[BDE_VERSION_TOGO]  = BDE_MAGIC_TOGO
	};

	for (i = 0; i < ARRAY_SIZE(map); i++) {
		if (memcmp(buf, map[i], 11) == 0)
			return (int) i;
	}

	return -1;
}

/* Returns: < 0 error, 1 nothing, 0 success
 */
static int get_bitlocker_headers(blkid_probe pr,
				int *type,
				const unsigned char **buf_hdr,
				const unsigned char **buf_fve)
{

	const unsigned char *buf;
	const struct bde_fve_metadata *fve;
	uint64_t off = 0;
	int kind;

	if (buf_hdr)
		*buf_hdr = NULL;
	if (buf_fve)
		*buf_fve = NULL;
	if (type)
		*type = -1;

	buf = blkid_probe_get_buffer(pr, BDE_HDR_OFFSET, BDE_HDR_SIZE);
	if (!buf)
		return errno ? -errno : 1;

	kind = get_bitlocker_type(buf);

	/* Check BitLocker header */
	switch (kind) {
	case BDE_VERSION_WIN7:
		off = le64_to_cpu(((const struct bde_header_win7 *) buf)->fve_metadata_offset);
		break;
	case BDE_VERSION_TOGO:
		off = le64_to_cpu(((const struct bde_header_togo *) buf)->fve_metadata_offset);
		break;
	case BDE_VERSION_VISTA:
		goto done;
	default:
		goto nothing;
	}

	if (!off || off % 64)
		goto nothing;
	if (buf_hdr)
		*buf_hdr = buf;

	/* Check Bitlocker FVE metadata header */
	buf = blkid_probe_get_buffer(pr, off, sizeof(struct bde_fve_metadata));
	if (!buf)
		return errno ? -errno : 1;

	fve = (const struct bde_fve_metadata *) buf;
	if (memcmp(fve->block_header.signature, BDE_MAGIC_FVE, sizeof(fve->block_header.signature)) != 0)
		goto nothing;

	if (buf_fve) {
		buf = blkid_probe_get_buffer(pr, off,
			(uint64_t) sizeof(struct bde_fve_metadata_block_header) + le32_to_cpu(fve->header.size));
		if (!buf)
			return errno ? -errno : 1;

		*buf_fve = buf;
	}
done:
	if (type)
		*type = kind;
	return 0;
nothing:
	return 1;
}

/*
 * This is used by vFAT and NTFS prober to avoid collisions with bitlocker.
 */
int blkid_probe_is_bitlocker(blkid_probe pr)
{
	return get_bitlocker_headers(pr, NULL, NULL, NULL) == 0;
}

static int probe_bitlocker(blkid_probe pr,
		const struct blkid_idmag *mag __attribute__((__unused__)))
{
	const unsigned char *buf_fve = NULL;
	const unsigned char *buf_hdr = NULL;
	const struct bde_fve_metadata_entry *entry;
	int rc, kind;
	uint64_t off;

	rc = get_bitlocker_headers(pr, &kind, &buf_hdr, &buf_fve);
	if (rc)
		return rc;

	if (buf_fve) {
		const struct bde_fve_metadata *fve = (const struct bde_fve_metadata *) buf_fve;

		blkid_probe_sprintf_version(pr, "%d", le16_to_cpu(fve->block_header.version));

		for (off = sizeof(struct bde_fve_metadata_header);
		     off + sizeof(struct bde_fve_metadata_entry) < le32_to_cpu(fve->header.size);
		     off += le16_to_cpu(entry->size)) {
			entry = (const struct bde_fve_metadata_entry *) ((const char *) &fve->header + off);
			if (off % 2 ||
			    le16_to_cpu(entry->size) < sizeof(struct bde_fve_metadata_entry) ||
			    off + le16_to_cpu(entry->size) > le32_to_cpu(fve->header.size))
				return -1;

			if (le16_to_cpu(entry->entry_type) == BDE_METADATA_ENTRY_TYPE_DESCRIPTION &&
			    le16_to_cpu(entry->value_type) == BDE_METADATA_VALUE_TYPE_STRING) {
				blkid_probe_set_utf8label(pr,
					entry->data, le16_to_cpu(entry->size) - sizeof(struct bde_fve_metadata_entry),
					UL_ENCODE_UTF16LE);
				break;
			}
		}

		/* Microsoft GUID format, interpreted as explained by Raymond Chen:
		 * https://devblogs.microsoft.com/oldnewthing/20220928-00/?p=107221
		 */
		blkid_probe_sprintf_uuid(pr, fve->header.volume_identifier, 16,
			"%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
			fve->header.volume_identifier[3], fve->header.volume_identifier[2], /* uint32_t Data1 */
			fve->header.volume_identifier[1], fve->header.volume_identifier[0],
			fve->header.volume_identifier[5], fve->header.volume_identifier[4], /* uint16_t Data2 */
			fve->header.volume_identifier[7], fve->header.volume_identifier[6], /* uint16_t Data3 */
			fve->header.volume_identifier[8], fve->header.volume_identifier[9], /* uint8_t Data4[8] */
			fve->header.volume_identifier[10], fve->header.volume_identifier[11],
			fve->header.volume_identifier[12], fve->header.volume_identifier[13],
			fve->header.volume_identifier[14], fve->header.volume_identifier[15]);
	}
	return 0;
}

/* See header details:
 * https://github.com/libyal/libbde/blob/master/documentation/BitLocker%20Drive%20Encryption%20(BDE)%20format.asciidoc
 */
const struct blkid_idinfo bitlocker_idinfo =
{
	.name		= "BitLocker",
	.usage		= BLKID_USAGE_CRYPTO,
	.probefunc	= probe_bitlocker,
	.magics		=
	{
		{ .magic = BDE_MAGIC_VISTA, .len = 11 },
		{ .magic = BDE_MAGIC_WIN7,  .len = 11 },
		{ .magic = BDE_MAGIC_TOGO,  .len = 11 },
		{ NULL }
	}
};
