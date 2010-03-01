/*
 * findmnt(8)
 *
 * Copyright (C) 2010 Red Hat, Inc. All rights reserved.
 * Written by Karel Zak <kzak@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <err.h>
#include <unistd.h>
#include <getopt.h>
#include <string.h>

#include <mount.h>

#include "pathnames.h"
#include "nls.h"

/*
 * Column IDs
 */
enum {
	COL_SOURCE,
	COL_TARGET,
	COL_FSTYPE,
	COL_OPTIONS,
	COL_ISMOUNTED,
	COL_LABEL,
	COL_UUID,

	__NCOLUMNS
};

/*
 * Column names
 */
const char *colnames[] = {
	[COL_SOURCE] = "source",
	[COL_TARGET] = "target",
	[COL_FSTYPE] = "fstype",
	[COL_OPTIONS] = "options",
	[COL_ISMOUNTED] = "mounted",
	[COL_LABEL] = "label",
	[COL_UUID] = "uuid"
};

enum {
	FL_EVALUATE	= (1 << 1),
	FL_CANONICALIZE = (1 << 2),
	FL_FIRSTONLY	= (1 << 3),
	FL_INVERT	= (1 << 4)
};

struct match_data {
	const char *source;
	const char *target;
	const char *fstypes;
	const char *options;
};

int flags;
int columns[ __NCOLUMNS ];
int ncolumns;
mnt_cache *cache;

/*
 * converts @name to column ID
 */
static int get_column_id(const char *name, size_t namesz)
{
	int i;

	for (i = 0; i < __NCOLUMNS; i++) {
		const char *cn = colnames[i];

		if (!strncmp(name, cn, namesz) && !*(cn + namesz))
			return i;
	}

	errx(EXIT_FAILURE, _("unknown column: %*s"), (int) namesz, name);
	return -1;
}

/*
 * parses list of columns from @str and set column IDs to columns[]
 */
static int ctl_set_columns(const char *str)
{
	const char *begin = NULL, *end = NULL, *p;

	ncolumns = 0;

	if (!str || !*str)
		return -1;

	p = str;
	for (; p && *p; p++) {
		if (!begin)
			begin = p;		/* begin of the column name */
		if (*p == ',')
			end = p;		/* terminate the name */
		if (*(p + 1) == '\0')
			end = p + 1;		/* end of string */
		if (!begin || !end)
			continue;
		if (end <= begin)
			return -1;

		columns[ ncolumns++ ] =	get_column_id(begin, end - begin);
	}
	return 0;
}

static int print_column(mnt_fs *fs, int id)
{
	const char *str = NULL;

	if (!fs)
		return -1;

	switch(id) {
	case COL_SOURCE:
		/* dir or dev */
		str = mnt_fs_get_source(fs);

		if (str && ((flags & FL_EVALUATE) || (flags & FL_CANONICALIZE)))
			str = mnt_resolve_spec(str, cache);
		break;
	case COL_TARGET:
		str = mnt_fs_get_target(fs);
		break;
	case COL_FSTYPE:
		str = mnt_fs_get_fstype(fs);
		break;
	case COL_OPTIONS:
		str = mnt_fs_get_optstr(fs);
		break;
	default:
		return -1;
	}

	if (str)
		printf("%s", str);
	return 0;
}

static int print_fs(mnt_fs *fs)
{
	int i;

	for (i = 0; i < ncolumns; i++) {
		print_column(fs, i);
		printf("\t");
	}
	printf("\n");
	return 0;
}

static mnt_tab *parse_tabfile(const char *path)
{
	mnt_tab *tb = mnt_new_tab(path);
	if (!tb)
		return NULL;

	if (mnt_tab_parse_file(tb) != 0)
		goto err;

	if (mnt_tab_get_nerrs(tb)) {
		char buf[BUFSIZ];
		mnt_tab_strerror(tb, buf, sizeof(buf));
		warnx(_("%s: parse error: %s"), path, buf);
	}
	return tb;
err:
	mnt_free_tab(tb);
	err(EXIT_FAILURE, _("can't read: %s"), path);

	return NULL;
}

static int match_func(mnt_fs *fs, void *data)
{
	struct match_data *m = (struct match_data *) data;
	int rc = flags & FL_INVERT ? 1 : 0;

	/*fprintf(stderr, "source: %s : %s\n", m->source, mnt_fs_get_source(fs));*/

	if (m->target && !mnt_fs_match_target(fs, m->target, cache))
		return rc;
	if (m->source && !mnt_fs_match_source(fs, m->source, cache))
		return rc;
	if (m->fstypes && !mnt_fs_match_fstype(fs, m->fstypes))
		return rc;
	if (m->options && !mnt_fs_match_options(fs, m->options))
		return rc;

	return !rc;
}

static inline int is_list_mode(struct match_data *m)
{
	return (!m->source && !m->target && !m->fstypes && !m->options);
}

static inline int is_mount_mode(struct match_data *m, int fl)
{
	if (!m->source)
	       return 0;		/* <devname|TAG=|mountpoint> is required */
	if (m->fstypes || m->options)
		return 0;		/* cannot be restricted by -t or -O */
	if (!(fl & FL_FIRSTONLY))
		return 0;		/* we have to return the first entry only */

	return 1;			/* ok */
}

