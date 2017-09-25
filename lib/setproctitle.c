/*
 *  set process title for ps (from sendmail)
 *
 *  Clobbers argv of our main procedure so ps(1) will display the title.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "setproctitle.h"

#ifndef SPT_BUFSIZE
# define SPT_BUFSIZE     2048
#endif

extern char **environ;

static char **argv0;
static size_t argv_lth;

void initproctitle (int argc, char **argv)
{
	int i;
	char **envp = environ;

	/*
	 * Move the environment so we can reuse the memory.
	 * (Code borrowed from sendmail.)
	 * WARNING: ugly assumptions on memory layout here;
	 *          if this ever causes problems, #undef DO_PS_FIDDLING
	 */
	for (i = 0; envp[i] != NULL; i++)
		continue;

	environ = malloc(sizeof(char *) * (i + 1));
	if (environ == NULL)
		return;

	for (i = 0; envp[i] != NULL; i++)
		if ((environ[i] = strdup(envp[i])) == NULL)
			return;
	environ[i] = NULL;

	if (i > 0)
		argv_lth = envp[i-1] + strlen(envp[i-1]) - argv[0];
	else
		argv_lth = argv[argc-1] + strlen(argv[argc-1]) - argv[0];
	if (argv_lth > 1)
		argv0 = argv;
}

void setproctitle (const char *prog, const char *txt)
{
        size_t i;
        char buf[SPT_BUFSIZE];

        if (!argv0)
                return;

	if (strlen(prog) + strlen(txt) + 5 > SPT_BUFSIZE)
		return;

	sprintf(buf, "%s -- %s", prog, txt);

        i = strlen(buf);
        if (i > argv_lth - 2) {
                i = argv_lth - 2;
                buf[i] = '\0';
        }
	memset(argv0[0], '\0', argv_lth);       /* clear the memory area */
        strcpy(argv0[0], buf);

        argv0[1] = NULL;
}
