#ifndef UTIL_LINUX_FDISK_H
#define UTIL_LINUX_FDISK_H
/*
   fdisk.h
*/

#include "c.h"

/* Let's temporary include private libfdisk header file. The final libfdisk.h
 * maybe included when fdisk.c and libfdisk code will be completely spit.
 */
#include "fdiskP.h"
#include "blkdev.h"
#include "colors.h"

extern int get_user_reply(struct fdisk_context *cxt,
			  const char *prompt,
			  char *buf, size_t bufsz);
extern int process_fdisk_menu(struct fdisk_context **cxt);

extern int ask_callback(struct fdisk_context *cxt, struct fdisk_ask *ask,
		    void *data __attribute__((__unused__)));

/* prototypes for fdisk.c */
extern void dump_firstsector(struct fdisk_context *cxt);
extern void dump_disklabel(struct fdisk_context *cxt);

extern void list_partition_types(struct fdisk_context *cxt);
extern void list_disk_geometry(struct fdisk_context *cxt);
extern void list_disklabel(struct fdisk_context *cxt);
extern void change_partition_type(struct fdisk_context *cxt);
extern struct fdisk_parttype *ask_partition_type(struct fdisk_context *cxt);

extern void toggle_dos_compatibility_flag(struct fdisk_context *cxt);

#endif /* UTIL_LINUX_FDISK_H */
