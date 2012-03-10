/*
 * Copyright (C) 2012 Sami Kerola <kerolasa@iki.fi>
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include "c.h"
#include "fileutils.h"
#include "pathnames.h"
#include "xalloc.h"

/* Create open temporary file in safe way.  Please notice that the
 * file permissions are -rw------- by default. */
int xmkstemp(char **tmpname)
{
	char *localtmp;
	char *tmpenv;
	mode_t old_mode;
	int fd;

	tmpenv = getenv("TMPDIR");
	if (tmpenv)
		xasprintf(&localtmp, "%s/%s.XXXXXX", tmpenv,
			  program_invocation_short_name);
	else
		xasprintf(&localtmp, "%s/%s.XXXXXX", _PATH_TMP,
			  program_invocation_short_name);
	old_mode = umask(077);
	fd = mkstemp(localtmp);
	umask(old_mode);
	if (fd == -1) {
		free(localtmp);
		localtmp = NULL;
	}
	*tmpname = localtmp;
	return fd;
}

#ifdef TEST_PROGRAM
int main(void)
{
	FILE *f;
	char *tmpname;
	f = xfmkstemp(&tmpname);
	unlink(tmpname);
	free(tmpname);
	fclose(f);
	return EXIT_FAILURE;
}
#endif
