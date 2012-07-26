#ifndef UTIL_LINUX_OPTUTILS_H
#define UTIL_LINUX_OPTUTILS_H

#include "c.h"
#include "nls.h"

static inline const char *option_to_longopt(int c, const struct option *opts)
{
	const struct option *o;

	for (o = opts; o->name; o++)
		if (o->val == c)
			return o->name;
	return NULL;
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
				fprintf(stderr, _("%s: options "),
						program_invocation_short_name);
				for (op = excl[e]; *op; op++) {
					if (opts)
						fprintf(stderr, "--%s ",
							option_to_longopt(*op, opts));
					else
						fprintf(stderr, "-%c ", *op);
				}
				fprintf(stderr, _("are mutually exclusive."));
				fputc('\n', stderr);
				exit(OPTUTILS_EXIT_CODE);
			}
			break;
		}
	}
}

#endif

