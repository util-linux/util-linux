/*
 * fdiskP.h - private library header file
 *
 * Copyright (C) 2012 Karel Zak <kzak@redhat.com>
 *
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 */

#ifndef _LIBFDISK_PRIVATE_H
#define _LIBFDISK_PRIVATE_H

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "c.h"
#include "libfdisk.h"

#include "nls.h"		/* temporary before dialog API will be implamented */
#include "tt.h"

/* features */
#define CONFIG_LIBFDISK_ASSERT
#define CONFIG_LIBFDISK_DEBUG

#ifdef CONFIG_LIBFDISK_ASSERT
#include <assert.h>
#endif

/*
 * Debug
 */
#if defined(TEST_PROGRAM) && !defined(LIBFDISK_DEBUG)
#define CONFIG_LIBFDISK_DEBUG
#endif

#ifdef CONFIG_LIBFDISK_DEBUG
# include <stdio.h>
# include <stdarg.h>

/* fdisk debugging flags/options */
#define FDISK_DEBUG_INIT	(1 << 1)
#define FDISK_DEBUG_CONTEXT	(1 << 2)
#define FDISK_DEBUG_TOPOLOGY    (1 << 3)
#define FDISK_DEBUG_GEOMETRY    (1 << 4)
#define FDISK_DEBUG_LABEL       (1 << 5)
#define FDISK_DEBUG_ASK         (1 << 6)
#define FDISK_DEBUG_FRONTEND	(1 << 7)
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

#else /* !CONFIG_LIBFDISK_DEBUG */
# define ON_DBG(m,x) do { ; } while (0)
# define DBG(m,x) do { ; } while (0)
# define DBG_FLUSH do { ; } while(0)
#endif


#ifdef TEST_PROGRAM
struct fdisk_test {
	const char	*name;
	int		(*body)(struct fdisk_test *ts, int argc, char *argv[]);
	const char	*usage;
};

/* test.c */
extern int fdisk_run_test(struct fdisk_test *tests, int argc, char *argv[]);
#endif


typedef unsigned long long sector_t;

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

/*
 * Label specific operations
 */
struct fdisk_label_operations {
	/* probe disk label */
	int (*probe)(struct fdisk_context *cxt);
	/* write in-memory changes to disk */
	int (*write)(struct fdisk_context *cxt);
	/* verify the partition table */
	int (*verify)(struct fdisk_context *cxt);
	/* create new disk label */
	int (*create)(struct fdisk_context *cxt);
	/* list partition table */
	int (*list)(struct fdisk_context *cxt);

	/* new partition */
	int (*part_add)(struct fdisk_context *cxt,
						size_t partnum,
						struct fdisk_parttype *t);
	/* delete partition */
	int (*part_delete)(struct fdisk_context *cxt,
						size_t partnum);
	/* get partition type */
	struct fdisk_parttype *(*part_get_type)(struct fdisk_context *cxt,
						size_t partnum);
	/* set partition type */
	int (*part_set_type)(struct fdisk_context *cxt,
						size_t partnum,
						struct fdisk_parttype *t);

	/* returns FDISK_PARTSTAT_* flags */
	int (*part_get_status)(struct fdisk_context *cxt,
						size_t partnum,
						int *status);

	int (*part_toggle_flag)(struct fdisk_context *cxt, size_t i, unsigned long flag);

	/* refresh alignment setting */
	int (*reset_alignment)(struct fdisk_context *cxt);

	/* free in-memory label stuff */
	void (*free)(struct fdisk_label *lb);

	/* deinit in-memory label stuff */
	void (*deinit)(struct fdisk_label *lb);
};

/*
 * Generic label
 */
struct fdisk_label {
	const char		*name;		/* label name */
	enum fdisk_labeltype	id;		/* FDISK_DISKLABEL_* */
	struct fdisk_parttype	*parttypes;	/* supported partitions types */
	size_t			nparttypes;	/* number of items in parttypes[] */

	size_t			nparts_max;	/* maximal number of partitions */
	size_t			nparts_cur;	/* number of currently used partitions */

	int			flags;		/* FDISK_LABEL_FL_* flags */

	unsigned int		changed:1;	/* label has been modified */

	const struct fdisk_label_operations *op;
};

/* label driver flags */
enum {
	FDISK_LABEL_FL_ADDPART_NOPARTNO = (1 << 1)
};

/* label allocators */
extern struct fdisk_label *fdisk_new_gpt_label(struct fdisk_context *cxt);
extern struct fdisk_label *fdisk_new_dos_label(struct fdisk_context *cxt);
extern struct fdisk_label *fdisk_new_bsd_label(struct fdisk_context *cxt);
extern struct fdisk_label *fdisk_new_mac_label(struct fdisk_context *cxt);
extern struct fdisk_label *fdisk_new_sgi_label(struct fdisk_context *cxt);
extern struct fdisk_label *fdisk_new_sun_label(struct fdisk_context *cxt);


