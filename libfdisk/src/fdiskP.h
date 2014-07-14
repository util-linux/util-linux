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
#include "list.h"
#include "debug.h"
#include <stdio.h>
#include <stdarg.h>

/* features */
#define CONFIG_LIBFDISK_ASSERT

#ifdef CONFIG_LIBFDISK_ASSERT
#include <assert.h>
#endif

/*
 * Debug
 */
#define FDISK_DEBUG_INIT	(1 << 1)
#define FDISK_DEBUG_CXT		(1 << 2)
#define FDISK_DEBUG_LABEL       (1 << 3)
#define FDISK_DEBUG_ASK         (1 << 4)
#define FDISK_DEBUG_FRONTEND	(1 << 5)
#define FDISK_DEBUG_PART	(1 << 6)
#define FDISK_DEBUG_PARTTYPE	(1 << 7)
#define FDISK_DEBUG_TAB		(1 << 8)
#define FDISK_DEBUG_ALL		0xFFFF

UL_DEBUG_DECLARE_MASK(libfdisk);
#define DBG(m, x)	__UL_DBG(libfdisk, FDISK_DEBUG_, m, x)
#define ON_DBG(m, x)	__UL_DBG_CALL(libfdisk, FDISK_DEBUG_, m, x)
#define DBG_FLUSH	__UL_DBG_FLUSH(libfdisk, FDISK_DEBUG_)

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
 * Generic iterator
 */
struct fdisk_iter {
        struct list_head        *p;		/* current position */
        struct list_head        *head;		/* start position */
	int			direction;	/* FDISK_ITER_{FOR,BACK}WARD */
};

#define IS_ITER_FORWARD(_i)	((_i)->direction == FDISK_ITER_FORWARD)
#define IS_ITER_BACKWARD(_i)	((_i)->direction == FDISK_ITER_BACKWARD)

#define FDISK_ITER_INIT(itr, list) \
	do { \
		(itr)->p = IS_ITER_FORWARD(itr) ? \
				(list)->next : (list)->prev; \
		(itr)->head = (list); \
	} while(0)

#define FDISK_ITER_ITERATE(itr, res, restype, member) \
	do { \
		res = list_entry((itr)->p, restype, member); \
		(itr)->p = IS_ITER_FORWARD(itr) ? \
				(itr)->p->next : (itr)->p->prev; \
	} while(0)

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

struct fdisk_partition {
	int		refcount;		/* reference counter */
	size_t		partno;			/* partition number */
	size_t		parent_partno;		/* for logical partitions */

	uint64_t	start;			/* first sectors */
	uint64_t	end;			/* last sector */
	uint64_t	size;			/* size in sectors */

	char		*name;			/* partition name */
	char		*uuid;			/* partition UUID */
	char		*attrs;			/* partition flags/attributes converted to string */
	struct fdisk_parttype	*type;		/* partition type */

	struct list_head	parts;		/* list of partitions */

	/* extra fields for partition_to_string() */
	char		start_post;		/* start postfix  (e.g. '+') */
	char		end_post;		/* end postfix */
	char		size_post;		/* size postfix */

	uint64_t	fsize;			/* bsd junk */
	uint64_t	bsize;
	uint64_t	cpg;

	char		boot;			/* is bootable (MBS only) */
	char		*start_addr;		/* start C/H/S in string */
	char		*end_addr;		/* end C/H/S in string */

	unsigned int	partno_follow_default : 1,	/* use default partno */
			start_follow_default : 1,	/* use default start */
			end_follow_default : 1,		/* use default end */
			freespace : 1,		/* this is free space */
			container : 1,		/* container partition (e.g. extended partition) */
			wholedisk : 1,		/* special system partition */
			used   : 1;		/* partition already used */
};

#define FDISK_EMPTY_PARTNO	((size_t) -1)
#define FDISK_EMPTY_PARTITION	{ .partno = FDISK_EMPTY_PARTNO }

struct fdisk_table {
	struct list_head	parts;		/* partitions */
	int			refcount;
	size_t			nents;		/* number of partitions */
};

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
	/* list disklabel details */
	int (*list)(struct fdisk_context *cxt);
	/* returns offset and size of the 'n' part of the PT */
	int (*locate)(struct fdisk_context *cxt, int n, const char **name, off_t *offset, size_t *size);
	/* reorder partitions */
	int (*reorder)(struct fdisk_context *cxt);

	/* get disk label ID */
	int (*get_id)(struct fdisk_context *cxt, char **id);
	/* set disk label ID */
	int (*set_id)(struct fdisk_context *cxt);

	/* new partition */
	int (*add_part)(struct fdisk_context *cxt, struct fdisk_partition *pa);

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

	/* return state of the partition */
	int (*part_is_used)(struct fdisk_context *cxt, size_t partnum);

	/* fill in partition struct */
	int (*get_part)(struct fdisk_context *cxt,
						size_t n,
						struct fdisk_partition *pa);

	int (*part_toggle_flag)(struct fdisk_context *cxt, size_t i, unsigned long flag);

	/* refresh alignment setting */
	int (*reset_alignment)(struct fdisk_context *cxt);

	/* free in-memory label stuff */
	void (*free)(struct fdisk_label *lb);

	/* deinit in-memory label stuff */
	void (*deinit)(struct fdisk_label *lb);
};

