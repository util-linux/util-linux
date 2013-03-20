/*-
 * Copyright (c) 1980 The Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * 1999-02-22 Arkadiusz Miśkiewicz <misiek@pld.ORG.PL>
 * - added Native Language Support
 * 2011-08-12 Davidlohr Bueso <dave@gnu.org>
 * - added $PATH lookup
 *
 * Copyright (C) 2013 Karel Zak <kzak@redhat.com>
 *               2013 Sami Kerola <kerolasa@iki.fi>
 */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>

#include "xalloc.h"
#include "nls.h"
#include "c.h"
#include "closestream.h"
#include "canonicalize.h"

/*#define DEBUG*/

static char uflag = 0;

/* supported types */
enum {
	BIN_DIR = (1 << 1),
	MAN_DIR = (1 << 2),
	SRC_DIR = (1 << 3),

	ALL_DIRS = BIN_DIR | MAN_DIR | SRC_DIR
};

/* directories */
struct wh_dirlist {
	int	type;
	dev_t	st_dev;
	ino_t	st_ino;
	char	*path;

	struct wh_dirlist *next;
};

static const char *bindirs[] = {
	"/usr/bin",
	"/usr/sbin",
	"/usr/lib",
	"/usr/lib64",
	"/bin",
	"/sbin",
	"/etc",
	"/usr/etc",
	"/lib",
	"/lib64",
	"/usr/games",
	"/usr/games/bin",
	"/usr/games/lib",
	"/usr/emacs/etc",
	"/usr/lib/emacs/*/etc",
	"/usr/TeX/bin",
	"/usr/tex/bin",
	"/usr/interviews/bin/LINUX",

	"/usr/X11R6/bin",
	"/usr/X386/bin",
	"/usr/bin/X11",
	"/usr/X11/bin",
	"/usr/X11R5/bin",

	"/usr/local/bin",
	"/usr/local/sbin",
	"/usr/local/etc",
	"/usr/local/lib",
	"/usr/local/games",
	"/usr/local/games/bin",
	"/usr/local/emacs/etc",
	"/usr/local/TeX/bin",
	"/usr/local/tex/bin",
	"/usr/local/bin/X11",

	"/usr/contrib",
	"/usr/hosts",
	"/usr/include",

	"/usr/g++-include",

	"/usr/ucb",
	"/usr/old",
	"/usr/new",
	"/usr/local",
	"/usr/libexec",
	"/usr/share",

	"/opt/*/bin",
	NULL
};

static const char *mandirs[] = {
	"/usr/man/*",
	"/usr/share/man/*",
	"/usr/X386/man/*",
	"/usr/X11/man/*",
	"/usr/TeX/man/*",
	"/usr/interviews/man/mann",
	NULL
};

static const char *srcdirs[] = {
	"/usr/src/*",
	"/usr/src/lib/libc/*",
	"/usr/src/lib/libc/net/*",
	"/usr/src/ucb/pascal",
	"/usr/src/ucb/pascal/utilities",
	"/usr/src/undoc",
	NULL
};

#ifdef DEBUG
# define DBG(_x)	do { \
				printf("DEBUG: "); \
				_x; \
				fputc('\n', stdout); \
			} while (0)
#else
# define DBG(_x)
#endif

static void __attribute__((__noreturn__)) usage(FILE *out)
{
	fputs(USAGE_HEADER, out);
	fprintf(out, _(" %s [options] file\n"), program_invocation_short_name);

	fputs(USAGE_OPTIONS, out);
	fputs(_(" -b         search only for binaries\n"), out);
	fputs(_(" -B <dirs>  define binaries lookup path\n"), out);
	fputs(_(" -m         search only for manuals\n"), out);
	fputs(_(" -M <dirs>  define man lookup path\n"), out);
	fputs(_(" -s         search only for sources\n"), out);
	fputs(_(" -S <dirs>  define sources lookup path\n"), out);
	fputs(_(" -f         terminate <dirs> argument list\n"), out);
	fputs(_(" -u         search for unusual entries\n"), out);
	fputs(_(" -l         output effective lookup paths\n"), out);
	fprintf(out, USAGE_MAN_TAIL("whereis(1)"));

	exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}

static void dirlist_add_dir(struct wh_dirlist **ls0, int type, const char *dir)
{
	struct stat st;
	struct wh_dirlist *prev = NULL, *ls = *ls0;

	DBG(printf("add dir: '%s'", dir));

	if (access(dir, R_OK) != 0)
		return;
	if (stat(dir, &st) != 0 || !S_ISDIR(st.st_mode))
		return;
	while (ls) {
		if (ls->st_ino == st.st_ino &&
		    ls->st_dev == st.st_dev &&
		    ls->type == type) {
			DBG(printf("  already in the list, ignore"));
			return;
		}
		prev = ls;
		ls = ls->next;
	}

	DBG(printf("  adding new directory"));

	ls = xcalloc(1, sizeof(*ls));
	ls->st_ino = st.st_ino;
	ls->st_dev = st.st_dev;
	ls->type = type;
	ls->path = canonicalize_path(dir);

	if (!*ls0)
		*ls0 = ls;		/* first in the list */
	else {
		assert(prev);
		prev->next = ls;	/* add to the end of the list */
	}
	return;
}

