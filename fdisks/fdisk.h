/*
   fdisk.h
*/

#include "c.h"

/* Let's temporary include private libfdisk header file. The final libfdisk.h
 * maybe included when fdisk.c and libfdisk code will be completely spit.
 */
#include "fdiskP.h"
#include "blkdev.h"


extern void toggle_units(struct fdisk_context *cxt);

static inline unsigned long
scround(struct fdisk_context *cxt, unsigned long num)
{
	unsigned long un = fdisk_context_get_units_per_sector(cxt);
	return (num + un - 1) / un;
}


extern int get_user_reply(struct fdisk_context *cxt,
			  const char *prompt,
			  char *buf, size_t bufsz);
extern int print_fdisk_menu(struct fdisk_context *cxt);
extern int process_fdisk_menu(struct fdisk_context *cxt);

extern int ask_callback(struct fdisk_context *cxt, struct fdisk_ask *ask,
		    void *data __attribute__((__unused__)));

/* prototypes for fdisk.c */
extern void list_partition_types(struct fdisk_context *cxt);
extern struct fdisk_parttype *ask_partition_type(struct fdisk_context *cxt);
extern void reread_partition_table(struct fdisk_context *cxt, int leave);

extern char *partition_type(struct fdisk_context *cxt, unsigned char type);
extern int warn_geometry(struct fdisk_context *cxt);
extern void toggle_dos_compatibility_flag(struct fdisk_context *cxt);
extern void warn_limits(struct fdisk_context *cxt);

