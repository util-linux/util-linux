/*
 * Copyright (C) 2012 Sami Kerola <kerolasa@iki.fi>
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/resource.h>

#include "c.h"
#include "fileutils.h"
#include "pathnames.h"

/* Create open temporary file in safe way.  Please notice that the
 * file permissions are -rw------- by default. */
int xmkstemp(char **tmpname, char *dir)
{
	char *localtmp;
	char *tmpenv;
	mode_t old_mode;
	int fd, rc;

	/* Some use cases must be capable of being moved atomically
	 * with rename(2), which is the reason why dir is here.  */
	if (dir != NULL)
		tmpenv = dir;
	else
		tmpenv = getenv("TMPDIR");

	if (tmpenv)
		rc = asprintf(&localtmp, "%s/%s.XXXXXX", tmpenv,
			  program_invocation_short_name);
	else
		rc = asprintf(&localtmp, "%s/%s.XXXXXX", _PATH_TMP,
			  program_invocation_short_name);

	if (rc < 0)
		return -1;

	old_mode = umask(077);
	fd = mkostemp(localtmp, O_RDWR|O_CREAT|O_EXCL|O_CLOEXEC);
	umask(old_mode);
	if (fd == -1) {
		free(localtmp);
		localtmp = NULL;
	}
	*tmpname = localtmp;
	return fd;
}

/*
 * portable getdtablesize()
 */
int get_fd_tabsize(void)
{
	int m;

#if defined(HAVE_GETDTABLESIZE)
	m = getdtablesize();
#elif defined(HAVE_GETRLIMIT) && defined(RLIMIT_NOFILE)
	struct rlimit rl;

	getrlimit(RLIMIT_NOFILE, &rl);
	m = rl.rlim_cur;
#elif defined(HAVE_SYSCONF) && defined(_SC_OPEN_MAX)
	m = sysconf(_SC_OPEN_MAX);
#else
	m = OPEN_MAX;
#endif
	return m;
}

#ifdef TEST_PROGRAM
int main(void)
{
	FILE *f;
	char *tmpname;
	f = xfmkstemp(&tmpname, NULL);
	unlink(tmpname);
	free(tmpname);
	fclose(f);
	return EXIT_FAILURE;
}
#endif


int mkdir_p(const char *path, mode_t mode)
{
	char *p, *dir;
	int rc = 0;

	if (!path || !*path)
		return -EINVAL;

	dir = p = strdup(path);
	if (!dir)
		return -ENOMEM;

	if (*p == '/')
		p++;

	while (p && *p) {
		char *e = strchr(p, '/');
		if (e)
			*e = '\0';
		if (*p) {
			rc = mkdir(dir, mode);
			if (rc && errno != EEXIST)
				break;
			rc = 0;
		}
		if (!e)
			break;
		*e = '/';
		p = e + 1;
	}

	free(dir);
	return rc;
}

/* returns basename and keeps dirname in the @path, if @path is "/" (root)
 * then returns empty string */
char *stripoff_last_component(char *path)
{
	char *p = path ? strrchr(path, '/') : NULL;

	if (!p)
		return NULL;
	*p = '\0';
	return p + 1;
}