/* special case for '*' in the paths */
static void dirlist_add_subdir(struct wh_dirlist **ls, int type, const char *dir)
{
	char buf[PATH_MAX], *d;
	DIR *dirp;
	struct dirent *dp;

	strncpy(buf, dir, PATH_MAX);
	buf[PATH_MAX - 1] = '\0';

	DBG(printf("add subdir: %s", buf));

	d = strchr(buf, '*');
	if (!d)
		return;
	*d = 0;

	dirp = opendir(buf);
	if (!dirp)
		return;

	DBG(printf("  scan: %s", buf));

	while ((dp = readdir(dirp)) != NULL) {
		if (!strcmp(dp->d_name, ".") || !strcmp(dp->d_name, ".."))
			continue;
		snprintf(d, PATH_MAX - (d - buf), "%s", dp->d_name);
		/* a dir definition can have a star in middle of path */
		strcat(buf, strchr(dir, '*') + 1);
		dirlist_add_dir(ls, type, buf);
	}
	closedir(dirp);
	return;
}

static void construct_dirlist_from_env(const char *env,
				       struct wh_dirlist **ls,
				       int type)
{
	char *key = NULL, *tok = NULL, *pathcp, *path = getenv(env);

	if (!path)
		return;
	pathcp = xstrdup(path);

	DBG(printf("construct from env: %s", path));

	for (tok = strtok_r(pathcp, ":", &key); tok;
	     tok = strtok_r(NULL, ":", &key))
		dirlist_add_dir(ls, type, tok);

	free(pathcp);
	return;
}

static void construct_dirlist_from_argv(struct wh_dirlist **ls,
					int *idx,
					int argc,
					char *argv[],
					int type)
{
	DBG(printf("construct argv[%d..]", *idx));

	for (; *idx < argc; (*idx)++) {
		if (*argv[*idx] == '-')			/* end of the list */
			return;
		dirlist_add_dir(ls, type, argv[*idx]);
	}
	return;
}

static void construct_dirlist(struct wh_dirlist **ls,
			      int type,
			      const char **paths)
{
	size_t i;

	DBG(printf("construct from dirs"));

	for (i = 0; paths[i]; i++) {
		if (!strchr(paths[i], '*'))
			dirlist_add_dir(ls, type, paths[i]);
		else
			dirlist_add_subdir(ls, type, paths[i]);
	}
	return;
}

static void free_dirlist(struct wh_dirlist **ls0, int type)
{
	struct wh_dirlist *prev = NULL, *next, *ls = *ls0;

	*ls0 = NULL;

	DBG(printf("freeing dirlist"));

	while (ls) {
		if (ls->type & type) {
			next = ls->next;

			DBG(printf("freeing dir: %s", ls->path));

			free(ls->path);
			free(ls);
			ls = next;
			if (prev)
				prev->next = ls;
		} else {
			if (!prev)
				*ls0 = ls;	/* first unremoved */
			prev = ls;
			ls = ls->next;
		}
	}

	return;
}


static int filename_equal(const char *cp, const char *dp)
{
	int i = strlen(dp);

	/*DBG(printf("compare '%s' and '%s'", cp, dp));*/

	if (dp[0] == 's' && dp[1] == '.' && filename_equal(cp, dp + 2))
		return 1;
	if (!strcmp(dp + i - 2, ".Z"))
		i -= 2;
	else if (!strcmp(dp + i - 3, ".gz"))
		i -= 3;
	else if (!strcmp(dp + i - 3, ".xz"))
		i -= 3;
	else if (!strcmp(dp + i - 4, ".bz2"))
		i -= 4;
	while (*cp && *dp && *cp == *dp)
		cp++, dp++, i--;
	if (*cp == 0 && *dp == 0)
		return 1;
	while (isdigit(*dp))
		dp++;
	if (*cp == 0 && *dp++ == '.') {
		--i;
		while (i > 0 && *dp)
			if (--i, *dp++ == '.')
				return (*dp++ == 'C' && *dp++ == 0);
		return 1;
	}
	return 0;
}

