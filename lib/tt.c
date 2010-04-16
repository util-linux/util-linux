/*
 * TT - Table or Tree, features:
 * - column width could be defined as absolute or relative to the terminal width
 * - allows to truncate or wrap data in columns
 * - prints tree if parent->child relation is defined
 * - draws the tree by ASCII or UTF8 lines (depends on terminal setting)
 *
 * Copyright (C) 2010 Karel Zak <kzak@redhat.com>
 *
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <termios.h>
#ifdef HAVE_LANGINFO_H
#include <langinfo.h>
#endif
#ifdef HAVE_SYS_IOCTL_H
#include <sys/ioctl.h>
#endif

#include "nls.h"
#include "widechar.h"
#include "c.h"
#include "tt.h"

struct tt_symbols {
	const char *branch;
	const char *vert;
	const char *right;
};

static const struct tt_symbols ascii_tt_symbols = {
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

static const struct tt_symbols utf8_tt_symbols = {
	.branch = UTF_VR UTF_H,
	.vert   = UTF_V " ",
	.right	= UTF_UR UTF_H,
};

#else /* !HAVE_WIDECHAR */
# define mbs_width       strlen(_s)
#endif /* !HAVE_WIDECHAR */

#define is_last_column(_tb, _cl) \
		list_last_entry(&(_cl)->cl_columns, &(_tb)->tb_columns)

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

/*
 * @flags: TT_FL_* flags (usually TT_FL_{ASCII,RAW})
 *
 * Returns: newly allocated table
 */
struct tt *tt_new_table(int flags)
{
	struct tt *tb;

	tb = calloc(1, sizeof(struct tt));
	if (!tb)
		return NULL;

	tb->flags = flags;
	INIT_LIST_HEAD(&tb->tb_lines);
	INIT_LIST_HEAD(&tb->tb_columns);

#ifdef HAVE_WIDECHAR
	if (!(flags & TT_FL_ASCII) && !strcmp(nl_langinfo(CODESET), "UTF-8"))
		tb->symbols = &utf8_tt_symbols;
	else
#endif
		tb->symbols = &ascii_tt_symbols;
	return tb;
}

void tt_free_table(struct tt *tb)
{
	if (!tb)
		return;
	while (!list_empty(&tb->tb_lines)) {
		struct tt_line *ln = list_entry(tb->tb_lines.next,
						struct tt_line, ln_lines);
		list_del(&ln->ln_lines);
		free(ln->data);
		free(ln);
	}
	while (!list_empty(&tb->tb_columns)) {
		struct tt_column *cl = list_entry(tb->tb_columns.next,
						struct tt_column, cl_columns);
		list_del(&cl->cl_columns);
		free(cl);
	}
	free(tb);
}

/*
 * @tb: table
 * @name: column header
 * @whint: column width hint (absolute width: N > 1; relative width: N < 1)
 * @flags: usually TT_FL_{TREE,TRUNCATE}
 *
 * The column is necessary to address (for example for tt_line_set_data()) by
 * sequential number. The first defined column has the colnum = 0. For example:
 *
 *	tt_define_column(tab, "FOO", 0.5, 0);		// colnum = 0
 *	tt_define_column(tab, "BAR", 0.5, 0);		// colnum = 1
 *      .
 *      .
 *	tt_line_set_data(line, 0, "foo-data");		// FOO column
 *	tt_line_set_data(line, 1, "bar-data");		// BAR column
 *
 * Returns: newly allocated column definition
 */
struct tt_column *tt_define_column(struct tt *tb, const char *name,
					double whint, int flags)
{
	struct tt_column *cl;

	if (!tb)
		return NULL;
	cl = calloc(1, sizeof(*cl));
	if (!cl)
		return NULL;

	cl->name = name;
	cl->width_hint = whint;
	cl->flags = flags;
	cl->seqnum = tb->ncols++;

	if (flags & TT_FL_TREE)
		tb->flags |= TT_FL_TREE;

	INIT_LIST_HEAD(&cl->cl_columns);
	list_add_tail(&cl->cl_columns, &tb->tb_columns);
	return cl;
}

/*
 * @tb: table
 * @parent: parental line or NULL
 *
 * Returns: newly allocate line
 */
struct tt_line *tt_add_line(struct tt *tb, struct tt_line *parent)
{
	struct tt_line *ln = NULL;

	if (!tb || !tb->ncols)
		goto err;
	ln = calloc(1, sizeof(*ln));
	if (!ln)
		goto err;
	ln->data = calloc(tb->ncols, sizeof(char *));
	if (!ln->data)
		goto err;

	ln->table = tb;
	ln->parent = parent;
	INIT_LIST_HEAD(&ln->ln_lines);
	INIT_LIST_HEAD(&ln->ln_children);
	INIT_LIST_HEAD(&ln->ln_branch);

