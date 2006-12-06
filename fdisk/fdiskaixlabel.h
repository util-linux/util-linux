#include <linux/types.h>   /* for __u32 etc */
/*
 * Copyright (C) Andreas Neuper, Sep 1998.
 *	This file may be redistributed under
 *	the terms of the GNU Public License.
 */

typedef struct {
	unsigned int   magic;        /* expect AIX_LABEL_MAGIC */
	unsigned int   fillbytes1[124];
	unsigned int   physical_volume_id;
	unsigned int   fillbytes2[124];
} aix_partition;

#define	AIX_LABEL_MAGIC		0xc9c2d4c1
#define	AIX_LABEL_MAGIC_SWAPPED	0xc1d4c2c9
#define	AIX_INFO_MAGIC		0x00072959
#define	AIX_INFO_MAGIC_SWAPPED	0x59290700

/* fdisk.c */
#define aixlabel ((aix_partition *)MBRbuffer)
extern char MBRbuffer[MAX_SECTOR_SIZE];
extern char changed[MAXIMUM_PARTS];
extern uint heads, sectors, cylinders;
extern int show_begin;
extern int aix_label;
extern char *partition_type(unsigned char type);
extern void update_units(void);
extern char read_chars(char *mesg);

/* fdiskaixlabel.c */
extern struct	systypes aix_sys_types[];
extern void 	aix_nolabel( void );
extern int 	check_aix_label( void );
