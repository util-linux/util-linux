/*
 * DASD partition table probing
 *
 * Copyright (C) 2026 Red Hat, Inc.
 *
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 *
 * Inspired by fdasd (s390-tools), Linux kernel and libparted.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <inttypes.h>

#include "pt-dasd.h"
#include "partitions.h"

static void dasd_get_volser(const char *volid, char *volser)
{
	int i = 0;

	for (i = 0; i < DASD_VOLSER_LENGTH; i++)
		volser[i] = dasd_ebcdic_to_ascii[(unsigned char) volid[i]];
	volser[DASD_VOLSER_LENGTH] = '\0';

	/* trim trailing spaces */
	for (i = DASD_VOLSER_LENGTH - 1; volser[i] == ' '; i--)
		volser[i] = '\0';
}

static void dasd_get_dsnam(const struct dasd_format1_label *f1, char *dsnam)
{
	size_t i = 0;

	for (i = 0; i < sizeof(f1->DS1DSNAM); i++)
		dsnam[i] = dasd_ebcdic_to_ascii[(unsigned char) f1->DS1DSNAM[i]];
	dsnam[sizeof(f1->DS1DSNAM)] = '\0';

	/* trim trailing spaces */
	for (i = sizeof(f1->DS1DSNAM) - 1; i != 0 && dsnam[i] == ' '; i--)
		dsnam[i] = '\0';
}

/*
 * CCHH for large volumes:
 * - upper 12 bits of hh hold the upper cylinder bits
 * - lower 4 bits of hh are the head number
 */
static uint32_t dasd_cchh_get_cc(const struct dasd_cchh *p)
{
	uint32_t cyl;

	cyl = be16_to_cpu(p->hh) & 0xFFF0;
	cyl <<= 12;
	cyl |= be16_to_cpu(p->cc);
	return cyl;
}

static uint16_t dasd_cchh_get_hh(const struct dasd_cchh *p)
{
	return be16_to_cpu(p->hh) & 0x000F;
}

static bool is_dasd_cdl_label(const unsigned char *buf)
{
	return memcmp(buf + 4, DASD_VOL1_MAGIC, 4) == 0;
}

static bool is_dasd_ldl_label(const unsigned char *buf)
{
	return memcmp(buf, DASD_LNX1_MAGIC, 4) == 0 ||
	       memcmp(buf, DASD_CMS1_MAGIC, 4) == 0;
}

/*
 * Format 4: 44-byte key field filled with 0x04 + DS4IDFMT (0xf4)
 */
static bool is_dasd_f4_label(const unsigned char *buf)
{
	int i = 0;

	for (i = 0; i < DASD_F4_KEYCD_LENGTH; i++) {
		if (buf[i] != DASD_F4_KEYCD_BYTE)
			return false;
	}
	if (buf[DASD_F4_KEYCD_LENGTH] != DASD_FMT_ID_F4)
		return false;

	return true;
}

/*
 * CDL -- up to three partitions defined by the F1/8 labels
 */
