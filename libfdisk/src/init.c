
#include "fdiskP.h"

UL_DEBUG_DEFINE_MASK(libfdisk);
UL_DEBUG_DEFINE_MASKANEMS(libfdisk) =
{
	{ "all", FDISK_DEBUG_ALL },
	{ "init", FDISK_DEBUG_INIT },
	{ "cxt", FDISK_DEBUG_CXT },
	{ "label", FDISK_DEBUG_LABEL },
	{ "ask", FDISK_DEBUG_ASK},
	{ "frontend", FDISK_DEBUG_FRONTEND },
	{ "part", FDISK_DEBUG_PART },
	{ "parttype", FDISK_DEBUG_PARTTYPE },
	{ "tab", FDISK_DEBUG_TAB},
	{ "dump", FDISK_DEBUG_DUMP},
	{ NULL, 0 }
};
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
