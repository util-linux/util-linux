#ifndef FDISK_AIX_LABEL_H
#define FDISK_AIX_LABEL_H

#include <stdint.h>
/*
 * Copyright (C) Andreas Neuper, Sep 1998.
 *	This file may be redistributed under
 *	the terms of the GNU Public License.
 */

struct aix_partition {
	unsigned int   magic;        /* expect AIX_LABEL_MAGIC */
	unsigned int   fillbytes1[124];
	unsigned int   physical_volume_id;
	unsigned int   fillbytes2[124];
};

#define	AIX_LABEL_MAGIC		0xc9c2d4c1
#define	AIX_LABEL_MAGIC_SWAPPED	0xc1d4c2c9
#define	AIX_INFO_MAGIC		0x00072959
#define	AIX_INFO_MAGIC_SWAPPED	0x59290700

/* fdiskaixlabel.c */
extern struct	systypes aix_sys_types[];
#endif /* FDISK_AIX_LABEL_H */
