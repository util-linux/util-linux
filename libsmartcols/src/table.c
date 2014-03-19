/*
 * table.c - functions handling the data at the table level
 *
 * Copyright (C) 2010-2014 Karel Zak <kzak@redhat.com>
 *
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <termios.h>
#include <ctype.h>

#include "nls.h"
#include "widechar.h"
#include "smartcolsP.h"

#ifdef HAVE_WIDECHAR
#define UTF_V	"\342\224\202"	/* U+2502, Vertical line drawing char */
#define UTF_VR	"\342\224\234"	/* U+251C, Vertical and right */
#define UTF_H	"\342\224\200"	/* U+2500, Horizontal */
#define UTF_UR	"\342\224\224"	/* U+2514, Up and right */
#endif /* !HAVE_WIDECHAR */

#define is_last_column(_tb, _cl) \
		list_entry_is_last(&(_cl)->cl_columns, &(_tb)->tb_columns)


/*
 * @flags: SCOLS_FL_* flags (usually SCOLS_FL_{ASCII,RAW})
 * @syms: tree symbols or NULL for default
 *
 * Note that this function add a new reference to @syms
 *
 * Returns: newly allocated table
 */
struct libscols_table *scols_new_table(int flags, struct libscols_symbols *syms)
{
	struct libscols_table *tb;

	tb = calloc(1, sizeof(struct libscols_table));
	if (!tb)
		return NULL;

	tb->flags = flags;
	tb->refcount = 1;
	tb->first_run = TRUE;
	tb->out = stdout;

	INIT_LIST_HEAD(&tb->tb_lines);
	INIT_LIST_HEAD(&tb->tb_columns);

	if (scols_table_set_symbols(tb, syms) == 0)
		return tb;

	scols_unref_table(tb);
	return NULL;
}

void scols_ref_table(struct libscols_table *tb)
{
	if (tb)
		tb->refcount++;
}

void scols_unref_table(struct libscols_table *tb)
{
	if (tb && (--tb->refcount <= 0)) {
		scols_table_remove_lines(tb);
		scols_table_remove_columns(tb);
		scols_unref_symbols(tb->symbols);
		free(tb);
	}
}

int scols_table_add_column(struct libscols_table *tb, struct libscols_column *cl)
{
	assert(tb);
	assert(cl);

	if (!tb || !cl || !list_empty(&tb->tb_lines))
		return -EINVAL;

	list_add_tail(&cl->cl_columns, &tb->tb_columns);
	cl->seqnum = tb->ncols++;
	scols_ref_column(cl);

	/* TODO:
	 *
	 * Currently it's possible to add/remove columns only if the table is
	 * empty (see list_empty(tb->tb_lines) above). It would be nice to
	 * enlarge/reduce lines cells[] always when we add/remove a new column.
	 */
	return 0;
}

int scols_table_remove_column(struct libscols_table *tb,
			      struct libscols_column *cl)
{
	assert(tb);
	assert(cl);

	if (!tb || !cl || !list_empty(&tb->tb_lines))
		return -EINVAL;

	list_del_init(&cl->cl_columns);
	tb->ncols--;
	scols_unref_column(cl);
	return 0;
}

int scols_table_remove_columns(struct libscols_table *tb)
{
	assert(tb);

	if (!tb || !list_empty(&tb->tb_lines))
		return -EINVAL;

	while (!list_empty(&tb->tb_columns)) {
		struct libscols_column *cl = list_entry(tb->tb_columns.next,
					struct libscols_column, cl_columns);
		scols_table_remove_column(tb, cl);
	}
	return 0;
}


/*
 * @tb: table
 * @name: column header
 * @whint: column width hint (absolute width: N > 1; relative width: N < 1)
 * @flags: usually SCOLS_FL_{TREE,TRUNCATE}
 *
 * This is shortcut for
 *
 *   cl = scols_new_column();
 *   scols_column_set_....(cl, ...);
 *   scols_table_add_column(tb, cl);
 *
 * The column width is possible to define by three ways:
 *
 *  @whint = 0..1    : relative width, percent of terminal width
 *
 *  @whint = 1..N    : absolute width, empty colum will be truncated to
 *                     the column header width
 *
 *  @whint = 1..N
 *  @flags = SCOLS_FL_STRICTWIDTH
 *                   : absolute width, empty colum won't be truncated
 *
 * The column is necessary to address (for example for scols_line_set_cell_data()) by
 * sequential number. The first defined column has the colnum = 0. For example:
 *
 *	scols_table_new_column(tab, "FOO", 0.5, 0);		// colnum = 0
 *	scols_table_new_column(tab, "BAR", 0.5, 0);		// colnum = 1
 *      .
 *      .
 *	scols_line_get_cell(line, 0);				// FOO column
 *	scols_line_get_cell(line, 1);				// BAR column
 *
 * Returns: newly allocated column
 */
