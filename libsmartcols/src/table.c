/*
 * table.c - functions handling the data at the table level
 *
 * Copyright (C) 2010-2014 Karel Zak <kzak@redhat.com>
 * Copyright (C) 2014 Ondrej Oprala <ooprala@redhat.com>
 *
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 */

/**
 * SECTION: table
 * @title: Table
 * @short_description: table data API
 *
 * Table data manipulation API.
 */


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


/**
 * scols_new_table:
 *
 * Returns: A newly allocated table.
 */
struct libscols_table *scols_new_table(void)
{
	struct libscols_table *tb;

	tb = calloc(1, sizeof(struct libscols_table));
	if (!tb)
		return NULL;

	tb->refcount = 1;
	tb->out = stdout;

	INIT_LIST_HEAD(&tb->tb_lines);
	INIT_LIST_HEAD(&tb->tb_columns);

	DBG(TAB, ul_debugobj(tb, "alloc"));
	return tb;
}

/**
 * scols_ref_table:
 * @tb: a pointer to a struct libscols_table instance
 *
 * Increases the refcount of @tb.
 */
void scols_ref_table(struct libscols_table *tb)
{
	if (tb)
		tb->refcount++;
}

/**
 * scols_unref_table:
 * @tb: a pointer to a struct libscols_table instance
 *
 * Decreases the refcount of @tb. When the count falls to zero, the instance
 * is automatically deallocated.
 */
void scols_unref_table(struct libscols_table *tb)
{
	if (tb && (--tb->refcount <= 0)) {
		DBG(TAB, ul_debugobj(tb, "dealloc"));
		scols_table_remove_lines(tb);
		scols_table_remove_columns(tb);
		scols_unref_symbols(tb->symbols);
		free(tb->linesep);
		free(tb->colsep);
		free(tb);
	}
}

/**
 * scols_table_add_column:
 * @tb: a pointer to a struct libscols_table instance
 * @cl: a pointer to a struct libscols_column instance
 *
 * Adds @cl to @tb's column list.
 *
 * Returns: 0, a negative number in case of an error.
 */
int scols_table_add_column(struct libscols_table *tb, struct libscols_column *cl)
{
	assert(tb);
	assert(cl);

	if (!tb || !cl || !list_empty(&tb->tb_lines))
		return -EINVAL;

	if (cl->flags & SCOLS_FL_TREE)
		tb->ntreecols++;

	DBG(TAB, ul_debugobj(tb, "add column %p", cl));
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

/**
 * scols_table_remove_column:
 * @tb: a pointer to a struct libscols_table instance
 * @cl: a pointer to a struct libscols_column instance
 *
 * Removes @cl from @tb.
 *
 * Returns: 0, a negative number in case of an error.
 */
int scols_table_remove_column(struct libscols_table *tb,
			      struct libscols_column *cl)
{
	assert(tb);
	assert(cl);

	if (!tb || !cl || !list_empty(&tb->tb_lines))
		return -EINVAL;

	if (cl->flags & SCOLS_FL_TREE)
		tb->ntreecols--;

	DBG(TAB, ul_debugobj(tb, "remove column %p", cl));
	list_del_init(&cl->cl_columns);
	tb->ncols--;
	scols_unref_column(cl);
	return 0;
}

/**
 * scols_table_remove_columns:
 * @tb: a pointer to a struct libscols_table instance
 *
 * Removes all of @tb's columns.
 *
 * Returns: 0, a negative number in case of an error.
 */
int scols_table_remove_columns(struct libscols_table *tb)
{
	assert(tb);

	if (!tb || !list_empty(&tb->tb_lines))
		return -EINVAL;

	DBG(TAB, ul_debugobj(tb, "remove all columns"));
	while (!list_empty(&tb->tb_columns)) {
		struct libscols_column *cl = list_entry(tb->tb_columns.next,
					struct libscols_column, cl_columns);
		scols_table_remove_column(tb, cl);
	}
	return 0;
}


/**
 * scols_table_new_column:
 * @tb: table
 * @name: column header
 * @whint: column width hint (absolute width: N > 1; relative width: N < 1)
 * @flags: flags integer
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
 *
 * The column is necessary to address by
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

	DBG(TAB, ul_debugobj(tb, "new column name=%s, whint=%g, flags=%d",
				name, whint, flags));
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

	if (scols_table_add_column(tb, cl))	/* this increments column ref-counter */
		goto err;

	scols_unref_column(cl);
	return cl;
err:
	scols_unref_column(cl);
	return NULL;
}

