
#include "fdiskP.h"

UL_DEBUG_DEFINE_MASK(libfdisk);

/**
 * fdisk_init_debug:
 * @mask: debug mask (0xffff to enable full debuging)
 *
 * If the @mask is not specified then this function reads
 * FDISK_DEBUG environment variable to get the mask.
 *
 * Already initialized debugging stuff cannot be changed. It does not
 * have effect to call this function twice.
 */
void fdisk_init_debug(int mask)
{
	__UL_INIT_DEBUG(libfdisk, FDISK_DEBUG_, mask, LIBFDISK_DEBUG);
}
