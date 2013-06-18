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

struct partition {
	unsigned char boot_ind;         /* 0x80 - active */
	unsigned char head;             /* starting head */
	unsigned char sector;           /* starting sector */
	unsigned char cyl;              /* starting cylinder */
	unsigned char sys_ind;          /* What partition type */
	unsigned char end_head;         /* end head */
	unsigned char end_sector;       /* end sector */
	unsigned char end_cyl;          /* end cylinder */
	unsigned char start4[4];        /* starting sector counting from 0 */
	unsigned char size4[4];         /* nr of sectors in partition */
} __attribute__ ((packed));

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
extern unsigned int read_int(struct fdisk_context *cxt,
			     unsigned int low, unsigned int dflt,
			     unsigned int high, unsigned int base, char *mesg);

extern char *partition_type(struct fdisk_context *cxt, unsigned char type);
extern char read_chars(struct fdisk_context *cxt, char *mesg);
extern int warn_geometry(struct fdisk_context *cxt);
extern void warn_limits(struct fdisk_context *cxt);
extern unsigned int read_int_with_suffix(struct fdisk_context *cxt,
					 unsigned int low, unsigned int dflt, unsigned int high,
				  unsigned int base, char *mesg, int *is_suffix_used);

extern int nowarn;


static inline int seek_sector(struct fdisk_context *cxt, sector_t secno)
{
	off_t offset = (off_t) secno * cxt->sector_size;

	return lseek(cxt->dev_fd, offset, SEEK_SET) == (off_t) -1 ? -errno : 0;
}

static inline int read_sector(struct fdisk_context *cxt, sector_t secno, unsigned char *buf)
{
	int rc = seek_sector(cxt, secno);

	if (rc < 0)
		return rc;

	return read(cxt->dev_fd, buf, cxt->sector_size) !=
			(ssize_t) cxt->sector_size ? -errno : 0;
}


