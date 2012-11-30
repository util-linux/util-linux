
#include "fdiskP.h"

int fdisk_debug_mask;


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
	if (fdisk_debug_mask & FDISK_DEBUG_INIT)
		return;
	if (!mask) {
		char *str = getenv("LIBFDISK_DEBUG");
		if (str)
			fdisk_debug_mask = strtoul(str, 0, 0);
	} else
		fdisk_debug_mask = mask;

	if (fdisk_debug_mask)
		fprintf(stderr, "libfdisk: debug mask set to 0x%04x.\n",
		       fdisk_debug_mask);

	fdisk_debug_mask |= FDISK_DEBUG_INIT;
}
