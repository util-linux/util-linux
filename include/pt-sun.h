#ifndef UTIL_LINUX_PT_SUN_H
#define UTIL_LINUX_PT_SUN_H

#include <stdint.h>

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
			uint16_t id;  /* tag */
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


static inline uint16_t sun_pt_checksum(struct sun_disklabel *label)
{
	uint16_t *ptr = ((uint16_t *) (label + 1)) - 1;
	uint16_t sum;

	for (sum = 0; ptr >= ((uint16_t *) label);)
		sum ^= *ptr--;

	return sum;
}

#endif /* UTIL_LINUX_PT_SUN_H */
