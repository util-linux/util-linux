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
#include <termios.h>
#ifdef HAVE_LANGINFO_H
#include <langinfo.h>
#endif
#ifdef HAVE_SYS_IOCTL_H
#include <sys/ioctl.h>
#endif
#include <assert.h>

#define USE_UNSTABLE_LIBMOUNT_API
#include <mount.h>

#include "pathnames.h"
#include "nls.h"
#include "c.h"
#include "tt.h"

/* flags */
enum {
	FL_EVALUATE	= (1 << 1),
	FL_CANONICALIZE = (1 << 2),
	FL_FIRSTONLY	= (1 << 3),
	FL_INVERT	= (1 << 4),
	FL_NOSWAPMATCH	= (1 << 6),
};

/* column IDs */
enum {
	COL_SOURCE,
	COL_TARGET,
	COL_FSTYPE,
	COL_OPTIONS,
	COL_VFS_OPTIONS,
	COL_FS_OPTIONS,
	COL_LABEL,
	COL_UUID,

	__NCOLUMNS
};

/* column names */
struct colinfo {
	const char	*name;		/* header */
	double		whint;		/* width hint (N < 1 is in percent of termwidth) */
	int		truncate;	/* boolean */
	const char	*match;		/* pattern for match_func() */
};

/* columns descriptions */
struct colinfo infos[__NCOLUMNS] = {
	[COL_SOURCE]  = { "SOURCE",     0.25, FALSE },
	[COL_TARGET]  = { "TARGET",     0.30, FALSE },
	[COL_FSTYPE]  = { "FSTYPE",     0.10, TRUE },
	[COL_OPTIONS] = { "OPTIONS",    0.10, TRUE },
	[COL_VFS_OPTIONS] = { "VFS-OPTIONS", 0.20, TRUE },
	[COL_FS_OPTIONS] = { "FS-OPTIONS", 0.10, TRUE },
	[COL_LABEL]   = { "LABEL",      0.10, FALSE },
	[COL_UUID]    = { "UUID",         36, FALSE },
};

/* global flags */
int flags;

/* array with IDs of enabled columns */
int columns[__NCOLUMNS];
int ncolumns;

/* libmount cache */
mnt_cache *cache;

static int get_column_id(int num)
{
	assert(num < ncolumns);
	assert(columns[num] < __NCOLUMNS);
	return columns[num];
}

static struct colinfo *get_column_info(int num)
{
	return &infos[ get_column_id(num) ];
}

static const char *column_id_to_name(int id)
{
	assert(id < __NCOLUMNS);
	return infos[id].name;
}

static const char *get_column_name(int num)
{
	return get_column_info(num)->name;
}

static float get_column_whint(int num)
{
	return get_column_info(num)->whint;
}

static int get_column_truncate(int num)
{
	return get_column_info(num)->truncate;
}

static const char *get_match(int id)
{
	assert(id < __NCOLUMNS);
	return infos[id].match;
}

static void set_match(int id, const char *match)
{
	assert(id < __NCOLUMNS);
	infos[id].match = match;
}

/*
 * "findmnt" without any filter
 */
static int is_listall_mode(void)
{
	return (!get_match(COL_SOURCE) &&
		!get_match(COL_TARGET) &&
		!get_match(COL_FSTYPE) &&
		!get_match(COL_OPTIONS));
}

/*
 * findmnt --first-only <devname|TAG=|mountpoint>
 *
 * ... it works like "mount <devname|TAG=|mountpoint>"
 */
static int is_mount_compatible_mode(void)
{
	if (!get_match(COL_SOURCE))
	       return 0;		/* <devname|TAG=|mountpoint> is required */
	if (get_match(COL_FSTYPE) || get_match(COL_OPTIONS))
		return 0;		/* cannot be restricted by -t or -O */
	if (!(flags & FL_FIRSTONLY))
		return 0;		/* we have to return the first entry only */

	return 1;			/* ok */
}

static void set_all_columns_truncate(int set)
{
	int i;

	for (i = 0; i < __NCOLUMNS; i++)
		infos[i].truncate = set;
}

