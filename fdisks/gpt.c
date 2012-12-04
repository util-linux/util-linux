/*
 * Copyright (C) 2007 Karel Zak <kzak@redhat.com>
 * Copyright (C) 2012 Davidlohr Bueso <dave@gnu.org>
 *
 * GUID Partition Table (GPT) support. Based on UEFI Specs 2.3.1
 * Chapter 5: GUID Partition Table (GPT) Disk Layout (Jun 27th, 2012).
 * Some ideas and inspiration from GNU parted and gptfdisk.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <uuid.h>

#include "nls.h"
#include "xalloc.h"
#include "common.h"
#include "fdisk.h"
#include "crc32.h"
#include "gpt.h"
#include "blkdev.h"
#include "bitops.h"
#include "strutils.h"
#include "all-io.h"

#define GPT_HEADER_SIGNATURE 0x5452415020494645LL /* EFI PART */
#define GPT_HEADER_REVISION_V1_02 0x00010200
#define GPT_HEADER_REVISION_V1_00 0x00010000
#define GPT_HEADER_REVISION_V0_99 0x00009900
#define GPT_HEADER_MINSZ          92 /* bytes */

#define GPT_PMBR_LBA        0
#define GPT_MBR_PROTECTIVE  1
#define GPT_MBR_HYBRID      2

#define GPT_PRIMARY_PARTITION_TABLE_LBA 0x00000001

#define EFI_PMBR_OSTYPE     0xEE
#define MSDOS_MBR_SIGNATURE 0xAA55
#define GPT_PART_NAME_LEN   72 / sizeof(uint16_t)
#define GPT_NPARTITIONS     128

/* Globally unique identifier */
struct gpt_guid {
	uint32_t   time_low;
	uint16_t   time_mid;
	uint16_t   time_hi_and_version;
	uint8_t    clock_seq_hi;
	uint8_t    clock_seq_low;
	uint8_t    node[6];
};


/* only checking that the GUID is 0 is enough to verify an empty partition. */
#define GPT_UNUSED_ENTRY_GUID						\
	((struct gpt_guid) { 0x00000000, 0x0000, 0x0000, 0x00, 0x00,	\
			     { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }})

/* Linux native partition type */
#define GPT_DEFAULT_ENTRY_GUID						\
	((struct gpt_guid) { 0x0FC63DAF, 0x8483, 0x4772, 0x8E, 0x79,	\
			     { 0x3D, 0x69, 0xD8, 0x47, 0x7D, 0xE4 }})


/*
 * Attribute bits
 */
struct gpt_attr {
	uint64_t            required_to_function:1;
	uint64_t            no_blockio_protocol:1;
	uint64_t            legacy_bios_bootable:1;
	uint64_t            reserved:45;
	uint64_t            guid_secific:16;
}  __attribute__ ((packed));

/* The GPT Partition entry array contains an array of GPT entries. */
struct gpt_entry {
	struct gpt_guid     partition_type_guid; /* purpose and type of the partition */
	struct gpt_guid     unique_partition_guid;
	uint64_t            lba_start;
	uint64_t            lba_end;
	struct gpt_attr     attr;
	uint16_t            partition_name[GPT_PART_NAME_LEN];
}  __attribute__ ((packed));

/* GPT header */
struct gpt_header {
	uint64_t            signature; /* header identification */
	uint32_t            revision; /* header version */
	uint32_t            size; /* in bytes */
	uint32_t            crc32; /* header CRC checksum */
	uint32_t            reserved1; /* must be 0 */
	uint64_t            my_lba; /* LBA that contains this struct (LBA 1) */
	uint64_t            alternative_lba; /* backup GPT header */
	uint64_t            first_usable_lba; /* first usable logical block for partitions */
	uint64_t            last_usable_lba; /* last usable logical block for partitions */
	struct gpt_guid     disk_guid; /* unique disk identifier */
	uint64_t            partition_entry_lba; /* stat LBA of the partition entry array */
	uint32_t            npartition_entries; /* total partition entries - normally 128 */
	uint32_t            sizeof_partition_entry; /* bytes for each GUID pt */
	uint32_t            partition_entry_array_crc32; /* partition CRC checksum */
	uint8_t             reserved2[512 - 92]; /* must be 0 */
} __attribute__ ((packed));

struct gpt_record {
	uint8_t             boot_indicator; /* unused by EFI, set to 0x80 for bootable */
	uint8_t             start_head; /* unused by EFI, pt start in CHS */
	uint8_t             start_sector; /* unused by EFI, pt start in CHS */
	uint8_t             start_track;
	uint8_t             os_type; /* EFI and legacy non-EFI OS types */
	uint8_t             end_head; /* unused by EFI, pt end in CHS */
	uint8_t             end_sector; /* unused by EFI, pt end in CHS */
	uint8_t             end_track; /* unused by EFI, pt end in CHS */
	uint32_t            starting_lba; /* used by EFI - start addr of the on disk pt */
	uint32_t            size_in_lba; /* used by EFI - size of pt in LBA */
} __attribute__ ((packed));

/* Protected MBR and legacy MBR share same structure */
struct gpt_legacy_mbr {
	uint8_t             boot_code[440];
	uint32_t            unique_mbr_signature;
	uint16_t            unknown;
	struct gpt_record   partition_record[4];
	uint16_t            signature;
} __attribute__ ((packed));

/*
 * Here be dragons!
 * See: http://en.wikipedia.org/wiki/GUID_Partition_Table#Partition_type_GUIDs
 */
#define DEF_GUID(_u, _n) \
	{ \
		.typestr = (_u), \
		.name = (_n),    \
	}

static struct fdisk_parttype gpt_parttypes[] =
{
	/* Generic OS */
	DEF_GUID("C12A7328-F81F-11D2-BA4B-00A0C93EC93B", N_("EFI System")),

	DEF_GUID("024DEE41-33E7-11D3-9D69-0008C781F39F", N_("MBR partition scheme")),
	/* Hah!IdontneedEFI */
	DEF_GUID("21686148-6449-6E6F-744E-656564454649", N_("BIOS boot partition")),

	/* Windows */
	DEF_GUID("E3C9E316-0B5C-4DB8-817D-F92DF00215AE", N_("Microsoft reserved")),
	DEF_GUID("EBD0A0A2-B9E5-4433-87C0-68B6B72699C7", N_("Microsoft basic data")),
	DEF_GUID("5808C8AA-7E8F-42E0-85D2-E1E90434CFB3", N_("Microsoft LDM metadata")),
	DEF_GUID("AF9B60A0-1431-4F62-BC68-3311714A69AD", N_("Microsoft LDM data")),
	DEF_GUID("DE94BBA4-06D1-4D40-A16A-BFD50179D6AC", N_("Windows recovery evironmnet")),
	DEF_GUID("37AFFC90-EF7D-4E96-91C3-2D7AE055B174", N_("IBM General Parallel Fs")),

	/* HP-UX */
	DEF_GUID("75894C1E-3AEB-11D3-B7C1-7B03A0000000", N_("HP-UX data partition")),
	DEF_GUID("E2A1E728-32E3-11D6-A682-7B03A0000000", N_("HP-UX service partition")),

