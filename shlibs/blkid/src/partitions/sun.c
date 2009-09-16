/*
 * sun (solaris-sparc) partition parsing code
 *
 * Copyright (C) 2009 Karel Zak <kzak@redhat.com>
 *
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>

#include "partitions.h"

/* Supported VTOC setting */
#define SUN_VTOC_SANITY		0x600DDEEE	/* magic number */
#define SUN_VTOC_VERSION	1

#define SUN_MAXPARTITIONS	8

/* Partition IDs */
#define SUN_TAG_WHOLEDISK          0x05

struct sun_disklabel {
	unsigned char info[128];   /* Informative text string */

	struct sun_vtoc {
		uint32_t version;     /* version */
		char	 volume[8];   /* volume name */
		uint16_t nparts;      /* num of partitions */

		struct sun_info {     /* partition information */
			uint16_t id;
			uint16_t flags;
		} __attribute__ ((packed)) infos[8];

		uint16_t padding;      /* padding */
		uint32_t bootinfo[3];  /* info needed by mboot */
		uint32_t sanity;       /* magic number */
		uint32_t reserved[10]; /* padding */
		uint32_t timestamp[8]; /* partition timestamp */
	} __attribute__ ((packed)) vtoc;

	uint32_t write_reinstruct;     /* sectors to skip, writes */
	uint32_t read_reinstruct;      /* sectors to skip, reads */
	unsigned char spare[148];      /* padding */
	uint16_t rspeed;               /* disk rotational speed */
	uint16_t pcylcount;            /* physical cylinder count */
	uint16_t sparecyl;             /* extra sects per cylinder */
	uint16_t obs1;
	uint16_t obs2;
	uint16_t ilfact;               /* interleave factor */
	uint16_t ncyl;                 /* data cylinder count */
	uint16_t nacyl;                /* alt. cylinder count */
	uint16_t ntrks;                /* tracks per cylinder   <---- */
	uint16_t nsect;                /* sectors per track     <---- */
	uint16_t obs3;
	uint16_t obs4;

	struct sun_partition {         /* partitions */
		uint32_t start_cylinder;
		uint32_t num_sectors;
	} __attribute__ ((packed)) partitions[8];

	uint16_t magic;                /* magic number */
	uint16_t csum;                 /* label xor'd checksum */
} __attribute__ ((packed));


uint16_t count_checksum(struct sun_disklabel *label)
{
	uint16_t *ptr = ((uint16_t *) (label + 1)) - 1;
	uint16_t sum;

	for (sum = 0; ptr >= ((uint16_t *) label);)
		sum ^= *ptr--;

	return sum;
}

static int probe_sun_pt(blkid_probe pr, const struct blkid_idmag *mag)
{
	struct sun_disklabel *l;
	struct sun_partition *p;
	struct sun_info *infos = NULL;
	blkid_parttable tab = NULL;
	blkid_partlist ls;
	uint16_t nparts;
	blkid_loff_t spc;
	int i;

	l = (struct sun_disklabel *) blkid_probe_get_sector(pr, 0);
	if (!l)
		goto nothing;

	if (count_checksum(l)) {
		DBG(DEBUG_LOWPROBE, printf(
			"detected corrupted sun disk label -- ignore\n"));
		goto nothing;
	}

	if (blkid_partitions_need_typeonly(pr))
		/* caller does not ask for details about partitions */
		return 0;

	ls = blkid_probe_get_partlist(pr);
	if (!ls)
		goto err;

	tab = blkid_partlist_new_parttable(ls, "sun", 0);
	if (!tab)
		goto err;

	/* default number of partitions */
	nparts = SUN_MAXPARTITIONS;

	/* sectors per cylinder (partition offset is in cylinders...) */
	spc = be16_to_cpu(l->ntrks) * be16_to_cpu(l->nsect);

	if ((be32_to_cpu(l->vtoc.sanity) == SUN_VTOC_SANITY) &&
	    (be32_to_cpu(l->vtoc.version) == SUN_VTOC_VERSION) &&
	    (be16_to_cpu(l->vtoc.nparts) <= SUN_MAXPARTITIONS)) {

		nparts = be16_to_cpu(l->vtoc.nparts);
		infos = l->vtoc.infos;			/* for partition type */
	}

	for (i = 0, p = l->partitions; i < nparts; i++, p++) {

		blkid_loff_t start;
		blkid_loff_t size;
		uint16_t type = infos ? be16_to_cpu(infos[i].id) : 0;

                start = be32_to_cpu(p->start_cylinder) * spc;
		size = be32_to_cpu(p->num_sectors);

		if (type == SUN_TAG_WHOLEDISK || !size)
			continue;

		if (!blkid_partlist_add_partition(ls, tab, type, start, size))
			goto err;
	}
	return 0;

nothing:
	return 1;
err:
	return -1;
}


const struct blkid_idinfo sun_pt_idinfo =
{
	.name		= "sun",
	.probefunc	= probe_sun_pt,
	.magics		=
	{
		{
		  .magic = "\xDA\xBE",		/* big-endian magic string */
		  .len = 2,
		  .sboff = offsetof(struct sun_disklabel, magic)
		},
		{ NULL }
	}
};

