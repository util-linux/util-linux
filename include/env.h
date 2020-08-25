#ifndef UTIL_LINUX_ENV_H
#define UTIL_LINUX_ENV_H

#include "c.h"
#include "nls.h"

struct ul_env_list;

extern void sanitize_env(void);
extern void __sanitize_env(struct ul_env_list **org);

extern int env_list_setenv(struct ul_env_list *ls);
extern void env_list_free(struct ul_env_list *ls);

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