	/* Linux */
	DEF_GUID("0FC63DAF-8483-4772-8E79-3D69D8477DE4", N_("Linux filesystem")),
	DEF_GUID("A19D880F-05FC-4D3B-A006-743F0F84911E", N_("Linux RAID")),
	DEF_GUID("0657FD6D-A4AB-43C4-84E5-0933C84B4F4F", N_("Linux swap")),
	DEF_GUID("E6D6D379-F507-44C2-A23C-238F2A3DF928", N_("Linux LVM")),
	DEF_GUID("8DA63339-0007-60C0-C436-083AC8230908", N_("Linux reserved")),

	/* FreeBSD */
	DEF_GUID("516E7CB4-6ECF-11D6-8FF8-00022D09712B", N_("FreeBSD data")),
	DEF_GUID("83BD6B9D-7F41-11DC-BE0B-001560B84F0F", N_("FreeBSD boot")),
	DEF_GUID("516E7CB5-6ECF-11D6-8FF8-00022D09712B", N_("FreeBSD swap")),
	DEF_GUID("516E7CB6-6ECF-11D6-8FF8-00022D09712B", N_("FreeBSD UFS")),
	DEF_GUID("516E7CBA-6ECF-11D6-8FF8-00022D09712B", N_("FreeBSD ZFS")),
	DEF_GUID("516E7CB8-6ECF-11D6-8FF8-00022D09712B", N_("FreeBSD Vinum")),

	/* Apple OSX */
	DEF_GUID("48465300-0000-11AA-AA11-00306543ECAC", N_("Apple HFS/HFS+")),
	DEF_GUID("55465300-0000-11AA-AA11-00306543ECAC", N_("Apple UFS")),
	DEF_GUID("52414944-0000-11AA-AA11-00306543ECAC", N_("Apple RAID")),
	DEF_GUID("52414944-5F4F-11AA-AA11-00306543ECAC", N_("Apple RAID offline")),
	DEF_GUID("426F6F74-0000-11AA-AA11-00306543ECAC", N_("Apple boot")),
	DEF_GUID("4C616265-6C00-11AA-AA11-00306543ECAC", N_("Apple label")),
	DEF_GUID("5265636F-7665-11AA-AA11-00306543ECAC", N_("Apple TV recovery")),
	DEF_GUID("53746F72-6167-11AA-AA11-00306543ECAC", N_("Apple Core storage")),

	/* Solaris */
	DEF_GUID("6A82CB45-1DD2-11B2-99A6-080020736631", N_("Solaris boot")),
	DEF_GUID("6A85CF4D-1DD2-11B2-99A6-080020736631", N_("Solaris root")),
	/* same as Apple ZFS */
	DEF_GUID("6A898CC3-1DD2-11B2-99A6-080020736631", N_("Solaris /usr & Apple ZFS")),
	DEF_GUID("6A87C46F-1DD2-11B2-99A6-080020736631", N_("Solaris swap")),
	DEF_GUID("6A8B642B-1DD2-11B2-99A6-080020736631", N_("Solaris backup")),
	DEF_GUID("6A8EF2E9-1DD2-11B2-99A6-080020736631", N_("Solaris /var")),
	DEF_GUID("6A90BA39-1DD2-11B2-99A6-080020736631", N_("Solaris /home")),
	DEF_GUID("6A9283A5-1DD2-11B2-99A6-080020736631", N_("Solaris alternate sector")),
	DEF_GUID("6A945A3B-1DD2-11B2-99A6-080020736631", N_("Solaris reserved 1")),
	DEF_GUID("6A9630D1-1DD2-11B2-99A6-080020736631", N_("Solaris reserved 2")),
	DEF_GUID("6A980767-1DD2-11B2-99A6-080020736631", N_("Solaris reserved 3")),
	DEF_GUID("6A96237F-1DD2-11B2-99A6-080020736631", N_("Solaris reserved 4")),
	DEF_GUID("6A8D2AC7-1DD2-11B2-99A6-080020736631", N_("Solaris reserved 5")),

	/* NetBSD */
	DEF_GUID("49F48D32-B10E-11DC-B99B-0019D1879648", N_("NetBSD swap")),
	DEF_GUID("49F48D5A-B10E-11DC-B99B-0019D1879648", N_("NetBSD FFS")),
	DEF_GUID("49F48D82-B10E-11DC-B99B-0019D1879648", N_("NetBSD LFS")),
	DEF_GUID("2DB519C4-B10E-11DC-B99B-0019D1879648", N_("NetBSD concatenated")),
	DEF_GUID("2DB519EC-B10E-11DC-B99B-0019D1879648", N_("NetBSD encrypted")),
	DEF_GUID("49F48DAA-B10E-11DC-B99B-0019D1879648", N_("NetBSD RAID")),

	/* ChromeOS */
	DEF_GUID("FE3A2A5D-4F32-41A7-B725-ACCC3285A309", N_("ChromeOS kernel")),
	DEF_GUID("3CB8E202-3B7E-47DD-8A3C-7FF2A13CFCEC", N_("ChromeOS root fs")),
	DEF_GUID("2E0A753D-9E48-43B0-8337-B15192CB1B5E", N_("ChromeOS reserved")),

	/* MidnightBSD */
	DEF_GUID("85D5E45A-237C-11E1-B4B3-E89A8F7FC3A7", N_("MidnightBSD data")),
	DEF_GUID("85D5E45E-237C-11E1-B4B3-E89A8F7FC3A7", N_("MidnightBSD boot")),
	DEF_GUID("85D5E45B-237C-11E1-B4B3-E89A8F7FC3A7", N_("MidnightBSD swap")),
	DEF_GUID("0394Ef8B-237C-11E1-B4B3-E89A8F7FC3A7", N_("MidnightBSD UFS")),
	DEF_GUID("85D5E45D-237C-11E1-B4B3-E89A8F7FC3A7", N_("MidnightBSD ZFS")),
	DEF_GUID("85D5E45C-237C-11E1-B4B3-E89A8F7FC3A7", N_("MidnightBSD Vinum")),
};

/* primary GPT header */
static struct gpt_header *pheader = NULL;
/* backup GPT header */
static struct gpt_header *bheader = NULL;
/* partition entry array */
static struct gpt_entry *ents = NULL;

#define gpt_partition_start(_e)		le64_to_cpu((_e)->lba_start)
#define gpt_partition_end(_e)		le64_to_cpu((_e)->lba_end)

/*
 * Returns the partition length, or 0 if end is before beginning.
 */
static uint64_t gpt_partition_size(const struct gpt_entry *e)
{
	uint64_t start = gpt_partition_start(e);
	uint64_t end = gpt_partition_end(e);

	return start > end ? 0 : end - start + 1ULL;
}

/*
 * UUID is traditionally 16 byte big-endian array, except Intel EFI
 * specification where the UUID is a structure of little-endian fields.
 */
static void swap_efi_guid(struct gpt_guid *uid)
{
	uid->time_low = swab32(uid->time_low);
	uid->time_mid = swab16(uid->time_mid);
	uid->time_hi_and_version = swab16(uid->time_hi_and_version);
}

static int string_to_uuid(const char *in, struct gpt_guid *uuid)
{
	if (uuid_parse(in, (unsigned char *) uuid))
		return -1;

	swap_efi_guid(uuid);
	return 0;
}

static void uuid_to_string(struct gpt_guid *uuid, char *out)
{
	uuid_unparse_upper((unsigned char *) uuid, out);
}

static const char *gpt_get_header_revstr(struct gpt_header *header)
{
	if (!header)
		goto unknown;

	switch (header->revision) {
	case GPT_HEADER_REVISION_V1_02:
		return "1.2";
	case GPT_HEADER_REVISION_V1_00:
		return "1.0";
	case GPT_HEADER_REVISION_V0_99:
		return "0.99";
	default:
		goto unknown;
	}

unknown:
	return "unknown";
}

