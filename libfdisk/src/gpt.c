/*
 * Copyright (C) 2007 Karel Zak <kzak@redhat.com>
 * Copyright (C) 2012 Davidlohr Bueso <dave@gnu.org>
 *
 * GUID Partition Table (GPT) support. Based on UEFI Specs 2.3.1
 * Chapter 5: GUID Partition Table (GPT) Disk Layout (Jun 27th, 2012).
 * Some ideas and inspiration from GNU parted and gptfdisk.
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

#include "fdiskP.h"

#include "crc32.h"
#include "blkdev.h"
#include "bitops.h"
#include "strutils.h"
#include "all-io.h"
#include "pt-mbr.h"

/**
 * SECTION: gpt
 * @title: UEFI GPT
 * @short_description: specific functionality
 */

#define GPT_HEADER_SIGNATURE 0x5452415020494645LL /* EFI PART */
#define GPT_HEADER_REVISION_V1_02 0x00010200
#define GPT_HEADER_REVISION_V1_00 0x00010000
#define GPT_HEADER_REVISION_V0_99 0x00009900
#define GPT_HEADER_MINSZ          92 /* bytes */

#define GPT_PMBR_LBA        0
#define GPT_MBR_PROTECTIVE  1
#define GPT_MBR_HYBRID      2

#define GPT_PRIMARY_PARTITION_TABLE_LBA 0x00000001ULL

#define EFI_PMBR_OSTYPE     0xEE
#define MSDOS_MBR_SIGNATURE 0xAA55
#define GPT_PART_NAME_LEN   (72 / sizeof(uint16_t))
#define GPT_NPARTITIONS     FDISK_GPT_NPARTITIONS_DEFAULT

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
#define GPT_DEFAULT_ENTRY_TYPE "0FC63DAF-8483-4772-8E79-3D69D8477DE4"

/*
 * Attribute bits
 */
enum {
	/* UEFI specific */
	GPT_ATTRBIT_REQ      = 0,
	GPT_ATTRBIT_NOBLOCK  = 1,
	GPT_ATTRBIT_LEGACY   = 2,

	/* GUID specific (range 48..64)*/
	GPT_ATTRBIT_GUID_FIRST	= 48,
	GPT_ATTRBIT_GUID_COUNT	= 16
};

#define GPT_ATTRSTR_REQ		"RequiredPartition"
#define GPT_ATTRSTR_REQ_TYPO	"RequiredPartiton"
#define GPT_ATTRSTR_NOBLOCK	"NoBlockIOProtocol"
#define GPT_ATTRSTR_LEGACY	"LegacyBIOSBootable"

/* The GPT Partition entry array contains an array of GPT entries. */
struct gpt_entry {
	struct gpt_guid     type; /* purpose and type of the partition */
	struct gpt_guid     partition_guid;
	uint64_t            lba_start;
	uint64_t            lba_end;
	uint64_t            attrs;
	uint16_t            name[GPT_PART_NAME_LEN];
}  __attribute__ ((packed));

/* GPT header */
struct gpt_header {
	uint64_t            signature; /* header identification */
	uint32_t            revision; /* header version */
	uint32_t            size; /* in bytes */
	uint32_t            crc32; /* header CRC checksum */
	uint32_t            reserved1; /* must be 0 */
	uint64_t            my_lba; /* LBA of block that contains this struct (LBA 1) */
	uint64_t            alternative_lba; /* backup GPT header */
	uint64_t            first_usable_lba; /* first usable logical block for partitions */
	uint64_t            last_usable_lba; /* last usable logical block for partitions */
	struct gpt_guid     disk_guid; /* unique disk identifier */
	uint64_t            partition_entry_lba; /* LBA of start of partition entries array */
	uint32_t            npartition_entries; /* total partition entries - normally 128 */
	uint32_t            sizeof_partition_entry; /* bytes for each GUID pt */
	uint32_t            partition_entry_array_crc32; /* partition CRC checksum */
	uint8_t             reserved2[512 - 92]; /* must all be 0 */
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

/* Probably the most complete list of the GUIDs are at:
 *	 https://wikipedia.org/wiki/GUID_Partition_Table
 */
static struct fdisk_parttype gpt_parttypes[] =
{
	/* Generic OS */
	DEF_GUID("C12A7328-F81F-11D2-BA4B-00A0C93EC93B", N_("EFI System")),

	DEF_GUID("024DEE41-33E7-11D3-9D69-0008C781F39F", N_("MBR partition scheme")),
	DEF_GUID("D3BFE2DE-3DAF-11DF-BA40-E3A556D89593", N_("Intel Fast Flash")),

	/* Hah!IdontneedEFI */
	DEF_GUID("21686148-6449-6E6F-744E-656564454649", N_("BIOS boot")),

	/* NIH syndrome */
	DEF_GUID("F4019732-066E-4E12-8273-346C5641494F", N_("Sony boot partition")),
	DEF_GUID("BFBFAFE7-A34F-448A-9A5B-6213EB736C22", N_("Lenovo boot partition")),

	/* PowerPC reference platform boot partition */
	DEF_GUID("9E1A2D38-C612-4316-AA26-8B49521E5A8B", N_("PowerPC PReP boot")),

	/* Open Network Install Environment */
	DEF_GUID("7412F7D5-A156-4B13-81DC-867174929325", N_("ONIE boot")),
	DEF_GUID("D4E6E2CD-4469-46F3-B5CB-1BFF57AFC149", N_("ONIE config")),

	/* Windows */
	DEF_GUID("E3C9E316-0B5C-4DB8-817D-F92DF00215AE", N_("Microsoft reserved")),
	DEF_GUID("EBD0A0A2-B9E5-4433-87C0-68B6B72699C7", N_("Microsoft basic data")),
	DEF_GUID("5808C8AA-7E8F-42E0-85D2-E1E90434CFB3", N_("Microsoft LDM metadata")),
	DEF_GUID("AF9B60A0-1431-4F62-BC68-3311714A69AD", N_("Microsoft LDM data")),
	DEF_GUID("DE94BBA4-06D1-4D40-A16A-BFD50179D6AC", N_("Windows recovery environment")),
	DEF_GUID("37AFFC90-EF7D-4E96-91C3-2D7AE055B174", N_("IBM General Parallel Fs")),
	DEF_GUID("E75CAF8F-F680-4CEE-AFA3-B001E56EFC2D", N_("Microsoft Storage Spaces")),

	/* HP-UX */
	DEF_GUID("75894C1E-3AEB-11D3-B7C1-7B03A0000000", N_("HP-UX data")),
	DEF_GUID("E2A1E728-32E3-11D6-A682-7B03A0000000", N_("HP-UX service")),

	/* Linux (http://www.freedesktop.org/wiki/Specifications/DiscoverablePartitionsSpec) */
	DEF_GUID("0657FD6D-A4AB-43C4-84E5-0933C84B4F4F", N_("Linux swap")),
	DEF_GUID("0FC63DAF-8483-4772-8E79-3D69D8477DE4", N_("Linux filesystem")),
	DEF_GUID("3B8F8425-20E0-4F3B-907F-1A25A76F98E8", N_("Linux server data")),
	DEF_GUID("44479540-F297-41B2-9AF7-D131D5F0458A", N_("Linux root (x86)")),
	DEF_GUID("69DAD710-2CE4-4E3C-B16C-21A1D49ABED3", N_("Linux root (ARM)")),
	DEF_GUID("4F68BCE3-E8CD-4DB1-96E7-FBCAF984B709", N_("Linux root (x86-64)")),
	DEF_GUID("B921B045-1DF0-41C3-AF44-4C6F280D3FAE", N_("Linux root (ARM-64)")),
	DEF_GUID("993D8D3D-F80E-4225-855A-9DAF8ED7EA97", N_("Linux root	(IA-64)")),
	DEF_GUID("8DA63339-0007-60C0-C436-083AC8230908", N_("Linux reserved")),
	DEF_GUID("933AC7E1-2EB4-4F13-B844-0E14E2AEF915", N_("Linux home")),
	DEF_GUID("A19D880F-05FC-4D3B-A006-743F0F84911E", N_("Linux RAID")),
	DEF_GUID("BC13C2FF-59E6-4262-A352-B275FD6F7172", N_("Linux extended boot")),
	DEF_GUID("E6D6D379-F507-44C2-A23C-238F2A3DF928", N_("Linux LVM")),
	/* ... too crazy, ignore for now:
	DEF_GUID("7FFEC5C9-2D00-49B7-8941-3EA10A5586B7", N_("Linux plain dm-crypt")),
	DEF_GUID("CA7D7CCB-63ED-4C53-861C-1742536059CC", N_("Linux LUKS")),
	*/

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
	DEF_GUID("0394EF8B-237E-11E1-B4B3-E89A8F7FC3A7", N_("MidnightBSD UFS")),
	DEF_GUID("85D5E45D-237C-11E1-B4B3-E89A8F7FC3A7", N_("MidnightBSD ZFS")),
	DEF_GUID("85D5E45C-237C-11E1-B4B3-E89A8F7FC3A7", N_("MidnightBSD Vinum")),

	/* Ceph */
	DEF_GUID("45B0969E-9B03-4F30-B4C6-B4B80CEFF106", N_("Ceph Journal")),
	DEF_GUID("45B0969E-9B03-4F30-B4C6-5EC00CEFF106", N_("Ceph Encrypted Journal")),
	DEF_GUID("4FBD7E29-9D25-41B8-AFD0-062C0CEFF05D", N_("Ceph OSD")),
	DEF_GUID("4FBD7E29-9D25-41B8-AFD0-5EC00CEFF05D", N_("Ceph crypt OSD")),
	DEF_GUID("89C57F98-2FE5-4DC0-89C1-F3AD0CEFF2BE", N_("Ceph disk in creation")),
	DEF_GUID("89C57F98-2FE5-4DC0-89C1-5EC00CEFF2BE", N_("Ceph crypt disk in creation")),

	/* VMware */
	DEF_GUID("AA31E02A-400F-11DB-9590-000C2911D1B8", N_("VMware VMFS")),
	DEF_GUID("9D275380-40AD-11DB-BF97-000C2911D1B8", N_("VMware Diagnostic")),
	DEF_GUID("381CFCCC-7288-11E0-92EE-000C2911D0B2", N_("VMware Virtual SAN")),
	DEF_GUID("77719A0C-A4A0-11E3-A47E-000C29745A24", N_("VMware Virsto")),
	DEF_GUID("9198EFFC-31C0-11DB-8F78-000C2911D1B8", N_("VMware Reserved")),

	/* OpenBSD */
	DEF_GUID("824CC7A0-36A8-11E3-890A-952519AD3F61", N_("OpenBSD data")),

	/* QNX */
	DEF_GUID("CEF5A9AD-73BC-4601-89F3-CDEEEEE321A1", N_("QNX6 file system")),

	/* Plan 9 */
	DEF_GUID("C91818F9-8025-47AF-89D2-F030D7000C2C", N_("Plan 9 partition"))
};

#define alignment_required(_x)  ((_x)->grain != (_x)->sector_size)

/* gpt_entry macros */
#define gpt_partition_start(_e)		le64_to_cpu((_e)->lba_start)
#define gpt_partition_end(_e)		le64_to_cpu((_e)->lba_end)

/*
 * in-memory fdisk GPT stuff
 */
struct fdisk_gpt_label {
	struct fdisk_label	head;		/* generic part */

	/* gpt specific part */
	struct gpt_header	*pheader;	/* primary header */
	struct gpt_header	*bheader;	/* backup header */

