/*
 * Copyright (C) 2008 Karel Zak <kzak@redhat.com>
 *
 * This file is part of util-linux-ng.
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This file is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * The original namei(1) was writtent by:
 *	Roger S. Southwick (May 2, 1990)
 *	Steve Tell (March 28, 1991)
 *	Arkadiusz Mikiewicz (1999-02-22)
 *	Li Zefan (2007-09-10).
 */
#include <stdio.h>
#include <unistd.h>
#include <getopt.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <err.h>
#include "nls.h"

#ifndef MAXSYMLINKS
#define MAXSYMLINKS 256
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define NAMEI_NOLINKS	(1 << 1)
#define NAMEI_MODES	(1 << 2)
#define NAMEI_MNTS	(1 << 3)

static int flags;

struct namei {
	struct stat	st;		/* item lstat() */
	char		*name;		/* item name */
	char		*abslink;	/* absolute symlink path */
	int		relstart;	/* offset of relative path in 'abslink' */
	struct namei	*next;		/* next item */
	int		level;
};

static void
free_namei(struct namei *nm)
{
	while (nm) {
		struct namei *next = nm->next;
		free(nm->name);
		free(nm->abslink);
		free(nm);
		nm = next;
	}
}

static void
readlink_to_namei(struct namei *nm, const char *path)
{
	char sym[PATH_MAX];
	size_t sz;

	sz = readlink(path, sym, sizeof(sym));
	if (sz < 1)
		err(EXIT_FAILURE, _("failed to read symlink: %s"), path);
	if (*sym != '/') {
		char *p = strrchr(path, '/');

		nm->relstart = p ? p - path : strlen(path);
		sz += nm->relstart + 1;
	}
	nm->abslink = malloc(sz + 1);
	if (!nm->abslink)
		err(EXIT_FAILURE, _("out of memory?"));

	if (*sym != '/') {
		memcpy(nm->abslink, path, nm->relstart);
		*(nm->abslink + nm->relstart) = '/';
		nm->relstart++;
		memcpy(nm->abslink + nm->relstart, sym, sz);
	} else
		memcpy(nm->abslink, sym, sz);
	nm->abslink[sz] = '\0';
}

static struct namei *
new_namei(struct namei *parent, const char *path, const char *fname, int lev)
{
	struct namei *nm;

	if (!fname)
		return NULL;
	nm = calloc(1, sizeof(*nm));
	if (!nm)
		err(EXIT_FAILURE, _("out of memory?"));
	if (parent)
		parent->next = nm;

	nm->level = lev;
	nm->name = strdup(fname);
	if (!nm->name)
		err(EXIT_FAILURE, _("out of memory?"));
	if (lstat(path, &nm->st) == -1)
		err(EXIT_FAILURE, _("could not stat '%s'"), path);
	return nm;
}

static struct namei *
add_namei(struct namei *parent, const char *orgpath, int start, struct namei **last)
{
	struct namei *nm = NULL, *first = NULL;
	char *fname, *end, *path;
	int level = 0;

	if (!orgpath)
		return NULL;
	if (parent) {
		nm = parent;
		level = parent->level + 1;
	}
	path = strdup(orgpath);
	if (!path)
		err(EXIT_FAILURE, _("out of memory?"));
	fname = path + start;

	/* root directory */
	if (*fname == '/') {
		while (*fname == '/')
			fname++; /* eat extra '/' */
		first = nm = new_namei(nm, "/", "/", level);
	}

	for (end = fname; fname && end; ) {
		/* set end of filename */
		end = strchr(fname, '/');
		if (end)
			*end = '\0';

		/* create a new entry */
		nm = new_namei(nm, path, fname, level);
		if (!first)
			first = nm;
		if (S_ISLNK(nm->st.st_mode))
			readlink_to_namei(nm, path);

		/* set begin of the next filename */
		if (end) {
			*end++ = '/';
			while (*end == '/')
				end++; /* eat extra '/' */
		}
		fname = end;
	}

	if (last)
		*last = nm;
	return first;
}


static int
follow_symlinks(struct namei *nm)
{
	int symcount = 0;

	for (; nm; nm = nm->next) {
		struct namei *next, *last;

		if (!S_ISLNK(nm->st.st_mode))
			continue;
		if (++symcount > MAXSYMLINKS) {
			/* drop the rest of the list */
			free_namei(nm->next);
			nm->next = NULL;
			return -1;
		}
		next = nm->next;
		nm->next = add_namei(nm, nm->abslink, nm->relstart, &last);
		if (last)
			last->next = next;
		else
			nm->next = next;
	}
	return 0;
}

