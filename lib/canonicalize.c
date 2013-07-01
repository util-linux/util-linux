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

/*
 * Converts private "dm-N" names to "/dev/mapper/<name>"
 *
 * Since 2.6.29 (patch 784aae735d9b0bba3f8b9faef4c8b30df3bf0128) kernel sysfs
 * provides the real DM device names in /sys/block/<ptname>/dm/name
 */
char *canonicalize_dm_name(const char *ptname)
{
	FILE	*f;
	size_t	sz;
	char	path[256], name[256], *res = NULL;

	if (!ptname || !*ptname)
		return NULL;

	snprintf(path, sizeof(path), "/sys/block/%s/dm/name", ptname);
	if (!(f = fopen(path, "r" UL_CLOEXECSTR)))
		return NULL;

	/* read "<name>\n" from sysfs */
	if (fgets(name, sizeof(name), f) && (sz = strlen(name)) > 1) {
		name[sz - 1] = '\0';
		snprintf(path, sizeof(path), "/dev/mapper/%s", name);

		if (access(path, F_OK) == 0)
			res = strdup(path);
	}
	fclose(f);
	return res;
}

char *canonicalize_path(const char *path)
{
	char *canonical, *p;

	if (!path || !*path)
		return NULL;

	canonical = realpath(path, NULL);
	if (!canonical)
		return strdup(path);

	p = strrchr(canonical, '/');
	if (p && strncmp(p, "/dm-", 4) == 0 && isdigit(*(p + 4))) {
		char *dm = canonicalize_dm_name(p + 1);
		if (dm) {
			free(canonical);
			return dm;
		}
	}

	return canonical;
}

char *canonicalize_path_restricted(const char *path)
{
	char *canonical, *p = NULL;
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
	if (canonical) {
		p = strrchr(canonical, '/');
		if (p && strncmp(p, "/dm-", 4) == 0 && isdigit(*(p + 4))) {
			char *dm = canonicalize_dm_name(p + 1);
			if (dm) {
				free(canonical);
				canonical = dm;
			}
		}
	} else
		errsv = errno;

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