	unsigned char *ents;			/* entries (partitions) */
};

static void gpt_deinit(struct fdisk_label *lb);

static inline struct fdisk_gpt_label *self_label(struct fdisk_context *cxt)
{
	return (struct fdisk_gpt_label *) cxt->label;
}

/*
 * Returns the partition length, or 0 if end is before beginning.
 */
static uint64_t gpt_partition_size(const struct gpt_entry *e)
{
	uint64_t start = gpt_partition_start(e);
	uint64_t end = gpt_partition_end(e);

	return start > end ? 0 : end - start + 1ULL;
}

/* prints UUID in the real byte order! */
static void gpt_debug_uuid(const char *mesg, struct gpt_guid *guid)
{
	const unsigned char *uuid = (unsigned char *) guid;

	fprintf(stderr, "%s: "
		"%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x\n",
		mesg,
		uuid[0], uuid[1], uuid[2], uuid[3],
		uuid[4], uuid[5],
		uuid[6], uuid[7],
		uuid[8], uuid[9],
		uuid[10], uuid[11], uuid[12], uuid[13], uuid[14],uuid[15]);
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

static int string_to_guid(const char *in, struct gpt_guid *guid)
{
	if (uuid_parse(in, (unsigned char *) guid)) {   /* BE */
		DBG(LABEL, ul_debug("GPT: failed to parse GUID: %s", in));
		return -EINVAL;
	}
	swap_efi_guid(guid);				/* LE */
	return 0;
}

static char *guid_to_string(const struct gpt_guid *guid, char *out)
{
	struct gpt_guid u = *guid;	/* LE */

	swap_efi_guid(&u);		/* BE */
	uuid_unparse_upper((unsigned char *) &u, out);

	return out;
}

static struct fdisk_parttype *gpt_partition_parttype(
		struct fdisk_context *cxt,
		const struct gpt_entry *e)
{
	struct fdisk_parttype *t;
	char str[UUID_STR_LEN];
	struct gpt_guid guid = e->type;

	guid_to_string(&guid, str);
	t = fdisk_label_get_parttype_from_string(cxt->label, str);
	return t ? : fdisk_new_unknown_parttype(0, str);
}

static void gpt_entry_set_type(struct gpt_entry *e, struct gpt_guid *uuid)
{
	e->type = *uuid;
	DBG(LABEL, gpt_debug_uuid("new type", uuid));
}

static int gpt_entry_set_name(struct gpt_entry *e, char *str)
{
	uint16_t name[GPT_PART_NAME_LEN] = { 0 };
	size_t i, mblen = 0;
	uint8_t *in = (uint8_t *) str;

	for (i = 0; *in && i < GPT_PART_NAME_LEN; in++) {
		if (!mblen) {
			if (!(*in & 0x80)) {
				name[i++] = *in;
			} else if ((*in & 0xE0) == 0xC0) {
				mblen = 1;
				name[i] = (uint16_t)(*in & 0x1F) << (mblen *6);
			} else if ((*in & 0xF0) == 0xE0) {
				mblen = 2;
				name[i] = (uint16_t)(*in & 0x0F) << (mblen *6);
			} else {
				/* broken UTF-8 or code point greater than U+FFFF */
				return -EILSEQ;
			}
		} else {
			/* incomplete UTF-8 sequence */
			if ((*in & 0xC0) != 0x80)
				return -EILSEQ;

			name[i] |= (uint16_t)(*in & 0x3F) << (--mblen *6);
			if (!mblen) {
				/* check for code points reserved for surrogate pairs*/
				if ((name[i] & 0xF800) == 0xD800)
					return -EILSEQ;
				i++;
			}
		}
	}

	for (i = 0; i < GPT_PART_NAME_LEN; i++)
		e->name[i] = cpu_to_le16(name[i]);

	return (int)((char *) in - str);
}

static int gpt_entry_set_uuid(struct gpt_entry *e, char *str)
{
	struct gpt_guid uuid;
	int rc;

	rc = string_to_guid(str, &uuid);
	if (rc)
		return rc;

	e->partition_guid = uuid;
	return 0;
}

static inline int gpt_entry_is_used(const struct gpt_entry *e)
{
	return memcmp(&e->type, &GPT_UNUSED_ENTRY_GUID,
			sizeof(struct gpt_guid)) != 0;
}


static const char *gpt_get_header_revstr(struct gpt_header *header)
{
	if (!header)
		goto unknown;

	switch (le32_to_cpu(header->revision)) {
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

static inline unsigned char *gpt_get_entry_ptr(struct fdisk_gpt_label *gpt, size_t i)
{
	return gpt->ents + le32_to_cpu(gpt->pheader->sizeof_partition_entry) * i;
}

static inline struct gpt_entry *gpt_get_entry(struct fdisk_gpt_label *gpt, size_t i)
{
	return (struct gpt_entry *) gpt_get_entry_ptr(gpt, i);
}

static inline struct gpt_entry *gpt_zeroize_entry(struct fdisk_gpt_label *gpt, size_t i)
{
	return (struct gpt_entry *) memset(gpt_get_entry_ptr(gpt, i),
			0, le32_to_cpu(gpt->pheader->sizeof_partition_entry));
}

/* Use to access array of entries, for() loops, etc. But don't use when
 * you directly do something with GPT header, then use uint32_t.
 */
static inline size_t gpt_get_nentries(struct fdisk_gpt_label *gpt)
{
	return (size_t) le32_to_cpu(gpt->pheader->npartition_entries);
}

static inline int gpt_calculate_sizeof_ents(struct gpt_header *hdr, uint32_t nents, size_t *sz)
{
	uint32_t esz = le32_to_cpu(hdr->sizeof_partition_entry);

	if (nents == 0 || esz == 0 || SIZE_MAX/esz < nents) {
		DBG(LABEL, ul_debug("GPT entreis array size check failed"));
		return -ERANGE;
	}

	*sz = nents * esz;
	return 0;
}

static inline int gpt_sizeof_ents(struct gpt_header *hdr, size_t *sz)
{
	return gpt_calculate_sizeof_ents(hdr, le32_to_cpu(hdr->npartition_entries), sz);
}


static char *gpt_get_header_id(struct gpt_header *header)
{
	char str[UUID_STR_LEN];
	struct gpt_guid guid = header->disk_guid;

	guid_to_string(&guid, str);

	return strdup(str);
}

/*
 * Builds a clean new valid protective MBR - will wipe out any existing data.
 * Returns 0 on success, otherwise < 0 on error.
 */
static int gpt_mknew_pmbr(struct fdisk_context *cxt)
{
	struct gpt_legacy_mbr *pmbr = NULL;
	int rc;

	if (!cxt || !cxt->firstsector)
		return -ENOSYS;

	if (fdisk_has_protected_bootbits(cxt))
		rc = fdisk_init_firstsector_buffer(cxt, 0, MBR_PT_BOOTBITS_SIZE);
	else
		rc = fdisk_init_firstsector_buffer(cxt, 0, 0);
	if (rc)
		return rc;

	pmbr = (struct gpt_legacy_mbr *) cxt->firstsector;
	memset(pmbr->partition_record, 0, sizeof(pmbr->partition_record));

	pmbr->signature = cpu_to_le16(MSDOS_MBR_SIGNATURE);
	pmbr->partition_record[0].os_type      = EFI_PMBR_OSTYPE;
	pmbr->partition_record[0].start_sector = 2;
	pmbr->partition_record[0].end_head     = 0xFF;
	pmbr->partition_record[0].end_sector   = 0xFF;
	pmbr->partition_record[0].end_track    = 0xFF;
	pmbr->partition_record[0].starting_lba = cpu_to_le32(1);
	pmbr->partition_record[0].size_in_lba  =
		cpu_to_le32((uint32_t) min( cxt->total_sectors - 1ULL, 0xFFFFFFFFULL) );

	return 0;
}

/* Move backup header to the end of the device */
static void gpt_fix_alternative_lba(struct fdisk_context *cxt, struct fdisk_gpt_label *gpt)
{
	struct gpt_header *p, *b;
	uint64_t esz, esects, last;

	if (!cxt)
		return;

	p = gpt->pheader;	/* primary */
	b = gpt->bheader;	/* backup */

	/* count size of partitions array */
	esz = (uint64_t) le32_to_cpu(p->npartition_entries) * sizeof(struct gpt_entry);
	esects = (esz + cxt->sector_size - 1) / cxt->sector_size;

	/* reference from primary to backup */
	p->alternative_lba = cpu_to_le64(cxt->total_sectors - 1ULL);

	/* reference from backup to primary */
	b->alternative_lba = p->my_lba;
	b->my_lba = p->alternative_lba;

	/* fix backup partitions array address */
	b->partition_entry_lba = cpu_to_le64(cxt->total_sectors - 1ULL - esects);

	/* update last usable LBA */
	last = cxt->total_sectors - 2ULL - esects;
	p->last_usable_lba  = cpu_to_le64(last);
	b->last_usable_lba  = cpu_to_le64(last);

	DBG(LABEL, ul_debug("Alternative-LBA updated to: %"PRIu64, le64_to_cpu(p->alternative_lba)));
}

/* some universal differences between the headers */
static void gpt_mknew_header_common(struct fdisk_context *cxt,
				    struct gpt_header *header, uint64_t lba)
{
	if (!cxt || !header)
		return;

	header->my_lba = cpu_to_le64(lba);

	if (lba == GPT_PRIMARY_PARTITION_TABLE_LBA) { /* primary */
		header->alternative_lba = cpu_to_le64(cxt->total_sectors - 1ULL);
		header->partition_entry_lba = cpu_to_le64(2ULL);
	} else { /* backup */
		uint64_t esz = (uint64_t) le32_to_cpu(header->npartition_entries) 
							* sizeof(struct gpt_entry);
		uint64_t esects = (esz + cxt->sector_size - 1) / cxt->sector_size;

		header->alternative_lba = cpu_to_le64(GPT_PRIMARY_PARTITION_TABLE_LBA);
		header->partition_entry_lba = cpu_to_le64(cxt->total_sectors - 1ULL - esects);
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

static struct gpt_header *gpt_copy_header(struct fdisk_context *cxt,
			   struct gpt_header *src)
{
	struct gpt_header *res;

	if (!cxt || !src)
		return NULL;

	assert(cxt->sector_size >= sizeof(struct gpt_header));

	res = calloc(1, cxt->sector_size);
	if (!res) {
		fdisk_warn(cxt, _("failed to allocate GPT header"));
		return NULL;
	}

	res->my_lba                 = src->alternative_lba;
	res->alternative_lba        = src->my_lba;

	res->signature              = src->signature;
	res->revision               = src->revision;
	res->size                   = src->size;
	res->npartition_entries     = src->npartition_entries;
	res->sizeof_partition_entry = src->sizeof_partition_entry;
	res->first_usable_lba       = src->first_usable_lba;
	res->last_usable_lba        = src->last_usable_lba;

	memcpy(&res->disk_guid, &src->disk_guid, sizeof(src->disk_guid));


	if (res->my_lba == GPT_PRIMARY_PARTITION_TABLE_LBA)
		res->partition_entry_lba = cpu_to_le64(2ULL);
	else {
		uint64_t esz = (uint64_t) le32_to_cpu(src->npartition_entries) * sizeof(struct gpt_entry);
		uint64_t esects = (esz + cxt->sector_size - 1) / cxt->sector_size;

		res->partition_entry_lba = cpu_to_le64(cxt->total_sectors - 1ULL - esects);
	}

	return res;
}

static int get_script_u64(struct fdisk_context *cxt, uint64_t *num, const char *name)
{
	const char *str;
	int pwr = 0, rc = 0;

	assert(cxt);

	*num = 0;

	if (!cxt->script)
		return 1;

	str = fdisk_script_get_header(cxt->script, name);
	if (!str)
		return 1;

	rc = parse_size(str, (uintmax_t *) num, &pwr);
	if (rc < 0)
		return rc;
	if (pwr)
		*num /= cxt->sector_size;
	return 0;
}

static int count_first_last_lba(struct fdisk_context *cxt,
				 uint64_t *first, uint64_t *last)
{
	int rc = 0;
	uint64_t flba, llba;

	uint64_t esz = 0;

	assert(cxt);
	assert(first);
	assert(last);

	*first = *last = 0;

	/* UEFI default */
	esz = sizeof(struct gpt_entry) * GPT_NPARTITIONS / cxt->sector_size;
	llba = cxt->total_sectors - 2ULL - esz;
	flba = esz + 2ULL;

	/* script default */
	if (cxt->script) {
		rc = get_script_u64(cxt, first, "first-lba");
		if (rc < 0)
			return rc;

		DBG(LABEL, ul_debug("FirstLBA: script=%"PRIu64", uefi=%"PRIu64", topology=%ju.",
		                    *first, flba,  (uintmax_t)cxt->first_lba));

		if (rc == 0 && (*first < flba || *first > llba)) {
			fdisk_warnx(cxt, _("First LBA specified by script is out of range."));
			return -ERANGE;
		}

		rc = get_script_u64(cxt, last, "last-lba");
		if (rc < 0)
			return rc;

		DBG(LABEL, ul_debug("LastLBA: script=%"PRIu64", uefi=%"PRIu64", topology=%ju.",
		                    *last, llba,  (uintmax_t)cxt->last_lba));

		if (rc == 0 && (*last > llba || *last < flba)) {
			fdisk_warnx(cxt, _("Last LBA specified by script is out of range."));
			return -ERANGE;
		}
	}

	if (!*last)
		*last = llba;

	/* default by topology */
	if (!*first)
		*first = flba < cxt->first_lba &&
			 cxt->first_lba < *last ? cxt->first_lba : flba;
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
	uint64_t first, last;
	int has_id = 0, rc;

	if (!cxt || !header)
		return -ENOSYS;

	header->signature = cpu_to_le64(GPT_HEADER_SIGNATURE);
	header->revision  = cpu_to_le32(GPT_HEADER_REVISION_V1_00);

	/* According to EFI standard it's valid to count all the first
	 * sector into header size, but some tools may have a problem
	 * to accept it, so use the header without the zeroed area.
	 * This does not have any impact to CRC, etc.   --kzak Jan-2015
	 */
	header->size = cpu_to_le32(sizeof(struct gpt_header)
				- sizeof(header->reserved2));

	/*
	 * 128 partitions are the default. It can go beyond that, but
	 * we're creating a de facto header here, so no funny business.
	 */
	header->npartition_entries     = cpu_to_le32(GPT_NPARTITIONS);
	header->sizeof_partition_entry = cpu_to_le32(sizeof(struct gpt_entry));

	rc = count_first_last_lba(cxt, &first, &last);
	if (rc)
		return rc;

	header->first_usable_lba = cpu_to_le64(first);
	header->last_usable_lba  = cpu_to_le64(last);

	gpt_mknew_header_common(cxt, header, lba);

	if (cxt->script) {
		const char *id = fdisk_script_get_header(cxt->script, "label-id");
		struct gpt_guid guid = header->disk_guid;
		if (id && string_to_guid(id, &guid) == 0)
			has_id = 1;
		header->disk_guid = guid;
	}

	if (!has_id) {
		struct gpt_guid guid;

		uuid_generate_random((unsigned char *) &header->disk_guid);
		guid = header->disk_guid;
		swap_efi_guid(&guid);
	}
	return 0;
}

/*
 * Checks if there is a valid protective MBR partition table.
 * Returns 0 if it is invalid or failure. Otherwise, return
 * GPT_MBR_PROTECTIVE or GPT_MBR_HYBRID, depending on the detection.
 */
static int valid_pmbr(struct fdisk_context *cxt)
{
	int i, part = 0, ret = 0; /* invalid by default */
	struct gpt_legacy_mbr *pmbr = NULL;

	if (!cxt->firstsector)
		goto done;

	pmbr = (struct gpt_legacy_mbr *) cxt->firstsector;

	if (le16_to_cpu(pmbr->signature) != MSDOS_MBR_SIGNATURE)
		goto done;

	/* seems like a valid MBR was found, check DOS primary partitions */
	for (i = 0; i < 4; i++) {
		if (pmbr->partition_record[i].os_type == EFI_PMBR_OSTYPE) {
			/*
			 * Ok, we at least know that there's a protective MBR,
			 * now check if there are other partition types for
			 * hybrid MBR.
			 */
			part = i;
			ret = GPT_MBR_PROTECTIVE;
			break;
		}
	}

	if (ret != GPT_MBR_PROTECTIVE)
		goto done;

	/* LBA of the GPT partition header */
	if (pmbr->partition_record[part].starting_lba !=
	    cpu_to_le32(GPT_PRIMARY_PARTITION_TABLE_LBA))
		goto done;

	for (i = 0 ; i < 4; i++) {
		if ((pmbr->partition_record[i].os_type != EFI_PMBR_OSTYPE) &&
		    (pmbr->partition_record[i].os_type != 0x00))
			ret = GPT_MBR_HYBRID;
	}

	/*
	 * Protective MBRs take up the lesser of the whole disk
	 * or 2 TiB (32bit LBA), ignoring the rest of the disk.
	 * Some partitioning programs, nonetheless, choose to set
	 * the size to the maximum 32-bit limitation, disregarding
	 * the disk size.
	 *
	 * Hybrid MBRs do not necessarily comply with this.
	 *
	 * Consider a bad value here to be a warning to support dd-ing
	 * an image from a smaller disk to a bigger disk.
	 */
	if (ret == GPT_MBR_PROTECTIVE) {
		uint64_t sz_lba = (uint64_t) le32_to_cpu(pmbr->partition_record[part].size_in_lba);
		if (sz_lba != cxt->total_sectors - 1ULL && sz_lba != 0xFFFFFFFFULL) {

			fdisk_warnx(cxt, _("GPT PMBR size mismatch (%"PRIu64" != %"PRIu64") "
					   "will be corrected by write."),
					sz_lba, cxt->total_sectors - 1ULL);

			/* Note that gpt_write_pmbr() overwrites PMBR, but we want to keep it valid already 
			 * in memory too to disable warnings when valid_pmbr() called next time */
			pmbr->partition_record[part].size_in_lba  =
				cpu_to_le32((uint32_t) min( cxt->total_sectors - 1ULL, 0xFFFFFFFFULL) );
			fdisk_label_set_changed(cxt->label, 1);
		}
	}
done:
	return ret;
}

static uint64_t last_lba(struct fdisk_context *cxt)
{
	struct stat s;
	uint64_t sectors = 0;

	memset(&s, 0, sizeof(s));
	if (fstat(cxt->dev_fd, &s) == -1) {
		fdisk_warn(cxt, _("gpt: stat() failed"));
		return 0;
	}

	if (S_ISBLK(s.st_mode))
		sectors = cxt->total_sectors - 1ULL;
	else if (S_ISREG(s.st_mode))
		sectors = ((uint64_t) s.st_size /
			   (uint64_t) cxt->sector_size) - 1ULL;
	else
		fdisk_warnx(cxt, _("gpt: cannot handle files with mode %o"), s.st_mode);

	DBG(LABEL, ul_debug("GPT last LBA: %"PRIu64"", sectors));
	return sectors;
}

static ssize_t read_lba(struct fdisk_context *cxt, uint64_t lba,
			void *buffer, const size_t bytes)
{
	off_t offset = lba * cxt->sector_size;

	if (lseek(cxt->dev_fd, offset, SEEK_SET) == (off_t) -1)
		return -1;
	return (size_t)read(cxt->dev_fd, buffer, bytes) != bytes;
}


/* Returns the GPT entry array */
static unsigned char *gpt_read_entries(struct fdisk_context *cxt,
					 struct gpt_header *header)
{
	size_t sz = 0;
	ssize_t ssz;

	unsigned char *ret = NULL;
	off_t offset;

	assert(cxt);
	assert(header);

	if (gpt_sizeof_ents(header, &sz))
		return NULL;

	if (sz > (size_t) SSIZE_MAX) {
		DBG(LABEL, ul_debug("GPT entries array too large to read()"));
		return NULL;
	}

	ret = calloc(1, sz);
	if (!ret)
		return NULL;

	offset = (off_t) le64_to_cpu(header->partition_entry_lba) *
		       cxt->sector_size;

	if (offset != lseek(cxt->dev_fd, offset, SEEK_SET))
		goto fail;

	ssz = read(cxt->dev_fd, ret, sz);
	if (ssz < 0 || (size_t) ssz != sz)
		goto fail;

	return ret;

fail:
	free(ret);
	return NULL;
}

static inline uint32_t count_crc32(const unsigned char *buf, size_t len,
				   size_t ex_off, size_t ex_len)
{
	return (ul_crc32_exclude_offset(~0L, buf, len, ex_off, ex_len) ^ ~0L);
}

static inline uint32_t gpt_header_count_crc32(struct gpt_header *header)
{
        return count_crc32((unsigned char *) header,		/* buffer */
			le32_to_cpu(header->size),		/* size of buffer */
			offsetof(struct gpt_header, crc32),	/* exclude */
			sizeof(header->crc32));			/* size of excluded area */
}

static inline uint32_t gpt_entryarr_count_crc32(struct gpt_header *header, unsigned char *ents)
{
	size_t arysz = 0;

	if (gpt_sizeof_ents(header, &arysz))
		return 0;

	return count_crc32(ents, arysz, 0, 0);
}


/*
 * Recompute header and partition array 32bit CRC checksums.
 * This function does not fail - if there's corruption, then it
 * will be reported when checksumming it again (ie: probing or verify).
 */
static void gpt_recompute_crc(struct gpt_header *header, unsigned char *ents)
{
	if (!header)
		return;

	header->partition_entry_array_crc32 =
			cpu_to_le32( gpt_entryarr_count_crc32(header, ents) );

	header->crc32 = cpu_to_le32( gpt_header_count_crc32(header) );
}

/*
 * Compute the 32bit CRC checksum of the partition table header.
 * Returns 1 if it is valid, otherwise 0.
 */
static int gpt_check_header_crc(struct gpt_header *header, unsigned char *ents)
{
	uint32_t orgcrc = le32_to_cpu(header->crc32),
		 crc = gpt_header_count_crc32(header);

	if (crc == orgcrc)
		return 1;

	/*
	 * If we have checksum mismatch it may be due to stale data, like a
	 * partition being added or deleted. Recompute the CRC again and make
	 * sure this is not the case.
	 */
	if (ents) {
		gpt_recompute_crc(header, ents);
		return gpt_header_count_crc32(header) == orgcrc;
	}

	return 0;
}

/*
 * It initializes the partition entry array.
 * Returns 1 if the checksum is valid, otherwise 0.
 */
static int gpt_check_entryarr_crc(struct gpt_header *header, unsigned char *ents)
{
	if (!header || !ents)
		return 0;

	return gpt_entryarr_count_crc32(header, ents) ==
			le32_to_cpu(header->partition_entry_array_crc32);
}

static int gpt_check_lba_sanity(struct fdisk_context *cxt, struct gpt_header *header)
{
	int ret = 0;
	uint64_t lu, fu, lastlba = last_lba(cxt);

	fu = le64_to_cpu(header->first_usable_lba);
	lu = le64_to_cpu(header->last_usable_lba);

	/* check if first and last usable LBA make sense */
	if (lu < fu) {
		DBG(LABEL, ul_debug("error: header last LBA is before first LBA"));
		goto done;
	}

	/* check if first and last usable LBAs with the disk's last LBA */
	if (fu > lastlba || lu > lastlba) {
		DBG(LABEL, ul_debug("error: header LBAs are after the disk's last LBA"));
		goto done;
	}

	/* the header has to be outside usable range */
	if (fu < GPT_PRIMARY_PARTITION_TABLE_LBA &&
	    GPT_PRIMARY_PARTITION_TABLE_LBA < lu) {
		DBG(LABEL, ul_debug("error: header outside of usable range"));
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
static struct gpt_header *gpt_read_header(struct fdisk_context *cxt,
					  uint64_t lba,
					  unsigned char **_ents)
{
	struct gpt_header *header = NULL;
	unsigned char *ents = NULL;
	uint32_t hsz;

	if (!cxt)
		return NULL;

	/* always allocate all sector, the area after GPT header
	 * has to be fill by zeros */
	assert(cxt->sector_size >= sizeof(struct gpt_header));

	header = calloc(1, cxt->sector_size);
	if (!header)
		return NULL;

	/* read and verify header */
	if (read_lba(cxt, lba, header, cxt->sector_size) != 0)
		goto invalid;

	if (!gpt_check_signature(header))
		goto invalid;

	/* make sure header size is between 92 and sector size bytes */
	hsz = le32_to_cpu(header->size);
	if (hsz < GPT_HEADER_MINSZ || hsz > cxt->sector_size)
		goto invalid;

	if (!gpt_check_header_crc(header, NULL))
		goto invalid;

	/* read and verify entries */
	ents = gpt_read_entries(cxt, header);
	if (!ents)
		goto invalid;

	if (!gpt_check_entryarr_crc(header, ents))
		goto invalid;

	if (!gpt_check_lba_sanity(cxt, header))
		goto invalid;

	/* valid header must be at MyLBA */
	if (le64_to_cpu(header->my_lba) != lba)
		goto invalid;

	if (_ents)
		*_ents = ents;
	else
		free(ents);

	DBG(LABEL, ul_debug("found valid GPT Header on LBA %"PRIu64"", lba));
	return header;
invalid:
	free(header);
	free(ents);

	DBG(LABEL, ul_debug("read GPT Header on LBA %"PRIu64" failed", lba));
	return NULL;
}


static int gpt_locate_disklabel(struct fdisk_context *cxt, int n,
		const char **name, uint64_t *offset, size_t *size)
{
	struct fdisk_gpt_label *gpt;

	assert(cxt);

	*name = NULL;
	*offset = 0;
	*size = 0;

	switch (n) {
	case 0:
		*name = "PMBR";
		*offset = 0;
		*size = 512;
		break;
	case 1:
		*name = _("GPT Header");
		*offset = (uint64_t) GPT_PRIMARY_PARTITION_TABLE_LBA * cxt->sector_size;
		*size = sizeof(struct gpt_header);
		break;
	case 2:
		*name = _("GPT Entries");
		gpt = self_label(cxt);
		*offset = (uint64_t) le64_to_cpu(gpt->pheader->partition_entry_lba) *
				     cxt->sector_size;
		return gpt_sizeof_ents(gpt->pheader, size);
	default:
		return 1;			/* no more chunks */
	}

	return 0;
}

static int gpt_get_disklabel_item(struct fdisk_context *cxt, struct fdisk_labelitem *item)
{
	struct gpt_header *h;
	int rc = 0;

	assert(cxt);
	assert(cxt->label);
	assert(fdisk_is_label(cxt, GPT));

	h = self_label(cxt)->pheader;

	switch (item->id) {
	case GPT_LABELITEM_ID:
		item->name = _("Disk identifier");
		item->type = 's';
		item->data.str = gpt_get_header_id(h);
		if (!item->data.str)
			rc = -ENOMEM;
		break;
	case GPT_LABELITEM_FIRSTLBA:
		item->name = _("First LBA");
		item->type = 'j';
		item->data.num64 = le64_to_cpu(h->first_usable_lba);
		break;
	case GPT_LABELITEM_LASTLBA:
		item->name = _("Last LBA");
		item->type = 'j';
		item->data.num64 = le64_to_cpu(h->last_usable_lba);
		break;
	case GPT_LABELITEM_ALTLBA:
		/* TRANSLATORS: The LBA (Logical Block Address) of the backup GPT header. */
		item->name = _("Alternative LBA");
		item->type = 'j';
		item->data.num64 = le64_to_cpu(h->alternative_lba);
		break;
	case GPT_LABELITEM_ENTRIESLBA:
		/* TRANSLATORS: The start of the array of partition entries. */
		item->name = _("Partition entries LBA");
		item->type = 'j';
		item->data.num64 = le64_to_cpu(h->partition_entry_lba);
		break;
	case GPT_LABELITEM_ENTRIESALLOC:
		item->name = _("Allocated partition entries");
		item->type = 'j';
		item->data.num64 = le32_to_cpu(h->npartition_entries);
		break;
	default:
		if (item->id < __FDISK_NLABELITEMS)
			rc = 1;	/* unsupported generic item */
		else
			rc = 2;	/* out of range */
		break;
	}

	return rc;
}

/*
 * Returns the number of partitions that are in use.
 */
static size_t partitions_in_use(struct fdisk_gpt_label *gpt)
{
	size_t i, used = 0;

	assert(gpt);
	assert(gpt->pheader);
	assert(gpt->ents);

	for (i = 0; i < gpt_get_nentries(gpt); i++) {
		struct gpt_entry *e = gpt_get_entry(gpt, i);

		if (gpt_entry_is_used(e))
			used++;
	}
	return used;
}


/*
 * Check if a partition is too big for the disk (sectors).
 * Returns the faulting partition number, otherwise 0.
 */
static uint32_t check_too_big_partitions(struct fdisk_gpt_label *gpt, uint64_t sectors)
{
	size_t i;

	assert(gpt);
	assert(gpt->pheader);
	assert(gpt->ents);

	for (i = 0; i < gpt_get_nentries(gpt); i++) {
		struct gpt_entry *e = gpt_get_entry(gpt, i);

		if (!gpt_entry_is_used(e))
			continue;
		if (gpt_partition_end(e) >= sectors)
			return i + 1;
	}

	return 0;
}

/*
 * Check if a partition ends before it begins
 * Returns the faulting partition number, otherwise 0.
 */
static uint32_t check_start_after_end_partitions(struct fdisk_gpt_label *gpt)
{
	size_t i;

	assert(gpt);
	assert(gpt->pheader);
	assert(gpt->ents);

	for (i = 0; i < gpt_get_nentries(gpt); i++) {
		struct gpt_entry *e = gpt_get_entry(gpt, i);

		if (!gpt_entry_is_used(e))
			continue;
		if (gpt_partition_start(e) > gpt_partition_end(e))
			return i + 1;
	}

	return 0;
}

/*
 * Check if partition e1 overlaps with partition e2.
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
 * Find any partitions that overlap.
 */
static uint32_t check_overlap_partitions(struct fdisk_gpt_label *gpt)
{
	size_t i, j;

	assert(gpt);
	assert(gpt->pheader);
	assert(gpt->ents);

	for (i = 0; i < gpt_get_nentries(gpt); i++)
		for (j = 0; j < i; j++) {
			struct gpt_entry *ei = gpt_get_entry(gpt, i);
			struct gpt_entry *ej = gpt_get_entry(gpt, j);

			if (!gpt_entry_is_used(ei) || !gpt_entry_is_used(ej))
				continue;
			if (partition_overlap(ei, ej)) {
				DBG(LABEL, ul_debug("GPT partitions overlap detected [%zu vs. %zu]", i, j));
				return i + 1;
			}
		}

	return 0;
}

/*
 * Find the first available block after the starting point; returns 0 if
 * there are no available blocks left, or error. From gdisk.
 */
static uint64_t find_first_available(struct fdisk_gpt_label *gpt, uint64_t start)
{
	int first_moved = 0;
	uint64_t first;
	uint64_t fu, lu;

	assert(gpt);
	assert(gpt->pheader);
	assert(gpt->ents);

	fu = le64_to_cpu(gpt->pheader->first_usable_lba);
	lu = le64_to_cpu(gpt->pheader->last_usable_lba);

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
		size_t i;

		first_moved = 0;
		for (i = 0; i < gpt_get_nentries(gpt); i++) {
			struct gpt_entry *e = gpt_get_entry(gpt, i);

			if (!gpt_entry_is_used(e))
				continue;
			if (first < gpt_partition_start(e))
				continue;
			if (first <= gpt_partition_end(e)) {
				first = gpt_partition_end(e) + 1;
				first_moved = 1;
			}
		}
	} while (first_moved == 1);

	if (first > lu)
		first = 0;

	return first;
}


/* Returns last available sector in the free space pointed to by start. From gdisk. */
static uint64_t find_last_free(struct fdisk_gpt_label *gpt, uint64_t start)
{
	size_t i;
	uint64_t nearest_start;

	assert(gpt);
	assert(gpt->pheader);
	assert(gpt->ents);

	nearest_start = le64_to_cpu(gpt->pheader->last_usable_lba);

	for (i = 0; i < gpt_get_nentries(gpt); i++) {
		struct gpt_entry *e = gpt_get_entry(gpt, i);
		uint64_t ps = gpt_partition_start(e);

		if (nearest_start > ps && ps > start)
			nearest_start = ps - 1ULL;
	}

	return nearest_start;
}

/* Returns the last free sector on the disk. From gdisk. */
static uint64_t find_last_free_sector(struct fdisk_gpt_label *gpt)
{
	int last_moved;
	uint64_t last = 0;

	assert(gpt);
	assert(gpt->pheader);
	assert(gpt->ents);

	/* start by assuming the last usable LBA is available */
	last = le64_to_cpu(gpt->pheader->last_usable_lba);
	do {
		size_t i;

		last_moved = 0;
		for (i = 0; i < gpt_get_nentries(gpt); i++) {
			struct gpt_entry *e = gpt_get_entry(gpt, i);

			if (last >= gpt_partition_start(e) &&
			    last <= gpt_partition_end(e)) {
				last = gpt_partition_start(e) - 1ULL;
				last_moved = 1;
			}
		}
	} while (last_moved == 1);

	return last;
}

/*
 * Finds the first available sector in the largest block of unallocated
 * space on the disk. Returns 0 if there are no available blocks left.
 * From gdisk.
 */
static uint64_t find_first_in_largest(struct fdisk_gpt_label *gpt)
{
	uint64_t start = 0, first_sect, last_sect;
	uint64_t segment_size, selected_size = 0, selected_segment = 0;

	assert(gpt);
	assert(gpt->pheader);
	assert(gpt->ents);

	do {
		first_sect = find_first_available(gpt, start);
		if (first_sect != 0) {
			last_sect = find_last_free(gpt, first_sect);
			segment_size = last_sect - first_sect + 1ULL;

			if (segment_size > selected_size) {
				selected_size = segment_size;
				selected_segment = first_sect;
			}
			start = last_sect + 1ULL;
		}
	} while (first_sect != 0);

	return selected_segment;
}

/*
 * Find the total number of free sectors, the number of segments in which
 * they reside, and the size of the largest of those segments. From gdisk.
 */
static uint64_t get_free_sectors(struct fdisk_context *cxt,
				 struct fdisk_gpt_label *gpt,
				 uint32_t *nsegments,
				 uint64_t *largest_segment)
{
	uint32_t num = 0;
	uint64_t first_sect, last_sect;
	uint64_t largest_seg = 0, segment_sz;
	uint64_t totfound = 0, start = 0; /* starting point for each search */

	if (!cxt->total_sectors)
		goto done;

	assert(gpt);
	assert(gpt->pheader);
	assert(gpt->ents);

	do {
		first_sect = find_first_available(gpt, start);
		if (first_sect) {
			last_sect = find_last_free(gpt, first_sect);
			segment_sz = last_sect - first_sect + 1;

			if (segment_sz > largest_seg)
				largest_seg = segment_sz;
			totfound += segment_sz;
			num++;
			start = last_sect + 1ULL;
		}
	} while (first_sect);

done:
	if (nsegments)
		*nsegments = num;
	if (largest_segment)
		*largest_segment = largest_seg;

	return totfound;
}

static int gpt_probe_label(struct fdisk_context *cxt)
{
	int mbr_type;
	struct fdisk_gpt_label *gpt;

	assert(cxt);
	assert(cxt->label);
	assert(fdisk_is_label(cxt, GPT));

	gpt = self_label(cxt);

	/* TODO: it would be nice to support scenario when GPT headers are OK,
	 *       but PMBR is corrupt */
	mbr_type = valid_pmbr(cxt);
	if (!mbr_type)
		goto failed;

	DBG(LABEL, ul_debug("found a %s MBR", mbr_type == GPT_MBR_PROTECTIVE ?
			    "protective" : "hybrid"));

	/* primary header */
	gpt->pheader = gpt_read_header(cxt, GPT_PRIMARY_PARTITION_TABLE_LBA,
				       &gpt->ents);

	if (gpt->pheader)
		/* primary OK, try backup from alternative LBA */
		gpt->bheader = gpt_read_header(cxt,
					le64_to_cpu(gpt->pheader->alternative_lba),
					NULL);
	else
		/* primary corrupted -- try last LBA */
		gpt->bheader = gpt_read_header(cxt, last_lba(cxt), &gpt->ents);

	if (!gpt->pheader && !gpt->bheader)
		goto failed;

	/* primary OK, backup corrupted -- recovery */
	if (gpt->pheader && !gpt->bheader) {
		fdisk_warnx(cxt, _("The backup GPT table is corrupt, but the "
				  "primary appears OK, so that will be used."));
		gpt->bheader = gpt_copy_header(cxt, gpt->pheader);
		if (!gpt->bheader)
			goto failed;
		gpt_recompute_crc(gpt->bheader, gpt->ents);
		fdisk_label_set_changed(cxt->label, 1);

	/* primary corrupted, backup OK -- recovery */
	} else if (!gpt->pheader && gpt->bheader) {
		fdisk_warnx(cxt, _("The primary GPT table is corrupt, but the "
				  "backup appears OK, so that will be used."));
		gpt->pheader = gpt_copy_header(cxt, gpt->bheader);
		if (!gpt->pheader)
			goto failed;
		gpt_recompute_crc(gpt->pheader, gpt->ents);
		fdisk_label_set_changed(cxt->label, 1);
	}

	/* The headers make be correct, but Backup do not have to be on the end
	 * of the device (due to device resize, etc.). Let's fix this issue. */
	if (le64_to_cpu(gpt->pheader->alternative_lba) > cxt->total_sectors ||
	    le64_to_cpu(gpt->pheader->alternative_lba) < cxt->total_sectors - 1ULL) {
		fdisk_warnx(cxt, _("The backup GPT table is not on the end of the device. "
				   "This problem will be corrected by write."));

		gpt_fix_alternative_lba(cxt, gpt);
		gpt_recompute_crc(gpt->bheader, gpt->ents);
		gpt_recompute_crc(gpt->pheader, gpt->ents);
		fdisk_label_set_changed(cxt->label, 1);
	}

	cxt->label->nparts_max = gpt_get_nentries(gpt);
	cxt->label->nparts_cur = partitions_in_use(gpt);
	return 1;
failed:
	DBG(LABEL, ul_debug("GPT probe failed"));
	gpt_deinit(cxt->label);
	return 0;
}

/*
 * Stolen from libblkid - can be removed once partition semantics
 * are added to the fdisk API.
 */
static char *encode_to_utf8(unsigned char *src, size_t count)
{
	uint16_t c;
	char *dest;
	size_t i, j;
	size_t len = count * 3 / 2;

	dest = calloc(1, len + 1);
	if (!dest)
		return NULL;

	for (j = i = 0; i + 2 <= count; i += 2) {
		/* always little endian */
		c = (src[i+1] << 8) | src[i];
		if (c == 0) {
			break;
		} else if (c < 0x80) {
			if (j+1 > len)
				break;
			dest[j++] = (uint8_t) c;
		} else if (c < 0x800) {
			if (j+2 > len)
				break;
			dest[j++] = (uint8_t) (0xc0 | (c >> 6));
			dest[j++] = (uint8_t) (0x80 | (c & 0x3f));
		} else {
			if (j+3 > len)
				break;
			dest[j++] = (uint8_t) (0xe0 | (c >> 12));
			dest[j++] = (uint8_t) (0x80 | ((c >> 6) & 0x3f));
			dest[j++] = (uint8_t) (0x80 | (c & 0x3f));
		}
	}

	return dest;
}

static int gpt_entry_attrs_to_string(struct gpt_entry *e, char **res)
{
	unsigned int n, count = 0;
	size_t l;
	char *bits, *p;
	uint64_t attrs;

	assert(e);
	assert(res);

	*res = NULL;
	attrs = e->attrs;
	if (!attrs)
		return 0;	/* no attributes at all */

	bits = (char *) &attrs;

	/* Note that sizeof() is correct here, we need separators between
	 * the strings so also count \0 is correct */
	*res = calloc(1, sizeof(GPT_ATTRSTR_NOBLOCK) +
			 sizeof(GPT_ATTRSTR_REQ) +
			 sizeof(GPT_ATTRSTR_LEGACY) +
			 sizeof("GUID:") + (GPT_ATTRBIT_GUID_COUNT * 3));
	if (!*res)
		return -errno;

	p = *res;
	if (isset(bits, GPT_ATTRBIT_REQ)) {
		memcpy(p, GPT_ATTRSTR_REQ, (l = sizeof(GPT_ATTRSTR_REQ)));
		p += l - 1;
	}
	if (isset(bits, GPT_ATTRBIT_NOBLOCK)) {
		if (p > *res)
			*p++ = ' ';
		memcpy(p, GPT_ATTRSTR_NOBLOCK, (l = sizeof(GPT_ATTRSTR_NOBLOCK)));
		p += l - 1;
	}
	if (isset(bits, GPT_ATTRBIT_LEGACY)) {
		if (p > *res)
			*p++ = ' ';
		memcpy(p, GPT_ATTRSTR_LEGACY, (l = sizeof(GPT_ATTRSTR_LEGACY)));
		p += l - 1;
	}

	for (n = GPT_ATTRBIT_GUID_FIRST;
	     n < GPT_ATTRBIT_GUID_FIRST + GPT_ATTRBIT_GUID_COUNT; n++) {

		if (!isset(bits, n))
			continue;
		if (!count) {
			if (p > *res)
				*p++ = ' ';
			p += sprintf(p, "GUID:%u", n);
		} else
			p += sprintf(p, ",%u", n);
		count++;
	}

	return 0;
}

static int gpt_entry_attrs_from_string(
			struct fdisk_context *cxt,
			struct gpt_entry *e,
			const char *str)
{
	const char *p = str;
	uint64_t attrs = 0;
	char *bits;

	assert(e);
	assert(p);

	DBG(LABEL, ul_debug("GPT: parsing string attributes '%s'", p));

	bits = (char *) &attrs;

	while (p && *p) {
		int bit = -1;

		while (isblank(*p)) p++;
		if (!*p)
			break;

		DBG(LABEL, ul_debug(" parsing item '%s'", p));

		if (strncmp(p, GPT_ATTRSTR_REQ,
					sizeof(GPT_ATTRSTR_REQ) - 1) == 0) {
			bit = GPT_ATTRBIT_REQ;
			p += sizeof(GPT_ATTRSTR_REQ) - 1;
		} else if (strncmp(p, GPT_ATTRSTR_REQ_TYPO,
					sizeof(GPT_ATTRSTR_REQ_TYPO) - 1) == 0) {
			bit = GPT_ATTRBIT_REQ;
			p += sizeof(GPT_ATTRSTR_REQ_TYPO) - 1;
		} else if (strncmp(p, GPT_ATTRSTR_LEGACY,
					sizeof(GPT_ATTRSTR_LEGACY) - 1) == 0) {
			bit = GPT_ATTRBIT_LEGACY;
			p += sizeof(GPT_ATTRSTR_LEGACY) - 1;
		} else if (strncmp(p, GPT_ATTRSTR_NOBLOCK,
					sizeof(GPT_ATTRSTR_NOBLOCK) - 1) == 0) {
			bit = GPT_ATTRBIT_NOBLOCK;
			p += sizeof(GPT_ATTRSTR_NOBLOCK) - 1;

		/* GUID:<bit> as well as <bit> */
		} else if (isdigit((unsigned char) *p)
			   || (strncmp(p, "GUID:", 5) == 0
			       && isdigit((unsigned char) *(p + 5)))) {
			char *end = NULL;

			if (*p == 'G')
				p += 5;

			errno = 0;
			bit = strtol(p, &end, 0);
			if (errno || !end || end == str
			    || bit < GPT_ATTRBIT_GUID_FIRST
			    || bit >= GPT_ATTRBIT_GUID_FIRST + GPT_ATTRBIT_GUID_COUNT)
				bit = -1;
			else
				p = end;
		}

		if (bit < 0) {
			fdisk_warnx(cxt, _("unsupported GPT attribute bit '%s'"), p);
			return -EINVAL;
		}

		if (*p && *p != ',' && !isblank(*p)) {
			fdisk_warnx(cxt, _("failed to parse GPT attribute string '%s'"), str);
			return -EINVAL;
		}

		setbit(bits, bit);

		while (isblank(*p)) p++;
		if (*p == ',')
			p++;
	}

	e->attrs = attrs;
	return 0;
}

static int gpt_get_partition(struct fdisk_context *cxt, size_t n,
			     struct fdisk_partition *pa)
{
	struct fdisk_gpt_label *gpt;
	struct gpt_entry *e;
	char u_str[UUID_STR_LEN];
	int rc = 0;
	struct gpt_guid guid;

	assert(cxt);
	assert(cxt->label);
	assert(fdisk_is_label(cxt, GPT));

	gpt = self_label(cxt);

	if (n >= gpt_get_nentries(gpt))
		return -EINVAL;

	gpt = self_label(cxt);
	e = gpt_get_entry(gpt, n);

	pa->used = gpt_entry_is_used(e) || gpt_partition_start(e);
	if (!pa->used)
		return 0;

	pa->start = gpt_partition_start(e);
	pa->size = gpt_partition_size(e);
	pa->type = gpt_partition_parttype(cxt, e);

	guid = e->partition_guid;
	if (guid_to_string(&guid, u_str)) {
		pa->uuid = strdup(u_str);
		if (!pa->uuid) {
			rc = -errno;
			goto done;
		}
	} else
		pa->uuid = NULL;

	rc = gpt_entry_attrs_to_string(e, &pa->attrs);
	if (rc)
		goto done;

	pa->name = encode_to_utf8((unsigned char *)e->name, sizeof(e->name));
	return 0;
done:
	fdisk_reset_partition(pa);
	return rc;
}


static int gpt_set_partition(struct fdisk_context *cxt, size_t n,
			     struct fdisk_partition *pa)
{
	struct fdisk_gpt_label *gpt;
	struct gpt_entry *e;
	int rc = 0;
	uint64_t start, end;

	assert(cxt);
	assert(cxt->label);
	assert(fdisk_is_label(cxt, GPT));

	gpt = self_label(cxt);

	if (n >= gpt_get_nentries(gpt))
		return -EINVAL;

	FDISK_INIT_UNDEF(start);
	FDISK_INIT_UNDEF(end);

	gpt = self_label(cxt);
	e = gpt_get_entry(gpt, n);

	if (pa->uuid) {
		char new_u[UUID_STR_LEN], old_u[UUID_STR_LEN];
		struct gpt_guid guid;

		guid = e->partition_guid;
		guid_to_string(&guid, old_u);
		rc = gpt_entry_set_uuid(e, pa->uuid);
		if (rc)
			return rc;
		guid = e->partition_guid;
		guid_to_string(&guid, new_u);
		fdisk_info(cxt, _("Partition UUID changed from %s to %s."),
			old_u, new_u);
	}

	if (pa->name) {
		int len;
		char *old = encode_to_utf8((unsigned char *)e->name, sizeof(e->name));
		len = gpt_entry_set_name(e, pa->name);
		if (len < 0)
			fdisk_warn(cxt, _("Failed to translate partition name, name not changed."));
		else
			fdisk_info(cxt, _("Partition name changed from '%s' to '%.*s'."),
				old, len, pa->name);
		free(old);
	}

	if (pa->type && pa->type->typestr) {
		struct gpt_guid typeid;

		rc = string_to_guid(pa->type->typestr, &typeid);
		if (rc)
			return rc;
		gpt_entry_set_type(e, &typeid);
	}
	if (pa->attrs) {
		rc = gpt_entry_attrs_from_string(cxt, e, pa->attrs);
		if (rc)
			return rc;
	}

	if (fdisk_partition_has_start(pa))
		start = pa->start;
	if (fdisk_partition_has_size(pa) || fdisk_partition_has_start(pa)) {
		uint64_t xstart = fdisk_partition_has_start(pa) ? pa->start : gpt_partition_start(e);
		uint64_t xsize  = fdisk_partition_has_size(pa)  ? pa->size  : gpt_partition_size(e);
		end = xstart + xsize - 1ULL;
	}

	if (!FDISK_IS_UNDEF(start)) {
		if (start < le64_to_cpu(gpt->pheader->first_usable_lba)) {
			fdisk_warnx(cxt, _("The start of the partition understeps FirstUsableLBA."));
			return -EINVAL;
		}
		e->lba_start = cpu_to_le64(start);
	}
	if (!FDISK_IS_UNDEF(end)) {
		if (end > le64_to_cpu(gpt->pheader->last_usable_lba)) {
			fdisk_warnx(cxt, _("The end of the partition oversteps LastUsableLBA."));
			return -EINVAL;
		}
		e->lba_end = cpu_to_le64(end);
	}
	gpt_recompute_crc(gpt->pheader, gpt->ents);
	gpt_recompute_crc(gpt->bheader, gpt->ents);

	fdisk_label_set_changed(cxt->label, 1);
	return rc;
}



/*
 * Write partitions.
 * Returns 0 on success, or corresponding error otherwise.
 */
static int gpt_write_partitions(struct fdisk_context *cxt,
				struct gpt_header *header, unsigned char *ents)
{
	off_t offset = (off_t) le64_to_cpu(header->partition_entry_lba) * cxt->sector_size;
	size_t towrite = 0;
	ssize_t ssz;
	int rc;

	rc = gpt_sizeof_ents(header, &towrite);
	if (rc)
		return rc;

	if (offset != lseek(cxt->dev_fd, offset, SEEK_SET))
		return -errno;

	ssz = write(cxt->dev_fd, ents, towrite);
	if (ssz < 0 || (ssize_t) towrite != ssz)
		return -errno;

	return 0;
}

/*
 * Write a GPT header to a specified LBA.
 *
 * We read all sector, so we have to write all sector back
 * to the device -- never ever rely on sizeof(struct gpt_header)!
 *
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
	struct gpt_legacy_mbr *pmbr;

	assert(cxt);
	assert(cxt->firstsector);

	pmbr = (struct gpt_legacy_mbr *) cxt->firstsector;

	/* zero out the legacy partitions */
	memset(pmbr->partition_record, 0, sizeof(pmbr->partition_record));

	pmbr->signature = cpu_to_le16(MSDOS_MBR_SIGNATURE);
	pmbr->partition_record[0].os_type      = EFI_PMBR_OSTYPE;
	pmbr->partition_record[0].start_sector = 2;
	pmbr->partition_record[0].end_head     = 0xFF;
	pmbr->partition_record[0].end_sector   = 0xFF;
	pmbr->partition_record[0].end_track    = 0xFF;
	pmbr->partition_record[0].starting_lba = cpu_to_le32(1);

	/*
	 * Set size_in_lba to the size of the disk minus one. If the size of the disk
	 * is too large to be represented by a 32bit LBA (2Tb), set it to 0xFFFFFFFF.
	 */
	if (cxt->total_sectors - 1ULL > 0xFFFFFFFFULL)
		pmbr->partition_record[0].size_in_lba = cpu_to_le32(0xFFFFFFFF);
	else
		pmbr->partition_record[0].size_in_lba =
			cpu_to_le32((uint32_t) (cxt->total_sectors - 1ULL));

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
	struct fdisk_gpt_label *gpt;
	int mbr_type;

	assert(cxt);
	assert(cxt->label);
	assert(fdisk_is_label(cxt, GPT));

	gpt = self_label(cxt);
	mbr_type = valid_pmbr(cxt);

	/* check that disk is big enough to handle the backup header */
	if (le64_to_cpu(gpt->pheader->alternative_lba) > cxt->total_sectors)
		goto err0;

	/* check that the backup header is properly placed */
	if (le64_to_cpu(gpt->pheader->alternative_lba) < cxt->total_sectors - 1ULL)
		goto err0;

	if (check_overlap_partitions(gpt))
		goto err0;

	/* recompute CRCs for both headers */
	gpt_recompute_crc(gpt->pheader, gpt->ents);
	gpt_recompute_crc(gpt->bheader, gpt->ents);

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
	if (gpt_write_partitions(cxt, gpt->bheader, gpt->ents) != 0)
		goto err1;
	if (gpt_write_header(cxt, gpt->bheader,
			     le64_to_cpu(gpt->pheader->alternative_lba)) != 0)
		goto err1;
	if (gpt_write_partitions(cxt, gpt->pheader, gpt->ents) != 0)
		goto err1;
	if (gpt_write_header(cxt, gpt->pheader, GPT_PRIMARY_PARTITION_TABLE_LBA) != 0)
		goto err1;

	if (mbr_type == GPT_MBR_HYBRID)
		fdisk_warnx(cxt, _("The device contains hybrid MBR -- writing GPT only. "
				   "You have to sync the MBR manually."));
	else if (gpt_write_pmbr(cxt) != 0)
		goto err1;

	DBG(LABEL, ul_debug("GPT write success"));
	return 0;
err0:
	DBG(LABEL, ul_debug("GPT write failed: incorrect input"));
	errno = EINVAL;
	return -EINVAL;
err1:
	DBG(LABEL, ul_debug("GPT write failed: %m"));
	return -errno;
}

/*
 * Verify data integrity and report any found problems for:
 *   - primary and backup header validations
 *   - partition validations
 */
static int gpt_verify_disklabel(struct fdisk_context *cxt)
{
	int nerror = 0;
	unsigned int ptnum;
	struct fdisk_gpt_label *gpt;

	assert(cxt);
	assert(cxt->label);
	assert(fdisk_is_label(cxt, GPT));

	gpt = self_label(cxt);
	if (!gpt)
		return -EINVAL;

	if (!gpt->bheader) {
		nerror++;
		fdisk_warnx(cxt, _("Disk does not contain a valid backup header."));
	}

	if (!gpt_check_header_crc(gpt->pheader, gpt->ents)) {
		nerror++;
		fdisk_warnx(cxt, _("Invalid primary header CRC checksum."));
	}
	if (gpt->bheader && !gpt_check_header_crc(gpt->bheader, gpt->ents)) {
		nerror++;
		fdisk_warnx(cxt, _("Invalid backup header CRC checksum."));
	}

	if (!gpt_check_entryarr_crc(gpt->pheader, gpt->ents)) {
		nerror++;
		fdisk_warnx(cxt, _("Invalid partition entry checksum."));
	}

	if (!gpt_check_lba_sanity(cxt, gpt->pheader)) {
		nerror++;
		fdisk_warnx(cxt, _("Invalid primary header LBA sanity checks."));
	}
	if (gpt->bheader && !gpt_check_lba_sanity(cxt, gpt->bheader)) {
		nerror++;
		fdisk_warnx(cxt, _("Invalid backup header LBA sanity checks."));
	}

	if (le64_to_cpu(gpt->pheader->my_lba) != GPT_PRIMARY_PARTITION_TABLE_LBA) {
		nerror++;
		fdisk_warnx(cxt, _("MyLBA mismatch with real position at primary header."));
	}
	if (gpt->bheader && le64_to_cpu(gpt->bheader->my_lba) != last_lba(cxt)) {
		nerror++;
		fdisk_warnx(cxt, _("MyLBA mismatch with real position at backup header."));

	}
	if (le64_to_cpu(gpt->pheader->alternative_lba) >= cxt->total_sectors) {
		nerror++;
		fdisk_warnx(cxt, _("Disk is too small to hold all data."));
	}

	/*
	 * if the GPT is the primary table, check the alternateLBA
	 * to see if it is a valid GPT
	 */
	if (gpt->bheader && (le64_to_cpu(gpt->pheader->my_lba) !=
			     le64_to_cpu(gpt->bheader->alternative_lba))) {
		nerror++;
		fdisk_warnx(cxt, _("Primary and backup header mismatch."));
	}

	ptnum = check_overlap_partitions(gpt);
	if (ptnum) {
		nerror++;
		fdisk_warnx(cxt, _("Partition %u overlaps with partition %u."),
				ptnum, ptnum+1);
	}

	ptnum = check_too_big_partitions(gpt, cxt->total_sectors);
	if (ptnum) {
		nerror++;
		fdisk_warnx(cxt, _("Partition %u is too big for the disk."),
				ptnum);
	}

	ptnum = check_start_after_end_partitions(gpt);
	if (ptnum) {
		nerror++;
		fdisk_warnx(cxt, _("Partition %u ends before it starts."),
				ptnum);
	}

	if (!nerror) { /* yay :-) */
		uint32_t nsegments = 0;
		uint64_t free_sectors = 0, largest_segment = 0;
		char *strsz = NULL;

		fdisk_info(cxt, _("No errors detected."));
		fdisk_info(cxt, _("Header version: %s"), gpt_get_header_revstr(gpt->pheader));
		fdisk_info(cxt, _("Using %zu out of %zu partitions."),
		       partitions_in_use(gpt),
		       gpt_get_nentries(gpt));

		free_sectors = get_free_sectors(cxt, gpt, &nsegments, &largest_segment);
		if (largest_segment)
			strsz = size_to_human_string(SIZE_SUFFIX_SPACE | SIZE_SUFFIX_3LETTER,
					largest_segment * cxt->sector_size);

		fdisk_info(cxt,
			   P_("A total of %ju free sectors is available in %u segment.",
			      "A total of %ju free sectors is available in %u segments "
			      "(the largest is %s).", nsegments),
			   free_sectors, nsegments, strsz);
		free(strsz);

	} else
		fdisk_warnx(cxt,
			P_("%d error detected.", "%d errors detected.", nerror),
			nerror);

	return 0;
}

/* Delete a single GPT partition, specified by partnum. */
static int gpt_delete_partition(struct fdisk_context *cxt,
				size_t partnum)
{
	struct fdisk_gpt_label *gpt;

	assert(cxt);
	assert(cxt->label);
	assert(fdisk_is_label(cxt, GPT));

	gpt = self_label(cxt);

	if (partnum >= cxt->label->nparts_max)
		return -EINVAL;

	if (!gpt_entry_is_used(gpt_get_entry(gpt, partnum)))
		return -EINVAL;

	/* hasta la vista, baby! */
	gpt_zeroize_entry(gpt, partnum);

	gpt_recompute_crc(gpt->pheader, gpt->ents);
	gpt_recompute_crc(gpt->bheader, gpt->ents);
	cxt->label->nparts_cur--;
	fdisk_label_set_changed(cxt->label, 1);

	return 0;
}


/* Performs logical checks to add a new partition entry */
static int gpt_add_partition(
		struct fdisk_context *cxt,
		struct fdisk_partition *pa,
		size_t *partno)
{
	uint64_t user_f, user_l;	/* user input ranges for first and last sectors */
	uint64_t disk_f, disk_l;	/* first and last available sector ranges on device*/
	uint64_t dflt_f, dflt_l;	/* largest segment (default) */
	struct gpt_guid typeid;
	struct fdisk_gpt_label *gpt;
	struct gpt_header *pheader;
	struct gpt_entry *e;
	struct fdisk_ask *ask = NULL;
	size_t partnum;
	int rc;

	assert(cxt);
	assert(cxt->label);
	assert(fdisk_is_label(cxt, GPT));

	gpt = self_label(cxt);

	assert(gpt);
	assert(gpt->pheader);
	assert(gpt->ents);

	pheader = gpt->pheader;

	rc = fdisk_partition_next_partno(pa, cxt, &partnum);
	if (rc) {
		DBG(LABEL, ul_debug("GPT failed to get next partno"));
		return rc;
	}

	assert(partnum < gpt_get_nentries(gpt));

	if (gpt_entry_is_used(gpt_get_entry(gpt, partnum))) {
		fdisk_warnx(cxt, _("Partition %zu is already defined.  "
			           "Delete it before re-adding it."), partnum +1);
		return -ERANGE;
	}
	if (gpt_get_nentries(gpt) == partitions_in_use(gpt)) {
		fdisk_warnx(cxt, _("All partitions are already in use."));
		return -ENOSPC;
	}
	if (!get_free_sectors(cxt, gpt, NULL, NULL)) {
		fdisk_warnx(cxt, _("No free sectors available."));
		return -ENOSPC;
	}

	rc = string_to_guid(pa && pa->type && pa->type->typestr ?
				pa->type->typestr:
				GPT_DEFAULT_ENTRY_TYPE, &typeid);
	if (rc)
		return rc;

	disk_f = find_first_available(gpt, le64_to_cpu(pheader->first_usable_lba));
	e = gpt_get_entry(gpt, 0);

	/* if first sector no explicitly defined then ignore small gaps before
	 * the first partition */
	if ((!pa || !fdisk_partition_has_start(pa))
	    && gpt_entry_is_used(e)
	    && disk_f < gpt_partition_start(e)) {

		do {
			uint64_t x;
			DBG(LABEL, ul_debug("testing first sector %"PRIu64"", disk_f));
			disk_f = find_first_available(gpt, disk_f);
			if (!disk_f)
				break;
			x = find_last_free(gpt, disk_f);
			if (x - disk_f >= cxt->grain / cxt->sector_size)
				break;
			DBG(LABEL, ul_debug("first sector %"PRIu64" addresses to small space, continue...", disk_f));
			disk_f = x + 1ULL;
		} while(1);

		if (disk_f == 0)
			disk_f = find_first_available(gpt, le64_to_cpu(pheader->first_usable_lba));
	}

	e = NULL;
	disk_l = find_last_free_sector(gpt);

	/* the default is the largest free space */
	dflt_f = find_first_in_largest(gpt);
	dflt_l = find_last_free(gpt, dflt_f);

	/* align the default in range <dflt_f,dflt_l>*/
	dflt_f = fdisk_align_lba_in_range(cxt, dflt_f, dflt_f, dflt_l);

	/* first sector */
	if (pa && pa->start_follow_default) {
		user_f = dflt_f;

	} else if (pa && fdisk_partition_has_start(pa)) {
		DBG(LABEL, ul_debug("first sector defined: %ju",  (uintmax_t)pa->start));
		if (pa->start != find_first_available(gpt, pa->start)) {
			fdisk_warnx(cxt, _("Sector %ju already used."),  (uintmax_t)pa->start);
			return -ERANGE;
		}
		user_f = pa->start;
	} else {
		/*  ask by dialog */
		for (;;) {
			if (!ask)
				ask = fdisk_new_ask();
			else
				fdisk_reset_ask(ask);
			if (!ask)
				return -ENOMEM;

			/* First sector */
			fdisk_ask_set_query(ask, _("First sector"));
			fdisk_ask_set_type(ask, FDISK_ASKTYPE_NUMBER);
			fdisk_ask_number_set_low(ask,     disk_f);	/* minimal */
			fdisk_ask_number_set_default(ask, dflt_f);	/* default */
			fdisk_ask_number_set_high(ask,    disk_l);	/* maximal */

			rc = fdisk_do_ask(cxt, ask);
			if (rc)
				goto done;

			user_f = fdisk_ask_number_get_result(ask);
			if (user_f != find_first_available(gpt, user_f)) {
				fdisk_warnx(cxt, _("Sector %ju already used."), user_f);
				continue;
			}
			break;
		}
	}


	/* Last sector */
	dflt_l = find_last_free(gpt, user_f);

	if (pa && pa->end_follow_default) {
		user_l = dflt_l;

	} else if (pa && fdisk_partition_has_size(pa)) {
		user_l = user_f + pa->size - 1;
		DBG(LABEL, ul_debug("size defined: %ju, end: %"PRIu64" (last possible: %"PRIu64")",
					 (uintmax_t)pa->size, user_l, dflt_l));

		if (user_l != dflt_l
		    && !pa->size_explicit
		    && alignment_required(cxt)
		    && user_l - user_f > (cxt->grain / fdisk_get_sector_size(cxt))) {

			user_l = fdisk_align_lba_in_range(cxt, user_l, user_f, dflt_l);
			if (user_l > user_f)
				user_l -= 1ULL;
		}
	} else {
		for (;;) {
			if (!ask)
				ask = fdisk_new_ask();
			else
				fdisk_reset_ask(ask);
			if (!ask)
				return -ENOMEM;

			fdisk_ask_set_query(ask, _("Last sector, +/-sectors or +/-size{K,M,G,T,P}"));
			fdisk_ask_set_type(ask, FDISK_ASKTYPE_OFFSET);
			fdisk_ask_number_set_low(ask,     user_f);	/* minimal */
			fdisk_ask_number_set_default(ask, dflt_l);	/* default */
			fdisk_ask_number_set_high(ask,    dflt_l);	/* maximal */
			fdisk_ask_number_set_base(ask,    user_f);	/* base for relative input */
			fdisk_ask_number_set_unit(ask,    cxt->sector_size);
			fdisk_ask_number_set_wrap_negative(ask, 1);	/* wrap negative around high */

			rc = fdisk_do_ask(cxt, ask);
			if (rc)
				goto done;

			user_l = fdisk_ask_number_get_result(ask);
			if (fdisk_ask_number_is_relative(ask)) {
				user_l = fdisk_align_lba_in_range(cxt, user_l, user_f, dflt_l);
				if (user_l > user_f)
					user_l -= 1ULL;
			}

			if (user_l >= user_f && user_l <= disk_l)
				break;

			fdisk_warnx(cxt, _("Value out of range."));
		}
	}


	if (user_f > user_l || partnum >= cxt->label->nparts_max) {
		fdisk_warnx(cxt, _("Could not create partition %zu"), partnum + 1);
		rc = -EINVAL;
		goto done;
	}

	/* Be paranoid and check against on-disk setting rather than against libfdisk cxt */
	if (user_l > le64_to_cpu(pheader->last_usable_lba)) {
		fdisk_warnx(cxt, _("The last usable GPT sector is %ju, but %ju is requested."),
				le64_to_cpu(pheader->last_usable_lba), user_l);
		rc = -EINVAL;
		goto done;
	}

	if (user_f < le64_to_cpu(pheader->first_usable_lba)) {
		fdisk_warnx(cxt, _("The first usable GPT sector is %ju, but %ju is requested."),
				le64_to_cpu(pheader->first_usable_lba), user_f);
		rc = -EINVAL;
		goto done;
	}

	assert(!FDISK_IS_UNDEF(user_l));
	assert(!FDISK_IS_UNDEF(user_f));
	assert(partnum < gpt_get_nentries(gpt));

	e = gpt_get_entry(gpt, partnum);
	e->lba_end = cpu_to_le64(user_l);
	e->lba_start = cpu_to_le64(user_f);

	gpt_entry_set_type(e, &typeid);

	if (pa && pa->uuid) {
		/* Sometimes it's necessary to create a copy of the PT and
		 * reuse already defined UUID
		 */
		rc = gpt_entry_set_uuid(e, pa->uuid);
		if (rc)
			goto done;
	} else {
		/* Any time a new partition entry is created a new GUID must be
		 * generated for that partition, and every partition is guaranteed
		 * to have a unique GUID.
		 */
		struct gpt_guid guid;

		uuid_generate_random((unsigned char *) &e->partition_guid);
		guid = e->partition_guid;
		swap_efi_guid(&guid);
	}

	if (pa && pa->name && *pa->name)
		gpt_entry_set_name(e, pa->name);
	if (pa && pa->attrs)
		gpt_entry_attrs_from_string(cxt, e, pa->attrs);

	DBG(LABEL, ul_debug("GPT new partition: partno=%zu, start=%"PRIu64", end=%"PRIu64", size=%"PRIu64"",
				partnum,
				gpt_partition_start(e),
				gpt_partition_end(e),
				gpt_partition_size(e)));

	gpt_recompute_crc(gpt->pheader, gpt->ents);
	gpt_recompute_crc(gpt->bheader, gpt->ents);

	/* report result */
	{
		struct fdisk_parttype *t;

		cxt->label->nparts_cur++;
		fdisk_label_set_changed(cxt->label, 1);

		t = gpt_partition_parttype(cxt, e);
		fdisk_info_new_partition(cxt, partnum + 1, user_f, user_l, t);
		fdisk_unref_parttype(t);
	}

	rc = 0;
	if (partno)
		*partno = partnum;
done:
	fdisk_unref_ask(ask);
	return rc;
}

/*
 * Create a new GPT disklabel - destroys any previous data.
 */
static int gpt_create_disklabel(struct fdisk_context *cxt)
{
	int rc = 0;
	size_t esz = 0;
	char str[UUID_STR_LEN];
	struct fdisk_gpt_label *gpt;
	struct gpt_guid guid;

	assert(cxt);
	assert(cxt->label);
	assert(fdisk_is_label(cxt, GPT));

	gpt = self_label(cxt);

	/* label private stuff has to be empty, see gpt_deinit() */
	assert(gpt->pheader == NULL);
	assert(gpt->bheader == NULL);

	/*
	 * When no header, entries or pmbr is set, we're probably
	 * dealing with a new, empty disk - so always allocate memory
	 * to deal with the data structures whatever the case is.
	 */
	rc = gpt_mknew_pmbr(cxt);
	if (rc < 0)
		goto done;

	assert(cxt->sector_size >= sizeof(struct gpt_header));

	/* primary */
	gpt->pheader = calloc(1, cxt->sector_size);
	if (!gpt->pheader) {
		rc = -ENOMEM;
		goto done;
	}
	rc = gpt_mknew_header(cxt, gpt->pheader, GPT_PRIMARY_PARTITION_TABLE_LBA);
	if (rc < 0)
		goto done;

	/* backup ("copy" primary) */
	gpt->bheader = calloc(1, cxt->sector_size);
	if (!gpt->bheader) {
		rc = -ENOMEM;
		goto done;
	}
	rc = gpt_mknew_header_from_bkp(cxt, gpt->bheader,
			last_lba(cxt), gpt->pheader);
	if (rc < 0)
		goto done;

	rc = gpt_sizeof_ents(gpt->pheader, &esz);
	if (rc)
		goto done;
	gpt->ents = calloc(1, esz);
	if (!gpt->ents) {
		rc = -ENOMEM;
		goto done;
	}
	gpt_recompute_crc(gpt->pheader, gpt->ents);
	gpt_recompute_crc(gpt->bheader, gpt->ents);

	cxt->label->nparts_max = gpt_get_nentries(gpt);
	cxt->label->nparts_cur = 0;

	guid = gpt->pheader->disk_guid;
	guid_to_string(&guid, str);
	fdisk_label_set_changed(cxt->label, 1);
	fdisk_info(cxt, _("Created a new GPT disklabel (GUID: %s)."), str);
done:
	return rc;
}

static int gpt_set_disklabel_id(struct fdisk_context *cxt)
{
	struct fdisk_gpt_label *gpt;
	struct gpt_guid uuid;
	char *str, *old, *new;
	int rc;

	assert(cxt);
	assert(cxt->label);
	assert(fdisk_is_label(cxt, GPT));

	gpt = self_label(cxt);
	if (fdisk_ask_string(cxt,
			_("Enter new disk UUID (in 8-4-4-4-12 format)"), &str))
		return -EINVAL;

	rc = string_to_guid(str, &uuid);
	free(str);

	if (rc) {
		fdisk_warnx(cxt, _("Failed to parse your UUID."));
		return rc;
	}

	old = gpt_get_header_id(gpt->pheader);

	gpt->pheader->disk_guid = uuid;
	gpt->bheader->disk_guid = uuid;

	gpt_recompute_crc(gpt->pheader, gpt->ents);
	gpt_recompute_crc(gpt->bheader, gpt->ents);

	new = gpt_get_header_id(gpt->pheader);

	fdisk_info(cxt, _("Disk identifier changed from %s to %s."), old, new);

	free(old);
	free(new);
	fdisk_label_set_changed(cxt->label, 1);
	return 0;
}

static int gpt_check_table_overlap(struct fdisk_context *cxt,
				   uint64_t first_usable,
				   uint64_t last_usable)
{
	struct fdisk_gpt_label *gpt = self_label(cxt);
	size_t i;
	int rc = 0;

	/* First check if there's enough room for the table. last_lba may have wrapped */
	if (first_usable > cxt->total_sectors || /* far too little space */
	    last_usable > cxt->total_sectors || /* wrapped */
	    first_usable > last_usable) { /* too little space */
		fdisk_warnx(cxt, _("Not enough space for new partition table!"));
		return -ENOSPC;
	}

	/* check that all partitions fit in the remaining space */
	for (i = 0; i < gpt_get_nentries(gpt); i++) {
		struct gpt_entry *e = gpt_get_entry(gpt, i);

		if (!gpt_entry_is_used(e))
		        continue;
		if (gpt_partition_start(e) < first_usable) {
			fdisk_warnx(cxt, _("Partition #%zu out of range (minimal start is %"PRIu64" sectors)"),
		                    i + 1, first_usable);
			rc = -EINVAL;
		}
		if (gpt_partition_end(e) > last_usable) {
			fdisk_warnx(cxt, _("Partition #%zu out of range (maximal end is %"PRIu64" sectors)"),
		                    i + 1, last_usable - 1ULL);
			rc = -EINVAL;
		}
	}
	return rc;
}

/**
 * fdisk_gpt_set_npartitions:
 * @cxt: context
 * @entries: new size
 *
 * Elarge GPT entries array if possible. The function check if an existing
 * partition does not overlap the entries array area. If yes, then it report
 * warning and returns -EINVAL.
 *
 * Returns: 0 on success, < 0 on error.
 * Since: 2.29
 */
int fdisk_gpt_set_npartitions(struct fdisk_context *cxt, uint32_t entries)
{
	struct fdisk_gpt_label *gpt;
	size_t old_size, new_size;
	uint32_t old;
	uint64_t first_usable, last_usable;
	int rc;

	assert(cxt);
	assert(cxt->label);

	if (!fdisk_is_label(cxt, GPT))
		return -EINVAL;

	gpt = self_label(cxt);

	old = le32_to_cpu(gpt->pheader->npartition_entries);
	if (old == entries)
		return 0;	/* do nothing, say nothing */

	/* calculate the size (bytes) of the entries array */
	rc = gpt_calculate_sizeof_ents(gpt->pheader, entries, &new_size);
	if (rc) {
		fdisk_warnx(cxt, _("The number of the partition has to be smaller than %zu."),
				UINT32_MAX / le32_to_cpu(gpt->pheader->sizeof_partition_entry));
		return rc;
	}

	rc = gpt_calculate_sizeof_ents(gpt->pheader, old, &old_size);
	if (rc)
		return rc;

	/* calculate new range of usable LBAs */
	first_usable = (uint64_t) (new_size / cxt->sector_size) + 2ULL;
	last_usable = cxt->total_sectors - 2ULL - (uint64_t) (new_size / cxt->sector_size);

	/* if expanding the table, first check that everything fits,
	 * then allocate more memory and zero. */
	if (entries > old) {
		unsigned char *ents;

		rc = gpt_check_table_overlap(cxt, first_usable, last_usable);
		if (rc)
			return rc;
		ents = realloc(gpt->ents, new_size);
		if (!ents) {
			fdisk_warnx(cxt, _("Cannot allocate memory!"));
			return -ENOMEM;
		}
		memset(ents + old_size, 0, new_size - old_size);
		gpt->ents = ents;
	}

	/* everything's ok, apply the new size */
	gpt->pheader->npartition_entries = cpu_to_le32(entries);
	gpt->bheader->npartition_entries = cpu_to_le32(entries);

	/* usable LBA addresses will have changed */
	fdisk_set_first_lba(cxt, first_usable);
	fdisk_set_last_lba(cxt, last_usable);
	gpt->pheader->first_usable_lba = cpu_to_le64(first_usable);
	gpt->bheader->first_usable_lba = cpu_to_le64(first_usable);
	gpt->pheader->last_usable_lba = cpu_to_le64(last_usable);
	gpt->bheader->last_usable_lba = cpu_to_le64(last_usable);


	/* The backup header must be recalculated */
	gpt_mknew_header_common(cxt, gpt->bheader, le64_to_cpu(gpt->pheader->alternative_lba));

	/* CRCs will have changed */
	gpt_recompute_crc(gpt->pheader, gpt->ents);
	gpt_recompute_crc(gpt->bheader, gpt->ents);

	/* update library info */
	cxt->label->nparts_max = gpt_get_nentries(gpt);

	fdisk_info(cxt, _("Partition table length changed from %"PRIu32" to %"PRIu64"."), old, entries);

	fdisk_label_set_changed(cxt->label, 1);
	return 0;
}

static int gpt_part_is_used(struct fdisk_context *cxt, size_t i)
{
	struct fdisk_gpt_label *gpt;
	struct gpt_entry *e;

	assert(cxt);
	assert(cxt->label);
	assert(fdisk_is_label(cxt, GPT));

	gpt = self_label(cxt);

	if (i >= gpt_get_nentries(gpt))
		return 0;

	e = gpt_get_entry(gpt, i);

	return gpt_entry_is_used(e) || gpt_partition_start(e);
}

/**
 * fdisk_gpt_is_hybrid:
 * @cxt: context
 *
 * The regular GPT contains PMBR (dummy protective MBR) where the protective
 * MBR does not address any partitions.
 *
 * Hybrid GPT contains regular MBR where this partition table addresses the
 * same partitions as GPT. It's recommended to not use hybrid GPT due to MBR
 * limits.
 *
 * The libfdisk does not provide functionality to sync GPT and MBR, you have to
 * directly access and modify (P)MBR (see fdisk_new_nested_context()).
 *
 * Returns: 1 if partition table detected as hybrid otherwise return 0
 */
int fdisk_gpt_is_hybrid(struct fdisk_context *cxt)
{
	assert(cxt);
	return valid_pmbr(cxt) == GPT_MBR_HYBRID;
}

/**
 * fdisk_gpt_get_partition_attrs:
 * @cxt: context
 * @partnum: partition number
 * @attrs: GPT partition attributes
 *
 * Sets @attrs for the given partition
 *
 * Returns: 0 on success, <0 on error.
 */
int fdisk_gpt_get_partition_attrs(
		struct fdisk_context *cxt,
		size_t partnum,
		uint64_t *attrs)
{
	struct fdisk_gpt_label *gpt;

	assert(cxt);
	assert(cxt->label);

	if (!fdisk_is_label(cxt, GPT))
		return -EINVAL;

	gpt = self_label(cxt);

	if (partnum >= gpt_get_nentries(gpt))
		return -EINVAL;

	*attrs = le64_to_cpu(gpt_get_entry(gpt, partnum)->attrs);
	return 0;
}

/**
 * fdisk_gpt_set_partition_attrs:
 * @cxt: context
 * @partnum: partition number
 * @attrs: GPT partition attributes
 *
 * Sets the GPT partition attributes field to @attrs.
 *
 * Returns: 0 on success, <0 on error.
 */
int fdisk_gpt_set_partition_attrs(
		struct fdisk_context *cxt,
		size_t partnum,
		uint64_t attrs)
{
	struct fdisk_gpt_label *gpt;

	assert(cxt);
	assert(cxt->label);

	if (!fdisk_is_label(cxt, GPT))
		return -EINVAL;

	DBG(LABEL, ul_debug("GPT entry attributes change requested partno=%zu", partnum));
	gpt = self_label(cxt);

	if (partnum >= gpt_get_nentries(gpt))
		return -EINVAL;

	gpt_get_entry(gpt, partnum)->attrs = cpu_to_le64(attrs);
	fdisk_info(cxt, _("The attributes on partition %zu changed to 0x%016" PRIx64 "."),
			partnum + 1, attrs);

	gpt_recompute_crc(gpt->pheader, gpt->ents);
	gpt_recompute_crc(gpt->bheader, gpt->ents);
	fdisk_label_set_changed(cxt->label, 1);
	return 0;
}

static int gpt_toggle_partition_flag(
		struct fdisk_context *cxt,
		size_t i,
		unsigned long flag)
{
	struct fdisk_gpt_label *gpt;
	struct gpt_entry *e;
	uint64_t attrs;
	uintmax_t tmp;
	char *bits;
	const char *name = NULL;
	int bit = -1, rc;

	assert(cxt);
	assert(cxt->label);
	assert(fdisk_is_label(cxt, GPT));

	DBG(LABEL, ul_debug("GPT entry attribute change requested partno=%zu", i));
	gpt = self_label(cxt);

	if (i >= gpt_get_nentries(gpt))
		return -EINVAL;

	e = gpt_get_entry(gpt, i);
	attrs = e->attrs;
	bits = (char *) &attrs;

	switch (flag) {
	case GPT_FLAG_REQUIRED:
		bit = GPT_ATTRBIT_REQ;
		name = GPT_ATTRSTR_REQ;
		break;
	case GPT_FLAG_NOBLOCK:
		bit = GPT_ATTRBIT_NOBLOCK;
		name = GPT_ATTRSTR_NOBLOCK;
		break;
	case GPT_FLAG_LEGACYBOOT:
		bit = GPT_ATTRBIT_LEGACY;
		name = GPT_ATTRSTR_LEGACY;
		break;
	case GPT_FLAG_GUIDSPECIFIC:
		rc = fdisk_ask_number(cxt, 48, 48, 63, _("Enter GUID specific bit"), &tmp);
		if (rc)
			return rc;
		bit = tmp;
		break;
	default:
		/* already specified PT_FLAG_GUIDSPECIFIC bit */
		if (flag >= 48 && flag <= 63) {
			bit = flag;
			flag = GPT_FLAG_GUIDSPECIFIC;
		}
		break;
	}

	if (bit < 0) {
		fdisk_warnx(cxt, _("failed to toggle unsupported bit %lu"), flag);
		return -EINVAL;
	}

	if (!isset(bits, bit))
		setbit(bits, bit);
	else
		clrbit(bits, bit);

	e->attrs = attrs;

	if (flag == GPT_FLAG_GUIDSPECIFIC)
		fdisk_info(cxt, isset(bits, bit) ?
			_("The GUID specific bit %d on partition %zu is enabled now.") :
			_("The GUID specific bit %d on partition %zu is disabled now."),
			bit, i + 1);
	else
		fdisk_info(cxt, isset(bits, bit) ?
			_("The %s flag on partition %zu is enabled now.") :
			_("The %s flag on partition %zu is disabled now."),
			name, i + 1);

	gpt_recompute_crc(gpt->pheader, gpt->ents);
	gpt_recompute_crc(gpt->bheader, gpt->ents);
	fdisk_label_set_changed(cxt->label, 1);
	return 0;
}

static int gpt_entry_cmp_start(const void *a, const void *b)
{
	const struct gpt_entry  *ae = (const struct gpt_entry *) a,
				*be = (const struct gpt_entry *) b;
	int au = gpt_entry_is_used(ae),
	    bu = gpt_entry_is_used(be);

	if (!au && !bu)
		return 0;
	if (!au)
		return 1;
	if (!bu)
		return -1;

	return cmp_numbers(gpt_partition_start(ae), gpt_partition_start(be));
}

/* sort partition by start sector */
static int gpt_reorder(struct fdisk_context *cxt)
{
	struct fdisk_gpt_label *gpt;
	size_t i, nparts, mess;

	assert(cxt);
	assert(cxt->label);
	assert(fdisk_is_label(cxt, GPT));

	gpt = self_label(cxt);
	nparts = gpt_get_nentries(gpt);

	for (i = 0, mess = 0; mess == 0 && i + 1 < nparts; i++)
		mess = gpt_entry_cmp_start(
				(const void *) gpt_get_entry(gpt, i),
				(const void *) gpt_get_entry(gpt, i + 1)) > 0;

	if (!mess) {
		fdisk_info(cxt, _("Nothing to do. Ordering is correct already."));
		return 1;
	}

	qsort(gpt->ents, nparts, sizeof(struct gpt_entry),
			gpt_entry_cmp_start);

	gpt_recompute_crc(gpt->pheader, gpt->ents);
	gpt_recompute_crc(gpt->bheader, gpt->ents);
	fdisk_label_set_changed(cxt->label, 1);

	return 0;
}

static int gpt_reset_alignment(struct fdisk_context *cxt)
{
	struct fdisk_gpt_label *gpt;
	struct gpt_header *h;

	assert(cxt);
	assert(cxt->label);
	assert(fdisk_is_label(cxt, GPT));

	gpt = self_label(cxt);
	h = gpt ? gpt->pheader : NULL;

	if (h) {
		/* always follow existing table */
		cxt->first_lba = le64_to_cpu(h->first_usable_lba);
		cxt->last_lba  = le64_to_cpu(h->last_usable_lba);
	} else {
		/* estimate ranges for GPT */
		uint64_t first, last;

		count_first_last_lba(cxt, &first, &last);

		if (cxt->first_lba < first)
			cxt->first_lba = first;
		if (cxt->last_lba > last)
			cxt->last_lba = last;
	}

	return 0;
}
/*
 * Deinitialize fdisk-specific variables
 */
static void gpt_deinit(struct fdisk_label *lb)
{
	struct fdisk_gpt_label *gpt = (struct fdisk_gpt_label *) lb;

	if (!gpt)
		return;

	free(gpt->ents);
	free(gpt->pheader);
	free(gpt->bheader);

	gpt->ents = NULL;
	gpt->pheader = NULL;
	gpt->bheader = NULL;
}

static const struct fdisk_label_operations gpt_operations =
{
	.probe		= gpt_probe_label,
	.write		= gpt_write_disklabel,
	.verify		= gpt_verify_disklabel,
	.create		= gpt_create_disklabel,
	.locate		= gpt_locate_disklabel,
	.get_item	= gpt_get_disklabel_item,
	.set_id		= gpt_set_disklabel_id,

	.get_part	= gpt_get_partition,
	.set_part	= gpt_set_partition,
	.add_part	= gpt_add_partition,
	.del_part	= gpt_delete_partition,
	.reorder	= gpt_reorder,

	.part_is_used	= gpt_part_is_used,
	.part_toggle_flag = gpt_toggle_partition_flag,

	.deinit		= gpt_deinit,

	.reset_alignment = gpt_reset_alignment
};

static const struct fdisk_field gpt_fields[] =
{
	/* basic */
	{ FDISK_FIELD_DEVICE,	N_("Device"),	 10,	0 },
	{ FDISK_FIELD_START,	N_("Start"),	  5,	FDISK_FIELDFL_NUMBER },
	{ FDISK_FIELD_END,	N_("End"),	  5,	FDISK_FIELDFL_NUMBER },
	{ FDISK_FIELD_SECTORS,	N_("Sectors"),	  5,	FDISK_FIELDFL_NUMBER },
	{ FDISK_FIELD_SIZE,	N_("Size"),	  5,	FDISK_FIELDFL_NUMBER | FDISK_FIELDFL_EYECANDY },
	{ FDISK_FIELD_TYPE,	N_("Type"),	0.1,	FDISK_FIELDFL_EYECANDY },
	/* expert */
	{ FDISK_FIELD_TYPEID,	N_("Type-UUID"), 36,	FDISK_FIELDFL_DETAIL },
	{ FDISK_FIELD_UUID,	N_("UUID"),	 36,	FDISK_FIELDFL_DETAIL },
	{ FDISK_FIELD_NAME,	N_("Name"),	0.2,	FDISK_FIELDFL_DETAIL },
	{ FDISK_FIELD_ATTR,	N_("Attrs"),	  0,	FDISK_FIELDFL_DETAIL }
};

/*
 * allocates GPT in-memory stuff
 */
struct fdisk_label *fdisk_new_gpt_label(struct fdisk_context *cxt __attribute__ ((__unused__)))
{
	struct fdisk_label *lb;
	struct fdisk_gpt_label *gpt;

	gpt = calloc(1, sizeof(*gpt));
	if (!gpt)
		return NULL;

	/* initialize generic part of the driver */
	lb = (struct fdisk_label *) gpt;
	lb->name = "gpt";
	lb->id = FDISK_DISKLABEL_GPT;
	lb->op = &gpt_operations;
	lb->parttypes = gpt_parttypes;
	lb->nparttypes = ARRAY_SIZE(gpt_parttypes);

	lb->fields = gpt_fields;
	lb->nfields = ARRAY_SIZE(gpt_fields);

	return lb;
}

#ifdef TEST_PROGRAM
static int test_getattr(struct fdisk_test *ts, int argc, char *argv[])
{
	const char *disk = argv[1];
	size_t part = strtoul(argv[2], NULL, 0) - 1;
	struct fdisk_context *cxt;
	uint64_t atters = 0;

	cxt = fdisk_new_context();
	fdisk_assign_device(cxt, disk, 1);

	if (!fdisk_is_label(cxt, GPT))
		return EXIT_FAILURE;

	if (fdisk_gpt_get_partition_attrs(cxt, part, &atters))
		return EXIT_FAILURE;

	printf("%s: 0x%016" PRIx64 "\n", argv[2], atters);

	fdisk_unref_context(cxt);
	return 0;
}

static int test_setattr(struct fdisk_test *ts, int argc, char *argv[])
{
	const char *disk = argv[1];
	size_t part = strtoul(argv[2], NULL, 0) - 1;
	uint64_t atters = strtoull(argv[3], NULL, 0);
	struct fdisk_context *cxt;

	cxt = fdisk_new_context();
	fdisk_assign_device(cxt, disk, 0);

	if (!fdisk_is_label(cxt, GPT))
		return EXIT_FAILURE;

	if (fdisk_gpt_set_partition_attrs(cxt, part, atters))
		return EXIT_FAILURE;

	if (fdisk_write_disklabel(cxt))
		return EXIT_FAILURE;

	fdisk_unref_context(cxt);
	return 0;
}

int main(int argc, char *argv[])
{
	struct fdisk_test tss[] = {
		{ "--getattr",  test_getattr,  "<disk> <partition>             print attributes" },
		{ "--setattr",  test_setattr,  "<disk> <partition> <value>     set attributes" },
		{ NULL }
	};

	return fdisk_run_test(tss, argc, argv);
}

#endif
