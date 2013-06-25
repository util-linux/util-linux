/*
   fdisk.h
*/

#include "c.h"

/* Let's temporary include private libfdisk header file. The final libfdisk.h
 * maybe included when fdisk.c and libfdisk code will be completely spit.
 */
#include "fdiskP.h"


#define DEFAULT_SECTOR_SIZE	512
#define MAX_SECTOR_SIZE	2048
#define SECTOR_SIZE	512	/* still used in BSD code */
#define MAXIMUM_PARTS	60

#define ACTIVE_FLAG     0x80

#define EXTENDED        0x05
#define WIN98_EXTENDED  0x0f
#define LINUX_PARTITION 0x81
#define LINUX_SWAP      0x82
#define LINUX_NATIVE    0x83
#define LINUX_EXTENDED  0x85
#define LINUX_LVM       0x8e
#define LINUX_RAID      0xfd


#define LINE_LENGTH	800

#define IS_EXTENDED(i) \
	((i) == EXTENDED || (i) == WIN98_EXTENDED || (i) == LINUX_EXTENDED)

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

extern int nowarn;

