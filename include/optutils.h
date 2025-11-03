/*
 * No copyright is claimed.  This code is in the public domain; do with
 * it what you wish.
 */
#ifndef UTIL_LINUX_OPTUTILS_H
#define UTIL_LINUX_OPTUTILS_H

#include <assert.h>

#include "c.h"
#include "nls.h"
#include "cctype.h"

/*
 * Converts the short option @c to the corresponding long option from @opts, or
 * returns NULL.
 */
static inline const char *ul_get_longopt(const struct option *opts, int c)
{
	const struct option *o;

	assert(!(opts == NULL));
	for (o = opts; o->name; o++)
		if (o->val == c)
			return o->name;
	return NULL;
}

/*
 * Converts the short options @c to "%c" or "0x<hex>" if not printable.
 */
static inline const char *ul_get_shortopt(char *buf, size_t bufsz, int c)
{
	if (c_isgraph(c))
		snprintf(buf, bufsz, "%c", c);
	else
		snprintf(buf, bufsz, "<0x%02x>", c);	/* should not happen */

	return buf;
}

#ifndef OPTUTILS_EXIT_CODE
# define OPTUTILS_EXIT_CODE EXIT_FAILURE
#endif

/*
 * Check collisions between options.
 *
 * The conflicts between options are described in ul_excl_t array. The
 * array contains groups of mutually exclusive options. For example
 *
 *	static const ul_excl_t excl[] = {
 *		{ 'Z','b','c' },		// first group
 *		{ 'b','x' },			// second group
 *		{ 0 }
 *	};
 *
 *	int excl_st[ARRAY_SIZE(excl)] = UL_EXCL_STATUS_INIT;
 *
 *	while ((c = getopt_long(argc, argv, "Zbcx", longopts, NULL)) != -1) {
 *
 *		err_exclusive_options(c, longopts, excl, excl_st);
 *
 *		switch (c) {
 *		case 'Z':
 *		   ....
 *		}
 *	}
 *
 * The array excl[] defines two groups of the mutually exclusive options. The
 * option '-b' is in the both groups.
 *
 * Note that the options in the group have to be in ASCII order (ABC..abc..) and
 * groups have to be also in ASCII order.
 *
 * The maximal number of the options in the group is 15 (size of the array is
 * 16, last is zero).
 *
 * The current status of options is stored in excl_st array. The size of the array
 * must be the same as number of the groups in the ul_excl_t array.
 *
 * If you're unsure then see sys-utils/mount.c or misc-utils/findmnt.c.
 */
#define UL_EXCL_STATUS_INIT	{ 0 }
typedef int ul_excl_t[16];

static inline void err_exclusive_options(
			int c,
			const struct option *opts,
			const ul_excl_t *excl,
			int *status)
{
	int e;

	for (e = 0; excl[e][0] && excl[e][0] <= c; e++) {
		const int *op = excl[e];

		for (; *op && *op <= c; op++) {
			if (*op != c)
				continue;
			if (status[e] == 0)
				status[e] = c;
			else if (status[e] != c) {
				const char *a = ul_get_longopt(opts, status[e]);
				const char *b = ul_get_longopt(opts, c);
				char buf[16];	/* short option in hex */

				errx(OPTUTILS_EXIT_CODE,
					_("options %s%s and %s%s cannot be combined"),
					a ? "--" : "-",
					a ? a : ul_get_shortopt(buf, sizeof(buf), status[e]),
					b ? "--" : "-",
					b ? b : ul_get_shortopt(buf, sizeof(buf), c));
			}
			break;
		}
	}
}

#endif
