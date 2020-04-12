#ifndef UTIL_LINUX_ENV_H
#define UTIL_LINUX_ENV_H

#include "c.h"
#include "nls.h"

extern void sanitize_env(void);
extern char *safe_getenv(const char *arg);


#ifndef XSETENV_EXIT_CODE
# define XSETENV_EXIT_CODE EXIT_FAILURE
#endif

static inline void xsetenv(char const *name, char const *val, int overwrite)
{
	if (setenv(name, val, overwrite) != 0)
		err(XSETENV_EXIT_CODE, _("failed to set the %s environment variable"), name);
}

static inline int remote_entry(char **argv, int remove, int last)
{
	memmove(argv + remove, argv + remove + 1, sizeof(char *) * (last - remove));
	return last - 1;
}

#endif /* UTIL_LINUX_ENV_H */