/*
 * converts @name to column ID
 */
static int column_name_to_id(const char *name, size_t namesz)
{
	int i;

	for (i = 0; i < __NCOLUMNS; i++) {
		const char *cn = column_id_to_name(i);

		if (!strncasecmp(name, cn, namesz) && !*(cn + namesz))
			return i;
	}
	warnx(_("unknown column: %s"), name);
	return -1;
}

/*
 * parses list of columns from @str and add IDs to columns[]
 */
static int set_columns(const char *str)
{
	const char *begin = NULL, *p;

	ncolumns = 0;

	if (!str || !*str)
		return -1;

	ncolumns = 0;

	for (p = str; p && *p; p++) {
		const char *end = NULL;
		int id;

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

		id = column_name_to_id(begin, end - begin);
		if (id == -1)
			return -1;
		columns[ ncolumns++ ] = id;
		begin = NULL;
		if (end && !*end)
			break;
	}
	return 0;
}

/* Returns LABEL or UUID */
static const char *get_tag(mnt_fs *fs, const char *tagname)
{
	const char *t, *v, *res;

	if (!mnt_fs_get_tag(fs, &t, &v) && !strcmp(t, tagname))
		res = v;
	else {
		res = mnt_fs_get_source(fs);
		if (res)
			res = mnt_resolve_spec(res, cache);
		if (res)
			res = mnt_cache_find_tag_value(cache, res, tagname);
	}

	return res;
}

/* reads FS data from libmount */
static const char *get_data(mnt_fs *fs, int num)
{
	const char *str = NULL;

	switch(get_column_id(num)) {
	case COL_SOURCE:
		/* dir or dev */
		str = mnt_fs_get_srcpath(fs);

		if (str && (flags & FL_CANONICALIZE))
			str = mnt_resolve_path(str, cache);
		if (!str) {
			str = mnt_fs_get_source(fs);

			if (str && (flags & FL_EVALUATE))
				str = mnt_resolve_spec(str, cache);
		}
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
	case COL_VFS_OPTIONS:
		str = mnt_fs_get_vfs_optstr(fs);
		break;
	case COL_FS_OPTIONS:
		str = mnt_fs_get_fs_optstr(fs);
		break;
	case COL_UUID:
		str = get_tag(fs, "UUID");
		break;
	case COL_LABEL:
		str = get_tag(fs, "LABEL");
		break;
	default:
		break;
	}
	return str;
}

/* adds one line to the output @tab */
static struct tt_line *add_line(struct tt *tt, mnt_fs *fs,
					struct tt_line *parent)
{
	int i;
	struct tt_line *line = tt_add_line(tt, parent);

	if (!line) {
		warn(_("failed to add line to output"));
		return NULL;
	}
	for (i = 0; i < ncolumns; i++)
		tt_line_set_data(line, i, get_data(fs, i));

	return line;
}

/* reads filesystems from @tb (libmount) and fillin @tab (output table) */
static int create_treenode(struct tt *tt, mnt_tab *tb,
				mnt_fs *fs, struct tt_line *parent_line)
{
	mnt_fs *chld = NULL;
	mnt_iter *itr = NULL;
	struct tt_line *line;
	int rc = -1;

	if (!fs) {
		/* first call, get root FS */
		if (mnt_tab_get_root_fs(tb, &fs))
			goto leave;
		parent_line = NULL;
	}

	itr = mnt_new_iter(MNT_ITER_FORWARD);
	if (!itr)
		goto leave;

	line = add_line(tt, fs, parent_line);
	if (!line)
		goto leave;

	/*
	 * add all children to the output table
	 */
	while(mnt_tab_next_child_fs(tb, itr, fs, &chld) == 0) {
		if (create_treenode(tt, tb, chld, line))
			goto leave;
	}
	rc = 0;
leave:
	mnt_free_iter(itr);
	return rc;
}

