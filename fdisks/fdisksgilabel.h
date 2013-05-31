#ifndef FDISK_SGI_LABEL_H
#define FDISK_SGI_LABEL_H

#include "bitops.h"
#include "pt-sgi.h"


/* toggle flags */
#define SGI_FLAG_BOOT	1
#define SGI_FLAG_SWAP	2

/* fdisksgilabel.c */
extern void	sgi_list_table( struct fdisk_context *cxt, int xtra );
extern int  sgi_change_sysid(struct fdisk_context *cxt, int i, int sys);
extern unsigned int	sgi_get_start_sector(struct fdisk_context *cxt, int i );
extern unsigned int	sgi_get_num_sectors(struct fdisk_context *cxt, int i );
extern void	sgi_set_ilfact( void );
extern void	sgi_set_rspeed( void );
extern void	sgi_set_pcylcount( void );
extern void	sgi_set_xcyl( void );
extern void	sgi_set_ncyl( void );
extern void	sgi_set_bootpartition(struct fdisk_context *cxt, int i );
extern void	sgi_set_swappartition(struct fdisk_context *cxt, int i );
extern int	sgi_get_bootpartition(struct fdisk_context *cxt);
extern int	sgi_get_swappartition(struct fdisk_context *cxt);


extern int sgi_set_bootfile(struct fdisk_context *cxt);
extern int sgi_create_info(struct fdisk_context *cxt);

#endif /* FDISK_SGI_LABEL_H */
