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

#define _FILE_OFFSET_BITS 64

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <asm/byteorder.h>
#include "crc32.h"
#include "gpt.h"
#include "partx.h"

#define BLKGETLASTSECT  _IO(0x12,108)   /* get last sector of block device */
#define BLKGETSIZE _IO(0x12,96)	        /* return device size */
#define BLKSSZGET  _IO(0x12,104)	/* get block device sector size */
#define BLKGETSIZE64 _IOR(0x12,114,sizeof(uint64_t))	/* return device size in bytes (u64 *arg) */

struct blkdev_ioctl_param {
        unsigned int block;
        size_t content_length;
        char * block_contents;
};

/**
 * efi_crc32() - EFI version of crc32 function
 * @buf: buffer to calculate crc32 of
 * @len - length of buf
 *
 * Description: Returns EFI-style CRC32 value for @buf
 * 
 * This function uses the little endian Ethernet polynomial
 * but seeds the function with ~0, and xor's with ~0 at the end.
 * Note, the EFI Specification, v1.02, has a reference to
 * Dr. Dobbs Journal, May 1994 (actually it's in May 1992).
 */
static inline uint32_t
efi_crc32(const void *buf, unsigned long len)
{
	return (crc32(~0L, buf, len) ^ ~0L);
}

/**
 * is_pmbr_valid(): test Protective MBR for validity
 * @mbr: pointer to a legacy mbr structure
 *
 * Description: Returns 1 if PMBR is valid, 0 otherwise.
 * Validity depends on two things:
 *  1) MSDOS signature is in the last two bytes of the MBR
 *  2) One partition of type 0xEE is found
 */
static int
is_pmbr_valid(legacy_mbr *mbr)
{
	int i, found = 0, signature = 0;
	if (!mbr)
		return 0;
	signature = (__le16_to_cpu(mbr->signature) == MSDOS_MBR_SIGNATURE);
	for (i = 0; signature && i < 4; i++) {
		if (mbr->partition[i].sys_type ==
                    EFI_PMBR_OSTYPE_EFI_GPT) {
			found = 1;
			break;
		}
	}
	return (signature && found);
}


/************************************************************
 * get_sector_size
 * Requires:
 *  - filedes is an open file descriptor, suitable for reading
 * Modifies: nothing
 * Returns:
 *  sector size, or 512.
 ************************************************************/
static int
get_sector_size(int filedes)
{
	int rc, sector_size = 512;

	rc = ioctl(filedes, BLKSSZGET, &sector_size);
	if (rc)
		sector_size = 512;
	return sector_size;
}

/************************************************************
 * _get_num_sectors
 * Requires:
 *  - filedes is an open file descriptor, suitable for reading
 * Modifies: nothing
 * Returns:
 *  Last LBA value on success 
 *  0 on error
 *
 * Try getting BLKGETSIZE64 and BLKSSZGET first,
 * then BLKGETSIZE if necessary.
 *  Kernels 2.4.15-2.4.18 and 2.5.0-2.5.3 have a broken BLKGETSIZE64
 *  which returns the number of 512-byte sectors, not the size of
 *  the disk in bytes. Fixed in kernels 2.4.18-pre8 and 2.5.4-pre3.
 ************************************************************/
static uint64_t
_get_num_sectors(int filedes)
{
	unsigned long sectors=0;
	int rc;
#if 0
        uint64_t bytes=0;

 	rc = ioctl(filedes, BLKGETSIZE64, &bytes);
	if (!rc)
		return bytes / get_sector_size(filedes);
#endif
        rc = ioctl(filedes, BLKGETSIZE, &sectors);
        if (rc)
                return 0;
        
	return sectors;
}

/************************************************************
 * last_lba(): return number of last logical block of device
 * 
 * @fd
 * 
 * Description: returns Last LBA value on success, 0 on error.
 * Notes: The value st_blocks gives the size of the file
 *        in 512-byte blocks, which is OK if
 *        EFI_BLOCK_SIZE_SHIFT == 9.
 ************************************************************/