static inline int partition_unused(const struct gpt_entry *e)
{
	return !memcmp(&e->partition_type_guid, &GPT_UNUSED_ENTRY_GUID,
			sizeof(struct gpt_guid));
}

/*
 * Builds a clean new valid protective MBR - will wipe out any existing data.
 * Returns 0 on success, otherwise < 0 on error.
 */
static int gpt_mknew_pmbr(struct fdisk_context *cxt)
{
	struct gpt_legacy_mbr *pmbr = NULL;

	if (!cxt || !cxt->firstsector)
		return -ENOSYS;

	fdisk_zeroize_firstsector(cxt);

	pmbr = (struct gpt_legacy_mbr *) cxt->firstsector;

	pmbr->signature = cpu_to_le16(MSDOS_MBR_SIGNATURE);
	pmbr->partition_record[0].os_type      = EFI_PMBR_OSTYPE;
	pmbr->partition_record[0].start_sector = 1;
	pmbr->partition_record[0].end_head     = 0xFE;
	pmbr->partition_record[0].end_sector   = 0xFF;
	pmbr->partition_record[0].end_track    = 0xFF;
	pmbr->partition_record[0].starting_lba = cpu_to_le32(1);
	pmbr->partition_record[0].size_in_lba  =
		cpu_to_le32(min((uint32_t) cxt->total_sectors - 1, 0xFFFFFFFF));

	return 0;
}

/* some universal differences between the headers */
static void gpt_mknew_header_common(struct fdisk_context *cxt,
				    struct gpt_header *header, uint64_t lba)
{
	if (!cxt || !header)
		return;

	header->my_lba = cpu_to_le64(lba);

	if (lba == GPT_PRIMARY_PARTITION_TABLE_LBA) { /* primary */
		header->alternative_lba = cpu_to_le64(cxt->total_sectors - 1);
		header->partition_entry_lba = cpu_to_le64(2);
	} else { /* backup */
		uint64_t esz = le32_to_cpu(header->npartition_entries) * sizeof(struct gpt_entry);
		uint64_t esects = (esz + cxt->sector_size - 1) / cxt->sector_size;

		header->alternative_lba = cpu_to_le64(GPT_PRIMARY_PARTITION_TABLE_LBA);
		header->partition_entry_lba = cpu_to_le64(cxt->total_sectors - 1 - esects);
	}
}

/*
 * Builds a new GPT header (at sector lba) from a backup header2.
 * If building a primary header, then backup is the secondary, and vice versa.
 *
 * Always pass a new (zeroized) header to build upon as we don't
 * explicitly zero-set some values such as CRCs and reserved.
 *
 * Returns 0 on success, otherwise < 0 on error.
 */
static int gpt_mknew_header_from_bkp(struct fdisk_context *cxt,
				     struct gpt_header *header,
				     uint64_t lba,
				     struct gpt_header *header2)
{
	if (!cxt || !header || !header2)
		return -ENOSYS;

	header->signature              = header2->signature;
	header->revision               = header2->revision;
	header->size                   = header2->size;
	header->npartition_entries     = header2->npartition_entries;
	header->sizeof_partition_entry = header2->sizeof_partition_entry;
	header->first_usable_lba       = header2->first_usable_lba;
	header->last_usable_lba        = header2->last_usable_lba;

	memcpy(&header->disk_guid,
	       &header2->disk_guid, sizeof(header2->disk_guid));
	gpt_mknew_header_common(cxt, header, lba);

	return 0;
}

/*
 * Builds a clean new GPT header (currently under revision 1.0).
 *
 * Always pass a new (zeroized) header to build upon as we don't
 * explicitly zero-set some values such as CRCs and reserved.
 *
 * Returns 0 on success, otherwise < 0 on error.
 */
static int gpt_mknew_header(struct fdisk_context *cxt,
			    struct gpt_header *header, uint64_t lba)
{
	uint64_t esz = 0, first, last;

	if (!cxt || !header)
		return -ENOSYS;

	esz = sizeof(struct gpt_entry) * GPT_NPARTITIONS / cxt->sector_size;

	header->signature = cpu_to_le64(GPT_HEADER_SIGNATURE);
	header->revision  = cpu_to_le32(GPT_HEADER_REVISION_V1_00);
	header->size      = cpu_to_le32(sizeof(struct gpt_header));

	/*
	 * 128 partitions is the default. It can go behond this, however,
	 * we're creating a de facto header here, so no funny business.
	 */
	header->npartition_entries     = cpu_to_le32(GPT_NPARTITIONS);
	header->sizeof_partition_entry = cpu_to_le32(sizeof(struct gpt_entry));

	last = cxt->total_sectors - 2 - esz;
	first = esz + 2;

	if (first < cxt->first_lba && cxt->first_lba < last)
		/* Align according to topology */
		first = cxt->first_lba;

	header->first_usable_lba = cpu_to_le64(first);
	header->last_usable_lba  = cpu_to_le64(last);

	gpt_mknew_header_common(cxt, header, lba);
	uuid_generate_random((unsigned char *) &header->disk_guid);
	swap_efi_guid(&header->disk_guid);

	return 0;
}

/*
 * Checks if there is a valid protective MBR partition table.
 * Returns 0 if it is invalid or failure. Otherwise, return
 * GPT_MBR_PROTECTIVE or GPT_MBR_HYBRID, depeding on the detection.
 */
static int valid_pmbr(struct fdisk_context *cxt)
{
	int i, ret = 0; /* invalid by default */
	struct gpt_legacy_mbr *pmbr = NULL;

	if (!cxt->firstsector)
		goto done;

	pmbr = (struct gpt_legacy_mbr *) cxt->firstsector;

	if (pmbr->signature != cpu_to_le64(MSDOS_MBR_SIGNATURE))
		goto done;

	/* LBA of the GPT partition header */
	if (pmbr->partition_record[0].starting_lba !=
	    cpu_to_le32(GPT_PRIMARY_PARTITION_TABLE_LBA))
		goto done;

	/* seems like a valid MBR was found, check DOS primary partitions */
	for (i = 0; i < 4; i++)
		if (pmbr->partition_record[i].os_type == EFI_PMBR_OSTYPE) {
			/*
			 * Ok, we at least know that there's a protective MBR,
			 * now check if there are other partition types for
			 * hybrid MBR.
			 */
			ret = GPT_MBR_PROTECTIVE;
			goto check_hybrid;
		}

check_hybrid:
	if (ret != GPT_MBR_PROTECTIVE)
		goto done;
	for (i = 0 ; i < 4; i++)
		if ((pmbr->partition_record[i].os_type != EFI_PMBR_OSTYPE) &&
		    (pmbr->partition_record[i].os_type != 0x00))
			ret = GPT_MBR_HYBRID;

	/*
	 * Protective MBRs take up the lesser of the whole disk
	 * or 2 TiB (32bit LBA), ignoring the rest of the disk.
	 *
	 * Hybrid MBRs do not necessarily comply with this.
	 */
	if (ret == GPT_MBR_PROTECTIVE)
		if (pmbr->partition_record[0].size_in_lba !=
		    cpu_to_le32(min((uint32_t) cxt->total_sectors - 1, 0xFFFFFFFF)))
			ret = 0;
done:
	return ret;
}

