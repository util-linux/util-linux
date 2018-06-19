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
	uid_t euid;
	gid_t egid;

	if (!path || !*path)
		return NULL;

	euid = geteuid();
	egid = getegid();

	/* drop permissions */
	if (setegid(getgid()) < 0 || seteuid(getuid()) < 0)
		return NULL;

	errsv = errno = 0;

	canonical = realpath(path, NULL);
	if (!canonical)
		errsv = errno;
	else if (is_dm_devname(canonical, &dmname)) {
		char *dm = canonicalize_dm_name(dmname);
		if (dm) {
			free(canonical);
			canonical = dm;
		}
	}

	/* restore */
	if (setegid(egid) < 0 || seteuid(euid) < 0) {
		free(canonical);
		return NULL;
	}

	errno = errsv;
	return canonical;
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