/* calls libmount fstab/mtab/mountinfo parser */
static mnt_tab *parse_tabfile(const char *path)
{
	mnt_tab *tb = mnt_new_tab(path);
	if (!tb) {
		warn(_("failed to initialize libmount tab"));
		return NULL;
	}
	if (mnt_tab_parse_file(tb) != 0) {
		mnt_free_tab(tb);
		warn(_("can't read: %s"), path);
		return NULL;
	}
	if (mnt_tab_get_nerrs(tb)) {
		char buf[BUFSIZ];
		mnt_tab_strerror(tb, buf, sizeof(buf));
		warnx(_("%s: parse error: %s"), path, buf);
	}

	return tb;
}

/* filter function for libmount (mnt_tab_find_next_fs()) */
static int match_func(mnt_fs *fs, void *data)
{
	int rc = flags & FL_INVERT ? 1 : 0;
	const char *m;

	m = get_match(COL_TARGET);
	if (m && !mnt_fs_match_target(fs, m, cache))
		return rc;

	m = get_match(COL_SOURCE);
	if (m && !mnt_fs_match_source(fs, m, cache))
		return rc;

	m = get_match(COL_FSTYPE);
	if (m && !mnt_fs_match_fstype(fs, m))
		return rc;

	m = get_match(COL_OPTIONS);
	if (m && !mnt_fs_match_options(fs, m))
		return rc;

	return !rc;
}

/* iterate over filesystems in @tb */
static mnt_fs *get_next_fs(mnt_tab *tb, mnt_iter *itr)
{
	mnt_fs *fs = NULL;

	if (is_listall_mode()) {
		/*
		 * Print whole file
		 */
		mnt_tab_next_fs(tb, itr, &fs);

	} else if (is_mount_compatible_mode()) {
		/*
		 * Look up for FS in the same way how mount(8) searchs in fstab
		 *
		 *   findmnt -f <spec>
		 */
		fs = mnt_tab_find_source(tb, get_match(COL_SOURCE),
					mnt_iter_get_direction(itr));
		if (!fs)
			fs = mnt_tab_find_target(tb, get_match(COL_SOURCE),
					mnt_iter_get_direction(itr));
	} else {
		/*
		 * Look up for all matching entries
		 *
		 *    findmnt [-l] <source> <target> [-O <options>] [-t <types>]
		 *    findmnt [-l] <spec> [-O <options>] [-t <types>]
		 */
again:
		mnt_tab_find_next_fs(tb, itr, match_func,  NULL, &fs);

		if (!fs &&
		    !(flags & FL_NOSWAPMATCH) &&
		    !get_match(COL_TARGET) && get_match(COL_SOURCE)) {

			/* swap 'spec' and target. */
			set_match(COL_TARGET, get_match(COL_SOURCE));
			set_match(COL_SOURCE, NULL);
			mnt_reset_iter(itr, -1);

			goto again;
		}
	}

	return fs;
}

static int __attribute__((__noreturn__)) usage(FILE *out)
{
	fprintf(out, _(
	"\nUsage:\n"
	" %1$s [options]\n"
	" %1$s [options] <device> | <mountpoint>\n"
	" %1$s [options] <device> <mountpoint>\n"
	" %1$s [options] [--source <device>] [--target <mountpoint>]\n"),
		program_invocation_short_name);

	fprintf(out, _(
	"\nOptions:\n"
	" -s, --fstab            search in static table of filesystems\n"
	" -m, --mtab             search in table of mounted filesystems\n"
	" -k, --kernel           search in kernel table of mounted \n"
        "                        filesystems (default)\n\n"

	" -c, --canonicalize     canonicalize printed paths\n"
	" -d, --direction <word> search direction - 'forward' or 'backward'\n"
	" -e, --evaluate         print all TAGs (LABEL/UUID) evaluated\n"
        " -f, --first-only       print the first found filesystem only\n"
	" -h, --help             print this help\n"
	" -i, --invert           invert sense of matching\n"
	" -l, --list             use list format ouput\n"
	" -n, --noheadings       don't print headings\n"
	" -u, --notruncate       don't truncate text in columns\n"
	" -O, --options <list>   limit the set of filesystems by mount options\n"
	" -o, --output <list>    output columns\n"
	" -r, --raw              use raw format output\n"
	" -a, --ascii            use ascii chars for tree formatting\n"
	" -t, --types <list>     limit the set of filesystem by FS types\n"
	" -S, --source <string>  device, LABEL= or UUID=device\n"
	" -T, --target <string>  mountpoint\n\n"));

	fprintf(out, _("\nFor more information see findmnt(1).\n"));

	exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}

