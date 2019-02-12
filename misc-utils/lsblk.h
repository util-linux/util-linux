/*
 * Copyright (C) 2010-2018 Red Hat, Inc. All rights reserved.
 * Written by Milan Broz <mbroz@redhat.com>
 *            Karel Zak <kzak@redhat.com>
 */
#ifndef UTIL_LINUX_LSBLK_H
#define UTIL_LINUX_LSBLK_H

#include <stdint.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <sys/statvfs.h>

#include <libsmartcols.h>

#include "c.h"
#include "list.h"
#include "debug.h"

#define LSBLK_DEBUG_INIT	(1 << 1)
#define LSBLK_DEBUG_FILTER	(1 << 2)
#define LSBLK_DEBUG_DEV		(1 << 3)
#define LSBLK_DEBUG_TREE	(1 << 4)
#define LSBLK_DEBUG_DEP		(1 << 5)
#define LSBLK_DEBUG_ALL		0xFFFF

UL_DEBUG_DECLARE_MASK(lsblk);
#define DBG(m, x)       __UL_DBG(lsblk, LSBLK_DEBUG_, m, x)
#define ON_DBG(m, x)    __UL_DBG_CALL(lsblk, LSBLK_DEBUG_, m, x)

#define UL_DEBUG_CURRENT_MASK	UL_DEBUG_MASK(lsblk)
#include "debugobj.h"

struct lsblk {
	struct libscols_table *table;	/* output table */

	struct libscols_column *sort_col;/* sort output by this column */
	int sort_id;

	int dedup_id;

	const char *sysroot;
	int flags;			/* LSBLK_* */

	unsigned int all_devices:1;	/* print all devices, including empty */
	unsigned int bytes:1;		/* print SIZE in bytes */
	unsigned int inverse:1;		/* print inverse dependencies */
	unsigned int merge:1;           /* merge sub-trees */
	unsigned int nodeps:1;		/* don't print slaves/holders */
	unsigned int scsi:1;		/* print only device with HCTL (SCSI) */
	unsigned int paths:1;		/* print devnames with "/dev" prefix */
	unsigned int sort_hidden:1;	/* sort column not between output columns */
	unsigned int dedup_hidden :1;	/* deduplication column not between output columns */
	unsigned int force_tree_order:1;/* sort lines by parent->tree relation */
};

extern struct lsblk *lsblk;     /* global handler */

struct lsblk_devprop {
	char *fstype;		/* detected fs, NULL or "?" if cannot detect */
	char *uuid;		/* filesystem UUID (or stack uuid) */
	char *ptuuid;		/* partition table UUID */
	char *pttype;		/* partition table type */
	char *label;		/* filesystem label */
	char *parttype;		/* partition type UUID */
	char *partuuid;		/* partition UUID */
	char *partlabel;	/* partition label */
	char *partflags;	/* partition flags */
	char *wwn;		/* storage WWN */
	char *serial;		/* disk serial number */
	char *model;		/* disk model */
};

/* Device dependence
 *
 * Note that the same device may be slave/holder for more another devices. It
 * means we need to allocate list member rather than use @child directly.
 */
struct lsblk_devdep {
	struct list_head        ls_childs;	/* item in parent->childs */
	struct list_head	ls_parents;	/* item in child->parents */

	struct lsblk_device	*child;
	struct lsblk_device	*parent;
};

struct lsblk_device {
	int	refcount;

	struct list_head	childs;		/* list with lsblk_devdep */
	struct list_head	parents;
	struct list_head	ls_roots;	/* item in devtree->roots list */
	struct list_head	ls_devices;	/* item in devtree->devices list */

	struct lsblk_device	*wholedisk;	/* for partitions */

	struct libscols_line	*scols_line;

	struct lsblk_devprop	*properties;
	struct stat	st;

	char *name;		/* kernel name in /sys/block */
	char *dm_name;		/* DM name (dm/block) */

	char *filename;		/* path to device node */
	char *dedupkey;		/* de-duplication key */

	struct path_cxt	*sysfs;

	char *mountpoint;	/* device mountpoint */
	struct statvfs fsstat;	/* statvfs() result */

	int npartitions;	/* # of partitions this device has */
	int nholders;		/* # of devices mapped directly to this device
				 * /sys/block/.../holders */
	int nslaves;		/* # of devices this device maps to */
	int maj, min;		/* devno */

	uint64_t discard_granularity;	/* sunknown:-1, yes:1, not:0 */

