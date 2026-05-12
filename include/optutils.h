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

/*
 * Check if @arg is an option whose value is expected in the next argv entry.
 * Returns 1 for options with required_argument that don't have an inline
 * value (i.e., not --name=val or bundled -Bval), 0 otherwise.
 */
static inline int ul_is_option_with_arg(const char *arg,
					const struct option *opts)
{
	const struct option *o;

	if (arg[0] != '-' || !arg[1])
		return 0;

	if (arg[1] == '-') {
		const char *name = arg + 2;

		if (!*name || strchr(name, '='))
			return 0;
		for (o = opts; o->name; o++) {
			if (o->has_arg == required_argument
			    && strcmp(name, o->name) == 0)
				return 1;
		}
		return 0;
	}

	/* short option(s): walk bundled chars; if an earlier char takes
	 * an argument, the rest of the string is consumed as its value */
	const char *p;

	for (p = arg + 1; *p; p++) {
		for (o = opts; o->name; o++) {
			if (o->val == *p)
				break;
		}
		if (!o->name)
			return 0;
		if (o->has_arg != no_argument)
			return !*(p + 1) && o->has_arg == required_argument;
	}

	return 0;
}

/*
 * Find the "--" separator in argv[], ignoring "--" when it appears as an
 * argument to an option that requires a value (e.g., -I -- or --log-in --).
 *
 * Note on getopt_long() behavior with "--":
 *  - required_argument: "--" is consumed as the option value, NOT treated
 *    as end-of-options (e.g., --log-in -- sets the value to "--")
 *  - optional_argument: "--" is NOT consumed as the value, it is treated
 *    as end-of-options (optarg is NULL)
 *  - no_argument: "--" is treated as end-of-options
 *
 * This function scans backward from each "--" counting consecutive
 * options-with-required-arg. They pair up (each consumes the next as its
 * value), so an odd count means "--" is consumed as an option value, an
 * even count (including zero) means "--" is the real separator.
 *
 * For example:
 *  --log-in -- file             odd (1), "--" is value for --log-in
 *  --log-in --log-in -- cmd     even (2), "--" is separator
 *
 * Returns the index of "--" in argv, or -1 if not found.
 */
static inline int ul_find_argv_separator(int argc, char *const argv[],
					 const struct option *opts)
{
	int i;

	for (i = 1; i < argc; i++) {
		int count, k;

		if (strcmp(argv[i], "--") != 0)
			continue;

		for (count = 0, k = i - 1; k >= 1; k--) {
			if (!ul_is_option_with_arg(argv[k], opts))
				break;
			count++;
		}

		if (count % 2 == 1)
			continue;

		return i;
	}

	return -1;
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