static uint64_t
last_lba(int filedes)
{
	int rc;
	uint64_t sectors = 0;
	struct stat s;
	memset(&s, 0, sizeof (s));
	rc = fstat(filedes, &s);
	if (rc == -1) {
		fprintf(stderr, "last_lba() could not stat: %s\n",
			strerror(errno));
		return 0;
	}

	if (S_ISBLK(s.st_mode)) {
		sectors = _get_num_sectors(filedes);
	} else {
		fprintf(stderr,
			"last_lba(): I don't know how to handle files with mode %x\n",
			s.st_mode);
		sectors = 1;
	}

	return sectors - 1;
}


static ssize_t
read_lastoddsector(int fd, uint64_t lba, void *buffer, size_t count)
{
        int rc;
        struct blkdev_ioctl_param ioctl_param;

        if (!buffer) return 0; 

        ioctl_param.block = 0; /* read the last sector */
        ioctl_param.content_length = count;
        ioctl_param.block_contents = buffer;

        rc = ioctl(fd, BLKGETLASTSECT, &ioctl_param);
        if (rc == -1) perror("read failed");

        return !rc;
}

static ssize_t
read_lba(int fd, uint64_t lba, void *buffer, size_t bytes)
{
	int sector_size = get_sector_size(fd);
	off_t offset = lba * sector_size;
        ssize_t bytesread;

	lseek(fd, offset, SEEK_SET);
	bytesread = read(fd, buffer, bytes);

        /* Kludge.  This is necessary to read/write the last
           block of an odd-sized disk, until Linux 2.5.x kernel fixes.
           This is only used by gpt.c, and only to read
           one sector, so we don't have to be fancy.
        */
        if (!bytesread && !(last_lba(fd) & 1) && lba == last_lba(fd)) {
                bytesread = read_lastoddsector(fd, lba, buffer, bytes);
        }
        return bytesread;
}

/**
 * alloc_read_gpt_entries(): reads partition entries from disk
 * @fd  is an open file descriptor to the whole disk
 * @gpt is a buffer into which the GPT will be put  
 * Description: Returns ptes on success,  NULL on error.
 * Allocates space for PTEs based on information found in @gpt.
 * Notes: remember to free pte when you're done!
 */
static gpt_entry *
alloc_read_gpt_entries(int fd, gpt_header * gpt)
{
	gpt_entry *pte;
        size_t count = __le32_to_cpu(gpt->num_partition_entries) *
                __le32_to_cpu(gpt->sizeof_partition_entry);

        if (!count) return NULL;

	pte = (gpt_entry *)malloc(count);
	if (!pte)
		return NULL;
	memset(pte, 0, count);

	if (!read_lba(fd, __le64_to_cpu(gpt->partition_entry_lba), pte,
                      count)) {
		free(pte);
		return NULL;
	}
	return pte;
}

/**
 * alloc_read_gpt_header(): Allocates GPT header, reads into it from disk
 * @fd  is an open file descriptor to the whole disk
 * @lba is the Logical Block Address of the partition table
 * 
 * Description: returns GPT header on success, NULL on error.   Allocates
 * and fills a GPT header starting at @ from @bdev.
 * Note: remember to free gpt when finished with it.
 */
static gpt_header *
alloc_read_gpt_header(int fd, uint64_t lba)
{
	gpt_header *gpt;
	gpt = (gpt_header *)
	    malloc(sizeof (gpt_header));
	if (!gpt)
		return NULL;
	memset(gpt, 0, sizeof (*gpt));
	if (!read_lba(fd, lba, gpt, sizeof (gpt_header))) {
		free(gpt);
		return NULL;
	}

	return gpt;
}

/**
 * is_gpt_valid() - tests one GPT header and PTEs for validity
 * @fd  is an open file descriptor to the whole disk
 * @lba is the logical block address of the GPT header to test
 * @gpt is a GPT header ptr, filled on return.
 * @ptes is a PTEs ptr, filled on return.
 *
 * Description: returns 1 if valid,  0 on error.
 * If valid, returns pointers to newly allocated GPT header and PTEs.
 */