static void
strmode(mode_t mode, char *str)
{
	if (S_ISDIR(mode))
		str[0] = 'd';
	else if (S_ISLNK(mode))
		str[0] = 'l';
	else if (S_ISCHR(mode))
		str[0] = 'c';
	else if (S_ISBLK(mode))
		str[0] = 'b';
	else if (S_ISSOCK(mode))
		str[0] = 's';
	else if (S_ISFIFO(mode))
		str[0] = 'p';
	else if (S_ISREG(mode))
		str[0] = '-';

	str[1] = mode & S_IRUSR ? 'r' : '-';
	str[2] = mode & S_IWUSR ? 'w' : '-';
	str[3] = (mode & S_ISUID
		? (mode & S_IXUSR ? 's' : 'S')
		: (mode & S_IXUSR ? 'x' : '-'));
	str[4] = mode & S_IRGRP ? 'r' : '-';
	str[5] = mode & S_IWGRP ? 'w' : '-';
	str[6] = (mode & S_ISGID
		? (mode & S_IXGRP ? 's' : 'S')
		: (mode & S_IXGRP ? 'x' : '-'));
	str[7] = mode & S_IROTH ? 'r' : '-';
	str[8] = mode & S_IWOTH ? 'w' : '-';
	str[9] = (mode & S_ISVTX
		? (mode & S_IXOTH ? 't' : 'T')
		: (mode & S_IXOTH ? 'x' : '-'));
	str[10] = '\0';
}

static void
print_namei(struct namei *nm, char *path)
{
	struct namei *prev = NULL;
	int i;

	if (path)
		printf("f: %s\n", path);

	for (; nm; prev = nm, nm = nm->next) {
		char md[11];

		strmode(nm->st.st_mode, md);

		if ((flags & NAMEI_MNTS) && prev &&
		    S_ISDIR(nm->st.st_mode) && S_ISDIR(prev->st.st_mode) &&
		    prev->st.st_dev != nm->st.st_dev)
			md[0] = 'D';

		for (i = 0; i < nm->level; i++)
			fputs("  ", stdout);

		if (flags & NAMEI_MODES)
			printf(" %s", md);
		else
			printf(" %c", md[0]);

		if (S_ISLNK(nm->st.st_mode))
			printf(" %s -> %s\n", nm->name,
					nm->abslink + nm->relstart);
		else
			printf(" %s\n", nm->name);
	}
}

static void
usage(int rc)
{
	const char *p = program_invocation_short_name;

	if (!*p)
		p = "namei";

	printf(_("\nUsage: %s [options] pathname [pathname ...]\n"), p);
	printf(_("\nOptions:\n"));

	printf(_(
	" -h, --help          displays this help text\n"
	" -x, --mountpoints   show mount point directories with a 'D'\n"
	" -m, --modes         show the mode bits of each file\n"
	" -n, --nosymlinks    don't follow symlinks\n"));

	printf(_("\nFor more information see namei(1).\n"));
	exit(rc);
}

struct option longopts[] =
{
	{ "help",	0, 0, 'h' },
	{ "mountpoints",0, 0, 'x' },
	{ "modes",	0, 0, 'm' },
	{ "nolinks",	0, 0, 'n' },
	{ NULL,		0, 0, 0 },
};

int
main(int argc, char **argv)
{
	extern int optind;
	int c;

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

	if (argc < 2)
		usage(EXIT_FAILURE);

	while ((c = getopt_long(argc, argv, "+h?xmn", longopts, NULL)) != -1) {
		switch(c) {
		case 'h':
		case '?':
			usage(EXIT_SUCCESS);
			break;
		case 'm':
			flags |= NAMEI_MODES;
			break;
		case 'x':
			flags |= NAMEI_MNTS;
			break;
		case 'n':
			flags |= NAMEI_NOLINKS;
			break;
		}
	}

	for(; optind < argc; optind++) {
		char *path = argv[optind];
		struct namei *nm = NULL;
		struct stat st;

		if (stat(path, &st) != 0)
			err(EXIT_FAILURE, _("failed to stat: %s"), path);

		nm = add_namei(NULL, path, 0, NULL);
		if (nm) {
			int sml = 0;

			if (!(flags & NAMEI_NOLINKS))
				sml = follow_symlinks(nm);
			print_namei(nm, path);
			free_namei(nm);
			if (sml == -1)
				errx(EXIT_FAILURE,
					_("%s: exceeded limit of symlinks"),
					path);
		}
	}

	return EXIT_SUCCESS;
}