/**
 * scols_table_next_column:
 * @tb: a pointer to a struct libscols_table instance
 * @itr: a pointer to a struct libscols_iter instance
 * @cl: a pointer to a pointer to a struct libscols_column instance
 *
 * Returns the next column of @tb via @cl.
 *
 * Returns: 0, a negative value in case of an error.
 */
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


/**
 * scols_table_get_ncols:
 * @tb: table
 *
 * Returns: the ncols table member, a negative number in case of an error.
 */
int scols_table_get_ncols(struct libscols_table *tb)
{
	assert(tb);
	return tb ? tb->ncols : -EINVAL;
}

/**
 * scols_table_get_nlines:
 * @tb: table
 *
 * Returns: the nlines table member, a negative number in case of an error.
 */
int scols_table_get_nlines(struct libscols_table *tb)
{
	assert(tb);
	return tb ? tb->nlines : -EINVAL;
}

/**
 * scols_table_set_stream:
 * @tb: table
 * @stream: output stream
 *
 * Sets the output stream for table @tb.
 *
 * Returns: 0, a negative number in case of an error.
 */
int scols_table_set_stream(struct libscols_table *tb, FILE *stream)
{
	assert(tb);
	if (!tb)
		return -EINVAL;

	DBG(TAB, ul_debugobj(tb, "setting alternative stream"));
	tb->out = stream;
	return 0;
}

/**
 * scols_table_get_stream:
 * @tb: table
 *
 * Gets the output stream for table @tb.
 *
 * Returns: stream pointer, NULL in case of an error or an unset stream.
 */
FILE *scols_table_get_stream(struct libscols_table *tb)
{
	assert(tb);
	return tb ? tb->out: NULL;
}

/**
 * scols_table_reduce_termwidth:
 * @tb: table
 * @reduce: width
 *
 * Reduce the output width to @reduce.
 *
 * Returns: 0, a negative value in case of an error.
 */
int scols_table_reduce_termwidth(struct libscols_table *tb, size_t reduce)
{
	assert(tb);
	if (!tb)
		return -EINVAL;

	DBG(TAB, ul_debugobj(tb, "reduce terminal width: %zu", reduce));
	tb->termreduce = reduce;
	return 0;
}

/**
 * scols_table_get_column:
 * @tb: table
 * @n: number of column (0..N)
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

/**
 * scols_table_add_line:
 * @tb: table
 * @ln: line
 *
 * Note that this function calls scols_line_alloc_cells() if number
 * of the cells in the line is too small for @tb.
 *
 * Returns: 0, a negative value in case of an error.
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

	DBG(TAB, ul_debugobj(tb, "add line %p", ln));
	list_add_tail(&ln->ln_lines, &tb->tb_lines);
	ln->seqnum = tb->nlines++;
	scols_ref_line(ln);
	return 0;
}

/**
 * scols_table_remove_line:
 * @tb: table
 * @ln: line
 *
 * Note that this function does not destroy the parent<->child relationship between lines.
 * You have to call scols_line_remove_child()
 *
 * Returns: 0, a negative value in case of an error.
 */
int scols_table_remove_line(struct libscols_table *tb,
			    struct libscols_line *ln)
{
	assert(tb);
	assert(ln);

	if (!tb || !ln)
		return -EINVAL;

	DBG(TAB, ul_debugobj(tb, "remove line %p", ln));
	list_del_init(&ln->ln_lines);
	tb->nlines--;
	scols_unref_line(ln);
	return 0;
}

/**
 * scols_table_remove_lines:
 * @tb: table
 *
 * This empties the table and also destroys all the parent<->child relationships.
 */
