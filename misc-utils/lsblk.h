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
#include "debug.h"

#define LSBLK_DEBUG_INIT	(1 << 1)
#define LSBLK_DEBUG_FILTER	(1 << 2)
#define LSBLK_DEBUG_DEV		(1 << 3)
#define LSBLK_DEBUG_CXT		(1 << 4)
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

	const char *sysroot;
	int flags;			/* LSBLK_* */

	unsigned int all_devices:1;	/* print all devices, including empty */
	unsigned int bytes:1;		/* print SIZE in bytes */
	unsigned int inverse:1;		/* print inverse dependencies */
	unsigned int nodeps:1;		/* don't print slaves/holders */
	unsigned int scsi:1;		/* print only device with HCTL (SCSI) */
	unsigned int paths:1;		/* print devnames with "/dev" prefix */
	unsigned int sort_hidden:1;	/* sort column not between output columns */
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

struct blkdev_cxt {
	struct blkdev_cxt *parent;
	struct lsblk_devprop *properties;

	struct libscols_line *scols_line;
	struct stat	st;

	char *name;		/* kernel name in /sys/block */
	char *dm_name;		/* DM name (dm/block) */

	char *filename;		/* path to device node */

	struct path_cxt	*sysfs;

	int partition;		/* is partition? TRUE/FALSE */

	char *mountpoint;	/* device mountpoint */
	struct statvfs fsstat;	/* statvfs() result */

	int npartitions;	/* # of partitions this device has */
	int nholders;		/* # of devices mapped directly to this device
				 * /sys/block/.../holders */
	int nslaves;		/* # of devices this device maps to */
	int maj, min;		/* devno */
	int discard;		/* supports discard */

	uint64_t size;		/* device size */

	unsigned int	is_mounted : 1,
			is_swap : 1,
			udev_requested : 1,
			blkid_requested : 1;
};

/* lsblk-mnt.c */
extern void lsblk_mnt_init(void);
extern void lsblk_mnt_deinit(void);

extern char *lsblk_device_get_mountpoint(struct blkdev_cxt *cxt);

/* lsblk-properties.c */
extern void lsblk_device_free_properties(struct lsblk_devprop *p);
extern struct lsblk_devprop *lsblk_device_get_properties(struct blkdev_cxt *cxt);
extern void lsblk_properties_deinit(void);

#endif /* UTIL_LINUX_LSBLK_H */