static int __attribute__((__noreturn__))
errx_mutually_exclusive(const char *opts)
{
	errx(EXIT_FAILURE, "%s %s", opts, _("options are mutually exclusive"));
}

int main(int argc, char *argv[])
{
	/* libmount */
	mnt_tab *tb = NULL;
	mnt_iter *itr = NULL;
	mnt_fs *fs = NULL;
	char *tabfile = NULL;
	int direction = MNT_ITER_FORWARD;

	/* table.h */
	struct tt *tt = NULL;
	int tt_flags = 0;

	int i, c, rc = EXIT_FAILURE;

	struct option longopts[] = {
	    { "ascii",        0, 0, 'a' },
	    { "canonicalize", 0, 0, 'c' },
	    { "direction",    1, 0, 'd' },
	    { "evaluate",     0, 0, 'e' },
	    { "first-only",   0, 0, 'f' },
	    { "fstab",        0, 0, 's' },
	    { "help",         0, 0, 'h' },
	    { "invert",       0, 0, 'i' },
	    { "kernel",       0, 0, 'k' },
	    { "list",         0, 0, 'l' },
	    { "mtab",         0, 0, 'm' },
	    { "noheadings",   0, 0, 'n' },
	    { "notruncate",   0, 0, 'u' },
	    { "options",      1, 0, 'O' },
	    { "output",       1, 0, 'o' },
	    { "raw",          0, 0, 'r' },
	    { "types",        1, 0, 't' },
	    { "source",       1, 0, 'S' },
	    { "target",       1, 0, 'T' },

	    { NULL,           0, 0, 0 }
	};

	assert(ARRAY_SIZE(columns) == __NCOLUMNS);

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

	/* default enabled columns */
	columns[ncolumns++] = COL_TARGET;
	columns[ncolumns++] = COL_SOURCE;
	columns[ncolumns++] = COL_FSTYPE;
	columns[ncolumns++] = COL_OPTIONS;

	/* default output format */
	tt_flags |= TT_FL_TREE;

	while ((c = getopt_long(argc, argv,
				"cd:ehifo:O:klmnrst:uS:T:", longopts, NULL)) != -1) {
		switch(c) {
		case 'a':
			tt_flags |= TT_FL_ASCII;
			break;
		case 'c':
			flags |= FL_CANONICALIZE;
			break;
		case 'd':
			if (!strcmp(optarg, "forward"))
				direction = MNT_ITER_FORWARD;
			else if (!strcmp(optarg, "backward"))
				direction = MNT_ITER_BACKWARD;
			else
				errx(EXIT_FAILURE,
					_("unknown direction '%s'"), optarg);
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
		case 'f':
			flags |= FL_FIRSTONLY;
			break;
		case 'u':
			set_all_columns_truncate(FALSE);
			break;
		case 'o':
			if (set_columns(optarg))
				exit(EXIT_FAILURE);
			break;
		case 'O':
			set_match(COL_OPTIONS, optarg);
			break;
		case 'm':		/* mtab */
			if (tabfile)
				errx_mutually_exclusive("--{fstab,mtab,kernel}");
			tabfile = _PATH_MOUNTED;
			tt_flags &= ~TT_FL_TREE;
			break;
		case 's':		/* fstab */
			if (tabfile)
				errx_mutually_exclusive("--{fstab,mtab,kernel}");
			tabfile = _PATH_MNTTAB;
			tt_flags &= ~TT_FL_TREE;
			break;
		case 'k':		/* kernel (mountinfo) */
			if (tabfile)
				 errx_mutually_exclusive("--{fstab,mtab,kernel}");
			tabfile = _PATH_PROC_MOUNTINFO;
			break;
		case 't':
			set_match(COL_FSTYPE, optarg);
			break;
		case 'r':
			tt_flags &= ~TT_FL_TREE;	/* disable the default */
			tt_flags |= TT_FL_RAW;		/* enable raw */
			break;
		case 'l':
			if (tt_flags & TT_FL_RAW)
				errx_mutually_exclusive("--{raw,list}");

			tt_flags &= ~TT_FL_TREE; /* disable the default */
			break;
		case 'n':
			tt_flags |= TT_FL_NOHEADINGS;
			break;
		case 'S':
			set_match(COL_SOURCE, optarg);
			flags |= FL_NOSWAPMATCH;
			break;
		case 'T':
			set_match(COL_TARGET, optarg);
			flags |= FL_NOSWAPMATCH;
			break;
		default:
			usage(stderr);
			break;
		}
	}

	if (!tabfile) {
		tabfile = _PATH_PROC_MOUNTINFO;

		if (access(tabfile, R_OK)) {		/* old kernel? */
			tabfile = _PATH_PROC_MOUNTS;
			tt_flags &= ~TT_FL_TREE;
		}
	}
	if (optind < argc && (get_match(COL_SOURCE) || get_match(COL_TARGET)))
		errx(EXIT_FAILURE, _(
			"options --target and --source can't be used together "
			"with command line element that is not an option"));

	if (optind < argc)
		set_match(COL_SOURCE, argv[optind++]);	/* dev/tag/mountpoint */
	if (optind < argc)
		set_match(COL_TARGET, argv[optind++]);	/* mountpoint */

	if (!is_listall_mode() || (flags & FL_FIRSTONLY))
		tt_flags &= ~TT_FL_TREE;

	if (!(flags & FL_NOSWAPMATCH) &&
	    !get_match(COL_TARGET) && get_match(COL_SOURCE)) {
		/*
		 * Check if we can swap source and target, it's
		 * not possible if the source is LABEL=/UUID=
		 */
		const char *x = get_match(COL_SOURCE);

		if (!strncmp(x, "LABEL=", 6) || !strncmp(x, "UUID=", 5))
			flags |= FL_NOSWAPMATCH;
	}

	/*
	 * initialize libmount
	 */
	mnt_init_debug(0);

	tb = parse_tabfile(tabfile);
	if (!tb)
		goto leave;

	itr = mnt_new_iter(direction);
	if (!itr) {
		warn(_("failed to initialize libmount iterator"));
		goto leave;
	}

	cache = mnt_new_cache();
	if (!cache) {
		warn(_("failed to initialize libmount cache"));
		goto leave;
	}
	mnt_tab_set_cache(tb, cache);

	/*
	 * initialize output formatting (tt.h)
	 */
	tt = tt_new_table(tt_flags);
	if (!tt) {
		warn(_("failed to initialize output table"));
		goto leave;
	}

	for (i = 0; i < ncolumns; i++) {
		int fl = get_column_truncate(i) ? TT_FL_TRUNCATE : 0;

		if (get_column_id(i) == COL_TARGET && (tt_flags & TT_FL_TREE))
			fl |= TT_FL_TREE;

		if (!tt_define_column(tt, get_column_name(i),
					get_column_whint(i), fl)) {
			warn(_("failed to initialize output column"));
			goto leave;
		}
	}

	/*
	 * Fill in data to the output table
	 */
	if (tt_flags & TT_FL_TREE) {
		if (create_treenode(tt, tb, NULL, NULL))
			goto leave;
	} else {
		while((fs = get_next_fs(tb, itr))) {
			if (!add_line(tt, fs, NULL))
				goto leave;
			if (flags & FL_FIRSTONLY)
				break;
		}
	}

	/*
	 * Print the output table
	 */
	tt_print_table(tt);
	rc = EXIT_SUCCESS;
leave:
	tt_free_table(tt);

	mnt_free_tab(tb);
	mnt_free_cache(cache);
	mnt_free_iter(itr);

	return rc;
}
