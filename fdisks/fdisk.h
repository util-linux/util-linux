/*
   fdisk.h
*/

#include "c.h"

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

#define ALIGN_UP	1
#define ALIGN_DOWN	2
#define ALIGN_NEAREST	3

#define LINE_LENGTH	800

#define IS_EXTENDED(i) \
	((i) == EXTENDED || (i) == WIN98_EXTENDED || (i) == LINUX_EXTENDED)

#define cround(n)	(display_in_cyl_units ? ((n)/units_per_sector)+1 : (n))
#define scround(x)	(((x)+units_per_sector-1)/units_per_sector)

/* fdisk debugging flags/options */
#define FDISK_DEBUG_INIT	(1 << 1)
#define FDISK_DEBUG_CONTEXT	(1 << 2)
#define FDISK_DEBUG_TOPOLOGY    (1 << 3)
#define FDISK_DEBUG_GEOMETRY    (1 << 4)
#define FDISK_DEBUG_LABEL       (1 << 5)
#define FDISK_DEBUG_ALL		0xFFFF

# define ON_DBG(m, x)	do { \
				if ((FDISK_DEBUG_ ## m) & fdisk_debug_mask) { \
					x; \
				}	   \
			} while (0)

# define DBG(m, x)	do { \
				if ((FDISK_DEBUG_ ## m) & fdisk_debug_mask) { \
					fprintf(stderr, "%d: fdisk: %8s: ", getpid(), # m); \
					x;				\
				} \
			} while (0)

# define DBG_FLUSH	do { \
				if (fdisk_debug_mask && \
				    fdisk_debug_mask != FDISK_DEBUG_INIT) \
					fflush(stderr);			\
			} while(0)

static inline void __attribute__ ((__format__ (__printf__, 1, 2)))
dbgprint(const char *mesg, ...)
{
	va_list ap;
	va_start(ap, mesg);
	vfprintf(stderr, mesg, ap);
	va_end(ap);
	fputc('\n', stderr);
}

extern int fdisk_debug_mask;
extern void fdisk_init_debug(int mask);

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

typedef unsigned long long sector_t;

/*
 * Supported partition table types (labels)
 */
enum fdisk_labeltype {
	FDISK_DISKLABEL_DOS = 1,
	FDISK_DISKLABEL_SUN = 2,
	FDISK_DISKLABEL_SGI = 4,
	FDISK_DISKLABEL_AIX = 8,
	FDISK_DISKLABEL_OSF = 16,
	FDISK_DISKLABEL_MAC = 32,
	FDISK_DISKLABEL_GPT = 64,
	FDISK_DISKLABEL_ANY = -1
};

/*
 * Partition types
 */
struct fdisk_parttype {
	unsigned int	type;		/* type as number or zero */
	const char	*name;		/* description */
	char		*typestr;	/* type as string or NULL */

	unsigned int	flags;		/* FDISK_PARTTYPE_* flags */
};

enum {
	FDISK_PARTTYPE_UNKNOWN		= (1 << 1),
	FDISK_PARTTYPE_INVISIBLE	= (1 << 2),
	FDISK_PARTTYPE_ALLOCATED	= (1 << 3)
};

#define fdisk_parttype_is_unknown(_x)	((_x) && ((_x)->flags & FDISK_PARTTYPE_UNKNONW))
#define fdisk_parttype_is_invisible(_x)	((_x) && ((_x)->flags & FDISK_PARTTYPE_INVISIBLE))
#define fdisk_parttype_is_allocated(_x)	((_x) && ((_x)->flags & FDISK_PARTTYPE_ALLOCATED))

/*
 * Legacy CHS based geometry
 */
struct fdisk_geometry {
	unsigned int heads;
	sector_t sectors;
	sector_t cylinders;
};

struct fdisk_context {
	int dev_fd;         /* device descriptor */
	char *dev_path;     /* device path */
	unsigned char *firstsector; /* buffer with master boot record */

	/* topology */
	unsigned long io_size;		/* I/O size used by fdisk */
	unsigned long optimal_io_size;	/* optional I/O returned by device */
	unsigned long min_io_size;	/* minimal I/O size */
	unsigned long phy_sector_size;	/* physical size */
	unsigned long sector_size;	/* logical size */
	unsigned long alignment_offset;

	enum fdisk_labeltype disklabel;	/* current disklabel */

	/* alignment */
	unsigned long grain;		/* alignment unit */
	sector_t first_lba;		/* recommended begin of the first partition */

	/* geometry */
	sector_t total_sectors; /* in logical sectors */
	struct fdisk_geometry geom;

