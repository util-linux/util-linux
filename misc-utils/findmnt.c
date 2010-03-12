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
#include <mount.h>

#include <assert.h>

#include "pathnames.h"
#include "nls.h"
#include "widechar.h"
#include "c.h"

/* flags */
enum {
	FL_EVALUATE	= (1 << 1),
	FL_CANONICALIZE = (1 << 2),
	FL_FIRSTONLY	= (1 << 3),
	FL_INVERT	= (1 << 4),
	FL_NOHEADINGS	= (1 << 5),
	FL_NOSWAPMATCH	= (1 << 6),
	FL_TREE		= (1 << 7),
	FL_RAW		= (1 << 8)
};

/* column IDs */
enum {
	COL_SOURCE,
	COL_TARGET,
	COL_FSTYPE,
	COL_OPTIONS,
	COL_LABEL,
	COL_UUID,

	__NCOLUMNS
};

struct treenode {
	mnt_fs		*fs;		/* filesystem */

	struct treenode	*parent;
	struct treenode	*first;		/* first child */
	struct treenode *last;		/* last child */

	struct treenode	*next;		/* next node in the same level */
};

/* column names */
struct colinfo {
	const char	*name;		/* header */
	double		whint;		/* width hint (N < 1 is in percent of termwidth) */
	int		wrap;		/* boolean (FALSE = truncate the column) */

	int		width;		/* real column width */
	const char	*match;		/* pattern for match_func() */
};

/* columns descriptions */
struct colinfo infos[__NCOLUMNS] = {
	[COL_SOURCE]  = { "SOURCE",     0.25, TRUE },
	[COL_TARGET]  = { "TARGET",     0.30, TRUE },
	[COL_FSTYPE]  = { "FSTYPE",     0.10, FALSE },
	[COL_OPTIONS] = { "OPTIONS",    0.10, FALSE },
	[COL_LABEL]   = { "LABEL",      0.10, TRUE },
	[COL_UUID]    = { "UUID",         36, TRUE },
};

struct treesym {
	const char *branch;
	const char *vert;
	const char *right;
};
const struct treesym ascii_tree_symbols = {
	.branch = "|-",
	.vert	= "| ",
	.right	= "`-",
};

#ifdef HAVE_WIDECHAR

#define	mbs_width(_s)	mbstowcs(NULL, _s, 0)

#define UTF_V	"\342\224\202"	/* U+2502, Vertical line drawing char */
#define UTF_VR	"\342\224\234"	/* U+251C, Vertical and right */
#define UTF_H	"\342\224\200"	/* U+2500, Horizontal */
#define UTF_UR	"\342\224\224"	/* U+2514, Up and right */

const struct treesym utf_tree_symbols = {
	.branch = UTF_VR UTF_H,
	.vert   = UTF_V " ",
	.right	= UTF_UR UTF_H,
};

const struct treesym *tree_symbols = &utf_tree_symbols;

#else /* !HAVE_WIDECHAR */

# define mbs_width       strlen(_s)
const struct treesym *tree_symbols = &ascii_tree_symbols;

#endif /* !HAVE_WIDECHAR */

/* global flags */
int flags;

/* array IDs of with enabled columns */
int columns[__NCOLUMNS];
int ncolumns;

int termwidth;	/* terminal width */
char *treebuf;	/* buffer for target column in tree mode */

/* libmount cache */
mnt_cache *cache;


static inline int is_last_column(int num)
{
	return num + 1 == ncolumns;
}

static inline int get_column_id(int num)
{
	int id;
	assert(num < ncolumns);

	id = columns[num];
	assert(id < __NCOLUMNS);
	return id;
}

static inline struct colinfo *get_column_desc(int num)
{
	return &infos[ get_column_id(num) ];
}

static inline const char *column_id_to_name(int id)
{
	assert(id < __NCOLUMNS);
	return infos[id].name;
}

