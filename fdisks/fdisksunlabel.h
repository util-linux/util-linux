#ifndef FDISK_SUN_LABEL_H
#define FDISK_SUN_LABEL_H

#include "pt-sun.h"

/* fdisksunlabel.c */
extern void sun_list_table(struct fdisk_context *cxt, int xtra);
extern void sun_set_alt_cyl(struct fdisk_context *cxt);
extern void sun_set_ncyl(struct fdisk_context *cxt, int cyl);
extern void sun_set_xcyl(struct fdisk_context *cxt);
extern void sun_set_ilfact(struct fdisk_context *cxt);
extern void sun_set_rspeed(struct fdisk_context *cxt);
extern void sun_set_pcylcount(struct fdisk_context *cxt);

extern void toggle_sunflags(struct fdisk_context *cxt, size_t i, uint16_t mask);

extern int sun_is_empty_type(struct fdisk_context *cxt, size_t i);

#endif /* FDISK_SUN_LABEL_H */