static void findin(const char *dir, const char *pattern, int *count, char **wait)
{
	DIR *dirp;
	struct dirent *dp;

	dirp = opendir(dir);
	if (dirp == NULL)
		return;

	DBG(printf("find '%s' in '%s'", pattern, dir));

	while ((dp = readdir(dirp)) != NULL) {
		if (!filename_equal(pattern, dp->d_name))
			continue;

		if (uflag && *count == 0)
			xasprintf(wait, "%s/%s", dir, dp->d_name);

		else if (uflag && *count == 1 && *wait) {
			printf("%s: %s %s/%s", pattern, *wait, dir,  dp->d_name);
			free(*wait);
			*wait = NULL;
		} else
			printf(" %s/%s", dir, dp->d_name);
		++(*count);
	}
	closedir(dirp);
	return;
}

static void lookup(const char *pattern, struct wh_dirlist *ls, int want)
{
	char patbuf[PATH_MAX];
	int count = 0;
	char *wait = NULL, *p;

	DBG(printf("lookup dirs for '%s' (%s)", patbuf, pattern));

	/* canonicalize pattern -- remove path suffix etc. */
	p = strrchr(pattern, '/');
	p = p ? p + 1 : (char *) pattern;
	strncpy(patbuf, p, PATH_MAX);
	patbuf[PATH_MAX - 1] = '\0';
	p = strrchr(patbuf, '.');
	if (p)
		*p = '\0';

	if (!uflag)
		/* if -u not specified then we always print the pattern */
		printf("%s:", patbuf);

	for (; ls; ls = ls->next) {
		if ((ls->type & want) && ls->path)
			findin(ls->path, patbuf, &count, &wait);
	}

	free(wait);

	if ((count && !uflag) || (uflag && count > 1))
		putchar('\n');
	return;
}

static void list_dirlist(struct wh_dirlist *ls)
{
	while (ls) {
		if (ls->path) {
			switch (ls->type) {
			case BIN_DIR:
				printf("bin: ");
				break;
			case MAN_DIR:
				printf("man: ");
				break;
			case SRC_DIR:
				printf("src: ");
				break;
			default:
				abort();
			}
			printf("%s\n", ls->path);
		}
		ls = ls->next;
	}
}

int main(int argc, char **argv)
{
	struct wh_dirlist *ls = NULL;
	int want = ALL_DIRS;
	int i;

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	if (argc == 1)
		usage(stderr);

	construct_dirlist(&ls, BIN_DIR, bindirs);
	construct_dirlist_from_env("PATH", &ls, BIN_DIR);

	construct_dirlist(&ls, MAN_DIR, mandirs);
	construct_dirlist_from_env("MANPATH", &ls, MAN_DIR);

	construct_dirlist(&ls, SRC_DIR, srcdirs);

	for (i = 1; i < argc; i++) {
		const char *arg = argv[i];

		if (*arg != '-') {
			lookup(arg, ls, want);
			continue;
		}

		if (i > 1 && *argv[i - 1] != '-')
			/* the list of search patterns has been interupted by
			 * any non-pattern option, then reset the mask for
			 * wanted directories. For example:
			 *
			 *    whereis -m ls -b tr
			 *
			 * search for "ls" in mandirs and "tr" in bindirs
			 */
			want = ALL_DIRS;

		for (++arg; arg && *arg; arg++) {
			switch (*arg) {
			case 'f':
				break;
			case 'u':
				uflag = 1;
				break;
			case 'B':
				if (*(arg + 1))
					usage(stderr);
				i++;
				free_dirlist(&ls, BIN_DIR);
				construct_dirlist_from_argv(
					&ls, &i, argc, argv, BIN_DIR);
				break;
			case 'M':
				if (*(arg + 1))
					usage(stderr);
				i++;
				free_dirlist(&ls, MAN_DIR);
				construct_dirlist_from_argv(
					&ls, &i, argc, argv, MAN_DIR);
				break;
			case 'S':
				if (*(arg + 1))
					usage(stderr);
				i++;
				free_dirlist(&ls, SRC_DIR);
				construct_dirlist_from_argv(
					&ls, &i, argc, argv, SRC_DIR);
				break;
			case 'b':
				want = want == ALL_DIRS ? BIN_DIR : want | BIN_DIR;
				break;
			case 'm':
				want = want == ALL_DIRS ? MAN_DIR : want | MAN_DIR;
				break;
			case 's':
				want = want == ALL_DIRS ? SRC_DIR : want | SRC_DIR;
				break;
			case 'l':
				list_dirlist(ls);
				break;
			case 'V':
				printf(UTIL_LINUX_VERSION);
				return EXIT_SUCCESS;
			case 'h':
				usage(stdout);
			default:
				usage(stderr);
			}
		}
	}


	DBG(printf("DONE"));
	free_dirlist(&ls, ALL_DIRS);
	return EXIT_SUCCESS;
}