/* fdisk dialog -- note that nothing from this stuff will be directly exported,
 * we will have get/set() function for everything.
 */
struct fdisk_ask {
	int		type;		/* FDISK_ASKTYPE_* */
	char		*query;

	union {
		/* FDISK_ASKTYPE_{NUMBER,OFFSET} */
		struct ask_number {
			uint64_t	hig;		/* high limit */
			uint64_t	low;		/* low limit */
			uint64_t	dfl;		/* default */
			uint64_t	result;
			uint64_t	base;		/* for relative results */
			uint64_t	unit;		/* unit for offsets */
			const char	*range;		/* by library generated list */
			unsigned int	relative:1;
		} num;
		/* FDISK_ASKTYPE_{WARN,WARNX,..} */
		struct ask_print {
			const char	*mesg;
			int		errnum;		/* errno */
		} print;
		/* FDISK_ASKTYPE_YESNO */
		struct ask_yesno {
			int		result;		/* TRUE or FALSE */
		} yesno;
		/* FDISK_ASKTYPE_STRING */
		struct ask_string {
			char		*result;	/* allocated */
		} str;
		/* FDISK_ASKTYPE_TABLE, see include/tt.h  */
		struct tt *table;
	} data;
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

	unsigned int display_in_cyl_units : 1,	/* for obscure labels */
		     display_details : 1;	/* expert display mode */

	/* alignment */
	unsigned long grain;		/* alignment unit */
	sector_t first_lba;		/* recommended begin of the first partition */

	/* geometry */
	sector_t total_sectors; /* in logical sectors */
	struct fdisk_geometry geom;

	struct fdisk_label *label;	/* current label, pointer to labels[] */

	size_t nlabels;			/* number of initialized label drivers */
	struct fdisk_label *labels[8];	/* all supported labels,
					 * FIXME: use any enum rather than hardcoded number */

	int	(*ask_cb)(struct fdisk_context *, struct fdisk_ask *, void *);	/* fdisk dialogs callback */
	void	*ask_data;		/* ask_cb() data */

	struct fdisk_context	*parent;	/* for nested PT */
};

/* context.c */
extern int __fdisk_context_switch_label(struct fdisk_context *cxt,
				    struct fdisk_label *lb);

extern int fdisk_context_use_cylinders(struct fdisk_context *cxt);
extern int fdisk_context_display_details(struct fdisk_context *cxt);


/* alignment.c */
extern sector_t fdisk_scround(struct fdisk_context *cxt, sector_t num);

extern sector_t fdisk_topology_get_first_lba(struct fdisk_context *cxt);
extern unsigned long fdisk_topology_get_grain(struct fdisk_context *cxt);

extern void fdisk_warn_alignment(struct fdisk_context *cxt,
				 sector_t lba, int partition);


#define FDISK_ALIGN_UP		1
#define FDISK_ALIGN_DOWN	2
#define FDISK_ALIGN_NEAREST	3

extern sector_t fdisk_align_lba(struct fdisk_context *cxt, sector_t lba, int direction);
extern sector_t fdisk_align_lba_in_range(struct fdisk_context *cxt, sector_t lba,
					 sector_t start, sector_t stop);


extern int fdisk_override_sector_size(struct fdisk_context *cxt, sector_t s);
extern int fdisk_override_geometry(struct fdisk_context *cxt,
		            unsigned int cylinders, unsigned int heads,
                            unsigned int sectors);

extern int fdisk_discover_geometry(struct fdisk_context *cxt);
extern int fdisk_discover_topology(struct fdisk_context *cxt);

/* utils.c */
extern void fdisk_zeroize_firstsector(struct fdisk_context *cxt);
extern int fdisk_read_firstsector(struct fdisk_context *cxt);
extern char *fdisk_partname(const char *dev, size_t partno);

/* label.c */
extern int fdisk_probe_labels(struct fdisk_context *cxt);
extern void fdisk_deinit_label(struct fdisk_label *lb);


/* ask.c */
extern int fdisk_ask_partnum(struct fdisk_context *cxt, size_t *partnum, int wantnew);

extern struct tt *fdisk_ask_get_table(struct fdisk_ask *ask);
extern int fdisk_print_table(struct fdisk_context *cxt, struct tt *tb);

extern int fdisk_info_new_partition(
			struct fdisk_context *cxt,
			int num, sector_t start, sector_t stop,
			struct fdisk_parttype *t);

#endif /* _LIBFDISK_PRIVATE_H */
