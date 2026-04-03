/*
 * No copyright is claimed.  This code is in the public domain; do with
 * it what you wish.
 */
#ifndef UTIL_LINUX_DEBUG_H
#define UTIL_LINUX_DEBUG_H


/*
 * util-linux debug macros
 *
 * The debug stuff is based on <name>_debug_mask that controls what outputs is
 * expected. The mask is usually initialized by <NAME>_DEBUG= env.variable
 *
 * After successful initialization the flag <PREFIX>_DEBUG_INIT is always set
 * to the mask (this flag is required). The <PREFIX> is usually library API
 * prefix (e.g. MNT_) or program name (e.g. CFDISK_)
 *
 * In the code is possible to use
 *
 *	DBG(FOO, ul_debug("this is output for foo"));
 *
 * where for the FOO has to be defined <PREFIX>_DEBUG_FOO.
 *
 * It's possible to initialize the mask by comma delimited strings with
 * subsystem names (e.g. "LIBMOUNT_DEBUG=options,tab"). In this case is
 * necessary to define mask names array. This functionality is optional.
 *
 * It's strongly recommended to use UL_* macros to define/declare/use
 * the debug stuff.
 *
 * See disk-utils/cfdisk.c: cfdisk_init_debug()  for programs debug
 *  or libmount/src/init.c: mnt_init_debug()     for library debug
 *
 */

#include <stdarg.h>
#include <string.h>

#include "c.h"

struct ul_debug_maskname {
	const char *name;
	unsigned mask;
	const char *help;
};

/*
 * Identifiers used within this header file:
 *
 * lib  - library name (e.g. libmount in libmount_debug_mask)
 * pref - flag prefix (e.g. LIBMOUNT_DEBUG_ in LIBMOUNT_DEBUG_HELP)
 * flag - flag postfix (e.g. HELP in LIBMOUNT_DEBUG_HELP)
 * h    - handle of object to print
 * x    - function to call
 */

#define UL_DEBUG_EMPTY_MASKNAMES {{ NULL, 0, NULL }}
#define UL_DEBUG_DEFINE_MASKNAMES(lib) static const struct ul_debug_maskname lib ## _masknames[]
#define UL_DEBUG_MASKNAMES(lib)	lib ## _masknames

#define UL_DEBUG_MASK(lib)         lib ## _debug_mask
#define UL_DEBUG_DEFINE_MASK(lib)  unsigned UL_DEBUG_MASK(lib)
#define UL_DEBUG_DECLARE_MASK(lib) extern UL_DEBUG_DEFINE_MASK(lib)
#define UL_DEBUG_ALL               0xFFFFFF

/*
 * Internal mask flags (above UL_DEBUG_ALL)
 */
#define __UL_DEBUG_FL_NOADDR	(1 << 24)	/* Don't print object address */


#define __UL_DBG_OBJ(lib, pref, flag, h, x) \
	do { \
		if ((pref ## flag) & lib ## _debug_mask) { \
			ul_debug_prefix(# lib, # flag, h, lib ## _debug_mask); \
			x; \
		} \
	} while (0)

#define __UL_DBG(lib, pref, flag, x) \
	__UL_DBG_OBJ(lib, pref, flag, NULL, x)

#define __UL_DBG_CALL(lib, pref, flag, x) \
	do { \
		if ((pref ## flag) & lib ## _debug_mask) { \
			x; \
		} \
	} while (0)

#define __UL_DBG_FLUSH(lib, pref) \
	do { \
		if (lib ## _debug_mask && \
		    lib ## _debug_mask != pref ## INIT) { \
			fflush(stderr); \
		} \
	} while (0)

#define __UL_INIT_DEBUG_FROM_STRING(lib, pref, mask, str) \
	do { \
		if (lib ## _debug_mask & pref ## INIT) \
		; \
		else if (!mask && str) { \
			lib ## _debug_mask = ul_debug_parse_mask(lib ## _masknames, str); \
		} else \
			lib ## _debug_mask = mask; \
		if (lib ## _debug_mask) { \
			if (is_privileged_execution()) { \
				lib ## _debug_mask |= __UL_DEBUG_FL_NOADDR; \
				fprintf(stderr, "%d: %s: don't print memory addresses (SUID executable).\n", getpid(), # lib); \
			} \
		} \
		lib ## _debug_mask |= pref ## INIT; \
	} while (0)


#define __UL_INIT_DEBUG_FROM_ENV(lib, pref, mask, env) \
	do { \
		const char *envstr = mask ? NULL : getenv(# env); \
		__UL_INIT_DEBUG_FROM_STRING(lib, pref, mask, envstr); \
	} while (0)

extern void ul_debug(const char *mesg, ...)
		__attribute__ ((__format__ (__printf__, 1, 2)));
extern void ul_debug_prefix(const char *lib, const char *flag,
			    const void *handle, int mask);
extern unsigned ul_debug_parse_mask(const struct ul_debug_maskname flagnames[],
				    const char *mask);
extern void ul_debug_print_masks(const char *env,
				 const struct ul_debug_maskname flagnames[]);

#endif /* UTIL_LINUX_DEBUG_H */
