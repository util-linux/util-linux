/*
 * Copyright (C) 2014 Ondrej Oprala <ooprala@redhat.com>
 *
 * This file may be distributed under the terms of the
 * GNU Lesser General Public License.
 */
#ifndef UTIL_LINUX_DEBUG_H
#define UTIL_LINUX_DEBUG_H

#include <stdarg.h>
#include <string.h>

#define UL_DEBUG_DEFINE_MASK(m) int m ## _debug_mask
#define UL_DEBUG_DECLARE_MASK(m) extern UL_DEBUG_DEFINE_MASK(m)

/* p - flag prefix, m - flag postfix */
#define UL_DEBUG_DEFINE_FLAG(p, m) p ## m

/* l - library name, p - flag prefix, m - flag postfix, x - function */
#define __UL_DBG(l, p, m, x) \
	do { \
		if ((p ## m) & l ## _debug_mask) { \
			fprintf(stderr, "%d: %s: %8s: ", getpid(), # l, # m); \
			x; \
		} \
	} while (0)

#define __UL_DBG_CALL(l, p, m, x) \
	do { \
		if ((p ## m) & l ## _debug_mask) { \
			x; \
		} \
	} while (0)

#define __UL_DBG_FLUSH(l, p) \
	do { \
		if (l ## _debug_mask && \
		    l ## _debug_mask != p ## INIT) { \
			fflush(stderr); \
		} \
	} while (0)


#define __UL_INIT_DEBUG(lib, pref, mask, env) \
	do { \
		if (lib ## _debug_mask & pref ## INIT) \
		; \
		else if (!mask) { \
			char *str = getenv(# env); \
			if (str) \
				lib ## _debug_mask = parse_envmask(lib ## _masknames, str); \
		} else \
			lib ## _debug_mask = mask; \
		lib ## _debug_mask |= pref ## INIT; \
		if (lib ## _debug_mask != pref ## INIT) { \
			__UL_DBG(lib, pref, INIT, ul_debug("library debug mask: 0x%04x", \
					lib ## _debug_mask)); \
		} \
	} while (0)

struct dbg_mask { char *mname; int val; };

static inline void __attribute__ ((__format__ (__printf__, 1, 2)))
ul_debug(const char *mesg, ...)
{
	va_list ap;
	va_start(ap, mesg);
	vfprintf(stderr, mesg, ap);
	va_end(ap);
	fputc('\n', stderr);
}

static inline void __attribute__ ((__format__ (__printf__, 2, 3)))
ul_debugobj(void *handler, const char *mesg, ...)
{
	va_list ap;

	if (handler)
		fprintf(stderr, "[%p]: ", handler);
	va_start(ap, mesg);
	vfprintf(stderr, mesg, ap);
	va_end(ap);
	fputc('\n', stderr);
}

static inline int parse_envmask(const struct dbg_mask const flagnames[],
				const char *mask)
{
	int res;
	char *ptr;

	/* let's check for a numeric mask first */
	res = strtoul(mask, &ptr, 0);

	/* perhaps it's a comma-separated string? */
	if (*ptr != '\0' && flagnames) {
		char *msbuf, *ms, *name;
		res = 0;

		ms = msbuf = strdup(mask);
		if (!ms)
			return res;

		while ((name = strtok_r(ms, ",", &ptr))) {
			size_t i = 0;
			ms = ptr;

			while (flagnames[i].mname) {
				if (!strcmp(name, flagnames[i].mname)) {
					res |= flagnames[i].val;
					break;
				}
				++i;
			}
			/* nothing else we can do by OR-ing the mask */
			if (res == 0xffff)
				break;
		}
		free(msbuf);
	}
	return res;
}
#endif /* UTIL_LINUX_DEBUG_H */