static int
is_gpt_valid(int fd, uint64_t lba,
             gpt_header ** gpt, gpt_entry ** ptes)
{
	int rc = 0;		/* default to not valid */
	uint32_t crc, origcrc;

	if (!gpt || !ptes)
                return 0;
	if (!(*gpt = alloc_read_gpt_header(fd, lba)))
		return 0;

	/* Check the GUID Partition Table signature */
	if (__le64_to_cpu((*gpt)->signature) != GPT_HEADER_SIGNATURE) {
		/* 
		   printf("GUID Partition Table Header signature is wrong: %" PRIx64" != %" PRIx64 "\n",
		   __le64_to_cpu((*gpt)->signature), GUID_PT_HEADER_SIGNATURE);
		 */
		free(*gpt);
		*gpt = NULL;
		return rc;
	}

	/* Check the GUID Partition Table Header CRC */
	origcrc = __le32_to_cpu((*gpt)->header_crc32);
	(*gpt)->header_crc32 = 0;
	crc = efi_crc32(*gpt, __le32_to_cpu((*gpt)->header_size));
	if (crc != origcrc) {
		// printf( "GPTH CRC check failed, %x != %x.\n", origcrc, crc);
		(*gpt)->header_crc32 = __cpu_to_le32(origcrc);
		free(*gpt);
		*gpt = NULL;
		return 0;
	}
	(*gpt)->header_crc32 = __cpu_to_le32(origcrc);

	/* Check that the my_lba entry points to the LBA
	 * that contains the GPT we read */
	if (__le64_to_cpu((*gpt)->my_lba) != lba) {
		// printf( "my_lba % PRIx64 "x != lba %"PRIx64 "x.\n", __le64_to_cpu((*gpt)->my_lba), lba);
		free(*gpt);
		*gpt = NULL;
		return 0;
	}

	if (!(*ptes = alloc_read_gpt_entries(fd, *gpt))) {
		free(*gpt);
		*gpt = NULL;
		return 0;
	}

	/* Check the GUID Partition Entry Array CRC */
	crc = efi_crc32(*ptes,
                        __le32_to_cpu((*gpt)->num_partition_entries) *
			__le32_to_cpu((*gpt)->sizeof_partition_entry));
	if (crc != __le32_to_cpu((*gpt)->partition_entry_array_crc32)) {
		// printf("GUID Partitition Entry Array CRC check failed.\n");
		free(*gpt);
		*gpt = NULL;
		free(*ptes);
		*ptes = NULL;
		return 0;
	}

	/* We're done, all's well */
	return 1;
}
/**
 * compare_gpts() - Search disk for valid GPT headers and PTEs
 * @pgpt is the primary GPT header
 * @agpt is the alternate GPT header
 * @lastlba is the last LBA number
 * Description: Returns nothing.  Sanity checks pgpt and agpt fields
 * and prints warnings on discrepancies.
 * 
 */