static uint64_t last_lba(struct fdisk_context *cxt)
{
	struct stat s;

	memset(&s, 0, sizeof(s));
	if (fstat(cxt->dev_fd, &s) == -1) {
		fprintf(stderr, "last_lba() could not stat: %m\n");
		return 0;
	}

	if (S_ISBLK(s.st_mode))
		return cxt->total_sectors - 1;
	else if (S_ISREG(s.st_mode)) {
		uint64_t sectors = s.st_size >> cxt->sector_size;
		return (sectors / cxt->sector_size) - 1ULL;
	} else {
		fprintf(stderr,
			"last_lba(): I don't know how to handle files with mode %o\n",
			s.st_mode);
	}
	return 0;
}

static ssize_t read_lba(struct fdisk_context *cxt, uint64_t lba,
			void *buffer, const size_t bytes)
{
	off_t offset = lba * cxt->sector_size;

	lseek(cxt->dev_fd, offset, SEEK_SET);
	return read(cxt->dev_fd, buffer, bytes);
}


/* Returns the GPT entry array */
static struct gpt_entry *gpt_get_entries(struct fdisk_context *cxt,
					 struct gpt_header *header, const ssize_t sz)
{
	struct gpt_entry *ret = xcalloc(1, sizeof(*ents) * sz);
	off_t offset = le64_to_cpu(header->partition_entry_lba) *
		       cxt->sector_size;

	if (offset != lseek(cxt->dev_fd, offset, SEEK_SET))
		return NULL;
	if (sz != read(cxt->dev_fd, ret, sz))
		return NULL;

	return ret;
}

static inline uint32_t count_crc32(const unsigned char *buf, size_t len)
{
	return (crc32(~0L, buf, len) ^ ~0L);
}

/*
 * Recompute header and partition array 32bit CRC checksums.
 * This function does not fail - if there's corruption, then it
 * will be reported when checksuming it again (ie: probing or verify).
 */
static void gpt_recompute_crc(struct gpt_header *header, struct gpt_entry *e)
{
	uint32_t crc = 0;
	size_t entry_sz = 0;

	if (!header)
		return;

	/* header CRC */
	header->crc32 = 0;
	crc = count_crc32((unsigned char *) header, le32_to_cpu(header->size));
	header->crc32 = cpu_to_le32(crc);

	/* partition entry array CRC */
	header->partition_entry_array_crc32 = 0;
	entry_sz = le32_to_cpu(header->npartition_entries) *
		le32_to_cpu(header->sizeof_partition_entry);

	crc = count_crc32((unsigned char *) e, entry_sz);
	header->partition_entry_array_crc32 = cpu_to_le32(crc);
}

/*
 * Compute the 32bit CRC checksum of the partition table header.
 * Returns 1 if it is valid, otherwise 0.
 */
static int gpt_check_header_crc(struct gpt_header *header)
{
	uint32_t crc, orgcrc = le32_to_cpu(header->crc32);

	header->crc32 = 0;
	crc = count_crc32((unsigned char *) header, le32_to_cpu(header->size));
	header->crc32 = cpu_to_le32(orgcrc);

	/*
	 * If we have checksum mismatch it may be due to stale data,
	 * like a partition being added or deleted. Recompute the CRC again
	 * and make sure this is not the case.
	 */
	if (crc != le32_to_cpu(header->crc32)) {
		gpt_recompute_crc(header, ents);
		orgcrc = le32_to_cpu(header->crc32);
		header->crc32 = 0;
		crc = count_crc32((unsigned char *) header, le32_to_cpu(header->size));
		header->crc32 = cpu_to_le32(orgcrc);

		return crc == le32_to_cpu(header->crc32);
	} else
		return 1;
}

/*
 * It initializes the partition entry array.
 * Returns 1 if the checksum is valid, otherwise 0.
 */
static int gpt_check_entryarr_crc(struct fdisk_context *cxt, struct gpt_header *header)
{
	int ret = 0;
	ssize_t entry_sz;
	uint32_t crc;

	if (!header)
		goto done;

	entry_sz = le32_to_cpu(header->npartition_entries) *
		   le32_to_cpu(header->sizeof_partition_entry);

	if (!entry_sz)
		goto done;

	/* read header entries */
	if (!ents)
		ents = gpt_get_entries(cxt, header, entry_sz);
	if (!ents)
		goto done;

	crc = count_crc32((unsigned char *) ents, entry_sz);
	ret = (crc == le32_to_cpu(header->partition_entry_array_crc32));
done:
	return ret;
}

static int gpt_check_lba_sanity(struct fdisk_context *cxt, struct gpt_header *header)
{
	int ret = 0;
	uint64_t lu, fu, lastlba = last_lba(cxt);

	fu = le64_to_cpu(header->first_usable_lba);
	lu = le64_to_cpu(header->last_usable_lba);

	/* check if first and last usable LBA make sense */
	if (lu < fu) {
		DBG(LABEL, dbgprint("error: header last LBA is before first LBA"));
		goto done;
	}

	/* check if first and last usable LBAs with the disk's last LBA */
	if (fu > lastlba || lu > lastlba) {
		DBG(LABEL, dbgprint("error: header LBAs are after the disk's last LBA"));
		goto done;
	}

	/* the header has to be outside usable range */
	if (fu < GPT_PRIMARY_PARTITION_TABLE_LBA &&
	    GPT_PRIMARY_PARTITION_TABLE_LBA < lu) {
		DBG(LABEL, dbgprint("error: header outside of usable range"));
		goto done;
	}

	ret = 1; /* sane */
done:
	return ret;
}

/* Check if there is a valid header signature */
static int gpt_check_signature(struct gpt_header *header)
{
	return header->signature == cpu_to_le64(GPT_HEADER_SIGNATURE);
}

/*
 * Return the specified GPT Header, or NULL upon failure/invalid.
 * Note that all tests must pass to ensure a valid header,
 * we do not rely on only testing the signature for a valid probe.
 */
static struct gpt_header *gpt_get_header(struct fdisk_context *cxt, uint64_t lba)
{
	struct gpt_header *header = NULL;
	uint32_t hsz;

	if (!cxt)
		return NULL;

	header = xcalloc(1, sizeof(*header));

	/* read specified LBA */
	if (!read_lba(cxt, lba, header, sizeof(struct gpt_header)))
		goto invalid;

	if (!gpt_check_signature(header))
		goto invalid;

	if (!gpt_check_header_crc(header) ||
	    !gpt_check_entryarr_crc(cxt, header))
		goto invalid;

	if (!gpt_check_lba_sanity(cxt, header))
		goto invalid;

	/* valid header must be at MyLBA */
	if (le64_to_cpu(header->my_lba) != lba)
		goto invalid;

	/* make sure header size is between 92 and sector size bytes */
	hsz = le32_to_cpu(header->size);
	if (hsz < GPT_HEADER_MINSZ || hsz > cxt->sector_size)
		goto invalid;

	return header;
invalid:
	free(header);
	return NULL;
}

/*
 * Return the Backup GPT Header, or NULL upon failure/invalid.
 */
static struct gpt_header *gpt_get_bheader(struct fdisk_context *cxt)
{
	return gpt_get_header(cxt, last_lba(cxt));
}

/*
 * Return the Primary GPT Header, or NULL upon failure/invalid.
 */
static struct gpt_header *gpt_get_pheader(struct fdisk_context *cxt)
{
	return gpt_get_header(cxt, GPT_PRIMARY_PARTITION_TABLE_LBA);
}

/*
 * Returns the number of partitions that are in use.
 */
