/*
 * Copyright (C) 1999 by Andries Brouwer
 * Copyright (C) 1999, 2000, 2003 by Theodore Ts'o
 * Copyright (C) 2001 by Andreas Dilger
 * Copyright (C) 2004 Kay Sievers <kay.sievers@vrfy.org>
 * Copyright (C) 2008 Karel Zak <kzak@redhat.com>
 *
 * Inspired also by libvolume_id by
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
#include <ctype.h>

#include "superblocks.h"
#include "cctype.h"

struct iso9660_date {
	unsigned char year[4];
	unsigned char month[2];
	unsigned char day[2];
	unsigned char hour[2];
	unsigned char minute[2];
	unsigned char second[2];
	unsigned char hundredth[2];
	unsigned char offset;
} __attribute__ ((packed));

/* PVD - Primary volume descriptor */
struct iso_volume_descriptor {
	unsigned char	vd_type;
	unsigned char	vd_id[5];
	unsigned char	vd_version;
	unsigned char	flags;
	unsigned char	system_id[32];
	unsigned char	volume_id[32];
	unsigned char	unused[8];
	unsigned char	space_size[8];
	unsigned char	escape_sequences[8];
	unsigned char  unused2[94];
	unsigned char  volume_set_id[128];
	unsigned char  publisher_id[128];
	unsigned char  data_preparer_id[128];
	unsigned char  application_id[128];
	unsigned char  unused3[111];
	struct iso9660_date created;
	struct iso9660_date modified;
} __attribute__((packed));

/* Boot Record */
struct boot_record {
	unsigned char	vd_type;
	unsigned char	vd_id[5];
	unsigned char	vd_version;
	unsigned char	boot_system_id[32];
	unsigned char	boot_id[32];
	unsigned char	unused[1];
} __attribute__((packed));

#define ISO_SUPERBLOCK_OFFSET		0x8000
#define ISO_SECTOR_SIZE			0x800
#define ISO_VD_BOOT_RECORD		0x0
#define ISO_VD_PRIMARY			0x1
#define ISO_VD_SUPPLEMENTARY		0x2
#define ISO_VD_END			0xff
#define ISO_VD_MAX			16

struct high_sierra_volume_descriptor {
	unsigned char	foo[8];
	unsigned char	type;
	unsigned char	id[5];
	unsigned char	version;
	unsigned char	unused1;
	unsigned char	system_id[32];
	unsigned char   volume_id[32];
} __attribute__((packed));

/* old High Sierra format */
static int probe_iso9660_hsfs(blkid_probe pr, const struct blkid_idmag *mag)
{
	struct high_sierra_volume_descriptor *iso;

	iso = blkid_probe_get_sb(pr, mag, struct high_sierra_volume_descriptor);
	if (!iso)
		return errno ? -errno : 1;

	blkid_probe_set_block_size(pr, ISO_SECTOR_SIZE);
	blkid_probe_set_version(pr, "High Sierra");
	blkid_probe_set_label(pr, iso->volume_id, sizeof(iso->volume_id));
	return 0;
}

static int probe_iso9660_set_uuid (blkid_probe pr, const struct iso9660_date *date)
{
	unsigned char buffer[16];
	unsigned int i, zeros = 0;

	buffer[0] = date->year[0];
	buffer[1] = date->year[1];
	buffer[2] = date->year[2];
	buffer[3] = date->year[3];
	buffer[4] = date->month[0];
	buffer[5] = date->month[1];
	buffer[6] = date->day[0];
	buffer[7] = date->day[1];
	buffer[8] = date->hour[0];
	buffer[9] = date->hour[1];
	buffer[10] = date->minute[0];
	buffer[11] = date->minute[1];
	buffer[12] = date->second[0];
	buffer[13] = date->second[1];
	buffer[14] = date->hundredth[0];
	buffer[15] = date->hundredth[1];

	/* count the number of zeros ('0') in the date buffer */
	for (i = 0, zeros = 0; i < sizeof(buffer); i++)
		if (buffer[i] == '0')
			zeros++;

	/* due to the iso9660 standard if all date fields are '0' and offset is 0, the date is unset */
	if (zeros == sizeof(buffer) && date->offset == 0)
		return 0;

	/* generate an UUID using this date and return success */
	blkid_probe_sprintf_uuid (pr, buffer, sizeof(buffer),
		"%c%c%c%c-%c%c-%c%c-%c%c-%c%c-%c%c-%c%c",
		buffer[0], buffer[1], buffer[2], buffer[3],
		buffer[4], buffer[5],
		buffer[6], buffer[7],
		buffer[8], buffer[9],
		buffer[10], buffer[11],
		buffer[12], buffer[13],
		buffer[14], buffer[15]);

	return 1;
}