static void
compare_gpts(gpt_header *pgpt, gpt_header *agpt, uint64_t lastlba)
{
	int error_found = 0;
	if (!pgpt || !agpt)
		return;
	if (__le64_to_cpu(pgpt->my_lba) != __le64_to_cpu(agpt->alternate_lba)) {
		fprintf(stderr, 
		       "GPT:Primary header LBA != Alt. header alternate_lba\n");
		fprintf(stderr,  "GPT:%" PRIx64 "x != %" PRIx64 "x\n",
		       __le64_to_cpu(pgpt->my_lba),
                       __le64_to_cpu(agpt->alternate_lba));
		error_found++;
	}
	if (__le64_to_cpu(pgpt->alternate_lba) != __le64_to_cpu(agpt->my_lba)) {
		fprintf(stderr, 
		       "GPT:Primary header alternate_lba != Alt. header my_lba\n");
		fprintf(stderr,  "GPT:%" PRIx64 " != %" PRIx64 "\n",
		       __le64_to_cpu(pgpt->alternate_lba),
                       __le64_to_cpu(agpt->my_lba));
		error_found++;
	}
	if (__le64_to_cpu(pgpt->first_usable_lba) !=
            __le64_to_cpu(agpt->first_usable_lba)) {
		fprintf(stderr,  "GPT:first_usable_lbas don't match.\n");
		fprintf(stderr,  "GPT:%" PRIx64 " != %" PRIx64 "\n",
		       __le64_to_cpu(pgpt->first_usable_lba),
                       __le64_to_cpu(agpt->first_usable_lba));
		error_found++;
	}
	if (__le64_to_cpu(pgpt->last_usable_lba) !=
            __le64_to_cpu(agpt->last_usable_lba)) {
		fprintf(stderr,  "GPT:last_usable_lbas don't match.\n");
		fprintf(stderr,  "GPT:%" PRIx64 " != %" PRIx64 "\n",
		       __le64_to_cpu(pgpt->last_usable_lba),
                       __le64_to_cpu(agpt->last_usable_lba));
		error_found++;
	}
	if (efi_guidcmp(pgpt->disk_guid, agpt->disk_guid)) {
		fprintf(stderr,  "GPT:disk_guids don't match.\n");
		error_found++;
	}
	if (__le32_to_cpu(pgpt->num_partition_entries) !=
            __le32_to_cpu(agpt->num_partition_entries)) {
		fprintf(stderr,  "GPT:num_partition_entries don't match: "
		       "0x%x != 0x%x\n",
		       __le32_to_cpu(pgpt->num_partition_entries),
		       __le32_to_cpu(agpt->num_partition_entries));
		error_found++;
	}
	if (__le32_to_cpu(pgpt->sizeof_partition_entry) !=
            __le32_to_cpu(agpt->sizeof_partition_entry)) {
		fprintf(stderr, 
		       "GPT:sizeof_partition_entry values don't match: "
		       "0x%x != 0x%x\n",
                       __le32_to_cpu(pgpt->sizeof_partition_entry),
		       __le32_to_cpu(agpt->sizeof_partition_entry));
		error_found++;
	}
	if (__le32_to_cpu(pgpt->partition_entry_array_crc32) !=
            __le32_to_cpu(agpt->partition_entry_array_crc32)) {
		fprintf(stderr, 
		       "GPT:partition_entry_array_crc32 values don't match: "
		       "0x%x != 0x%x\n",
                       __le32_to_cpu(pgpt->partition_entry_array_crc32),
		       __le32_to_cpu(agpt->partition_entry_array_crc32));
		error_found++;
	}
	if (__le64_to_cpu(pgpt->alternate_lba) != lastlba) {
		fprintf(stderr, 
		       "GPT:Primary header thinks Alt. header is not at the end of the disk.\n");
		fprintf(stderr,  "GPT:%" PRIx64 " != %" PRIx64 "\n",
		       __le64_to_cpu(pgpt->alternate_lba), lastlba);
		error_found++;
	}

	if (__le64_to_cpu(agpt->my_lba) != lastlba) {
		fprintf(stderr, 
		       "GPT:Alternate GPT header not at the end of the disk.\n");
		fprintf(stderr,  "GPT:%" PRIx64 " != %" PRIx64 "\n",
		       __le64_to_cpu(agpt->my_lba), lastlba);
		error_found++;
	}

	if (error_found)
		fprintf(stderr, 
		       "GPT: Use GNU Parted to correct GPT errors.\n");
	return;
}

/**
 * find_valid_gpt() - Search disk for valid GPT headers and PTEs
 * @fd  is an open file descriptor to the whole disk
 * @gpt is a GPT header ptr, filled on return.
 * @ptes is a PTEs ptr, filled on return.
 * Description: Returns 1 if valid, 0 on error.
 * If valid, returns pointers to newly allocated GPT header and PTEs.
 * Validity depends on finding either the Primary GPT header and PTEs valid,
 * or the Alternate GPT header and PTEs valid, and the PMBR valid.
 */