static uint32_t partitions_in_use(struct gpt_header *header, struct gpt_entry *e)
{
	uint32_t i, used = 0;

	if (!header || ! e)
		return 0;

	for (i = 0; i < le32_to_cpu(header->npartition_entries); i++)
		if (!partition_unused(&e[i]))
			used++;
	return used;
}


/*
 * Check if a partition is too big for the disk (sectors).
 * Returns the faulting partition number, otherwise 0.
 */
static int partition_check_too_big(struct gpt_header *header,
				   struct gpt_entry *e, uint64_t sectors)
{
	uint32_t i;

	for (i = 0; i < le32_to_cpu(header->npartition_entries); i++) {
		if (partition_unused(&e[i]))
			continue;
		if (gpt_partition_end(&e[i]) >= sectors)
			return i + 1;
	}

	return 0;
}

/*
 * Check if a partition ends before it begins
 * Returns the faulting partition number, otherwise 0.
 */
static int partition_start_after_end(struct gpt_header *header, struct gpt_entry *e)
{
	uint32_t i;

	for (i = 0; i < le32_to_cpu(header->npartition_entries); i++) {
		if (partition_unused(&e[i]))
			continue;
		if (gpt_partition_start(&e[i]) > gpt_partition_end(&e[i]))
			return i + 1;
	}

	return 0;
}

/*
 * Check if partition e1 overlaps with partition e2
 */
static inline int partition_overlap(struct gpt_entry *e1, struct gpt_entry *e2)
{
	uint64_t start1 = gpt_partition_start(e1);
	uint64_t end1   = gpt_partition_end(e1);
	uint64_t start2 = gpt_partition_start(e2);
	uint64_t end2   = gpt_partition_end(e2);

	return (start1 && start2 && (start1 <= end2) != (end1 < start2));
}

/*
 * Find any paritions that overlap.
 */
static int partition_check_overlaps(struct gpt_header *header, struct gpt_entry *e)
{
	uint32_t i, j;

	for (i = 0; i < le32_to_cpu(header->npartition_entries); i++)
		for (j = 0; j < i; j++) {
			if (partition_unused(&e[i]) ||
			    partition_unused(&e[j]))
				continue;
			if (partition_overlap(&e[i], &e[j]))
				/* two overlaping partitions is enough! */
				return i + 1;
		}

	return 0;
}

/*
 * Find the first available block after the starting point; returns 0 if
 * there are no available blocks left, or error. From gdisk.
 */
static uint64_t find_first_available(struct gpt_header *header,
				     struct gpt_entry *e, uint64_t start)
{
	uint64_t first;
	uint32_t i, first_moved = 0;

	uint64_t fu, lu;

	if (!header || !e)
		return 0;

	fu = le64_to_cpu(header->first_usable_lba);
	lu = le64_to_cpu(header->last_usable_lba);

	/*
	 * Begin from the specified starting point or from the first usable
	 * LBA, whichever is greater...
	 */
	first = start < fu ? fu : start;

	/*
	 * Now search through all partitions; if first is within an
	 * existing partition, move it to the next sector after that
	 * partition and repeat. If first was moved, set firstMoved
	 * flag; repeat until firstMoved is not set, so as to catch
	 * cases where partitions are out of sequential order....
	 */
	do {
		first_moved = 0;
		for (i = 0; i < le32_to_cpu(header->npartition_entries); i++) {
			if (partition_unused(&e[i]))
				continue;
			if (first < gpt_partition_start(&e[i]))
				continue;
			if (first <= gpt_partition_end(&e[i])) {
				first = gpt_partition_end(&e[i]) + 1;
				first_moved = 1;
			}
		}
	} while (first_moved == 1);

	if (first > lu)
		first = 0;

	return first;
}


/* Returns last available sector in the free space pointed to by start. From gdisk. */
static uint64_t find_last_free(struct gpt_header *header,
			       struct gpt_entry *e, uint64_t start)
{
	uint32_t i;
	uint64_t nearest_start;

	if (!header || !e)
		return 0;

	nearest_start = le64_to_cpu(header->last_usable_lba);

	for (i = 0; i < le32_to_cpu(header->npartition_entries); i++) {
		uint64_t ps = gpt_partition_start(&e[i]);

		if (nearest_start > ps && ps > start)
			nearest_start = ps - 1;
	}

	return nearest_start;
}

/* Returns the last free sector on the disk. From gdisk. */
static uint64_t find_last_free_sector(struct gpt_header *header,
				      struct gpt_entry *e)
{
	uint32_t i, last_moved;
	uint64_t last = 0;

	if (!header || !e)
		goto done;

	/* start by assuming the last usable LBA is available */
	last = le64_to_cpu(header->last_usable_lba);
	do {
		last_moved = 0;
		for (i = 0; i < le32_to_cpu(header->npartition_entries); i++) {
			if ((last >= gpt_partition_start(&e[i])) &&
			    (last <= gpt_partition_end(&e[i]))) {
				last = gpt_partition_start(&e[i]) - 1;
				last_moved = 1;
			}
		}
	} while (last_moved == 1);
done:
	return last;
}

/*
 * Finds the first available sector in the largest block of unallocated
 * space on the disk. Returns 0 if there are no available blocks left.
 * From gdisk.
 */
static uint64_t find_first_in_largest(struct gpt_header *header, struct gpt_entry *e)
{
	uint64_t start = 0, first_sect, last_sect;
	uint64_t segment_size, selected_size = 0, selected_segment = 0;

	if (!header || !e)
		goto done;

	do {
		first_sect =  find_first_available(header, e, start);
		if (first_sect != 0) {
			last_sect = find_last_free(header, e, first_sect);
			segment_size = last_sect - first_sect + 1;

			if (segment_size > selected_size) {
				selected_size = segment_size;
				selected_segment = first_sect;
			}
			start = last_sect + 1;
		}
	} while (first_sect != 0);

done:
	return selected_segment;
}

/*
 * Find the total number of free sectors, the number of segments in which
 * they reside, and the size of the largest of those segments. From gdisk.
 */
static uint64_t get_free_sectors(struct fdisk_context *cxt, struct gpt_header *header,
				 struct gpt_entry *e, uint32_t *nsegments,
				 uint64_t *largest_segment)
{
	uint32_t num = 0;
	uint64_t first_sect, last_sect;
	uint64_t largest_seg = 0, segment_sz;
	uint64_t totfound = 0, start = 0; /* starting point for each search */

	if (!cxt->total_sectors)
		goto done;

	do {
		first_sect = find_first_available(header, e, start);
		if (first_sect) {
			last_sect = find_last_free(header, e, first_sect);
			segment_sz = last_sect - first_sect + 1;

			if (segment_sz > largest_seg)
				largest_seg = segment_sz;
			totfound += segment_sz;
			num++;
			start = last_sect + 1;
		}
	} while (first_sect);

done:
	if (nsegments)
		*nsegments = num;
	if (largest_segment)
		*largest_segment = largest_seg;

	return totfound;
}

/*
 * Initialize fdisk-specific variables - call once probing passes!
 */
static void gpt_init(struct fdisk_context *cxt)
{
	cxt->disklabel = FDISK_DISKLABEL_GPT;
	partitions = le32_to_cpu(pheader->npartition_entries);
}

/*
 * Deinitialize fdisk-specific variables
 */
