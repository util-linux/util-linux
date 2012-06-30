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
static inline void exclusive_option(int *what, const int how,
				    const char *errmesg)
{
	if (*what == 0) {
		*what = how;
		return;
	}
	if (*what == how)
		return;
	errx(OPTUTILS_EXIT_CODE,
	     _("options %s are mutually exclusive"), errmesg);
}

#endif

