/*
    gpt.[ch]

    Copyright (C) 2000-2001 Dell Computer Corporation <Matt_Domsch@dell.com> 

    EFI GUID Partition Table handling
    Per Intel EFI Specification v1.02
    http://developer.intel.com/technology/efi/efi.htm

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#ifndef _GPT_H
#define _GPT_H


#include <inttypes.h>
#include "partx.h"
#include "dos.h"
#include "efi.h"

#define EFI_PMBR_OSTYPE_EFI 0xEF
#define EFI_PMBR_OSTYPE_EFI_GPT 0xEE
#define MSDOS_MBR_SIGNATURE 0xaa55
#define GPT_BLOCK_SIZE 512

#define GPT_HEADER_SIGNATURE 0x5452415020494645ULL
#define GPT_HEADER_REVISION_V1_02 0x00010200
#define GPT_HEADER_REVISION_V1_00 0x00010000
#define GPT_HEADER_REVISION_V0_99 0x00009900
#define GPT_PRIMARY_PARTITION_TABLE_LBA 1

typedef struct _gpt_header {
	uint64_t signature;
	uint32_t revision;
	uint32_t header_size;
	uint32_t header_crc32;
	uint32_t reserved1;
	uint64_t my_lba;
	uint64_t alternate_lba;
	uint64_t first_usable_lba;
	uint64_t last_usable_lba;
	efi_guid_t disk_guid;
	uint64_t partition_entry_lba;
	uint32_t num_partition_entries;
	uint32_t sizeof_partition_entry;
	uint32_t partition_entry_array_crc32;
	uint8_t reserved2[GPT_BLOCK_SIZE - 92];
} __attribute__ ((packed)) gpt_header;

typedef struct _gpt_entry_attributes {
	uint64_t required_to_function:1;
	uint64_t reserved:47;
        uint64_t type_guid_specific:16;
} __attribute__ ((packed)) gpt_entry_attributes;

typedef struct _gpt_entry {
	efi_guid_t partition_type_guid;
	efi_guid_t unique_partition_guid;
	uint64_t starting_lba;
	uint64_t ending_lba;
	gpt_entry_attributes attributes;
	efi_char16_t partition_name[72 / sizeof(efi_char16_t)];
} __attribute__ ((packed)) gpt_entry;


/* 
   These values are only defaults.  The actual on-disk structures
   may define different sizes, so use those unless creating a new GPT disk!
*/

#define GPT_DEFAULT_RESERVED_PARTITION_ENTRY_ARRAY_SIZE 16384
/* 
   Number of actual partition entries should be calculated
   as: 
*/
#define GPT_DEFAULT_RESERVED_PARTITION_ENTRIES \
        (GPT_DEFAULT_RESERVED_PARTITION_ENTRY_ARRAY_SIZE / \
         sizeof(gpt_entry))


/* Protected Master Boot Record  & Legacy MBR share same structure */
/* Needs to be packed because the u16s force misalignment. */

typedef struct _legacy_mbr {
	uint8_t bootcode[440];
	uint32_t unique_mbr_signature;
	uint16_t unknown;
	struct partition partition[4];
	uint16_t signature;
} __attribute__ ((packed)) legacy_mbr;


#define EFI_GPT_PRIMARY_PARTITION_TABLE_LBA 1

/* Functions */
int read_gpt_pt (int fd, struct slice all, struct slice *sp, int ns);


#endif

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-indent-level: 4 
 * c-brace-imaginary-offset: 0
 * c-brace-offset: -4
 * c-argdecl-indent: 4
 * c-label-offset: -4
 * c-continued-statement-offset: 4
 * c-continued-brace-offset: 0
 * indent-tabs-mode: nil
 * tab-width: 8
 * End:
 */
