/*
 * environ[] array cleanup code and getenv() wappers
 *
 * No copyright is claimed.  This code is in the public domain; do with
 * it what you wish.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef HAVE_SYS_PRCTL_H
#include <sys/prctl.h>
#else
#define PR_GET_DUMPABLE 3
#endif
#if (!defined(HAVE_PRCTL) && defined(linux))
#include <sys/syscall.h>
#endif
#include <unistd.h>
#include <sys/types.h>

#include "env.h"

#ifndef HAVE_ENVIRON_DECL
extern char **environ;
#endif

static char * const forbid[] = {
        "BASH_ENV=",    /* GNU creeping featurism strikes again... */
        "ENV=",
        "HOME=",
        "IFS=",
        "KRB_CONF=",
        "LD_",          /* anything with the LD_ prefix */
        "LIBPATH=",
        "MAIL=",
        "NLSPATH=",
        "PATH=",
        "SHELL=",
        "SHLIB_PATH=",
        (char *) 0
};

/* these are allowed, but with no slashes inside
   (to work around security problems in GNU gettext) */
static char * const noslash[] = {
        "LANG=",
        "LANGUAGE=",
        "LC_",          /* anything with the LC_ prefix */
        (char *) 0
};

void
sanitize_env(void)
{
        char **envp = environ;
        char * const *bad;
        char **cur;
        int last = 0;

        for (cur = envp; *cur; cur++)
                last++;

        for (cur = envp; *cur; cur++) {
                for (bad = forbid; *bad; bad++) {
                        if (strncmp(*cur, *bad, strlen(*bad)) == 0) {
                                last = remote_entry(envp, cur - envp, last);
                                cur--;
                                break;
                        }
                }
        }

        for (cur = envp; *cur; cur++) {
                for (bad = noslash; *bad; bad++) {
                        if (strncmp(*cur, *bad, strlen(*bad)) != 0)
                                continue;
                        if (!strchr(*cur, '/'))
                                continue;  /* OK */
                        last = remote_entry(envp, cur - envp, last);
                        cur--;
                        break;
                }
        }
}


char *safe_getenv(const char *arg)
{
	uid_t ruid = getuid();

	if (ruid != 0 || (ruid != geteuid()) || (getgid() != getegid()))
		return NULL;
#ifdef HAVE_PRCTL
	if (prctl(PR_GET_DUMPABLE, 0, 0, 0, 0) == 0)
		return NULL;
#else
#if (defined(linux) && defined(SYS_prctl))
	if (syscall(SYS_prctl, PR_GET_DUMPABLE, 0, 0, 0, 0) == 0)
		return NULL;
#endif
#endif
#ifdef HAVE_SECURE_GETENV
return secure_getenv(arg);
#elif HAVE___SECURE_GETENV
	return __secure_getenv(arg);
#else
	return getenv(arg);
#endif
}

#ifdef TEST_PROGRAM
int main(void)
{
	char *const *bad;
	char copy[32];
	char *p;
	int retval = EXIT_SUCCESS;

	for (bad = forbid; *bad; bad++) {
		strcpy(copy, *bad);
		p = strchr(copy, '=');
		if (p)
			*p = '\0';
		setenv(copy, copy, 1);
	}
	sanitize_env();
	for (bad = forbid; *bad; bad++) {
		strcpy(copy, *bad);
		p = strchr(copy, '=');
		if (p)
			*p = '\0';
		p = getenv(copy);
		if (p) {
			warnx("%s was not removed", copy);
			retval = EXIT_FAILURE;
		}
	}
	return retval;
}
#endif