static void gpt_deinit(struct fdisk_context *cxt)
{
	free(ents);
	free(pheader);
	free(bheader);
	ents = NULL;
	pheader = NULL;
	bheader = NULL;

	cxt->disklabel = FDISK_DISKLABEL_ANY;
	partitions = 0;
}

static int gpt_probe_label(struct fdisk_context *cxt)
{
	int mbr_type;

	if (!cxt)
		goto failed;

	mbr_type = valid_pmbr(cxt);
	if (!mbr_type)
		goto failed;

	DBG(LABEL, dbgprint("found a %s MBR", mbr_type == GPT_MBR_PROTECTIVE ?
			    "protective" : "hybrid"));

	pheader = gpt_get_pheader(cxt);

	/*
	 * TODO: If the primary GPT is corrupt, we must check the last LBA of the
	 * device to see if it has a valid GPT Header and point to a valid GPT
	 * Partition Entry Array.
	 * If it points to a valid GPT Partition Entry Array, then software should
	 * restore the primary GPT if allowed by platform policy settings.
	 *
	 * For now we just abort GPT probing!
	 */
	if (!pheader)
		goto failed;

	/* OK, probing passed, now initialize backup header and fdisk variables. */
	bheader = gpt_get_bheader(cxt);
	gpt_init(cxt);

	printf(_("\nWARNING: fdisk GPT support is currently new, and therefore "
		 "in an experimental phase. Use at your own discretion.\n\n"));

	return 1;
failed:
	return 0;
}

/*
 * Stolen from libblkid - can be removed once partition semantics
 * are added to the fdisk API.
 */
static char *encode_to_utf8(unsigned char *src, size_t count)
{
	uint16_t c;
	char *dest = xmalloc(count * sizeof(char));
	size_t i, j, len = count;

	memset(dest, 0, sizeof(char) * count);

	for (j = i = 0; i + 2 <= count; i += 2) {
		/* always little endian */
		c = (src[i+1] << 8) | src[i];
		if (c == 0) {
			dest[j] = '\0';
			break;
		} else if (c < 0x80) {
			if (j+1 >= len)
				break;
			dest[j++] = (uint8_t) c;
		} else if (c < 0x800) {
			if (j+2 >= len)
				break;
			dest[j++] = (uint8_t) (0xc0 | (c >> 6));
			dest[j++] = (uint8_t) (0x80 | (c & 0x3f));
		} else {
			if (j+3 >= len)
				break;
			dest[j++] = (uint8_t) (0xe0 | (c >> 12));
			dest[j++] = (uint8_t) (0x80 | ((c >> 6) & 0x3f));
			dest[j++] = (uint8_t) (0x80 | (c & 0x3f));
		}
	}
	dest[j] = '\0';

	return dest;
}

/*
 * List label partitions.
 * This function must currently exist to comply with standard fdisk
 * requirements, but once partition semantics are added to the fdisk
 * API it can be removed for custom implementation (see gpt_label struct).
 */
void gpt_list_table(struct fdisk_context *cxt,
		    int xtra  __attribute__ ((__unused__)))
{
	uint32_t i;
	uint64_t fu = le64_to_cpu(pheader->first_usable_lba);
	uint64_t lu = le64_to_cpu(pheader->last_usable_lba);

	printf("\n#         Start          End    Size  Type            Name\n");

	for (i = 0; i < le32_to_cpu(pheader->npartition_entries); i++) {
		char *name = NULL, *sizestr = NULL;
		uint64_t start = gpt_partition_start(&ents[i]);
		uint64_t size = gpt_partition_size(&ents[i]);
		struct fdisk_parttype *t;

		if (partition_unused(&ents[i]) || !size)
			continue;

		/* the partition has to inside usable range */
		if (start < fu || start + size - 1 > lu)
			continue;

		name = encode_to_utf8((unsigned char *)ents[i].partition_name,
				      sizeof(ents[i].partition_name));
		if (!name)
			continue;
		sizestr = size_to_human_string(SIZE_SUFFIX_1LETTER,
					       size * cxt->sector_size);
		if (!sizestr)
			continue;

		t = fdisk_get_partition_type(cxt, i);

		printf("%2d %12ju %12ju  %6s  %-15.15s %s\n",
		       i+1,
		       start,
		       gpt_partition_end(&ents[i]),
		       sizestr,
		       t->name,
		       name);

		check_alignment(cxt, start, i);

		free(name);
		free(sizestr);
		fdisk_free_parttype(t);
	}
}

/*
 * Write partitions.
 * Returns 0 on success, or corresponding error otherwise.
 */
static int gpt_write_partitions(struct fdisk_context *cxt,
				struct gpt_header *header, struct gpt_entry *e)
{
	off_t offset = le64_to_cpu(header->partition_entry_lba) * cxt->sector_size;
	uint32_t nparts = le32_to_cpu(header->npartition_entries);
	uint32_t totwrite = nparts * le32_to_cpu(header->sizeof_partition_entry);

	if (offset != lseek(cxt->dev_fd, offset, SEEK_SET))
		goto fail;
	if (totwrite == write(cxt->dev_fd, e, totwrite))
		return 0;
fail:
	return -errno;
}

/*
 * Write a GPT header to a specified LBA
 * Returns 0 on success, or corresponding error otherwise.
 */
static int gpt_write_header(struct fdisk_context *cxt,
			    struct gpt_header *header, uint64_t lba)
{
	off_t offset = lba * cxt->sector_size;

	if (offset != lseek(cxt->dev_fd, offset, SEEK_SET))
		goto fail;
	if (cxt->sector_size ==
	    (size_t) write(cxt->dev_fd, header, cxt->sector_size))
		return 0;
fail:
	return -errno;
}

/*
 * Write the protective MBR.
 * Returns 0 on success, or corresponding error otherwise.
 */
static int gpt_write_pmbr(struct fdisk_context *cxt)
{
	off_t offset;
	struct gpt_legacy_mbr *pmbr = NULL;

	if (!cxt || !cxt->firstsector)
		return -EINVAL;

	pmbr = (struct gpt_legacy_mbr *) cxt->firstsector;

	/* zero out the legacy partitions */
	memset(pmbr->partition_record, 0, sizeof(pmbr->partition_record));

	pmbr->signature = cpu_to_le16(MSDOS_MBR_SIGNATURE);
	pmbr->partition_record[0].os_type      = EFI_PMBR_OSTYPE;
	pmbr->partition_record[0].start_sector = 1;
	pmbr->partition_record[0].end_head     = 0xFE;
	pmbr->partition_record[0].end_sector   = 0xFF;
	pmbr->partition_record[0].end_track    = 0xFF;
	pmbr->partition_record[0].starting_lba = cpu_to_le32(1);

	/*
	 * Set size_in_lba to the size of the disk minus one. If the size of the disk
	 * is too large to be represented by a 32bit LBA (2Tb), set it to 0xFFFFFFFF.
	 */
	if (cxt->total_sectors - 1 > 0xFFFFFFFFULL)
		pmbr->partition_record[0].size_in_lba = cpu_to_le32(0xFFFFFFFF);
	else
		pmbr->partition_record[0].size_in_lba =
			cpu_to_le32(cxt->total_sectors - 1UL);

	offset = GPT_PMBR_LBA * cxt->sector_size;
	if (offset != lseek(cxt->dev_fd, offset, SEEK_SET))
		goto fail;

	/* pMBR covers the first sector (LBA) of the disk */
	if (write_all(cxt->dev_fd, pmbr, cxt->sector_size))
		goto fail;
	return 0;
fail:
	return -errno;
}

