#ifndef FDISK_SUN_LABEL_H
#define FDISK_SUN_LABEL_H

#include "pt-sun.h"

/* public SUN specific functions (TODO: move to libfdisk.h) */
extern void fdisk_sun_set_alt_cyl(struct fdisk_context *cxt);
extern void fdisk_sun_set_ncyl(struct fdisk_context *cxt, int cyl);
extern void fdisk_sun_set_xcyl(struct fdisk_context *cxt);
extern void fdisk_sun_set_ilfact(struct fdisk_context *cxt);
extern void fdisk_sun_set_rspeed(struct fdisk_context *cxt);
extern void fdisk_sun_set_pcylcount(struct fdisk_context *cxt);

#endif /* FDISK_SUN_LABEL_H */