struct libscols_column *scols_table_new_column(struct libscols_table *tb,
					       const char *name,
					       double whint,
					       int flags)
{
	struct libscols_column *cl;
	struct libscols_cell *hr;

	assert (tb);
	if (!tb)
		return NULL;
	cl = scols_new_column();
	if (!cl)
		return NULL;

	/* set column name */
	hr = scols_column_get_header(cl);
	if (!hr)
		goto err;
	if (scols_cell_set_data(hr, name))
		goto err;

	scols_column_set_whint(cl, whint);
	scols_column_set_flags(cl, flags);

	if (flags & SCOLS_FL_TREE)
		tb->flags |= SCOLS_FL_TREE;

	if (scols_table_add_column(tb, cl))	/* this increments column ref-counter */
		goto err;

	scols_unref_column(cl);
	return cl;
err:
	scols_unref_column(cl);
	return NULL;
}

int scols_table_next_column(struct libscols_table *tb,
			    struct libscols_iter *itr,
			    struct libscols_column **cl)
{
	int rc = 1;

	if (!tb || !itr || !cl)
		return -EINVAL;
	*cl = NULL;

	if (!itr->head)
		SCOLS_ITER_INIT(itr, &tb->tb_columns);
	if (itr->p != itr->head) {
		SCOLS_ITER_ITERATE(itr, *cl, struct libscols_column, cl_columns);
		rc = 0;
	}

	return rc;
}


/*
 * @tb: table
 *
 * Returns: ncols integer
 */
int scols_table_get_ncols(struct libscols_table *tb)
{
	assert(tb);
	return tb ? tb->ncols : -EINVAL;
}

/*
 * @tb: table
 *
 * Returns: nlines integer
 */
int scols_table_get_nlines(struct libscols_table *tb)
{
	assert(tb);
	return tb ? tb->nlines : -EINVAL;
}

/*
 * @tb: table
 *
 * Returns: flags integer
 */
int scols_table_get_flags(struct libscols_table *tb)
{
	assert(tb);
	return tb ? tb->flags: -EINVAL;
}

int scols_table_set_stream(struct libscols_table *tb, FILE *stream)
{
	assert(tb);
	if (!tb)
		return -EINVAL;

	tb->out = stream;
	return 0;
}

FILE *scols_table_get_stream(struct libscols_table *tb)
{
	assert(tb);
	return tb ? tb->out: NULL;
}

int scols_table_reduce_termwidth(struct libscols_table *tb, size_t reduce)
{
	assert(tb);
	if (!tb)
		return -EINVAL;

	tb->termreduce = reduce;
	return 0;
}

/*
 * @tb: table
 * @: number of column (0..N)
 *
 * Returns: pointer to column or NULL
 */
struct libscols_column *scols_table_get_column(struct libscols_table *tb,
					       size_t n)
{
	struct libscols_iter itr;
	struct libscols_column *cl;

	assert(tb);
	if (!tb)
		return NULL;
	if (n >= tb->ncols)
		return NULL;

	scols_reset_iter(&itr, SCOLS_ITER_FORWARD);
	while (scols_table_next_column(tb, &itr, &cl) == 0) {
		if (cl->seqnum == n)
			return cl;
	}
	return NULL;
}

/*
 * Note that this functiion calls scols_line_alloc_cells() if number
 * of the cells in the line is too small for @tb.
 */
int scols_table_add_line(struct libscols_table *tb, struct libscols_line *ln)
{

	assert(tb);
	assert(ln);

	if (!tb || !ln)
		return -EINVAL;

	if (tb->ncols > ln->ncells) {
		int rc = scols_line_alloc_cells(ln, tb->ncols);
		if (rc)
			return rc;
	}

	list_add_tail(&ln->ln_lines, &tb->tb_lines);
	ln->seqnum = tb->nlines++;
	scols_ref_line(ln);
	return 0;
}

/* Note that this function does not destroy parent->child relation between lines.
 * You have to call scols_line_remove_child()
 */
int scols_table_remove_line(struct libscols_table *tb,
			    struct libscols_line *ln)
{
	assert(tb);
	assert(ln);

	if (!tb || !ln)
		return -EINVAL;

	list_del_init(&ln->ln_lines);
	tb->nlines--;
	scols_unref_line(ln);
	return 0;
}

/* This make the table empty and also destroy all parent<->child relations */
void scols_table_remove_lines(struct libscols_table *tb)
{
	assert(tb);
	if (!tb)
		return;

	while (!list_empty(&tb->tb_lines)) {
		struct libscols_line *ln = list_entry(tb->tb_lines.next,
						struct libscols_line, ln_lines);
		if (ln->parent)
			scols_line_remove_child(ln->parent, ln);
		scols_table_remove_line(tb, ln);
	}
}

