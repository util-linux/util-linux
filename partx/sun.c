/*
 * Lifted from kpartx's sun.c
 *
 * Copyrights of the original file apply
 * Copyright (c) 2007 Hannes Reinecke
 *
 * Integrated to partx (utils-linux-ng)
 *       Davidlohr Bueso <dave@gnu.org>
 */

#include <stdio.h>
#include <sys/types.h>

#include "bitops.h"
#include "partx.h"

#define SUN_DISK_MAGIC		0xDABE	/* Disk magic number */
#define SUN_DISK_MAXPARTITIONS	8

struct __attribute__ ((packed)) sun_raw_part {
	u_int32_t	start_cylinder; /* where the part starts... */
	u_int32_t	num_sectors;	/* ...and it's length */
};

struct __attribute__ ((packed)) sun_part_info {
	u_int8_t	spare1;
	u_int8_t	id;		/* Partition type */
	u_int8_t	spare2;
	u_int8_t	flags;		/* Partition flags */
};

struct __attribute__ ((packed)) sun_disk_label {
	char		info[128];	/* Informative text string */
	u_int8_t	spare0[14];
	struct sun_part_info infos[SUN_DISK_MAXPARTITIONS];
	u_int8_t	spare1[246];	/* Boot information etc. */
	u_int16_t	rspeed;		/* Disk rotational speed */
	u_int16_t	pcylcount;	/* Physical cylinder count */
	u_int16_t	sparecyl;	/* extra sects per cylinder */
	u_int8_t	spare2[4];	/* More magic... */
	u_int16_t	ilfact;		/* Interleave factor */
	u_int16_t	ncyl;		/* Data cylinder count */
	u_int16_t	nacyl;		/* Alt. cylinder count */
	u_int16_t	ntrks;		/* Tracks per cylinder */
	u_int16_t	nsect;		/* Sectors per track */
	u_int8_t	spare3[4];	/* Even more magic... */
	struct sun_raw_part partitions[SUN_DISK_MAXPARTITIONS];
	u_int16_t	magic;		/* Magic number */
	u_int16_t	csum;		/* Label xor'd checksum */
};

/* Checksum Verification */
static int
sun_verify_checksum (struct sun_disk_label *label)
{
	u_int16_t *ush = ((u_int16_t *)(label + 1)) - 1;
	u_int16_t csum = 0;

	while (ush >= (u_int16_t *)label)
		csum ^= *ush--;

	return !csum;
}

int
read_sun_pt(int fd, struct slice all, struct slice *sp, int ns) {
	struct sun_disk_label *l;
	struct sun_raw_part *s;
	unsigned int offset = all.start, end;
	int i, j, n;
	unsigned char *bp;

	bp = getblock(fd, offset);
	if (bp == NULL)
		return -1;

	l = (struct sun_disk_label *) bp;
	if(be16_to_cpu(l->magic) != SUN_DISK_MAGIC)
		return -1;

	if (!sun_verify_checksum(l)) {
		fprintf(stderr, "Corrupted Sun disk label\n");
		return -1;
	}

	for(i=0, n=0; i<SUN_DISK_MAXPARTITIONS; i++) {
		s = &l->partitions[i];

		if (s->num_sectors == 0)
			continue;
		if (n < ns) {
			sp[n].start = offset +
				be32_to_cpu(s->start_cylinder) * be16_to_cpu(l->nsect) * be16_to_cpu(l->ntrks);
			sp[n].size = be32_to_cpu(s->num_sectors);
			n++;
		} else {
			fprintf(stderr,
				"sun_disklabel: too many slices\n");
			break;
		}
	}
	/*
	 * Convention has it that the SUN disklabel will always have
	 * the 'c' partition spanning the entire disk.
	 * So we have to check for contained slices.
	 */
	for(i = 0; i < SUN_DISK_MAXPARTITIONS; i++) {
		if (sp[i].size == 0)
			continue;

		end = sp[i].start + sp[i].size;
		for(j = 0; j < SUN_DISK_MAXPARTITIONS; j ++) {
			if ( i == j )
				continue;
			if (sp[j].size == 0)
				continue;

			if (sp[i].start < sp[j].start) {
				if (end > sp[j].start &&
				    end < sp[j].start + sp[j].size) {
					/* Invalid slice */
					fprintf(stderr,
						"sun_disklabel: slice %d overlaps with %d\n", i , j);
					sp[i].size = 0;
				}
			}
		}
	}
	return n;
}