static inline const char *get_column_name(int num)
{
	return get_column_desc(num)->name;
}

static inline float get_column_whint(int num)
{
	return get_column_desc(num)->whint;
}

static inline int get_column_width(int num)
{
	return get_column_desc(num)->width;
}

static inline void set_column_width(int num, int width)
{
	get_column_desc(num)->width = width;
}

static inline int get_column_wrap(int num)
{
	return get_column_desc(num)->wrap;
}

static inline const char *get_match(int id)
{
	assert(id < __NCOLUMNS);
	return infos[id].match;
}

static inline void set_match(int id, const char *match)
{
	assert(id < __NCOLUMNS);
	infos[id].match = match;
}

/*
 * "findmnt" without any filter
 */
static inline int is_listall_mode(void)
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
static inline int is_mount_compatible_mode(void)
{
	if (!get_match(COL_SOURCE))
	       return 0;		/* <devname|TAG=|mountpoint> is required */
	if (get_match(COL_FSTYPE) || get_match(COL_OPTIONS))
		return 0;		/* cannot be restricted by -t or -O */
	if (!(flags & FL_FIRSTONLY))
		return 0;		/* we have to return the first entry only */

	return 1;			/* ok */
}

static void set_all_columns_wrap(int set)
{
	int i;

	for (i = 0; i < __NCOLUMNS; i++)
		infos[i].wrap = set;
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

	errx(EXIT_FAILURE, _("unknown column: %s"), name);
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

		columns[ ncolumns++ ] =	column_name_to_id(begin, end - begin);
		begin = NULL;
		if (end && !*end)
			break;
	}
	return 0;
}

/* TODO: move to lib/terminal.c */
static int terminal_get_width(void)
{
#ifdef TIOCGSIZE
	struct ttysize	t_win;
#endif
#ifdef TIOCGWINSZ
	struct winsize	w_win;
#endif
        const char	*cp;

#ifdef TIOCGSIZE
	if (ioctl (0, TIOCGSIZE, &t_win) == 0)
		return t_win.ts_cols;
#endif
#ifdef TIOCGWINSZ
	if (ioctl (0, TIOCGWINSZ, &w_win) == 0)
		return w_win.ws_col;
#endif
        cp = getenv("COLUMNS");
	if (cp)
		return strtol(cp, NULL, 10);
	return 80;
}

static void recount_widths(void)
{
	int i, width = 0, ignore_wraps;

	/* set minimal width (= size of column header) */
	for (i = 0; i < ncolumns; i++) {
		const char *name = get_column_name(i);
		size_t len = mbs_width(name);
		float hint = get_column_whint(i);

		if (get_column_width(i) < len)
			/* enlarge to small columns */
			set_column_width(i, len);

		else if (hint >= 1)
			/* set absolute widths */
			set_column_width(i, (int) hint);
	}

	/* count used space */
	for (i = 0; i < ncolumns; i++)
		width += get_column_width(i) + (is_last_column(i) ? 0 : 1);

	if (width == termwidth)
		return;

	if (width < termwidth) {
		/* cool, use the extra space for the last column */
		i = ncolumns - 1;
		set_column_width(i, get_column_width(i) + (termwidth - width));

		return;
	}

	/* bad, we have to reduce output width, this is done in two steps:
	 * 1/ reduce columns with a relative width (see colinfo.whint) and
	 *    without wrap flag (this columns could be truncated)
	 * 2) reduce columns with a relative width with wrap flag
	 */
	ignore_wraps = 1;
	while(width > termwidth) {
		int org = width;
		for (i = ncolumns - 1; i >= 0 && width > termwidth; i--) {
			float hint = get_column_whint(i);
			int w = get_column_width(i);

			if (hint > 1)
				/* never truncate columns with absolute sizes */
				continue;

			if (get_column_id(i) == COL_TARGET && (flags & FL_TREE))
				/* never truncate the tree */
				continue;

			if (ignore_wraps && get_column_wrap(i))
				continue;

			if (w > hint * termwidth) {
				set_column_width(i, w - 1);
				width--;
			}
		}

		if (org == width) {
			if (ignore_wraps)
				ignore_wraps = 0;
			else
				break;
		}
	}
/*
	fprintf(stderr, "terminal: %d, output: %d\n", termwidth, width);
	for (i = 0; i < ncolumns; i++)
		fprintf(stderr, "width: %s=%d [%d]\n",
			get_column_name(i),
			get_column_width(i),
			(int) (get_column_whint(i) * termwidth));
*/
}

