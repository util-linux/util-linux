/*
 * canonicalize.c -- canonicalize pathname by removing symlinks
 *
 * This file may be distributed under the terms of the
 * GNU Lesser General Public License.
 *
 * Copyright (C) 2009-2013 Karel Zak <kzak@redhat.com>
 */
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include "canonicalize.h"
#include "pathnames.h"

/*
 * Converts private "dm-N" names to "/dev/mapper/<name>"
 *
 * Since 2.6.29 (patch 784aae735d9b0bba3f8b9faef4c8b30df3bf0128) kernel sysfs
 * provides the real DM device names in /sys/block/<ptname>/dm/name
 */
char *__canonicalize_dm_name(const char *prefix, const char *ptname)
{
	FILE	*f;
	size_t	sz;
	char	path[256], name[sizeof(path) - sizeof(_PATH_DEV_MAPPER)], *res = NULL;

	if (!ptname || !*ptname)
		return NULL;

	if (!prefix)
		prefix = "";

	snprintf(path, sizeof(path), "%s/sys/block/%s/dm/name", prefix, ptname);
	if (!(f = fopen(path, "r" UL_CLOEXECSTR)))
		return NULL;

	/* read "<name>\n" from sysfs */
	if (fgets(name, sizeof(name), f) && (sz = strlen(name)) > 1) {
		name[sz - 1] = '\0';
		snprintf(path, sizeof(path), _PATH_DEV_MAPPER "/%s", name);

		if (prefix || access(path, F_OK) == 0)
			res = strdup(path);
	}
	fclose(f);
	return res;
}

char *canonicalize_dm_name(const char *ptname)
{
	return __canonicalize_dm_name(NULL, ptname);
}

static int is_dm_devname(char *canonical, char **name)
{
	struct stat sb;
	char *p = strrchr(canonical, '/');

	*name = NULL;

	if (!p
	    || strncmp(p, "/dm-", 4) != 0
	    || !isdigit(*(p + 4))
	    || stat(canonical, &sb) != 0
	    || !S_ISBLK(sb.st_mode))
		return 0;

	*name = p + 1;
	return 1;
}

/*
 * This function does not canonicalize the path! It just prepends CWD before a
 * relative path. If the path is no relative than returns NULL. The path does
 * not have to exist.
 */
char *absolute_path(const char *path)
{
	char cwd[PATH_MAX], *res, *p;
	size_t psz, csz;

	if (!is_relative_path(path)) {
		errno = EINVAL;
		return NULL;
	}
	if (!getcwd(cwd, sizeof(cwd)))
		return NULL;

	/* simple clean up */
	if (startswith(path, "./"))
		path += 2;
	else if (strcmp(path, ".") == 0)
		path = NULL;

	if (!path || !*path)
		return strdup(cwd);

	csz = strlen(cwd);
	psz = strlen(path);

	p = res = malloc(csz + 1 + psz + 1);
	if (!res)
		return NULL;

	memcpy(p, cwd, csz);
	p += csz;
	*p++ = '/';
	memcpy(p, path, psz + 1);

	return res;
}

char *canonicalize_path(const char *path)
{
	char *canonical, *dmname;

	if (!path || !*path)
		return NULL;

	canonical = realpath(path, NULL);
	if (!canonical)
		return strdup(path);

	if (is_dm_devname(canonical, &dmname)) {
		char *dm = canonicalize_dm_name(dmname);
		if (dm) {
			free(canonical);
			return dm;
		}
	}

	return canonical;
}

char *canonicalize_path_restricted(const char *path)
{
	char *canonical, *dmname;
	int errsv;
	int pipes[2], ret, pid;
	ssize_t write_ret;

	if (!path || !*path)
		return NULL;

	ret = pipe(pipes);
	if (ret < 0)
		return NULL;

	/*
	 * To accurately assume identity of getuid() we must use setuid()
	 * but if we do that, we lose ability to reassume euid of 0, so
	 * we fork to do the check to keep euid intact.
	 */

	pid = fork();
	if (pid < 0) {
		close(pipes[0]);
		close(pipes[1]);
		return NULL;
	} else if (pid) {
		size_t amt;
		ssize_t read_ret;
		int len;

		canonical = NULL;
		errsv = 0;

		read_ret = read(pipes[0], &len, sizeof(len));
		if (read_ret == 0) {
			errsv = EIO;
			goto error;
		}
		if (read_ret < 0)
			goto serror;

		if (len < 0) {
			errsv = -len;
			goto error;
		}

		canonical = malloc(len + 1);
		if (!canonical)
			goto serror;

		amt = 0;
		while (amt < (size_t) len) {
			read_ret = read(pipes[0], canonical + amt, len - amt);
			if (read_ret == 0) {
				errsv = EIO;
				goto error;
			}
			if (read_ret < 0)
				goto serror;

			amt += read_ret;
		}

		canonical[len] = '\0';

		if (0) {
		serror:
			errsv = errno;
		error:
			if (canonical) {
				free(canonical);
				canonical = NULL;
			}
		}

		close(pipes[0]);
		close(pipes[1]);

		/* We make a best effort to reap child */
		waitpid(pid, NULL, 0);

		errno = errsv;

		return canonical;
	}

	/* drop permissions */
	if (setgid(getgid()) < 0 || setuid(getuid()) < 0)
		goto cerror;

	canonical = realpath(path, NULL);
	if (canonical && is_dm_devname(canonical, &dmname)) {
		char *dm = canonicalize_dm_name(dmname);
		if (dm) {
			free(canonical);
			canonical = dm;
		}
	}

	if (!canonical) {
 cerror:
		errsv = -errno;
		write_ret = write(pipes[1], &errsv, sizeof(errsv));
		/* Doesn't matter if this fails */
		(void) write_ret;
	} else {
		errsv = strlen(canonical);

		write_ret = write(pipes[1], &errsv, sizeof(errsv));
		if (write_ret >= 0) {
			size_t amt;
			amt = 0;
			while (amt < (size_t) errsv) {
				write_ret = write(pipes[1], canonical + amt,
						  errsv - amt);
				if (write_ret < 0)
					break;
				amt += write_ret;
			}
		}
	}

	exit(0);
}


#ifdef TEST_PROGRAM_CANONICALIZE
int main(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: %s <device>\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	fprintf(stdout, "orig: %s\n", argv[1]);
	fprintf(stdout, "real: %s\n", canonicalize_path(argv[1]));
	exit(EXIT_SUCCESS);
}
#endif