/*
 * Writes in-memory GPT and pMBR data to disk.
 * Returns 0 if successful write, otherwise, a corresponding error.
 * Any indication of error will abort the operation.
 */
static int gpt_write_disklabel(struct fdisk_context *cxt)
{
	if (!cxt)
		goto err0;

	/* we do not want to mess up hybrid MBRs by creating a valid pmbr */
	if (valid_pmbr(cxt) == GPT_MBR_HYBRID)
		goto err0;

	/* check that disk is big enough to handle the backup header */
	if (pheader->alternative_lba > cxt->total_sectors)
		goto err0;

	/* check that the backup header is properly placed */
	if (pheader->alternative_lba < cxt->total_sectors - 1)
		/* TODO: correct this (with user authorization) and write */
		goto err0;

	if (partition_check_overlaps(pheader, ents))
		goto err0;

	/* recompute CRCs for both headers */
	gpt_recompute_crc(pheader, ents);
	gpt_recompute_crc(bheader, ents);

	/*
	 * UEFI requires writing in this specific order:
	 *   1) backup partition tables
	 *   2) backup GPT header
	 *   3) primary partition tables
	 *   4) primary GPT header
	 *   5) protective MBR
	 *
	 * If any write fails, we abort the rest.
	 */
	if (gpt_write_partitions(cxt, bheader, ents) != 0)
		goto err1;
	if (gpt_write_header(cxt, bheader, pheader->alternative_lba) != 0)
		goto err1;
	if (gpt_write_partitions(cxt, pheader, ents) != 0)
		goto err1;
	if (gpt_write_header(cxt, pheader, GPT_PRIMARY_PARTITION_TABLE_LBA) != 0)
		goto err1;
	if (gpt_write_pmbr(cxt) != 0)
		goto err1;

	return 0;
err0:
	return -EINVAL;
err1:
	return -errno;
}

/*
 * Verify data integrity and report any found problems for:
 *   - primary and backup header validations
 *   - paritition validations
 */
static int gpt_verify_disklabel(struct fdisk_context *cxt)
{
	int nerror = 0;
	uint64_t ptnum;

	if (!bheader) {
		nerror++;
		printf(_("Disk does not contain a valid backup header.\n"));
	}

	if (!gpt_check_header_crc(pheader)) {
		nerror++;
		printf(_("Invalid primary header CRC checksum.\n"));
	}
	if (bheader && !gpt_check_header_crc(bheader)) {
		nerror++;
		printf(_("Invalid backup header CRC checksum.\n"));
	}

	if (!gpt_check_entryarr_crc(cxt, pheader)) {
		nerror++;
		printf(_("Invalid partition entry checksum.\n"));
	}

	if (!gpt_check_lba_sanity(cxt, pheader)) {
		nerror++;
		printf(_("Invalid primary header LBA sanity checks.\n"));
	}
	if (bheader && !gpt_check_lba_sanity(cxt, bheader)) {
		nerror++;
		printf(_("Invalid backup header LBA sanity checks.\n"));
	}

	if (le64_to_cpu(pheader->my_lba) != GPT_PRIMARY_PARTITION_TABLE_LBA) {
		nerror++;
		printf(_("MyLBA mismatch with real position at primary header.\n"));
	}
	if (bheader && le64_to_cpu(bheader->my_lba) != last_lba(cxt)) {
		nerror++;
		printf(_("MyLBA mismatch with real position at backup header.\n"));

	}

	if (pheader->alternative_lba >= cxt->total_sectors) {
		nerror++;
		printf(_("Disk is to small to hold all data.\n"));
	}

	/*
	 * if the GPT is the primary table, check the alternateLBA
	 * to see if it is a valid GPT
	 */
	if (bheader && (pheader->my_lba != bheader->alternative_lba)) {
		nerror++;
		printf(_("Primary and backup header mismatch.\n"));
	}

	ptnum = partition_check_overlaps(pheader, ents);
	if (ptnum) {
		nerror++;
		printf(_("Partition %ld overlaps with partition %ld.\n"), ptnum, ptnum+1);
	}

	ptnum = partition_check_too_big(pheader, ents, cxt->total_sectors);
	if (ptnum) {
		nerror++;
		printf(_("Partition %ld is too big for the disk.\n"), ptnum);
	}

	ptnum = partition_start_after_end(pheader, ents);
	if (ptnum) {
		nerror++;
		printf(_("Partition %ld ends before it starts.\n"), ptnum);
	}

	if (!nerror) { /* yay :-) */
		uint32_t nsegments = 0;
		uint64_t free_sectors = 0, largest_segment = 0;

		printf(_("No errors detected\n"));
		printf(_("Header version: %s\n"), gpt_get_header_revstr(pheader));
		printf(_("Using %d out of %d partitions\n"),
		       partitions_in_use(pheader, ents),
		       le32_to_cpu(pheader->npartition_entries));

		free_sectors = get_free_sectors(cxt, pheader, ents,
						&nsegments, &largest_segment);
		printf(_("A total of %ld free sectors available in %d segment(s) "
			 "(largest %ld).\n"),
		       free_sectors, nsegments, largest_segment);
	} else
		printf(_("Detected %d error(s).\n"), nerror);

	return 0;
}

/* Delete a single GPT partition, specified by partnum. */
static int gpt_delete_partition(struct fdisk_context *cxt, int partnum)
{
	if (!cxt || partition_unused(&ents[partnum]) || partnum < 0)
		return -EINVAL;

	/* hasta la vista, baby! */
	memset(&ents[partnum], 0, sizeof(ents[partnum]));
	if (!partition_unused(&ents[partnum]))
		return -EINVAL;
	else {
		gpt_recompute_crc(pheader, ents);
		gpt_recompute_crc(bheader, ents);
	}

	return 0;
}

static void gpt_entry_set_type(struct gpt_entry *e, struct gpt_guid *type)
{
	size_t i;

	/*
	 * Copy corresponding partition type GUID. Only the first three blocks
	 * are endian-aware.
	 */
	e->partition_type_guid.time_low = cpu_to_le32(type->time_low);
	e->partition_type_guid.time_mid = cpu_to_le16(type->time_mid);
	e->partition_type_guid.time_hi_and_version = cpu_to_le16(type->time_hi_and_version);
	e->partition_type_guid.clock_seq_hi = type->clock_seq_hi;
	e->partition_type_guid.clock_seq_low = type->clock_seq_low;
	for (i = 0; i < 6; i++)
		e->partition_type_guid.node[i] = type->node[i];

	DBG(LABEL, fprintf(stderr, "new type: %08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X\n",
		type->time_low, type->time_mid, type->time_hi_and_version,
		type->clock_seq_hi, type->clock_seq_low,
		type->node[0], type->node[1], type->node[2],
		type->node[3], type->node[4], type->node[5]));
}

/*
 * Create a new GPT partition entry, specified by partnum, and with a range
 * of fsect to lsenct sectors, of type t.
 * Returns 0 on success, or negative upon failure.
 */
static int gpt_create_new_partition(int partnum, uint64_t fsect, uint64_t lsect,
				    struct gpt_guid *type,
				    struct gpt_entry *entries)
{
	struct gpt_entry *e = NULL;

	if (fsect > lsect || partnum < 0)
		return -EINVAL;

	e = xcalloc(1, sizeof(*e));
	e->lba_end = cpu_to_le64(lsect);
	e->lba_start = cpu_to_le64(fsect);

	gpt_entry_set_type(e, type);