	list_add_tail(&ln->ln_lines, &tb->tb_lines);

	if (parent)
		list_add_tail(&ln->ln_children, &parent->ln_branch);
	return ln;
err:
	free(ln);
	return NULL;
}

/*
 * @tb: table
 * @colnum: number of column (0..N)
 *
 * Returns: pointer to column or NULL
 */
struct tt_column *tt_get_column(struct tt *tb, int colnum)
{
	struct list_head *p;

	list_for_each(p, &tb->tb_columns) {
		struct tt_column *cl =
				list_entry(p, struct tt_column, cl_columns);
		if (cl->seqnum == colnum)
			return cl;
	}
	return NULL;
}

/*
 * @ln: line
 * @colnum: number of column (0..N)
 * @data: printable data
 *
 * Stores data that will be printed to the table cell.
 */
int tt_line_set_data(struct tt_line *ln, int colnum, const char *data)
{
	struct tt_column *cl;

	if (!ln)
		return -1;
	cl = tt_get_column(ln->table, colnum);
	if (!cl)
		return -1;
	ln->data[cl->seqnum] = data;
	return 0;
}

static int get_terminal_width(void)
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
	return 0;
}

static char *line_get_ascii_art(struct tt_line *ln, char *buf, size_t *bufsz)
{
	const char *art;
	size_t len;

	if (!ln->parent)
		return buf;

	buf = line_get_ascii_art(ln->parent, buf, bufsz);
	if (!buf)
		return NULL;

	if (list_last_entry(&ln->ln_children, &ln->parent->ln_branch))
		art = " ";
	else
		art = ln->table->symbols->vert;

	len = strlen(art);
	if (*bufsz < len)
		return NULL;	/* no space, internal error */

	memcpy(buf, art, len);
	*bufsz -= len;
	return buf + len;
}

static char *line_get_data(struct tt_line *ln, struct tt_column *cl,
				char *buf, size_t bufsz)
{
	const char *data = ln->data[cl->seqnum];
	const struct tt_symbols *sym;
	char *p = buf;

	memset(buf, 0, bufsz);

	if (!data)
		return NULL;
	if (!(cl->flags & TT_FL_TREE)) {
		strncpy(buf, data, bufsz);
		buf[bufsz - 1] = '\0';
		return buf;
	}
	if (ln->parent) {
		p = line_get_ascii_art(ln->parent, buf, &bufsz);
		if (!p)
			return NULL;
	}

	sym = ln->table->symbols;

	if (!ln->parent)
		snprintf(p, bufsz, "%s", data);			/* root node */
	else if (list_last_entry(&ln->ln_children, &ln->parent->ln_branch))
		snprintf(p, bufsz, "%s%s", sym->right, data);	/* last chaild */
	else
		snprintf(p, bufsz, "%s%s", sym->branch, data);	/* any child */

	return buf;
}

static void recount_widths(struct tt *tb, char *buf, size_t bufsz)
{
	struct list_head *p;
	int width = 0, trunc_only;

	/* set width according to the size of data
	 */
	list_for_each(p, &tb->tb_columns) {
		struct tt_column *cl =
				list_entry(p, struct tt_column, cl_columns);
		struct list_head *lp;

		list_for_each(lp, &tb->tb_lines) {
			struct tt_line *ln =
				list_entry(lp, struct tt_line, ln_lines);

			char *data = line_get_data(ln, cl, buf, bufsz);
			size_t len = data ? mbs_width(data) : 0;

			if (cl->width < len)
				cl->width = len;
		}
	}

	/* set minimal width (= size of column header)
	 */
	list_for_each(p, &tb->tb_columns) {
		struct tt_column *cl =
				list_entry(p, struct tt_column, cl_columns);

		cl->width_min = mbs_width(cl->name);

		if (cl->width < cl->width_min)
			cl->width = cl->width_min;
		else if (cl->width_hint >= 1 &&
			 cl->width_min < (int) cl->width_hint)
			cl->width = (int) cl->width_hint;

		width += cl->width + (is_last_column(tb, cl) ? 0 : 1);
	}

	if (width == tb->termwidth)
		goto leave;
	if (width < tb->termwidth) {
		/* cool, use the extra space for the last column */
		struct tt_column *cl = list_entry(
			tb->tb_columns.prev, struct tt_column, cl_columns);

		cl->width += tb->termwidth - width;
		goto leave;
	}

	/* bad, we have to reduce output width, this is done in two steps:
	 * 1/ reduce columns with a relative width and with truncate flag
	 * 2) reduce columns with a relative width without truncate flag
	 */
	trunc_only = 1;
	while(width > tb->termwidth) {
		int org = width;

		list_for_each(p, &tb->tb_columns) {
			struct tt_column *cl =
				list_entry(p, struct tt_column, cl_columns);

			if (width <= tb->termwidth)
				break;
			if (cl->width_hint > 1)
				continue;	/* never truncate columns with absolute sizes */
			if (cl->flags & TT_FL_TREE)
				continue;	/* never truncate the tree */
			if (trunc_only && !(cl->flags & TT_FL_TRUNCATE))
				continue;
			if (cl->width == cl->width_min)
				continue;
			if (cl->width > cl->width_hint * tb->termwidth) {
				cl->width--;
				width--;
			}
		}
		if (org == width) {
			if (trunc_only)
				trunc_only = 0;
			else
				break;
		}
	}
leave:
/*
	fprintf(stderr, "terminal: %d, output: %d\n", tb->termwidth, width);

	list_for_each(p, &tb->tb_columns) {
		struct tt_column *cl =
			list_entry(p, struct tt_column, cl_columns);

		fprintf(stderr, "width: %s=%d [%d]\n",
			cl->name, cl->width,
			cl->width_hint > 1 ? (int) cl->width_hint :
					     (int) (cl->width_hint * tb->termwidth));
	}
*/
	return;
}