static int
find_valid_gpt(int fd, gpt_header ** gpt, gpt_entry ** ptes)
{
        extern int force_gpt;
	int good_pgpt = 0, good_agpt = 0, good_pmbr = 0;
	gpt_header *pgpt = NULL, *agpt = NULL;
	gpt_entry *pptes = NULL, *aptes = NULL;
	legacy_mbr *legacymbr = NULL;
	uint64_t lastlba;
	if (!gpt || !ptes)
		return 0;

	lastlba = last_lba(fd);
	good_pgpt = is_gpt_valid(fd, GPT_PRIMARY_PARTITION_TABLE_LBA,
				 &pgpt, &pptes);
        if (good_pgpt) {
		good_agpt = is_gpt_valid(fd,
                                         __le64_to_cpu(pgpt->alternate_lba),
					 &agpt, &aptes);
                if (!good_agpt) {
                        good_agpt = is_gpt_valid(fd, lastlba,
                                                 &agpt, &aptes);
                }
        }
        else {
                good_agpt = is_gpt_valid(fd, lastlba,
                                         &agpt, &aptes);
        }

        /* The obviously unsuccessful case */
        if (!good_pgpt && !good_agpt) {
                goto fail;
        }

	/* This will be added to the EFI Spec. per Intel after v1.02. */
        legacymbr = malloc(sizeof (*legacymbr));
        if (legacymbr) {
                memset(legacymbr, 0, sizeof (*legacymbr));
                read_lba(fd, 0, (uint8_t *) legacymbr,
                         sizeof (*legacymbr));
                good_pmbr = is_pmbr_valid(legacymbr);
                free(legacymbr);
                legacymbr=NULL;
        }

        /* Failure due to bad PMBR */
        if ((good_pgpt || good_agpt) && !good_pmbr && !force_gpt) {
                fprintf(stderr,
                       "  Warning: Disk has a valid GPT signature "
                       "but invalid PMBR.\n"
                       "  Assuming this disk is *not* a GPT disk anymore.\n"
                       "  Use gpt kernel option to override.  "
                       "Use GNU Parted to correct disk.\n");
                goto fail;
        }

        /* Would fail due to bad PMBR, but force GPT anyhow */
        if ((good_pgpt || good_agpt) && !good_pmbr && force_gpt) {
                fprintf(stderr, 
                       "  Warning: Disk has a valid GPT signature but "
                       "invalid PMBR.\n"
                       "  Use GNU Parted to correct disk.\n"
                       "  gpt option taken, disk treated as GPT.\n");
        }

        compare_gpts(pgpt, agpt, lastlba);

        /* The good cases */
        if (good_pgpt && (good_pmbr || force_gpt)) {
                *gpt  = pgpt;
                *ptes = pptes;
                if (agpt)  { free(agpt);   agpt = NULL; }
                if (aptes) { free(aptes); aptes = NULL; }
                if (!good_agpt) {
                        fprintf(stderr, 
			       "Alternate GPT is invalid, "
                               "using primary GPT.\n");
                }
                return 1;
        }
        else if (good_agpt && (good_pmbr || force_gpt)) {
                *gpt  = agpt;
                *ptes = aptes;
                if (pgpt)  { free(pgpt);   pgpt = NULL; }
                if (pptes) { free(pptes); pptes = NULL; }
                fprintf(stderr, 
                       "Primary GPT is invalid, using alternate GPT.\n");
                return 1;
        }

 fail:
        if (pgpt)  { free(pgpt);   pgpt=NULL; }
        if (agpt)  { free(agpt);   agpt=NULL; }
        if (pptes) { free(pptes); pptes=NULL; }
        if (aptes) { free(aptes); aptes=NULL; }
        *gpt = NULL;
        *ptes = NULL;
        return 0;
}

/**
 * read_gpt_pt() 
 * @fd
 * @all - slice with start/size of whole disk
 *
 *  0 if this isn't our partition table
 *  number of partitions if successful
 *
 */
int
read_gpt_pt (int fd, struct slice all, struct slice *sp, int ns)
{
	gpt_header *gpt = NULL;
	gpt_entry *ptes = NULL;
	uint32_t i;
	int n = 0;
        int last_used_index=-1;

	if (!find_valid_gpt (fd, &gpt, &ptes) || !gpt || !ptes) {
		if (gpt)
			free (gpt);
		if (ptes)
			free (ptes);
		return 0;
	}

	for (i = 0; i < __le32_to_cpu(gpt->num_partition_entries) && i < ns; i++) {
		if (!efi_guidcmp (NULL_GUID, ptes[i].partition_type_guid)) {
			sp[n].start = 0;
			sp[n].size = 0;
			n++;
		} else {
			sp[n].start = __le64_to_cpu(ptes[i].starting_lba);
			sp[n].size  = __le64_to_cpu(ptes[i].ending_lba) -
				__le64_to_cpu(ptes[i].starting_lba) + 1;
                        last_used_index=n;
			n++;
		}
	}
	free (ptes);
	free (gpt);
	return last_used_index+1;
}