static char *get_treenode_ascii_art(struct treenode *node,
					char *buf, size_t *bufsz)
{
	const char *sym;
	size_t len;

	if (!node->parent)
		return buf;
	else {
		buf = get_treenode_ascii_art(node->parent, buf, bufsz);
		if (!buf)
			return NULL;
		sym = node->next ? tree_symbols->vert : " ";
	}
	len = strlen(sym);

	if (*bufsz < len)
		return NULL;	/* no space, internal error */

	memcpy(buf, sym, len);
	*bufsz -= len;
	return buf + len;
}

/* Returns LABEl or UUID */
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

static const char *get_tree_target(mnt_fs *fs, char *buf, size_t bufsz)
{
	struct treenode *node;
	const char *target;
	char *p = buf;

	node = (struct treenode *) mnt_fs_get_userdata(fs);
	if (!node)
		return NULL;

	target = mnt_fs_get_target(fs);
	if (!target)
		return NULL;

	if (node->parent) {
		p = get_treenode_ascii_art(node->parent, buf, &bufsz);
		if (!p)
			return NULL;
	}

	if (node->next)
		snprintf(p, bufsz, "%s%s", tree_symbols->branch, target);
	else if (node->parent)
		snprintf(p, bufsz, "%s%s", tree_symbols->right, target);
	else
		snprintf(p, bufsz, "%s", target);	/* root node */

	return buf;
}

static const char *get_column_data(mnt_fs *fs, int num)
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
		str = flags & FL_TREE ? get_tree_target(fs, treebuf, termwidth) :
					mnt_fs_get_target(fs);
		break;
	case COL_FSTYPE:
		str = mnt_fs_get_fstype(fs);
		break;
	case COL_OPTIONS:
		str = mnt_fs_get_optstr(fs);
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

	return str ? str : "-";
}

/* TODO: move to lib/mbalign.c */
#ifdef HAVE_WIDECHAR
static size_t wc_truncate (wchar_t *wc, size_t width)
{
  size_t cells = 0;
  int next_cells = 0;

  while (*wc)
    {
      next_cells = wcwidth (*wc);
      if (next_cells == -1) /* non printable */
        {
          *wc = 0xFFFD; /* L'\uFFFD' (replacement char) */
          next_cells = 1;
        }
      if (cells + next_cells > width)
        break;
      cells += next_cells;
      wc++;
    }
  *wc = L'\0';
  return cells;
}
#endif

/* TODO: move to lib/mbalign.c */
static size_t mbs_truncate(char *str, size_t width)
{
	size_t bytes = strlen(str) + 1;
#ifdef HAVE_WIDECHAR
	size_t sz = mbs_width(str);
	wchar_t *wcs = NULL;
	int rc = -1;

	if (sz <= width)
		return sz;		/* truncate is unnecessary */

	if (sz == (size_t) -1)
		goto done;

	wcs = malloc(sz * sizeof(wchar_t));
	if (!wcs)
		goto done;

	if (!mbstowcs(wcs, str, sz))
		goto done;
	rc = wc_truncate(wcs, width);
	wcstombs(str, wcs, bytes);
done:
	free(wcs);
	return rc;
#else
	if (width < bytes) {
		str[width] = '\0';
		return width;
	}
	return bytes;			/* truncate is unnecessary */
#endif
}