	uint64_t size;		/* device size */
	int removable;		/* unknown:-1, yes:1, not:0 */

	unsigned int	is_mounted : 1,
			is_swap : 1,
			is_printed : 1,
			udev_requested : 1,
			blkid_requested : 1;
};

#define device_is_partition(_x)		((_x)->wholedisk != NULL)

/*
 * Note that lsblk tree uses bottom devices (devices without slaves) as root
 * of the tree, and partitions are interpreted as a dependence too; it means:
 *    sda -> sda1 -> md0
 *
 * The flag 'is_inverted' turns the tree over (root is device without holders):
 *    md0 -> sda1 -> sda
 */
struct lsblk_devtree {
	int	refcount;

	struct list_head	roots;		/* tree root devices */
	struct list_head	devices;	/* all devices */

	unsigned int	is_inverse : 1;		/* inverse tree */
};


/*
 * Generic iterator
 */
struct lsblk_iter {
	struct list_head        *p;		/* current position */
	struct list_head        *head;		/* start position */
	int			direction;	/* LSBLK_ITER_{FOR,BACK}WARD */
};

#define LSBLK_ITER_FORWARD	0
#define LSBLK_ITER_BACKWARD	1

#define IS_ITER_FORWARD(_i)	((_i)->direction == LSBLK_ITER_FORWARD)
#define IS_ITER_BACKWARD(_i)	((_i)->direction == LSBLK_ITER_BACKWARD)

#define LSBLK_ITER_INIT(itr, list) \
	do { \
		(itr)->p = IS_ITER_FORWARD(itr) ? \
				(list)->next : (list)->prev; \
		(itr)->head = (list); \
	} while(0)

#define LSBLK_ITER_ITERATE(itr, res, restype, member) \
	do { \
		res = list_entry((itr)->p, restype, member); \
		(itr)->p = IS_ITER_FORWARD(itr) ? \
				(itr)->p->next : (itr)->p->prev; \
	} while(0)


/* lsblk-mnt.c */
extern void lsblk_mnt_init(void);
extern void lsblk_mnt_deinit(void);

extern char *lsblk_device_get_mountpoint(struct lsblk_device *dev);

/* lsblk-properties.c */
extern void lsblk_device_free_properties(struct lsblk_devprop *p);
extern struct lsblk_devprop *lsblk_device_get_properties(struct lsblk_device *dev);
extern void lsblk_properties_deinit(void);

/* lsblk-devtree.c */
void lsblk_reset_iter(struct lsblk_iter *itr, int direction);
struct lsblk_device *lsblk_new_device(void);
void lsblk_ref_device(struct lsblk_device *dev);
void lsblk_unref_device(struct lsblk_device *dev);
int lsblk_device_new_dependence(struct lsblk_device *parent, struct lsblk_device *child);
int lsblk_device_has_child(struct lsblk_device *dev, struct lsblk_device *child);
int lsblk_device_next_child(struct lsblk_device *dev,
                          struct lsblk_iter *itr,
                          struct lsblk_device **child);

int lsblk_device_is_last_parent(struct lsblk_device *dev, struct lsblk_device *parent);
int lsblk_device_next_parent(
                        struct lsblk_device *dev,
                        struct lsblk_iter *itr,
                        struct lsblk_device **parent);

struct lsblk_devtree *lsblk_new_devtree(void);
void lsblk_ref_devtree(struct lsblk_devtree *tr);
void lsblk_unref_devtree(struct lsblk_devtree *tr);
int lsblk_devtree_add_root(struct lsblk_devtree *tr, struct lsblk_device *dev);
int lsblk_devtree_next_root(struct lsblk_devtree *tr,
                            struct lsblk_iter *itr,
                            struct lsblk_device **dev);
int lsblk_devtree_add_device(struct lsblk_devtree *tr, struct lsblk_device *dev);
int lsblk_devtree_next_device(struct lsblk_devtree *tr,
                            struct lsblk_iter *itr,
                            struct lsblk_device **dev);
int lsblk_devtree_has_device(struct lsblk_devtree *tr, struct lsblk_device *dev);
struct lsblk_device *lsblk_devtree_get_device(struct lsblk_devtree *tr, const char *name);
int lsblk_devtree_remove_device(struct lsblk_devtree *tr, struct lsblk_device *dev);
int lsblk_devtree_deduplicate_devices(struct lsblk_devtree *tr);

#endif /* UTIL_LINUX_LSBLK_H */
