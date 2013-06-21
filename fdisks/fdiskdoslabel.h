#ifndef FDISK_DOS_LABEL_H
#define FDISK_DOS_LABEL_H

#include "pt-mbr.h"

extern struct dos_partition *fdisk_dos_get_partition(
				struct fdisk_context *cxt,
				size_t i);

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