static int is_str_empty(const unsigned char *str, size_t len)
{
	size_t i;

	if (!str || !*str)
		return 1;

	for (i = 0; i < len; i++)
		if (!isspace(str[i]))
			return 0;
	return 1;
}

static int is_utf16be_str_empty(unsigned char *utf16, size_t len)
{
	size_t i;

	for (i = 0; i < len; i += 2) {
		if (utf16[i] != 0x0 || !isspace(utf16[i + 1]))
			return 0;
	}
	return 1;
}

/* if @utf16 is prefix of @ascii (ignoring non-representable characters and upper-case conversion)
 * then reconstruct prefix from @utf16 and @ascii, append suffix from @ascii, fill it into @out
 * and returns length of bytes written into @out; otherwise returns zero */
static size_t merge_utf16be_ascii(unsigned char *out, const unsigned char *utf16, const unsigned char *ascii, size_t len)
{
	size_t o, a, u;

	for (o = 0, a = 0, u = 0; u + 1 < len && a < len; o += 2, a++, u += 2) {
		/* Surrogate pair with code point above U+FFFF */
		if (utf16[u] >= 0xD8 && utf16[u] <= 0xDB && u + 3 < len &&
		    utf16[u + 2] >= 0xDC && utf16[u + 2] <= 0xDF) {
			out[o++] = utf16[u++];
			out[o++] = utf16[u++];
		}
		/* Value '_' is replacement for non-representable character */
		if (ascii[a] == '_') {
			out[o] = utf16[u];
			out[o + 1] = utf16[u + 1];
		} else if (utf16[u] == 0x00 && utf16[u + 1] == '_') {
			out[o] = 0x00;
			out[o + 1] = ascii[a];
		} else if (utf16[u] == 0x00 && c_toupper(ascii[a]) == c_toupper(utf16[u + 1])) {
			out[o] = 0x00;
			out[o + 1] = c_isupper(ascii[a]) ? utf16[u + 1] : ascii[a];
		} else {
			return 0;
		}
	}

	for (; a < len; o += 2, a++) {
		out[o] = 0x00;
		out[o + 1] = ascii[a];
	}

	return o;
}

