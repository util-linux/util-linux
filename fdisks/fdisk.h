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

#define cround(n)	(display_in_cyl_units ? ((n)/units_per_sector)+1 : (n))
#define scround(x)	(((x)+units_per_sector-1)/units_per_sector)

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

enum menutype {
	MAIN_MENU,
	EXPERT_MENU,
};

enum failure {
	ioctl_error,
	unable_to_read,
	unable_to_seek,
	unable_to_write
};

/*
 * labels
 */
extern struct fdisk_context *fdisk_new_context_from_filename(const char *fname, int readonly);
extern void fdisk_free_context(struct fdisk_context *cxt);
extern struct fdisk_parttype *fdisk_get_partition_type(struct fdisk_context *cxt, int partnum);
extern int fdisk_set_partition_type(struct fdisk_context *cxt, int partnum,
			     struct fdisk_parttype *t);

/* prototypes for fdisk.c */
extern char *line_ptr;
extern int partitions;
extern unsigned int display_in_cyl_units, units_per_sector;

extern void check_consistency(struct fdisk_context *cxt, struct partition *p, int partition);
extern void check(struct fdisk_context *cxt, int n, unsigned int h, unsigned int s, unsigned int c, unsigned int start);

extern void fatal(struct fdisk_context *cxt, enum failure why);
extern int  get_partition(struct fdisk_context *cxt, int warn, int max);
extern void list_partition_types(struct fdisk_context *cxt);
extern int read_line (int *asked);
extern char read_char(char *mesg);
extern struct fdisk_parttype *read_partition_type(struct fdisk_context *cxt);
extern void reread_partition_table(struct fdisk_context *cxt, int leave);
extern struct partition *get_part_table(int);
extern unsigned int read_int(struct fdisk_context *cxt,
			     unsigned int low, unsigned int dflt,
			     unsigned int high, unsigned int base, char *mesg);
extern void print_menu(struct fdisk_context *cxt, enum menutype menu);
extern void print_partition_size(struct fdisk_context *cxt, int num, sector_t start, sector_t stop, int sysid);

extern void fill_bounds(sector_t *first, sector_t *last);

extern char *partition_type(struct fdisk_context *cxt, unsigned char type);
extern char read_chars(char *mesg);
extern void set_changed(int);
extern void set_all_unchanged(void);
extern int warn_geometry(struct fdisk_context *cxt);
extern void warn_limits(struct fdisk_context *cxt);
extern unsigned int read_int_with_suffix(struct fdisk_context *cxt,
					 unsigned int low, unsigned int dflt, unsigned int high,
				  unsigned int base, char *mesg, int *is_suffix_used);
extern int get_partition_dflt(struct fdisk_context *cxt, int warn, int max, int dflt);

#define PLURAL	0
#define SINGULAR 1
extern const char * str_units(int);

extern sector_t get_nr_sects(struct partition *p);

extern int nowarn;
extern int MBRbuffer_changed;

/* start_sect and nr_sects are stored little endian on all machines */
/* moreover, they are not aligned correctly */
static inline void
store4_little_endian(unsigned char *cp, unsigned int val) {
	cp[0] = (val & 0xff);
	cp[1] = ((val >> 8) & 0xff);
	cp[2] = ((val >> 16) & 0xff);
	cp[3] = ((val >> 24) & 0xff);
}

static inline unsigned int read4_little_endian(const unsigned char *cp)
{
	return (unsigned int)(cp[0]) + ((unsigned int)(cp[1]) << 8)
		+ ((unsigned int)(cp[2]) << 16)
		+ ((unsigned int)(cp[3]) << 24);
}

static inline void set_nr_sects(struct partition *p, sector_t nr_sects)
{
	store4_little_endian(p->size4, nr_sects);
}

static inline void set_start_sect(struct partition *p, unsigned int start_sect)
{
	store4_little_endian(p->start4, start_sect);
}

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

static inline sector_t get_start_sect(struct partition *p)
{
	return read4_little_endian(p->start4);
}

static inline int is_cleared_partition(struct partition *p)
{
	return !(!p || p->boot_ind || p->head || p->sector || p->cyl ||
		 p->sys_ind || p->end_head || p->end_sector || p->end_cyl ||
		 get_start_sect(p) || get_nr_sects(p));
}