static void __attribute__((__noreturn__)) usage(FILE *out)
{
	fprintf(out, _("Usage: %s [options] <spec> [<target>]\n\nOptions:\n"),
			program_invocation_short_name);

	fprintf(out, _(
	" -s, --fstab            search in static table of filesystems\n"
	" -m, --mtab             search in table of mounted filesystems (default)\n"
	" -k, --kernel           search in kernel (mountinfo) file\n\n"

	" -c, --canonicalize     canonicalize printed paths\n"
	" -d, --direction <word> search direction - 'forward' or 'backward'\n"
	" -e, --evaluate         print all TAGs (LABEL/UUID) evaluated\n"
	" -h, --help             print this help\n"
	" -i, --invert           invert sense of matching\n"
        " -l, --first-only       print the first found filesystem only\n"
	" -o, --output <list>    output columns\n"
	" -O, --options <list>   limit the set of filesystems by mount options\n"
	" -t, --types <list>     limit the set of filesystem by FS types\n"));

	fprintf(out, _("\nFor more information see findmnt(1).\n"));

	exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}

int main(int argc, char *argv[])
{
	char *tabfile = NULL;
	int direction = MNT_ITER_FORWARD, ct = 0;
	mnt_tab *tb;
	mnt_iter *itr;
	mnt_fs *fs = NULL;
	int c;
	struct match_data mdata_buf, *mdata = &mdata_buf;

	struct option longopts[] = {
	    { "fstab",        0, 0, 's' },
	    { "mtab",         0, 0, 'm' },
	    { "kernel",       0, 0, 'k' },
	    { "canonicalize", 0, 0, 'c' },
	    { "direction",    1, 0, 'd' },
	    { "evaluate",     0, 0, 'e' },
	    { "help",         0, 0, 'h' },
	    { "invert",       0, 0, 'i' },
	    { "first-only",   0, 0, 'l' },
	    { "output",       0, 0, 'o' },
	    { "options",      1, 0, 'O' },
	    { "types",        1, 0, 't' },
	    { NULL,           0, 0, 0 }
	};

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

	/* default */
	columns[ncolumns++] = COL_SOURCE;
	columns[ncolumns++] = COL_TARGET;
	columns[ncolumns++] = COL_FSTYPE;
	columns[ncolumns++] = COL_OPTIONS;

	memset(mdata, 0, sizeof(*mdata));

	while ((c = getopt_long(argc, argv, "cd:ehiloO:kmst:", longopts, NULL)) != -1) {
		switch(c) {
		case 'c':
			flags |= FL_CANONICALIZE;
			break;
		case 'd':
			if (!strcmp(optarg, _("forward")))
				direction = MNT_ITER_FORWARD;
			else if (!strcmp(optarg, _("backward")))
				direction = MNT_ITER_BACKWARD;
			else
				errx(EXIT_FAILURE,
					_("uknown direction '%s')"), optarg);
			break;
		case 'e':
			flags |= FL_EVALUATE;
			break;
		case 'h':
			usage(stdout);
			break;
		case 'i':
			flags |= FL_INVERT;
			break;
		case 'l':
			flags |= FL_FIRSTONLY;
			break;
		case 'o':
			ctl_set_columns(optarg);
			break;
		case 'O':
			mdata->options = optarg;
			break;
		case 'm':
			if (tabfile)
				errx(EXIT_FAILURE, _("--{fstab,mtab,kernel} "
					"options are mutually exclusive"));
			tabfile = _PATH_MOUNTED;
			break;
		case 's':
			if (tabfile)
				errx(EXIT_FAILURE, _("--{fstab,mtab,kernel} "
					"options are mutually exclusive"));
			tabfile = _PATH_MNTTAB;
			break;
		case 'k':
			if (tabfile)
				errx(EXIT_FAILURE, _("--{fstab,mtab,kernel} "
					"options are mutually exclusive"));
			tabfile = _PATH_PROC_MOUNTINFO;
			break;
		case 't':
			mdata->fstypes = optarg;
			break;
		default:
			usage(stderr);
			break;
		}
	}

	if (!tabfile)
		tabfile = _PATH_MOUNTED;
	if (optind < argc)
		/* dev, tag or mountpoint */
		mdata->source = argv[optind++];
	if (optind < argc)
		/* mountpoint */
		mdata->target = argv[optind++];

	tb = parse_tabfile(tabfile);
	if (!tb)
		return EXIT_FAILURE;

	itr = mnt_new_iter(direction);
	if (!itr)
		err(EXIT_FAILURE, _("failed to initialize libmount iterator"));

	cache = mnt_new_cache();
	if (!cache)
		err(EXIT_FAILURE, _("failed to initialize libmount cache"));

	mnt_tab_set_cache(tb, cache);

	if (is_list_mode(mdata)) {
		/*
		 * Print whole file
		 */
		while(mnt_tab_next_fs(tb, itr, &fs) == 0) {
			print_fs(fs);
			ct++;
			if (flags & FL_FIRSTONLY)
				break;
		}
	} else if (is_mount_mode(mdata, flags)) {

		/*
		 * Look up for FS in the same way how mount(8) searchs in fstab
		 *
		 *   findmnt -l <spec>
		 */
		fs = mnt_tab_find_source(tb, mdata->source, direction);
		if (!fs)
			fs = mnt_tab_find_target(tb, mdata->source, direction);
		if (fs) {
			print_fs(fs);
			ct++;
		}
	} else {
		/*
		 * Look up for all matching entries
		 *
		 *    findmnt [-l] <source> <target> [-O <options>] [-t <types>]
		 *    findmnt [-l] <spec> [-O <options>] [-t <types>]
		 */
again:
		while (!mnt_tab_find_next_fs(tb, itr,
					match_func, (void *) mdata, &fs)) {
			print_fs(fs);
			ct++;
			if (flags & FL_FIRSTONLY)
				break;
		}
		if (!ct && !mdata->target && mdata->source) {
			/* swap 'spec' and target. */
			mdata->target = mdata->source;
			mdata->source = NULL;
			mnt_reset_iter(itr, direction);
			goto again;
		}
	}

	mnt_free_tab(tb);
	mnt_free_cache(cache);
	mnt_free_iter(itr);

	return ct ? EXIT_SUCCESS : 2;
}