int scols_table_next_line(struct libscols_table *tb,
			  struct libscols_iter *itr,
			  struct libscols_line **ln)
{
	int rc = 1;

	if (!tb || !itr || !ln)
		return -EINVAL;
	*ln = NULL;

	if (!itr->head)
		SCOLS_ITER_INIT(itr, &tb->tb_lines);
	if (itr->p != itr->head) {
		SCOLS_ITER_ITERATE(itr, *ln, struct libscols_line, ln_lines);
		rc = 0;
	}

	return rc;
}

/*
 * @tb: table
 * @parent: parental line or NULL
 *
 * This is shortcut for
 *
 *   ln = scols_new_linen();
 *   scols_line_set_....(cl, ...);
 *   scols_table_add_line(tb, ln);

 *
 * Returns: newly allocate line
 */
struct libscols_line *scols_table_new_line(struct libscols_table *tb,
					   struct libscols_line *parent)
{
	struct libscols_line *ln;

	assert(tb);
	assert(tb->ncols);

	if (!tb || !tb->ncols)
		return NULL;
	ln = scols_new_line();
	if (!ln)
		return NULL;

	if (scols_table_add_line(tb, ln))
		goto err;
	if (parent)
		scols_line_add_child(parent, ln);

	scols_unref_line(ln);	/* ref-counter incremented by scols_table_add_line() */
	return ln;
err:
	scols_unref_line(ln);
	return NULL;
}


struct libscols_line *scols_table_get_line(struct libscols_table *tb,
					   size_t n)
{
	struct libscols_iter itr;
	struct libscols_line *ln;

	assert(tb);
	if (!tb)
		return NULL;
	if (n >= tb->nlines)
		return NULL;

	scols_reset_iter(&itr, SCOLS_ITER_FORWARD);
	while (scols_table_next_line(tb, &itr, &ln) == 0) {
		if (ln->seqnum == n)
			return ln;
	}
	return NULL;
}

/*
 * Creates a new independent table copy, except struct libscols_symbols that
 * are shared between the tables.
 */
struct libscols_table *scols_copy_table(struct libscols_table *tb)
{
	struct libscols_table *ret;
	struct libscols_line *ln;
	struct libscols_column *cl;
	struct libscols_iter itr;

	assert(tb);
	if (!tb)
		return NULL;
	ret = scols_new_table(tb->flags, tb->symbols);
	if (!ret)
		return NULL;

	/* columns */
	scols_reset_iter(&itr, SCOLS_ITER_FORWARD);
	while (scols_table_next_column(tb, &itr, &cl) == 0) {
		cl = scols_copy_column(cl);
		if (!cl)
			goto err;
		if (scols_table_add_column(ret, cl))
			goto err;
		scols_unref_column(cl);
	}

	/* lines */
	scols_reset_iter(&itr, SCOLS_ITER_FORWARD);
	while (scols_table_next_line(tb, &itr, &ln) == 0) {
		struct libscols_line *newln = scols_copy_line(ln);
		if (!newln)
			goto err;
		if (scols_table_add_line(ret, newln))
			goto err;
		if (ln->parent) {
			struct libscols_line *p =
				scols_table_get_line(ret, ln->parent->seqnum);
			if (p)
				scols_line_add_child(p, newln);
		}
		scols_unref_line(newln);
	}

	return ret;
err:
	scols_unref_table(ret);
	return NULL;
}

int scols_table_set_symbols(struct libscols_table *tb,
			    struct libscols_symbols *sy)
{
	assert(tb);

	if (!tb)
		return -EINVAL;

	if (tb->symbols)				/* unref old */
		scols_unref_symbols(tb->symbols);
	if (sy) {					/* ref user defined */
		tb->symbols = sy;
		scols_ref_symbols(sy);
	} else {					/* default symbols */
		tb->symbols = scols_new_symbols();
		if (!tb->symbols)
			return -ENOMEM;
#if defined(HAVE_WIDECHAR)
		if (!(tb->flags & SCOLS_FL_ASCII) &&
		    !strcmp(nl_langinfo(CODESET), "UTF-8")) {
			scols_symbols_set_branch(tb->symbols, UTF_VR UTF_H);
			scols_symbols_set_vertical(tb->symbols, UTF_V " ");
			scols_symbols_set_right(tb->symbols, UTF_UR UTF_H);
		} else
#endif
		{
			scols_symbols_set_branch(tb->symbols, "|-");
			scols_symbols_set_vertical(tb->symbols, "| ");
			scols_symbols_set_right(tb->symbols, "`-");
		}
	}

	return 0;
}