void scols_table_remove_lines(struct libscols_table *tb)
{
	assert(tb);
	if (!tb)
		return;

	DBG(TAB, ul_debugobj(tb, "remove all lines"));
	while (!list_empty(&tb->tb_lines)) {
		struct libscols_line *ln = list_entry(tb->tb_lines.next,
						struct libscols_line, ln_lines);
		if (ln->parent)
			scols_line_remove_child(ln->parent, ln);
		scols_table_remove_line(tb, ln);
	}
}

/**
 * scols_table_next_line:
 * @tb: a pointer to a struct libscols_table instance
 * @itr: a pointer to a struct libscols_iter instance
 * @ln: a pointer to a pointer to a struct libscols_line instance
 *
 * Finds the next line and returns a pointer to it via @ln.
 *
 * Returns: 0, a negative value in case of an error.
 */
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

/**
 * scols_table_new_line:
 * @tb: table
 * @parent: parental line or NULL
 *
 * This is shortcut for
 *
 *   ln = scols_new_line();
 *   scols_table_add_line(tb, ln);
 *   scols_line_add_child(parent, ln);
 *
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

/**
 * scols_table_get_line:
 * @tb: table
 * @n: column number (0..N)
 *
 * This is a shortcut for
 *
 *   ln = scols_new_line();
 *   scols_line_set_....(cl, ...);
 *   scols_table_add_line(tb, ln);
 *
 * Returns: a newly allocate line
 */
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

/**
 * scols_copy_table:
 * @tb: table
 *
 * Creates a new independent table copy, except struct libscols_symbols that
 * are shared between the tables.
 *
 * Returns: a newly allocated copy of @tb
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
	ret = scols_new_table();
	if (!ret)
		return NULL;

	DBG(TAB, ul_debugobj(tb, "copy into %p", ret));

	if (tb->symbols)
		scols_table_set_symbols(ret, tb->symbols);

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

	/* separators */
	if (scols_table_set_column_separator(ret, tb->colsep) ||
	    scols_table_set_line_separator(ret, tb->linesep))
		goto err;

	return ret;
err:
	scols_unref_table(ret);
	return NULL;
}

/**
 * scols_table_set_symbols:
 * @tb: table
 * @sy: symbols or NULL
 *
 * Add a reference to @sy from the table. The symbols are used by library to
 * draw tree output. If no symbols are specified then library checks the
 * current environment to select ASCII or UTF8 symbols. This default behavior
 * could be controlled by scols_table_enable_ascii().
 *
 * Returns: 0, a negative value in case of an error.
 */
int scols_table_set_symbols(struct libscols_table *tb,
			    struct libscols_symbols *sy)
{
	assert(tb);

	if (!tb)
		return -EINVAL;