static void print_column_data(const char *data0, int num)
{
	size_t len, wrap, i;
	int width;
	char *data = (char *) data0;

	if (flags & FL_RAW) {
		fputs(data, stdout);
		if (!is_last_column(num))
			fputc(' ', stdout);
		return;
	}

	/* note that 'len' and 'width' is number of cells, not bytes */
	len = mbs_width(data);

	if (!len || len == (size_t) -1) {
		len = 0;
		data = NULL;
	}

	width = get_column_width(num);
	wrap = get_column_wrap(num);

	if (is_last_column(num) && len < width)
		width = len;

	if (len > width && !wrap) {
		data = strdup(data);
		if (data)
			len = mbs_truncate(data, width);
		if (!data || len == (size_t) -1) {
			len = 0;
			data = NULL;
		}
	}
	if (data)
		fputs(data, stdout);
	for (i = len; i < width; i++)		/* padding */
		fputc(' ', stdout);

	if (!is_last_column(num)) {
		if (len > width && wrap) {
			fputc('\n', stdout);

			for (i = 0; i <= num; i++)
				printf("%*s ",
					-get_column_width(i), " ");
		} else
			fputc(' ', stdout);	/* columns separator */
	}
	if (data != data0)
		free(data);
}

static void print_fs(mnt_fs *fs, int line)
{
	int i;

	/* print header */
	if (!(flags & FL_NOHEADINGS) && !line) {
		for (i = 0; i < ncolumns; i++)
			print_column_data(get_column_name(i), i);
		printf("\n");
	}

	/* print data */
	for (i = 0; i < ncolumns; i++) {
		const char *data = get_column_data(fs, i);
		print_column_data(data, i);
	}
	printf("\n");
}

