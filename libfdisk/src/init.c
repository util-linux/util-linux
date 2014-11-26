
#include "fdiskP.h"


/**
 * SECTION: init
 * @title: Library initialization
 * @short_description: initialize debug stuff
 *
 */

UL_DEBUG_DEFINE_MASK(libfdisk);
UL_DEBUG_DEFINE_MASKNAMES(libfdisk) =
{
	{ "all",	LIBFDISK_DEBUG_ALL,	"info about all subsystems" },
	{ "ask",	LIBFDISK_DEBUG_ASK,	"fdisk dialogs" },
	{ "help",	LIBFDISK_DEBUG_HELP,	"this help" },
	{ "cxt",	LIBFDISK_DEBUG_CXT,	"library context (handler)" },
	{ "label",	LIBFDISK_DEBUG_LABEL,	"disk label utils" },
	{ "part",	LIBFDISK_DEBUG_PART,	"partition utils" },
	{ "parttype",	LIBFDISK_DEBUG_PARTTYPE,"partition type utils" },
	{ "script",	LIBFDISK_DEBUG_SCRIPT,	"sfdisk-like scripts" },
	{ "tab",	LIBFDISK_DEBUG_TAB,	"table utils"},
	{ NULL, 0 }
};

/**
 * fdisk_init_debug:
 * @mask: debug mask (0xffff to enable full debuging)
 *
 * If the @mask is not specified then this function reads
 * LIBFDISK_DEBUG environment variable to get the mask.
 *
 * Already initialized debugging stuff cannot be changed. It does not
 * have effect to call this function twice.
 *
 * It's strongly recommended to use fdisk_init_debug(0) in your code.
 */
void fdisk_init_debug(int mask)
{
	if (libfdisk_debug_mask)
		return;

	__UL_INIT_DEBUG(libfdisk, LIBFDISK_DEBUG_, mask, LIBFDISK_DEBUG);


	if (libfdisk_debug_mask != LIBFDISK_DEBUG_INIT
	    && libfdisk_debug_mask != (LIBFDISK_DEBUG_HELP|LIBFDISK_DEBUG_INIT)) {

		DBG(INIT, ul_debug("library debug mask: 0x%04x", libfdisk_debug_mask));
	}

	ON_DBG(HELP, ul_debug_print_masks("LIBFDISK_DEBUG",
				UL_DEBUG_MASKNAMES(libfdisk)));
}