	DBG(TAB, ul_debugobj(tb, "setting alternative symbols %p", sy));

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
		if (!scols_table_is_ascii(tb) &&
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
/**
 * scols_table_enable_colors:
 * @tb: table
 * @enable: 1 or 0
 *
 * Enable/disable colors.
 *
 * Returns: 0 on success, negative number in case of an error.
 */
int scols_table_enable_colors(struct libscols_table *tb, int enable)
{
	assert(tb);
	if (!tb)
		return -EINVAL;

	DBG(TAB, ul_debugobj(tb, "colors: %s", enable ? "ENABLE" : "DISABLE"));
	tb->colors_wanted = enable;
	return 0;
}
/**
 * scols_table_enable_raw:
 * @tb: table
 * @enable: 1 or 0
 *
 * Enable/disable raw output format. The parsable output formats
 * (export and raw) are mutually exclusive.
 *
 * Returns: 0 on success, negative number in case of an error.
 */
int scols_table_enable_raw(struct libscols_table *tb, int enable)
{
	assert(tb);
	if (!tb)
		return -EINVAL;

	DBG(TAB, ul_debugobj(tb, "raw: %s", enable ? "ENABLE" : "DISABLE"));
	if (enable)
		tb->format = SCOLS_FMT_RAW;
	else if (tb->format == SCOLS_FMT_RAW)
		tb->format = 0;
	return 0;
}

/**
 * scols_table_enable_export:
 * @tb: table
 * @enable: 1 or 0
 *
 * Enable/disable export output format (COLUMNAME="value" ...).
 * The parsable output formats (export and raw) are mutually exclusive.
 *
 * Returns: 0 on success, negative number in case of an error.
 */
int scols_table_enable_export(struct libscols_table *tb, int enable)
{
	assert(tb);
	if (!tb)
		return -EINVAL;

	DBG(TAB, ul_debugobj(tb, "export: %s", enable ? "ENABLE" : "DISABLE"));
	if (enable)
		tb->format = SCOLS_FMT_EXPORT;
	else if (tb->format == SCOLS_FMT_EXPORT)
		tb->format = 0;
	return 0;
}

/**
 * scols_table_enable_ascii:
 * @tb: table
 * @enable: 1 or 0
 *
 * The ASCII-only output is relevant for tree-like outputs. The library
 * checks if the current environment is UTF8 compatible by default. This
 * function overrides this check and force the library to use ASCII chars
 * for the tree.
 *
 * If a custom libcols_symbols are specified (see scols_table_set_symbols()
 * then ASCII flag setting is ignored.
 *
 * Returns: 0 on success, negative number in case of an error.
 */
int scols_table_enable_ascii(struct libscols_table *tb, int enable)
{
	assert(tb);
	if (!tb)
		return -EINVAL;

	DBG(TAB, ul_debugobj(tb, "ascii: %s", enable ? "ENABLE" : "DISABLE"));
	tb->ascii = enable ? 1 : 0;
	return 0;
}

/**
 * scols_table_enable_noheadings:
 * @tb: table
 * @enable: 1 or 0
 *
 * Enable/disable header line.
 *
 * Returns: 0 on success, negative number in case of an error.
 */
int scols_table_enable_noheadings(struct libscols_table *tb, int enable)
{
	assert(tb);
	if (!tb)
		return -EINVAL;
	DBG(TAB, ul_debugobj(tb, "noheading: %s", enable ? "ENABLE" : "DISABLE"));
	tb->no_headings = enable ? 1 : 0;
	return 0;
}

/**
 * scols_table_enable_maxout:
 * @tb: table
 * @enable: 1 or 0
 *
 * The extra space after last column is ignored by default. The output
 * maximization use the extra space for all columns.
 *
 * Returns: 0 on success, negative number in case of an error.
 */
int scols_table_enable_maxout(struct libscols_table *tb, int enable)
{
	assert(tb);
	if (!tb)
		return -EINVAL;
	DBG(TAB, ul_debugobj(tb, "maxout: %s", enable ? "ENABLE" : "DISABLE"));
	tb->maxout = enable ? 1 : 0;
	return 0;
}

/**
 * scols_table_colors_wanted:
 * @tb: table
 *
 * Returns: 1 if colors are enabled.
 */
int scols_table_colors_wanted(struct libscols_table *tb)
{
	assert(tb);
	return tb && tb->colors_wanted;
}

/**
 * scols_table_is_empty:
 * @tb: table
 *
 * Returns: 1  if the table is empty.
 */
int scols_table_is_empty(struct libscols_table *tb)
{
	assert(tb);
	return !tb || !tb->nlines;
}

/**
 * scols_table_is_ascii:
 * @tb: table
 *
 * Returns: 1 if ASCII tree is enabled.
 */
int scols_table_is_ascii(struct libscols_table *tb)
{
	assert(tb);
	return tb && tb->ascii;
}

/**
 * scols_table_is_noheadings:
 * @tb: table
 *
 * Returns: 1 if header output is disabled.
 */
int scols_table_is_noheadings(struct libscols_table *tb)
{
	assert(tb);
	return tb && tb->no_headings;
}

/**
 * scols_table_is_export:
 * @tb: table
 *
 * Returns: 1 if export output format is enabled.
 */
int scols_table_is_export(struct libscols_table *tb)
{
	assert(tb);
	return tb && tb->format == SCOLS_FMT_EXPORT;
}

/**
 * scols_table_is_raw:
 * @tb: table
 *
 * Returns: 1 if raw output format is enabled.
 */
int scols_table_is_raw(struct libscols_table *tb)
{
	assert(tb);
	return tb && tb->format == SCOLS_FMT_RAW;
}


/**
 * scols_table_is_maxout
 * @tb: table
 *
 * Returns: 1 if output maximization is enabled, negative value in case of an error.
 */
int scols_table_is_maxout(struct libscols_table *tb)
{
	assert(tb);
	return tb && tb->maxout;
}

/**
 * scols_table_is_tree:
 * @tb: table
 *
 * Returns: returns 1 tree-like output is expected.
 */
int scols_table_is_tree(struct libscols_table *tb)
{
	assert(tb);
	return tb && tb->ntreecols > 0;
}

/**
 * scols_table_set_column_separator:
 * @tb: table
 * @sep: separator
 *
 * Sets the column separator of @tb to @sep.
 * Please note that @sep should always take up a single cell in the output.
 *
 * Returns: 0, a negative value in case of an error.
 */
int scols_table_set_column_separator(struct libscols_table *tb, const char *sep)
{
	char *p = NULL;

	assert (tb);

	if (!tb)
		return -EINVAL;

	if (sep) {
		p = strdup(sep);
		if (!p)
			return -ENOMEM;
	}

	DBG(TAB, ul_debugobj(tb, "new columns separator: %s", sep));
	free(tb->colsep);
	tb->colsep = p;
	return 0;
}

/**
 * scols_table_set_line_separator:
 * @tb: table
 * @sep: separator
 *
 * Sets the line separator of @tb to @sep.
 *
 * Returns: 0, a negative value in case of an error.
 */
int scols_table_set_line_separator(struct libscols_table *tb, const char *sep)
{
	char *p = NULL;

	assert (tb);

	if (!tb)
		return -EINVAL;

	if (sep) {
		p = strdup(sep);
		if (!p)
			return -ENOMEM;
	}

	DBG(TAB, ul_debugobj(tb, "new lines separator: %s", sep));
	free(tb->linesep);
	tb->linesep = p;
	return 0;
}

/**
 * scols_table_get_column_separator:
 * @tb: table
 *
 * Returns: @tb column separator, NULL in case of an error
 */
char *scols_table_get_column_separator(struct libscols_table *tb)
{
	assert (tb);

	if (!tb)
		return NULL;
	return tb->colsep;
}

/**
 * scols_table_get_line_separator:
 * @tb: table
 *
 * Returns: @tb line separator, NULL in case of an error
 */
char *scols_table_get_line_separator(struct libscols_table *tb)
{
	assert (tb);

	if (!tb)
		return NULL;
	return tb->linesep;

}

static int cells_cmp_wrapper(struct list_head *a, struct list_head *b, void *data)
{
	struct libscols_column *cl = (struct libscols_column *) data;
	struct libscols_line *ra, *rb;
	struct libscols_cell *ca, *cb;

	assert(a);
	assert(b);
	assert(cl);

	ra = list_entry(a, struct libscols_line, ln_lines);
	rb = list_entry(b, struct libscols_line, ln_lines);
	ca = scols_line_get_cell(ra, cl->seqnum);
	cb = scols_line_get_cell(rb, cl->seqnum);

	return cl->cmpfunc(ca, cb, cl->cmpfunc_data);
}

/**
 * scols_sort_table:
 * @tb: table
 * @cl: order by this column
 *
 * Orders the table by the column. See also scols_column_set_cmpfunc().
 *
 * Returns: 0, a negative value in case of an error.
 */
int scols_sort_table(struct libscols_table *tb, struct libscols_column *cl)
{
	assert(tb);
	assert(cl);

	if (!tb || !cl)
		return -EINVAL;

	DBG(TAB, ul_debugobj(tb, "sorting table"));
	list_sort(&tb->tb_lines, cells_cmp_wrapper, cl);
	return 0;
}