/*
 * fdisk_label_operations->list() output column
 */
struct fdisk_column {
	int		id;		/* FDISK_COL_* */
	const char	*name;		/* column header */
	double		width;
	int		scols_flags;	/* SCOLS_FL_* */

	int		flags;		/* FDISK_COLFL_* */
};

/* note that the defauls is to display a column always */
enum {
	FDISK_COLFL_DETAIL	= (1 << 1),	/* only display if fdisk_context_display_details() */
	FDISK_COLFL_EYECANDY	= (1 << 2),	/* don't display if fdisk_context_display_details() */
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

	unsigned int		changed:1,	/* label has been modified */
				disabled:1;	/* this driver is disabled at all */

	const struct fdisk_column *columns;	/* all possible columns */
	size_t			ncolumns;

	const struct fdisk_label_operations *op;
};


/* label driver flags */
enum {
	FDISK_LABEL_FL_REQUIRE_GEOMETRY = (1 << 2),
	FDISK_LABEL_FL_INCHARS_PARTNO   = (1 << 3)
};

/* label allocators */
extern struct fdisk_label *fdisk_new_gpt_label(struct fdisk_context *cxt);
extern struct fdisk_label *fdisk_new_dos_label(struct fdisk_context *cxt);
extern struct fdisk_label *fdisk_new_bsd_label(struct fdisk_context *cxt);
extern struct fdisk_label *fdisk_new_sgi_label(struct fdisk_context *cxt);
extern struct fdisk_label *fdisk_new_sun_label(struct fdisk_context *cxt);


struct ask_menuitem {
	char	key;
	const char	*name;
	const char	*desc;

	struct ask_menuitem *next;
};

/* fdisk dialog -- note that nothing from this stuff will be directly exported,
 * we will have get/set() function for everything.
 */
struct fdisk_ask {
	int		type;		/* FDISK_ASKTYPE_* */
	char		*query;
	unsigned int	flags;

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
			unsigned int	relative :1,
					inchars  :1;
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
		/* FDISK_ASKTYPE_MENU */
		struct ask_menu {
			int		dfl;		/* default meni item */
			int		result;
			struct ask_menuitem *first;
		} menu;
	} data;
};

struct fdisk_context {
	int dev_fd;         /* device descriptor */
	char *dev_path;     /* device path */

	unsigned char *firstsector; /* buffer with master boot record */
	unsigned long firstsector_bufsz;

	/* topology */
	unsigned long io_size;		/* I/O size used by fdisk */
	unsigned long optimal_io_size;	/* optional I/O returned by device */
	unsigned long min_io_size;	/* minimal I/O size */
	unsigned long phy_sector_size;	/* physical size */
	unsigned long sector_size;	/* logical size */
	unsigned long alignment_offset;

	unsigned int readonly : 1,		/* don't write to the device */
		     display_in_cyl_units : 1,	/* for obscure labels */
		     display_details : 1,	/* expert display mode */
		     listonly : 1;		/* list partition, nothing else */

	/* alignment */
	unsigned long grain;		/* alignment unit */
	sector_t first_lba;		/* recommended begin of the first partition */
	sector_t last_lba;		/* recomennded end of last partition */

	/* geometry */
	sector_t total_sectors;	/* in logical sectors */
	struct fdisk_geometry geom;

	/* user setting to overwrite device default */
	struct fdisk_geometry user_geom;
	unsigned long user_pyh_sector;
	unsigned long user_log_sector;

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

extern int fdisk_context_enable_listonly(struct fdisk_context *cxt, int enable);
extern int fdisk_context_listonly(struct fdisk_context *cxt);


/* alignment.c */
extern sector_t fdisk_scround(struct fdisk_context *cxt, sector_t num);
extern sector_t fdisk_cround(struct fdisk_context *cxt, sector_t num);

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


extern int fdisk_override_geometry(struct fdisk_context *cxt,
		            unsigned int cylinders, unsigned int heads,
                            unsigned int sectors);

extern int fdisk_discover_geometry(struct fdisk_context *cxt);
extern int fdisk_discover_topology(struct fdisk_context *cxt);

extern int fdisk_apply_user_device_properties(struct fdisk_context *cxt);
extern void fdisk_zeroize_device_properties(struct fdisk_context *cxt);

/* utils.c */
extern int fdisk_init_firstsector_buffer(struct fdisk_context *cxt);
extern int fdisk_read_firstsector(struct fdisk_context *cxt);
extern char *fdisk_partname(const char *dev, size_t partno);

/* label.c */
extern int fdisk_probe_labels(struct fdisk_context *cxt);
extern void fdisk_deinit_label(struct fdisk_label *lb);
extern const struct fdisk_column *fdisk_label_get_column(
					struct fdisk_label *lb, int id);

/* ask.c */
extern int fdisk_ask_partnum(struct fdisk_context *cxt, size_t *partnum, int wantnew);

extern int fdisk_info_new_partition(
			struct fdisk_context *cxt,
			int num, sector_t start, sector_t stop,
			struct fdisk_parttype *t);

#endif /* _LIBFDISK_PRIVATE_H */