static void set_widths(mnt_fs *fs)
{
	int i;

	for (i = 0; i < ncolumns; i++) {
		const char *data = get_column_data(fs, i);
		size_t len = data ? mbs_width(data) : 0;
		int old = get_column_width(i);

		if (old < len)
			set_column_width(i, len);
	}
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

static struct treenode *create_treenode(mnt_tab *tb, mnt_fs *fs)
{
	mnt_fs *chld = NULL;
	mnt_iter *itr = NULL;
	struct treenode *node = NULL;

	if (!fs) {
		/* first call - start with root FS and initialize tree buffer */
		if (mnt_tab_get_root_fs(tb, &fs))
			goto err;

		treebuf = malloc(termwidth);
		if (!treebuf)
			goto err;
	}

	itr = mnt_new_iter(MNT_ITER_FORWARD);
	if (!itr)
		goto err;

	node = calloc(1, sizeof(*node));
	if (!node)
		goto err;

	node->fs = fs;
	mnt_fs_set_userdata(fs, (void *) node);

	while(mnt_tab_next_child_fs(tb, itr, fs, &chld) == 0) {
		struct treenode *chnode;

		chnode = create_treenode(tb, chld);
		if (!chnode)
			break;

		chnode->parent = node;

		if (node->last)
			node->last->next = chnode;
		else
			node->first = chnode;

		node->last = chnode;
	}

	return node;

err:
	if (!fs)
		free(treebuf);
	free(node);
	mnt_free_iter(itr);
	return NULL;
}

static void print_treenode(struct treenode *node, int line)
{
	print_fs(node->fs, line++);

	/* print children */
	node = node->first;
	while(node) {
		print_treenode(node, line++);
		node = node->next;
	}
}

static void free_treenode(struct treenode *node)
{
	struct treenode *chld = node->first;

	if (!node->parent)		/* root node */
		free(treebuf);

	while(chld) {
		struct treenode *next = chld->next;
		free_treenode(chld);
		chld = next;
	}

	free(node);
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
	" -m, --mtab             search in table of mounted filesystems (default)\n"
	" -k, --kernel           search in kernel (mountinfo) file\n\n"

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
	char *tabfile = NULL;
	int direction = MNT_ITER_FORWARD;
	mnt_tab *tb;
	mnt_iter *itr;
	mnt_fs *fs = NULL;
	int c, ct = 0;

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
	flags |= FL_TREE;

	while ((c = getopt_long(argc, argv,
				"cd:ehifo:O:klmnrst:uS:T:", longopts, NULL)) != -1) {
		switch(c) {
		case 'a':
			tree_symbols = &ascii_tree_symbols;
			break;
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
		case 'f':
			flags |= FL_FIRSTONLY;
			break;
		case 'u':
			set_all_columns_wrap(TRUE);
			break;
		case 'o':
			set_columns(optarg);
			break;
		case 'O':
			set_match(COL_OPTIONS, optarg);
			break;
		case 'm':
			if (tabfile)
				errx_mutually_exclusive("--{fstab,mtab,kernel}");
			tabfile = _PATH_MOUNTED;
			flags &= ~FL_TREE;	/* disable the default */
			break;
		case 's':
			if (tabfile)
				errx_mutually_exclusive("--{fstab,mtab,kernel}");
			tabfile = _PATH_MNTTAB;
			flags &= ~FL_TREE;	/* disable the default */
			break;
		case 'k':
			if (tabfile)
				 errx_mutually_exclusive("--{fstab,mtab,kernel}");
			tabfile = _PATH_PROC_MOUNTINFO;
			break;
		case 't':
			set_match(COL_FSTYPE, optarg);
			break;
		case 'r':
			if (!(flags & FL_TREE) && !(flags & FL_RAW))
				errx_mutually_exclusive("--{raw,list}");

			flags &= ~FL_TREE;	/* disable the default */
			flags |= FL_RAW;	/* enable raw */
			break;
		case 'l':
			if (flags & FL_RAW)
				errx_mutually_exclusive("--{raw,list}");

			flags &= ~FL_TREE;	/* disable the default */
			break;
		case 'n':
			flags |= FL_NOHEADINGS;
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

	if (!tabfile)
		tabfile = _PATH_PROC_MOUNTINFO;

#ifdef HAVE_WIDECHAR
	/* non-UTF terminal */
	if ((flags & FL_TREE) && tree_symbols != &ascii_tree_symbols &&
					strcmp(nl_langinfo(CODESET), "UTF-8"))
		tree_symbols = &ascii_tree_symbols;
#endif

	if (optind < argc && (get_match(COL_SOURCE) || get_match(COL_TARGET)))
		errx(EXIT_FAILURE, _(
			"options --target and --source can't be used together "
			"with command line element that is not an option"));

	if (optind < argc)
		set_match(COL_SOURCE, argv[optind++]);	/* dev/tag/mountpoint */
	if (optind < argc)
		set_match(COL_TARGET, argv[optind++]);	/* mountpoint */

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

	termwidth = terminal_get_width();

	if (flags & FL_TREE) {
		struct treenode *tree = create_treenode(tb, NULL);
		if (!tree)
			err(EXIT_FAILURE, _("failed to create tree"));

		while (mnt_tab_next_fs(tb, itr, &fs) == 0) {
			set_widths(fs);
			ct++;
		}
		recount_widths();
		print_treenode(tree, 0);
		free_treenode(tree);
	} else {
		/* set width */

		if (!(flags & FL_RAW)) {
			while((fs = get_next_fs(tb, itr))) {
				set_widths(fs);
				if (flags & FL_FIRSTONLY)
					break;
			}
			ct = 0;
			mnt_reset_iter(itr, -1);
			recount_widths();
		}

		/* Print */
		while((fs = get_next_fs(tb, itr))) {
			print_fs(fs, ct++);
			if (flags & FL_FIRSTONLY)
				break;
		}
	}
	mnt_free_tab(tb);
	mnt_free_cache(cache);
	mnt_free_iter(itr);

	return ct ? EXIT_SUCCESS : 2;
}