/* note that this function modifies @data
 */
static void print_data(struct tt *tb, struct tt_column *cl, char *data)
{
	size_t len, i;
	int width;

	if (!data)
		data = "";

	/* raw mode */
	if (tb->flags & TT_FL_RAW) {
		fputs(data, stdout);
		if (!is_last_column(tb, cl))
			fputc(' ', stdout);
		return;
	}

	/* note that 'len' and 'width' are number of cells, not bytes */
	len = mbs_width(data);

	if (!len || len == (size_t) -1) {
		len = 0;
		data = NULL;
	}
	width = cl->width;

	if (is_last_column(tb, cl) && len < width)
		width = len;

	/* truncate data */
	if (len > width && (cl->flags & TT_FL_TRUNCATE)) {
		len = mbs_truncate(data, width);
		if (!data || len == (size_t) -1) {
			len = 0;
			data = NULL;
		}
	}
	if (data)
		fputs(data, stdout);
	for (i = len; i < width; i++)
		fputc(' ', stdout);		/* padding */

	if (!is_last_column(tb, cl)) {
		if (len > width && !(cl->flags & TT_FL_TRUNCATE)) {
			fputc('\n', stdout);
			for (i = 0; i <= cl->seqnum; i++) {
				struct tt_column *x = tt_get_column(tb, i);
				printf("%*s ", -x->width, " ");
			}
		} else
			fputc(' ', stdout);	/* columns separator */
	}
}

static void print_line(struct tt_line *ln, char *buf, size_t bufsz)
{
	struct list_head *p;

	/* set width according to the size of data
	 */
	list_for_each(p, &ln->table->tb_columns) {
		struct tt_column *cl =
				list_entry(p, struct tt_column, cl_columns);

		print_data(ln->table, cl, line_get_data(ln, cl, buf, bufsz));
	}
	fputc('\n', stdout);
}

static void print_header(struct tt *tb, char *buf, size_t bufsz)
{
	struct list_head *p;

	if ((tb->flags & TT_FL_NOHEADINGS) || list_empty(&tb->tb_lines))
		return;

	/* set width according to the size of data
	 */
	list_for_each(p, &tb->tb_columns) {
		struct tt_column *cl =
				list_entry(p, struct tt_column, cl_columns);

		strncpy(buf, cl->name, bufsz);
		buf[bufsz - 1] = '\0';
		print_data(tb, cl, buf);
	}
	fputc('\n', stdout);
}

static void print_table(struct tt *tb, char *buf, size_t bufsz)
{
	struct list_head *p;

	print_header(tb, buf, bufsz);

	list_for_each(p, &tb->tb_lines) {
		struct tt_line *ln = list_entry(p, struct tt_line, ln_lines);

		print_line(ln, buf, bufsz);
	}
}

static void print_tree_line(struct tt_line *ln, char *buf, size_t bufsz)
{
	struct list_head *p;

	print_line(ln, buf, bufsz);

	if (list_empty(&ln->ln_branch))
		return;

	/* print all children */
	list_for_each(p, &ln->ln_branch) {
		struct tt_line *chld =
				list_entry(p, struct tt_line, ln_children);
		print_tree_line(chld, buf, bufsz);
	}
}

static void print_tree(struct tt *tb, char *buf, size_t bufsz)
{
	struct list_head *p;

	print_header(tb, buf, bufsz);

	list_for_each(p, &tb->tb_lines) {
		struct tt_line *ln = list_entry(p, struct tt_line, ln_lines);

		if (ln->parent)
			continue;

		print_tree_line(ln, buf, bufsz);
	}
}

/*
 * @tb: table
 *
 * Prints the table to stdout
 */