/* iso9660 [+ Microsoft Joliet Extension] */
static int probe_iso9660(blkid_probe pr, const struct blkid_idmag *mag)
{
	struct boot_record *boot = NULL;
	struct iso_volume_descriptor *pvd = NULL;
	struct iso_volume_descriptor *joliet = NULL;
	unsigned char buf[256];
	size_t len;
	int is_unicode_empty;
	int is_ascii_empty;
	int i;
	uint64_t off;

	if (blkid_probe_get_hint(pr, mag->hoff, &off) < 0)
		off = 0;

	if (off % ISO_SECTOR_SIZE)
		return 1;

	if (strcmp(mag->magic, "CDROM") == 0)
		return probe_iso9660_hsfs(pr, mag);

	for (i = 0, off += ISO_SUPERBLOCK_OFFSET; i < ISO_VD_MAX && (!boot || !pvd || !joliet); i++, off += ISO_SECTOR_SIZE) {
		unsigned char *desc =
			blkid_probe_get_buffer(pr,
					off,
					max(sizeof(struct boot_record),
					    sizeof(struct iso_volume_descriptor)));

		if (desc == NULL || desc[0] == ISO_VD_END)
			break;
		else if (!boot && desc[0] == ISO_VD_BOOT_RECORD)
			boot = (struct boot_record *)desc;
		else if (!pvd && desc[0] == ISO_VD_PRIMARY)
			pvd = (struct iso_volume_descriptor *)desc;
		else if (!joliet && desc[0] == ISO_VD_SUPPLEMENTARY) {
			joliet = (struct iso_volume_descriptor *)desc;
			if (memcmp(joliet->escape_sequences, "%/@", 3) != 0 &&
			    memcmp(joliet->escape_sequences, "%/C", 3) != 0 &&
			    memcmp(joliet->escape_sequences, "%/E", 3) != 0)
				joliet = NULL;
		}
	}

	if (!pvd)
		return errno ? -errno : 1;

	blkid_probe_set_block_size(pr, ISO_SECTOR_SIZE);

	if (joliet && (len = merge_utf16be_ascii(buf, joliet->system_id, pvd->system_id, sizeof(pvd->system_id))) != 0)
		blkid_probe_set_utf8_id_label(pr, "SYSTEM_ID", buf, len, UL_ENCODE_UTF16BE);
	else if (joliet)
		blkid_probe_set_utf8_id_label(pr, "SYSTEM_ID", joliet->system_id, sizeof(joliet->system_id), UL_ENCODE_UTF16BE);
	else
		blkid_probe_set_id_label(pr, "SYSTEM_ID", pvd->system_id, sizeof(pvd->system_id));

	if (joliet && (len = merge_utf16be_ascii(buf, joliet->volume_set_id, pvd->volume_set_id, sizeof(pvd->volume_set_id))) != 0)
		blkid_probe_set_utf8_id_label(pr, "VOLUME_SET_ID", buf, len, UL_ENCODE_UTF16BE);
	else if (joliet)
		blkid_probe_set_utf8_id_label(pr, "VOLUME_SET_ID", joliet->volume_set_id, sizeof(joliet->volume_set_id), UL_ENCODE_UTF16BE);
	else
		blkid_probe_set_id_label(pr, "VOLUME_SET_ID", pvd->volume_set_id, sizeof(pvd->volume_set_id));

	is_ascii_empty = (is_str_empty(pvd->publisher_id, sizeof(pvd->publisher_id)) || pvd->publisher_id[0] == '_');
	is_unicode_empty = (!joliet || is_utf16be_str_empty(joliet->publisher_id, sizeof(joliet->publisher_id)) || (joliet->publisher_id[0] == 0x00 && joliet->publisher_id[1] == '_'));
	if (!is_unicode_empty && !is_ascii_empty && (len = merge_utf16be_ascii(buf, joliet->publisher_id, pvd->publisher_id, sizeof(pvd->publisher_id))) != 0)
		blkid_probe_set_utf8_id_label(pr, "PUBLISHER_ID", buf, len, UL_ENCODE_UTF16BE);
	else if (!is_unicode_empty)
		blkid_probe_set_utf8_id_label(pr, "PUBLISHER_ID", joliet->publisher_id, sizeof(joliet->publisher_id), UL_ENCODE_UTF16BE);
	else if (!is_ascii_empty)
		blkid_probe_set_id_label(pr, "PUBLISHER_ID", pvd->publisher_id, sizeof(pvd->publisher_id));

	is_ascii_empty = (is_str_empty(pvd->data_preparer_id, sizeof(pvd->data_preparer_id)) || pvd->data_preparer_id[0] == '_');
	is_unicode_empty = (!joliet || is_utf16be_str_empty(joliet->data_preparer_id, sizeof(joliet->data_preparer_id)) || (joliet->data_preparer_id[0] == 0x00 && joliet->data_preparer_id[1] == '_'));
	if (!is_unicode_empty && !is_ascii_empty && (len = merge_utf16be_ascii(buf, joliet->data_preparer_id, pvd->data_preparer_id, sizeof(pvd->data_preparer_id))) != 0)
		blkid_probe_set_utf8_id_label(pr, "DATA_PREPARER_ID", buf, len, UL_ENCODE_UTF16BE);
	else if (!is_unicode_empty)
		blkid_probe_set_utf8_id_label(pr, "DATA_PREPARER_ID", joliet->data_preparer_id, sizeof(joliet->data_preparer_id), UL_ENCODE_UTF16BE);
	else if (!is_ascii_empty)
		blkid_probe_set_id_label(pr, "DATA_PREPARER_ID", pvd->data_preparer_id, sizeof(pvd->data_preparer_id));

	is_ascii_empty = (is_str_empty(pvd->application_id, sizeof(pvd->application_id)) || pvd->application_id[0] == '_');
	is_unicode_empty = (!joliet || is_utf16be_str_empty(joliet->application_id, sizeof(joliet->application_id)) || (joliet->application_id[0] == 0x00 && joliet->application_id[1] == '_'));
	if (!is_unicode_empty && !is_ascii_empty && (len = merge_utf16be_ascii(buf, joliet->application_id, pvd->application_id, sizeof(pvd->application_id))) != 0)
		blkid_probe_set_utf8_id_label(pr, "APPLICATION_ID", buf, len, UL_ENCODE_UTF16BE);
	else if (!is_unicode_empty)
		blkid_probe_set_utf8_id_label(pr, "APPLICATION_ID", joliet->application_id, sizeof(joliet->application_id), UL_ENCODE_UTF16BE);
	else if (!is_ascii_empty)
		blkid_probe_set_id_label(pr, "APPLICATION_ID", pvd->application_id, sizeof(pvd->application_id));

	/* create an UUID using the modified/created date */
	if (! probe_iso9660_set_uuid(pr, &pvd->modified))
		probe_iso9660_set_uuid(pr, &pvd->created);

	if (boot)
		blkid_probe_set_id_label(pr, "BOOT_SYSTEM_ID",
					boot->boot_system_id,
					sizeof(boot->boot_system_id));

	if (joliet)
		blkid_probe_set_version(pr, "Joliet Extension");

	/* Label in Joliet is UNICODE (UTF16BE) but can contain only 16 characters. Label in PVD is
	 * subset of ASCII but can contain up to the 32 characters. Non-representable characters are
	 * stored as replacement character '_'. Label in Joliet is in most cases trimmed but UNICODE
	 * version of label in PVD. Based on these facts try to reconstruct original label if label
	 * in Joliet is prefix of the label in PVD (ignoring non-representable characters).
	 */
	if (joliet && (len = merge_utf16be_ascii(buf, joliet->volume_id, pvd->volume_id, sizeof(pvd->volume_id))) != 0)
		blkid_probe_set_utf8label(pr, buf, len, UL_ENCODE_UTF16BE);
	else if (joliet)
		blkid_probe_set_utf8label(pr, joliet->volume_id, sizeof(joliet->volume_id), UL_ENCODE_UTF16BE);
	else
		blkid_probe_set_label(pr, pvd->volume_id, sizeof(pvd->volume_id));

	return 0;
}

const struct blkid_idinfo iso9660_idinfo =
{
	.name		= "iso9660",
	.usage		= BLKID_USAGE_FILESYSTEM,
	.probefunc	= probe_iso9660,
	.flags		= BLKID_IDINFO_TOLERANT,
	.magics		=
	{
		{ .magic = "CD001", .len = 5, .kboff = 32, .sboff = 1, .hoff = "session_offset" },
		{ .magic = "CDROM", .len = 5, .kboff = 32, .sboff = 9, .hoff = "session_offset" },
		{ NULL }
	}
};

