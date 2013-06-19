#ifndef FDISK_DOS_LABEL_H
#define FDISK_DOS_LABEL_H

#include "pt-mbr.h"

/*
 * per partition table entry data
 *
 * The four primary partitions have the same sectorbuffer
 * and have NULL ex_entry.
 *
 * Each logical partition table entry has two pointers, one for the
 * partition and one link to the next one.
 */
struct pte {
	struct dos_partition *pt_entry;	/* on-disk MBR entry */
	struct dos_partition *ex_entry;	/* on-disk EBR entry */
	char changed;			/* boolean */
	sector_t offset;	        /* disk sector number */
	unsigned char *sectorbuffer;	/* disk sector contents */
};

extern struct pte ptes[MAXIMUM_PARTS];

#define pt_offset(b, n)	((struct dos_partition *)((b) + 0x1be + \
					      (n) * sizeof(struct dos_partition)))

extern sector_t extended_offset;

extern struct dos_partition *dos_get_pt_entry(int);

extern void dos_print_mbr_id(struct fdisk_context *cxt);
extern int dos_set_mbr_id(struct fdisk_context *cxt);
extern void dos_init(struct fdisk_context *cxt);

extern int dos_list_table(struct fdisk_context *cxt, int xtra);
extern void dos_list_table_expert(struct fdisk_context *cxt, int extend);

extern void dos_fix_partition_table_order(struct fdisk_context *cxt);
extern void dos_move_begin(struct fdisk_context *cxt, int i);
extern void dos_toggle_active(struct fdisk_context *cxt, int i);

#define is_dos_compatible(_x) \
		   (fdisk_is_disklabel(_x, DOS) && \
                    fdisk_dos_is_compatible(fdisk_context_get_label(_x, NULL)))

/* toggle flags */
#define DOS_FLAG_ACTIVE	1

#endif