static int probe_dasd_pt_cdl(blkid_probe pr, blkid_partlist ls,
			     blkid_parttable tab,
			     unsigned int blocksize)
{
	const struct dasd_format4_label *f4;
	const struct dasd_format1_label *f1;
	const unsigned char *buf;
	unsigned int blk_per_trk = 0;
	uint16_t heads;
	uint32_t cylinders;
	unsigned int blk;
	int partno = 0;

	/*
	 * Looking for Format 4 label at blocks 3-20.
	 * On a real DASD, it is at CC=0 HH=1 R=1, which maps to
	 * linux block = blk_per_trk (one track after the start),
	 * but we don't know blk_per_trk yet.
	 */
	for (blk = 3; blk <= 20; blk++) {
		buf = blkid_probe_get_buffer(pr,
			(uint64_t) blk * blocksize,
			sizeof(struct dasd_format4_label));
		if (!buf)
			return errno ? -errno : BLKID_PROBE_NONE;
		if (is_dasd_f4_label(buf)) {
			blk_per_trk = blk;
			break;
		}
	}

	if (!blk_per_trk) {
		DBG(LOWPROBE, ul_debug("DASD: CDL detected but no F4 label found"));
		return BLKID_PROBE_NONE;
	}

	f4 = (const struct dasd_format4_label *) buf;

	heads = be16_to_cpu(f4->DS4DSTRK);
	if (heads == 0)
		return BLKID_PROBE_NONE;

	cylinders = be16_to_cpu(f4->DS4DSCYL);

	/* large volume -> use DS4DCYL */
	if (cylinders == DASD_LV_COMPAT_CYL)
		cylinders = be32_to_cpu(f4->DS4DCYL);

	DBG(LOWPROBE, ul_debug("DASD CDL: blk_per_trk=%u heads=%u cylinders=%u blocksize=%u",
			blk_per_trk, heads, cylinders, blocksize));

	/* scan for format 1 and format 8 labels describing the partitions */
	for (blk = blk_per_trk + 1; blk < blk_per_trk + 20 && partno < DASD_MAX_PARTITIONS; blk++) {
		char dsnam[sizeof(f1->DS1DSNAM) + 1];
		char *last_dot;
		size_t namelen;
		uint32_t start_cc, end_cc;
		uint16_t start_hh, end_hh;
		uint64_t start_trk, end_trk;
		uint64_t start_512, size_512;
		blkid_partition par;

		buf = blkid_probe_get_buffer(pr,
				(uint64_t) blk * blocksize,
				sizeof(struct dasd_format1_label));
		if (!buf)
			return errno ? -errno : BLKID_PROBE_NONE;

		f1 = (const struct dasd_format1_label *) buf;

		/* only format 1 and 8 are valid partition descriptors */
		if (f1->DS1FMTID != DASD_FMT_ID_F1 && f1->DS1FMTID != DASD_FMT_ID_F8)
			continue;

		if (cylinders > DASD_LV_COMPAT_CYL) {
			/* large volume encoding */
			start_cc = dasd_cchh_get_cc(&f1->DS1EXT1.llimit);
			start_hh = dasd_cchh_get_hh(&f1->DS1EXT1.llimit);
			end_cc = dasd_cchh_get_cc(&f1->DS1EXT1.ulimit);
			end_hh = dasd_cchh_get_hh(&f1->DS1EXT1.ulimit);
		} else {
			start_cc = be16_to_cpu(f1->DS1EXT1.llimit.cc);
			start_hh = be16_to_cpu(f1->DS1EXT1.llimit.hh);
			end_cc = be16_to_cpu(f1->DS1EXT1.ulimit.cc);
			end_hh = be16_to_cpu(f1->DS1EXT1.ulimit.hh);
		}

		start_trk = (uint64_t) start_cc * heads + start_hh;
		end_trk = (uint64_t) end_cc * heads + end_hh;

		if (end_trk <= start_trk)
			return BLKID_PROBE_NONE;

		/* convert to 512 sectors */
		start_512 = start_trk * blk_per_trk * blocksize / 512;
		size_512 = (end_trk - start_trk + 1) * blk_per_trk * blocksize / 512;

		DBG(LOWPROBE, ul_debug("DASD CDL part%d: CC=%u-%u HH=%u-%u "
				"trk=%"PRIu64"-%"PRIu64" start=%"PRIu64" size=%"PRIu64,
				partno + 1, start_cc, end_cc, start_hh, end_hh,
				start_trk, end_trk, start_512, size_512));

		par = blkid_partlist_add_partition(ls, tab, start_512, size_512);
		if (!par)
			return -ENOMEM;

		dasd_get_dsnam(f1, dsnam);

		/* split dsnam into name and type at the last '.' */
		last_dot = strrchr(dsnam, '.');
		if (last_dot) {
			namelen = min((size_t)(last_dot - dsnam), sizeof(dsnam) - 1);
			blkid_partition_set_name(par, (unsigned char *) dsnam, namelen);
			blkid_partition_set_type_string(par, (unsigned char *) last_dot + 1,
							strlen(last_dot + 1));
		} else {
			blkid_partition_set_type_string(par, (unsigned char *) dsnam,
							strlen(dsnam));
		}

		partno++;
	}

	return BLKID_PROBE_OK;
}

/*
 * LDL -- single implicit partition starting at block 3
 */
static int probe_dasd_pt_ldl(blkid_probe pr, blkid_partlist ls,
			     blkid_parttable tab,
			     const struct dasd_volume_label_ldl *vlabel,
			     unsigned int blocksize)
{
	uint64_t start_512, size_512;
	uint64_t blocks;
	blkid_partition par;

	start_512 = (uint64_t) 3 * blocksize / 512;

	if ((unsigned char) vlabel->ldl_version >= 0xf2) {
		blocks = be64_to_cpu(vlabel->formatted_blocks);
		if (blocks <= 3) {
			DBG(LOWPROBE, ul_debug("DASD LDL: invalid formatted_blocks %"PRIu64, blocks));
			return BLKID_PROBE_NONE;
		}
		size_512 = (blocks - 3) * blocksize / 512;
	} else {
		size_512 = blkid_probe_get_size(pr) / 512 - start_512;
	}

	DBG(LOWPROBE, ul_debug("DASD LDL: start=%"PRIu64" size=%"PRIu64" blocksize=%u",
			start_512, size_512, blocksize));

	par = blkid_partlist_add_partition(ls, tab, start_512, size_512);
	if (!par)
		return -ENOMEM;

	return BLKID_PROBE_OK;
}

