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
	unsigned char  unused1[222];
	unsigned char  publisher_id[128];
	unsigned char  unused2[128];
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
#define ISO_VD_OFFSET			(ISO_SUPERBLOCK_OFFSET + ISO_SECTOR_SIZE)
#define ISO_VD_BOOT_RECORD		0x0
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

/* returns 1 if the begin of @ascii is equal to @utf16 string.
 */
static int ascii_eq_utf16be(unsigned char *ascii,
			unsigned char *utf16, size_t len)
{
	size_t a, u;

	for (a = 0, u = 0; u < len; a++, u += 2) {
		if (utf16[u] != 0x0 || ascii[a] != utf16[u + 1])
			return 0;
	}
	return 1;
}

/* old High Sierra format */
static int probe_iso9660_hsfs(blkid_probe pr, const struct blkid_idmag *mag)
{
	struct high_sierra_volume_descriptor *iso;

	iso = blkid_probe_get_sb(pr, mag, struct high_sierra_volume_descriptor);
	if (!iso)
		return errno ? -errno : 1;

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

/* iso9660 [+ Microsoft Joliet Extension] */
static int probe_iso9660(blkid_probe pr, const struct blkid_idmag *mag)
{
	struct iso_volume_descriptor *iso;
	unsigned char label[32];
	int i;
	int off;

	if (strcmp(mag->magic, "CDROM") == 0)
		return probe_iso9660_hsfs(pr, mag);

	iso = blkid_probe_get_sb(pr, mag, struct iso_volume_descriptor);
	if (!iso)
		return errno ? -errno : 1;

	memcpy(label, iso->volume_id, sizeof(label));

	if (!is_str_empty(iso->system_id, sizeof(iso->system_id)))
		blkid_probe_set_id_label(pr, "SYSTEM_ID",
				iso->system_id, sizeof(iso->system_id));

	if (!is_str_empty(iso->publisher_id, sizeof(iso->publisher_id)))
		blkid_probe_set_id_label(pr, "PUBLISHER_ID",
				iso->publisher_id, sizeof(iso->publisher_id));

	if (!is_str_empty(iso->application_id, sizeof(iso->application_id)))
		blkid_probe_set_id_label(pr, "APPLICATION_ID",
				iso->application_id, sizeof(iso->application_id));

	/* create an UUID using the modified/created date */
	if (! probe_iso9660_set_uuid(pr, &iso->modified))
		probe_iso9660_set_uuid(pr, &iso->created);

	/* Joliet Extension and Boot Record */
	off = ISO_VD_OFFSET;
	for (i = 0; i < ISO_VD_MAX; i++) {
		struct boot_record *boot= (struct boot_record *)
			blkid_probe_get_buffer(pr,
					off,
					max(sizeof(struct boot_record),
					    sizeof(struct iso_volume_descriptor)));

		if (boot == NULL || boot->vd_type == ISO_VD_END)
			break;

		if (boot->vd_type == ISO_VD_BOOT_RECORD) {
			if (!is_str_empty(boot->boot_system_id,
					  sizeof(boot->boot_system_id)))
				blkid_probe_set_id_label(pr, "BOOT_SYSTEM_ID",
							boot->boot_system_id,
							sizeof(boot->boot_system_id));
			off += ISO_SECTOR_SIZE;
			continue;
		}

		/* Not a Boot record, lets see if its supplementary volume descriptor */
		iso = (struct iso_volume_descriptor *) boot;

		if (iso->vd_type != ISO_VD_SUPPLEMENTARY) {
			off += ISO_SECTOR_SIZE;
			continue;
		}

		if (memcmp(iso->escape_sequences, "%/@", 3) == 0 ||
		    memcmp(iso->escape_sequences, "%/C", 3) == 0 ||
		    memcmp(iso->escape_sequences, "%/E", 3) == 0) {

			blkid_probe_set_version(pr, "Joliet Extension");

			/* Is the Joliet (UTF16BE) label equal to the label in
			 * the PVD? If yes, use PVD label.  The Joliet version
			 * of the label could be trimmed (because UTF16..).
			 */
			if (ascii_eq_utf16be(label, iso->volume_id, 32))
				break;

			blkid_probe_set_utf8label(pr,
					iso->volume_id,
					sizeof(iso->volume_id),
					BLKID_ENC_UTF16BE);
			goto has_label;
		}
		off += ISO_SECTOR_SIZE;
	}

	/* Joliet not found, let use standard iso label */
	blkid_probe_set_label(pr, label, sizeof(label));

has_label:
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
		{ .magic = "CD001", .len = 5, .kboff = 32, .sboff = 1 },
		{ .magic = "CDROM", .len = 5, .kboff = 32, .sboff = 9 },
		{ NULL }
	}
};

