#ifndef FDISK_SGI_LABEL_H
#define FDISK_SGI_LABEL_H

#include "bitops.h"
#include "pt-sgi.h"

/*
 * controller flags
 */
#define	SECTOR_SLIP	0x01
#define	SECTOR_FWD	0x02
#define	TRACK_FWD	0x04
#define	TRACK_MULTIVOL	0x08
#define	IGNORE_ERRORS	0x10
#define	RESEEK		0x20
#define	CMDTAGQ_ENABLE	0x40

typedef struct {
	unsigned int   magic;		/* looks like a magic number */
	unsigned int   a2;
	unsigned int   a3;
	unsigned int   a4;
	unsigned int   b1;
	unsigned short b2;
	unsigned short b3;
	unsigned int   c[16];
	unsigned short d[3];
	unsigned char  scsi_string[50];
	unsigned char  serial[137];
	unsigned short check1816;
	unsigned char  installer[225];
} sgiinfo;

#define	SGI_LABEL_MAGIC		0x0be5a941
#define	SGI_LABEL_MAGIC_SWAPPED	0x41a9e50b
#define	SGI_INFO_MAGIC		0x00072959
#define	SGI_INFO_MAGIC_SWAPPED	0x59290700

#define SSWAP16(x) (other_endian ? swab16(x) : (uint16_t)(x))
#define SSWAP32(x) (other_endian ? swab32(x) : (uint32_t)(x))

/* toggle flags */
#define SGI_FLAG_BOOT	1
#define SGI_FLAG_SWAP	2

/* fdisk.c */
#define sgilabel ((struct sgi_disklabel *)cxt->firstsector)
#define sgiparam (sgilabel->devparam)

/* fdisksgilabel.c */
extern void	sgi_list_table( struct fdisk_context *cxt, int xtra );
extern int  sgi_change_sysid(struct fdisk_context *cxt, int i, int sys);
extern unsigned int	sgi_get_start_sector(struct fdisk_context *cxt, int i );
extern unsigned int	sgi_get_num_sectors(struct fdisk_context *cxt, int i );
extern void	create_sgiinfo(struct fdisk_context *cxt);
extern void	sgi_set_ilfact( void );
extern void	sgi_set_rspeed( void );
extern void	sgi_set_pcylcount( void );
extern void	sgi_set_xcyl( void );
extern void	sgi_set_ncyl( void );
extern void	sgi_set_bootpartition(struct fdisk_context *cxt, int i );
extern void	sgi_set_swappartition(struct fdisk_context *cxt, int i );
extern int	sgi_get_bootpartition(struct fdisk_context *cxt);
extern int	sgi_get_swappartition(struct fdisk_context *cxt);
extern void	sgi_set_bootfile(struct fdisk_context *cxt);

#endif /* FDISK_SGI_LABEL_H */
