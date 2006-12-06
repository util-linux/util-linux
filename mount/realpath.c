/*
 * realpath.c -- canonicalize pathname by removing symlinks
 * Copyright (C) 1993 Rick Sladkey <jrs@world.std.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Library Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Library Public License for more details.
 */

#define resolve_symlinks

/*
 * This routine is part of libc.  We include it nevertheless,
 * since the libc version has some security flaws.
 */

#include <limits.h>		/* for PATH_MAX */
#ifndef PATH_MAX
#define PATH_MAX 8192
#endif
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include "realpath.h"
#include "sundries.h"		/* for xstrdup */

#define MAX_READLINKS 32

char *
myrealpath(const char *path, char *resolved_path, int maxreslth) {
	int readlinks = 0;
	char *npath;
	char link_path[PATH_MAX+1];
	int n;
	char *buf = NULL;

	npath = resolved_path;

	/* If it's a relative pathname use getcwd for starters. */
	if (*path != '/') {
		if (!getcwd(npath, maxreslth-2))
			return NULL;
		npath += strlen(npath);
		if (npath[-1] != '/')
			*npath++ = '/';
	} else {
		*npath++ = '/';
		path++;
	}

	/* Expand each slash-separated pathname component. */
	while (*path != '\0') {
		/* Ignore stray "/" */
		if (*path == '/') {
			path++;
			continue;
		}
		if (*path == '.' && (path[1] == '\0' || path[1] == '/')) {
			/* Ignore "." */
			path++;
			continue;
		}
		if (*path == '.' && path[1] == '.' &&
		    (path[2] == '\0' || path[2] == '/')) {
			/* Backup for ".." */
			path += 2;
			while (npath > resolved_path+1 &&
			       (--npath)[-1] != '/')
				;
			continue;
		}
		/* Safely copy the next pathname component. */
		while (*path != '\0' && *path != '/') {
			if (npath-resolved_path > maxreslth-2) {
				errno = ENAMETOOLONG;
				goto err;
			}
			*npath++ = *path++;
		}

		/* Protect against infinite loops. */
		if (readlinks++ > MAX_READLINKS) {
			errno = ELOOP;
			goto err;
		}

		/* See if last pathname component is a symlink. */
		*npath = '\0';
		n = readlink(resolved_path, link_path, PATH_MAX);
		if (n < 0) {
			/* EINVAL means the file exists but isn't a symlink. */
			if (errno != EINVAL)
				goto err;
		} else {
#ifdef resolve_symlinks		/* Richard Gooch dislikes sl resolution */
			int m;

			/* Note: readlink doesn't add the null byte. */
			link_path[n] = '\0';
			if (*link_path == '/')
				/* Start over for an absolute symlink. */
				npath = resolved_path;
			else
				/* Otherwise back up over this component. */
				while (*(--npath) != '/')
					;

			/* Insert symlink contents into path. */
			m = strlen(path);
			if (buf)
				free(buf);
			buf = xmalloc(m + n + 1);
			memcpy(buf, link_path, n);
			memcpy(buf + n, path, m + 1);
			path = buf;
#endif
		}
		*npath++ = '/';
	}
	/* Delete trailing slash but don't whomp a lone slash. */
	if (npath != resolved_path+1 && npath[-1] == '/')
		npath--;
	/* Make sure it's null terminated. */
	*npath = '\0';

	if (buf)
		free(buf);
	return resolved_path;

 err:
	if (buf)
		free(buf);
	return NULL;
}