static const unsigned int dasd_blocksizes[] = { 4096, 2048, 1024, 512 };

static int probe_dasd_pt(blkid_probe pr,
		const struct blkid_idmag *mag __attribute__((__unused__)))
{
	const unsigned char *buf;
	blkid_parttable tab = NULL;
	blkid_partlist ls;
	char volser[DASD_VOLSER_LENGTH + 1];
	unsigned int blocksize;
	bool is_cdl = false;
	bool is_ldl = false;
	const struct dasd_volume_label_cdl *cdl = NULL;
	const struct dasd_volume_label_ldl *ldl = NULL;
	const char *magic;
	int rc;
	size_t i = 0;

	blocksize = blkid_probe_get_sectorsize(pr);
	buf = blkid_probe_get_buffer(pr,
			(uint64_t) 2 * blocksize,
			sizeof(struct dasd_volume_label_ldl));
	if (!buf)
		return errno ? -errno : BLKID_PROBE_NONE;

	/* CDL -- "VOL1" at byte 4 */
	if (is_dasd_cdl_label(buf))
		is_cdl = true;
	/* LDL -- "LNX1" or "CMS1" at byte 0 */
	else if (is_dasd_ldl_label(buf))
		is_ldl = true;

	/*
	 * check the other known DASD block sizes as well in case we are
	 * scanning e.g. 512 disk image
	 */
	if (!is_cdl && !is_ldl) {
		for (i = 0; i < ARRAY_SIZE(dasd_blocksizes); i++) {
			if (dasd_blocksizes[i] == blocksize)
				continue;

			buf = blkid_probe_get_buffer(pr,
					(uint64_t) 2 * dasd_blocksizes[i],
					sizeof(struct dasd_volume_label_ldl));
			if (!buf) {
				if (errno)
					return -errno;
				continue;
			}

			if (is_dasd_cdl_label(buf)) {
				is_cdl = true;
				blocksize = dasd_blocksizes[i];
				break;
			}
			if (is_dasd_ldl_label(buf)) {
				is_ldl = true;
				blocksize = dasd_blocksizes[i];
				break;
			}
		}
	}

	if (!is_cdl && !is_ldl)
		return BLKID_PROBE_NONE;

	DBG(LOWPROBE, ul_debug("DASD: %s label detected (blocksize=%u)",
			is_cdl ? "CDL" : "LDL", blocksize));

	if (is_cdl) {
		cdl = (const struct dasd_volume_label_cdl *) buf;

		if (blkid_probe_set_magic(pr,
				(uint64_t) 2 * blocksize +
					offsetof(struct dasd_volume_label_cdl, vollbl),
				4, (const unsigned char *) DASD_VOL1_MAGIC))
			return BLKID_PROBE_NONE;

		dasd_get_volser(cdl->volid, volser);
	} else {
		ldl = (const struct dasd_volume_label_ldl *) buf;
		magic = memcmp(buf, DASD_LNX1_MAGIC, 4) == 0 ? DASD_LNX1_MAGIC : DASD_CMS1_MAGIC;

		if (blkid_probe_set_magic(pr,
				(uint64_t) 2 * blocksize +
					offsetof(struct dasd_volume_label_ldl, vollbl),
				4, (const unsigned char *) magic))
			return BLKID_PROBE_NONE;

		dasd_get_volser(ldl->volid, volser);
	}

	blkid_partitions_strcpy_ptuuid(pr, volser);

	if (blkid_partitions_need_typeonly(pr))
		return BLKID_PROBE_OK;

	ls = blkid_probe_get_partlist(pr);
	if (!ls)
		return BLKID_PROBE_NONE;

	tab = blkid_partlist_new_parttable(ls, "dasd", 0);
	if (!tab)
		return -ENOMEM;

	blkid_parttable_set_id(tab, (unsigned char *) volser);

	if (is_cdl)
		rc = probe_dasd_pt_cdl(pr, ls, tab, blocksize);
	else
		rc = probe_dasd_pt_ldl(pr, ls, tab, ldl, blocksize);

	return rc;
}

const struct blkid_idinfo dasd_pt_idinfo =
{
	.name		= "dasd",
	.probefunc	= probe_dasd_pt,

	/*
	 * magic location unfortunately depends on the device geometry and
	 * DASD version and format (CDL or LDL)
	 */
	.magics		= BLKID_NONE_MAGIC
};