	/* deal with partition name
	for (i = 0; i < GPT_PART_NAME_LEN; i++)
		e->partition_name[i] =
			cpu_to_le16((uint16_t) gpt_sys_types[sys].name[i]);
	*/

	/*
	 * Any time a new partition entry is created a new GUID must be
	 * generated for that partition, and every partition is guaranteed
	 * to have a unique GUID.
	 */
	uuid_generate_random((unsigned char *) &e->unique_partition_guid);
	swap_efi_guid(&e->unique_partition_guid);

	memcpy(&entries[partnum] , e, sizeof(*e));

	gpt_recompute_crc(pheader, entries);
	gpt_recompute_crc(bheader, entries);

	free(e);
	return 0;
}

/* Performs logical checks to add a new partition entry */
static int gpt_add_partition(struct fdisk_context *cxt, int partnum,
			     struct fdisk_parttype *t)
{
	uint64_t user_f, user_l;	/* user input ranges for first and last sectors */
	uint64_t disk_f, disk_l;	/* first and last available sector ranges on device*/
	uint64_t dflt_f, dflt_l;	/* largest segment (default) */
	struct gpt_guid uuid = GPT_DEFAULT_ENTRY_GUID;

	/* check basic tests before even considering adding a new partition */
	if (!cxt || partnum < 0)
		return -EINVAL;
	if (!partition_unused(&ents[partnum])) {
		printf(_("Partition %d is already defined. "
			 "Delete it before re-adding it.\n"), partnum +1);
		return -EINVAL;
	}
	if (le32_to_cpu(pheader->npartition_entries) == partitions_in_use(pheader, ents)) {
		printf(_("All partitions are already in use.\n"));
		return -EINVAL;
	}

	if (!get_free_sectors(cxt, pheader, ents, NULL, NULL)) {
		printf(_("No free sectors available.\n"));
		return -ENOSPC;
	}

	disk_f = find_first_available(pheader, ents, 0);
	disk_l = find_last_free_sector(pheader, ents);

	/* the default is the largest free space */
	dflt_f = find_first_in_largest(pheader, ents);
	dflt_l = find_last_free(pheader, ents, dflt_f);

	/* align the default in range <dflt_f,dflt_l>*/
	dflt_f = align_lba_in_range(cxt, dflt_f, dflt_f, dflt_l);

	if (t && t->typestr)
		string_to_uuid(t->typestr, &uuid);

	/* get user input for first and last sectors of the new partition */
	for (;;) {
		int is_suffix_used = 0;

		/* first sector */
		user_f = read_int(cxt,	disk_f,	/* minimal */
					dflt_f, /* default */
					disk_l, /* maximal */
					0, _("First sector"));

		if (user_f < disk_f || user_f > disk_l)
			continue;	/* bug in read_int() dialog? */

		if (user_f != find_first_available(pheader, ents, user_f)) {
			printf(_("Sector %ju already used\n"), user_f);
			continue;
		}

		/* Last sector */
		dflt_l = find_last_free(pheader, ents, user_f);
		user_l = read_int_with_suffix(cxt,
					user_f, /* minimal */
					dflt_l, /* default */
					dflt_l, /* maximal */
					user_f, /* base for relative input */
					_("Last sector, +sectors or +size{K,M,G}"),
					&is_suffix_used);

		if (is_suffix_used)
			user_l = align_lba_in_range(cxt, user_l, user_f, dflt_l) - 1;

		if (user_l > user_f && user_l <= disk_l)
			break;
	}

	if (gpt_create_new_partition(partnum, user_f, user_l, &uuid, ents) != 0)
		printf(_("Could not create partition %d\n"), partnum + 1);
	else
		printf(_("Created partition %d\n"), partnum + 1);

	return 0;
}

/*
 * Create a new GPT disklabel - destroys any previous data.
 */
static int gpt_create_disklabel(struct fdisk_context *cxt)
{
	int rc = 0;
	ssize_t entry_sz = 0;

	/*
	 * Reset space or clear data from headers, pt entries and
	 * protective MBR. Big fat warning: any previous content is
	 * overwritten, so ask users to be sure!.
	 *
	 * When no header, entries or pmbr is set, we're probably
	 * dealing with a new, empty disk - so always allocate memory
	 * to deal with the data structures whatever the case is.
	 */
	gpt_deinit(cxt);

	rc = gpt_mknew_pmbr(cxt);
	if (rc < 0)
		goto done;

	pheader = xcalloc(1, sizeof(*pheader));
	rc = gpt_mknew_header(cxt, pheader, GPT_PRIMARY_PARTITION_TABLE_LBA);
	if (rc < 0)
		goto done;

	bheader = xcalloc(1, sizeof(*bheader));
	rc = gpt_mknew_header_from_bkp(cxt, bheader, last_lba(cxt), pheader);
	if (rc < 0)
		goto done;

	entry_sz = le32_to_cpu(pheader->npartition_entries) *
		le32_to_cpu(pheader->sizeof_partition_entry);
	ents = xcalloc(1, sizeof(*ents) * entry_sz);

	gpt_recompute_crc(pheader, ents);
	gpt_recompute_crc(bheader, ents);

	gpt_init(cxt);
	fprintf(stderr, ("Building a new GPT disklabel "
			    "(GUID: %08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X)\n"),
			    pheader->disk_guid.time_low, pheader->disk_guid.time_mid,
			    pheader->disk_guid.time_hi_and_version,
			    pheader->disk_guid.clock_seq_hi,
			    pheader->disk_guid.clock_seq_low,
			    pheader->disk_guid.node[0], pheader->disk_guid.node[1],
			    pheader->disk_guid.node[2], pheader->disk_guid.node[3],
			    pheader->disk_guid.node[4], pheader->disk_guid.node[5]);
done:
	return rc;
}

static struct fdisk_parttype *gpt_get_partition_type(struct fdisk_context *cxt,
						     int i)
{
	struct fdisk_parttype *t;
	struct gpt_guid uuid;
	char str[37];

	if (!cxt || i < 0 || (uint32_t) i >= le32_to_cpu(pheader->npartition_entries))
		return NULL;

	uuid = ents[i].partition_type_guid;
	swap_efi_guid(&uuid);

	uuid_to_string(&uuid, str);
	t = fdisk_get_parttype_from_string(cxt, str);
	if (!t)
		t = fdisk_new_unknown_parttype(0, str);

	return t;
}


static int gpt_set_partition_type(struct fdisk_context *cxt, int i,
				  struct fdisk_parttype *t)
{
	struct gpt_guid uuid;

	if (!cxt || i < 0 || (uint32_t) i >= le32_to_cpu(pheader->npartition_entries) ||
	      !t || !t->typestr || string_to_uuid(t->typestr, &uuid) != 0)
		return -EINVAL;

	gpt_entry_set_type(&ents[i], &uuid);
	gpt_recompute_crc(pheader, ents);
	gpt_recompute_crc(bheader, ents);
	return 0;
}

const struct fdisk_label gpt_label =
{
	.name = "gpt",
	.parttypes = gpt_parttypes,
	.nparttypes = ARRAY_SIZE(gpt_parttypes),

	.probe = gpt_probe_label,
	.write = gpt_write_disklabel,
	.verify = gpt_verify_disklabel,
	.create = gpt_create_disklabel,
	.part_add = gpt_add_partition,
	.part_delete = gpt_delete_partition,
	.part_get_type = gpt_get_partition_type,
	.part_set_type = gpt_set_partition_type
};
