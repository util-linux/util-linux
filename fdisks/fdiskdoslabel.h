#ifndef FDISK_DOS_LABEL_H
#define FDISK_DOS_LABEL_H

/*
 * per partition table entry data
 *
 * The four primary partitions have the same sectorbuffer (MBRbuffer)
 * and have NULL ext_pointer.
 * Each logical partition table entry has two pointers, one for the
 * partition and one link to the next one.
 */
struct pte {
	struct partition *part_table;	/* points into sectorbuffer */
	struct partition *ext_pointer;	/* points into sectorbuffer */
	char changed;			/* boolean */
	sector_t offset;	        /* disk sector number */
	unsigned char *sectorbuffer;	/* disk sector contents */
};

extern struct pte ptes[MAXIMUM_PARTS];
extern int dos_compatible_flag;

#define pt_offset(b, n)	((struct partition *)((b) + 0x1be + \
					      (n) * sizeof(struct partition)))

extern int ext_index; /* the prime extended partition */
extern sector_t extended_offset, sector_offset;

/* A valid partition table sector ends in 0x55 0xaa */
static inline unsigned int part_table_flag(unsigned char *b)
{
	return ((unsigned int) b[510]) + (((unsigned int) b[511]) << 8);
}

static inline sector_t get_partition_start(struct pte *pe)
{
	return pe->offset + get_start_sect(pe->part_table);
}

extern void dos_print_mbr_id(struct fdisk_context *cxt);
extern void dos_set_mbr_id(struct fdisk_context *cxt);
extern int is_dos_partition(int t);
extern void dos_init(struct fdisk_context *cxt);

extern int mbr_is_valid_magic(unsigned char *b);

#endif