int tt_print_table(struct tt *tb)
{
	char *line;

	if (!tb)
		return -1;
	if (!tb->termwidth) {
		tb->termwidth = get_terminal_width();
		if (tb->termwidth <= 0)
			tb->termwidth = 80;
	}
	line = malloc(tb->termwidth);
	if (!line)
		return -1;
	if (!(tb->flags & TT_FL_RAW))
		recount_widths(tb, line, tb->termwidth);
	if (tb->flags & TT_FL_TREE)
		print_tree(tb, line, tb->termwidth);
	else
		print_table(tb, line, tb->termwidth);

	free(line);
	return 0;
}

#ifdef TEST_PROGRAM
#include <err.h>
#include <errno.h>

enum { MYCOL_NAME, MYCOL_FOO, MYCOL_BAR, MYCOL_PATH };

int main(int argc, char *argv[])
{
	struct tt *tb;
	struct tt_line *ln, *pr, *root;
	int flags = 0, notree = 0, i;

	if (argc == 2 && !strcmp(argv[1], "--help")) {
		printf("%s [--ascii | --raw | --list]\n",
				program_invocation_short_name);
		return EXIT_SUCCESS;
	} else if (argc == 2 && !strcmp(argv[1], "--ascii"))
		flags |= TT_FL_ASCII;
	else if (argc == 2 && !strcmp(argv[1], "--raw")) {
		flags |= TT_FL_RAW;
		notree = 1;
	} else if (argc == 2 && !strcmp(argv[1], "--list"))
		notree = 1;

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

	tb = tt_new_table(flags);
	if (!tb)
		err(EXIT_FAILURE, "table initialization failed");

	tt_define_column(tb, "NAME", 0.3, notree ? 0 : TT_FL_TREE);
	tt_define_column(tb, "FOO", 0.3, TT_FL_TRUNCATE);
	tt_define_column(tb, "BAR", 0.3, 0);
	tt_define_column(tb, "PATH", 0.3, 0);

	for (i = 0; i < 2; i++) {
		root = ln = tt_add_line(tb, NULL);
		tt_line_set_data(ln, MYCOL_NAME, "AAA");
		tt_line_set_data(ln, MYCOL_FOO, "a-foo-foo");
		tt_line_set_data(ln, MYCOL_BAR, "barBar-A");
		tt_line_set_data(ln, MYCOL_PATH, "/mnt/AAA");

		pr = ln = tt_add_line(tb, ln);
		tt_line_set_data(ln, MYCOL_NAME, "AAA.A");
		tt_line_set_data(ln, MYCOL_FOO, "a.a-foo-foo");
		tt_line_set_data(ln, MYCOL_BAR, "barBar-A.A");
		tt_line_set_data(ln, MYCOL_PATH, "/mnt/AAA/A");

		ln = tt_add_line(tb, pr);
		tt_line_set_data(ln, MYCOL_NAME, "AAA.A.AAA");
		tt_line_set_data(ln, MYCOL_FOO, "a.a.a-foo-foo");
		tt_line_set_data(ln, MYCOL_BAR, "barBar-A.A.A");
		tt_line_set_data(ln, MYCOL_PATH, "/mnt/AAA/A/AAA");

		ln = tt_add_line(tb, root);
		tt_line_set_data(ln, MYCOL_NAME, "AAA.B");
		tt_line_set_data(ln, MYCOL_FOO, "a.b-foo-foo");
		tt_line_set_data(ln, MYCOL_BAR, "barBar-A.B");
		tt_line_set_data(ln, MYCOL_PATH, "/mnt/AAA/B");

		ln = tt_add_line(tb, pr);
		tt_line_set_data(ln, MYCOL_NAME, "AAA.A.BBB");
		tt_line_set_data(ln, MYCOL_FOO, "a.a.b-foo-foo");
		tt_line_set_data(ln, MYCOL_BAR, "barBar-A.A.BBB");
		tt_line_set_data(ln, MYCOL_PATH, "/mnt/AAA/A/BBB");

		ln = tt_add_line(tb, pr);
		tt_line_set_data(ln, MYCOL_NAME, "AAA.A.CCC");
		tt_line_set_data(ln, MYCOL_FOO, "a.a.c-foo-foo");
		tt_line_set_data(ln, MYCOL_BAR, "barBar-A.A.CCC");
		tt_line_set_data(ln, MYCOL_PATH, "/mnt/AAA/A/CCC");

		ln = tt_add_line(tb, root);
		tt_line_set_data(ln, MYCOL_NAME, "AAA.C");
		tt_line_set_data(ln, MYCOL_FOO, "a.c-foo-foo");
		tt_line_set_data(ln, MYCOL_BAR, "barBar-A.C");
		tt_line_set_data(ln, MYCOL_PATH, "/mnt/AAA/C");
	}

	tt_print_table(tb);
	tt_free_table(tb);

	return EXIT_SUCCESS;
}
#endif
