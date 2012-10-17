#ifndef UTIL_LINUX_ENV_H
#define UTIL_LINUX_ENV_H

#include "c.h"

extern void sanitize_env(void);
extern char *safe_getenv(const char *arg);

static inline void xsetenv(char const *name, char const *val, int overwrite)
{
	if (setenv(name, val, overwrite) != 0)
		err(EXIT_FAILURE, "failed to set the %s environment variable", name);
}

#endif /* UTIL_LINUX_ENV_H */