	/* label operations and description */
	const struct fdisk_label *label;
};

#define fdisk_is_disklabel(c, x) fdisk_dev_is_disklabel(c, FDISK_DISKLABEL_ ## x)

/*
 * Label specific operations
 */
struct fdisk_label {
	const char *name;

	/* array with partition types */
	struct fdisk_parttype	*parttypes;
	size_t			nparttypes;	/* number of items in parttypes[] */

	/* probe disk label */
	int (*probe)(struct fdisk_context *cxt);
	/* write in-memory changes to disk */
	int (*write)(struct fdisk_context *cxt);
	/* verify the partition table */
	int (*verify)(struct fdisk_context *cxt);
	/* create new disk label */
	int (*create)(struct fdisk_context *cxt);
	/* new partition */
	int (*part_add)(struct fdisk_context *cxt, int partnum, struct fdisk_parttype *t);
	/* delete partition */
	int (*part_delete)(struct fdisk_context *cxt, int partnum);
	/* get partition type */
	struct fdisk_parttype *(*part_get_type)(struct fdisk_context *cxt, int partnum);
	/* set partition type */
	int (*part_set_type)(struct fdisk_context *cxt, int partnum, struct fdisk_parttype *t);
	/* refresh alignment setting */
	int (*reset_alignment)(struct fdisk_context *cxt);
};

/*
 * labels
 */
extern const struct fdisk_label aix_label;
extern const struct fdisk_label dos_label;
extern const struct fdisk_label bsd_label;
extern const struct fdisk_label mac_label;
extern const struct fdisk_label sun_label;
extern const struct fdisk_label sgi_label;
extern const struct fdisk_label gpt_label;

extern struct fdisk_context *fdisk_new_context_from_filename(const char *fname, int readonly);
extern int fdisk_dev_has_topology(struct fdisk_context *cxt);
extern int fdisk_dev_has_disklabel(struct fdisk_context *cxt);
extern int fdisk_dev_is_disklabel(struct fdisk_context *cxt, enum fdisk_labeltype l);
extern int fdisk_dev_sectsz_is_default(struct fdisk_context *cxt);
extern void fdisk_free_context(struct fdisk_context *cxt);
extern void fdisk_zeroize_firstsector(struct fdisk_context *cxt);
extern int fdisk_context_force_sector_size(struct fdisk_context *cxt, sector_t s);
extern int fdisk_context_set_user_geometry(struct fdisk_context *cxt,
			    unsigned int cylinders, unsigned int heads,
			    unsigned int sectors);
extern int fdisk_delete_partition(struct fdisk_context *cxt, int partnum);
extern int fdisk_add_partition(struct fdisk_context *cxt, int partnum, struct fdisk_parttype *t);
extern int fdisk_write_disklabel(struct fdisk_context *cxt);
extern int fdisk_verify_disklabel(struct fdisk_context *cxt);
extern int fdisk_create_disklabel(struct fdisk_context *cxt, const char *name);
extern int fdisk_reset_alignment(struct fdisk_context *cxt);
extern struct fdisk_parttype *fdisk_get_partition_type(struct fdisk_context *cxt, int partnum);
extern int fdisk_set_partition_type(struct fdisk_context *cxt, int partnum,
			     struct fdisk_parttype *t);

extern size_t fdisk_get_nparttypes(struct fdisk_context *cxt);
extern struct fdisk_parttype *fdisk_get_parttype_from_code(struct fdisk_context *cxt,
                                unsigned int code);
extern struct fdisk_parttype *fdisk_get_parttype_from_string(struct fdisk_context *cxt,
                                const char *str);
extern struct fdisk_parttype *fdisk_parse_parttype(struct fdisk_context *cxt, const char *str);

extern struct fdisk_parttype *fdisk_new_unknown_parttype(unsigned int type, const char *typestr);
extern void fdisk_free_parttype(struct fdisk_parttype *type);

extern sector_t fdisk_topology_get_first_lba(struct fdisk_context *cxt);
extern unsigned long fdisk_topology_get_grain(struct fdisk_context *cxt);

/* prototypes for fdisk.c */
extern char *line_ptr;
extern int partitions;
extern unsigned int display_in_cyl_units, units_per_sector;

extern void check_consistency(struct fdisk_context *cxt, struct partition *p, int partition);
extern void check_alignment(struct fdisk_context *cxt, sector_t lba, int partition);
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
extern sector_t align_lba(struct fdisk_context *cxt, sector_t lba, int direction);
extern sector_t align_lba_in_range(struct fdisk_context *cxt, sector_t lba, sector_t start, sector_t stop);
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
