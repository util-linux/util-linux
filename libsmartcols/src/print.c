/*
 * table.c - functions handling the data at the table level
 *
 * Copyright (C) 2010-2014 Karel Zak <kzak@redhat.com>
 * Copyright (C) 2016 Igor Gnatenko <i.gnatenko.brain@gmail.com>
 *
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 */

/**
 * SECTION: table_print
 * @title: Table print
 * @short_description: output functions
 *
 * Table output API.
 */

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <termios.h>
#include <ctype.h>

#include "mbsalign.h"
#include "carefulputc.h"
#include "smartcolsP.h"

/* Fallback for symbols
 *
 * Note that by default library define all the symbols, but in case user does
 * not define all symbols or if we extended the symbols struct then we need
 * fallback to be more robust and backwardly compatible.
 */
#define titlepadding_symbol(tb)	((tb)->symbols->title_padding ? (tb)->symbols->title_padding : " ")
#define branch_symbol(tb)	((tb)->symbols->branch ? (tb)->symbols->branch : "|-")
#define vertical_symbol(tb)	((tb)->symbols->vert ? (tb)->symbols->vert : "| ")
#define right_symbol(tb)	((tb)->symbols->right ? (tb)->symbols->right : "`-")

#define cellpadding_symbol(tb)  ((tb)->padding_debug ? "." : \
				 ((tb)->symbols->cell_padding ? (tb)->symbols->cell_padding: " "))

#define want_repeat_header(tb)	(!(tb)->header_repeat || (tb)->header_next <= (tb)->termlines_used)


/* returns pointer to the end of used data */
static int line_ascii_art_to_buffer(struct libscols_table *tb,
				    struct libscols_line *ln,
				    struct libscols_buffer *buf)
{
	const char *art;
	int rc;

	assert(ln);
	assert(buf);

	if (!ln->parent)
		return 0;

	rc = line_ascii_art_to_buffer(tb, ln->parent, buf);
	if (rc)
		return rc;

	if (is_last_child(ln))
		art = "  ";
	else
		art = vertical_symbol(tb);

	return buffer_append_data(buf, art);
}

static int has_pending_data(struct libscols_table *tb)
{
	struct libscols_column *cl;
	struct libscols_iter itr;

	scols_reset_iter(&itr, SCOLS_ITER_FORWARD);
	while (scols_table_next_column(tb, &itr, &cl) == 0) {
		if (scols_column_is_hidden(cl))
			continue;
		if (cl->pending_data)
			return 1;
	}
	return 0;
}

/* print padding or ASCII-art instead of data of @cl */
static void print_empty_cell(struct libscols_table *tb,
			  struct libscols_column *cl,
			  struct libscols_line *ln,	/* optional */
			  size_t bufsz)
{
	size_t len_pad = 0;		/* in screen cells as opposed to bytes */

	/* generate tree ASCII-art rather than padding */
	if (ln && scols_column_is_tree(cl)) {
		if (!ln->parent) {
			/* only print symbols->vert if followed by child */
			if (!list_empty(&ln->ln_branch)) {
				fputs(vertical_symbol(tb), tb->out);
				len_pad = mbs_safe_width(vertical_symbol(tb));
			}
		} else {
			/* use the same draw function as though we were intending to draw an L-shape */
			struct libscols_buffer *art = new_buffer(bufsz);
			char *data;

			if (art) {
				/* whatever the rc, len_pad will be sensible */
				line_ascii_art_to_buffer(tb, ln, art);
				if (!list_empty(&ln->ln_branch) && has_pending_data(tb))
					buffer_append_data(art, vertical_symbol(tb));
				data = buffer_get_safe_data(tb, art, &len_pad, NULL);
				if (data && len_pad)
					fputs(data, tb->out);
				free_buffer(art);
			}
		}
	}

	if (is_last_column(cl))
		return;

	/* fill rest of cell with space */
	for(; len_pad < cl->width; ++len_pad)
		fputs(cellpadding_symbol(tb), tb->out);

	fputs(colsep(tb), tb->out);
}


static const char *get_cell_color(struct libscols_table *tb,
				  struct libscols_column *cl,
				  struct libscols_line *ln,	/* optional */
				  struct libscols_cell *ce)	/* optional */
{
	const char *color = NULL;

	if (tb && tb->colors_wanted) {
		if (ce)
			color = ce->color;
		if (ln && !color)
			color = ln->color;
		if (!color)
			color = cl->color;
	}
	return color;
}

/* Fill the start of a line with padding (or with tree ascii-art).
 *
 * This is necessary after a long non-truncated column, as this requires the
 * next column to be printed on the next line. For example (see 'DDD'):
 *
 * aaa bbb ccc ddd eee
 * AAA BBB CCCCCCC
 *             DDD EEE
 * ^^^^^^^^^^^^
 *  new line padding
 */
static void print_newline_padding(struct libscols_table *tb,
				  struct libscols_column *cl,
				  struct libscols_line *ln,	/* optional */
				  size_t bufsz)
{
	size_t i;

	assert(tb);
	assert(cl);

	fputs(linesep(tb), tb->out);		/* line break */
	tb->termlines_used++;

	/* fill cells after line break */
	for (i = 0; i <= (size_t) cl->seqnum; i++)
		print_empty_cell(tb, scols_table_get_column(tb, i), ln, bufsz);
}

/*
 * Pending data
 *
 * The first line in the multi-line cells (columns with SCOLS_FL_WRAP flag) is
 * printed as usually and output is truncated to match column width.
 *
 * The rest of the long text is printed on next extra line(s). The extra lines
 * don't exist in the table (not represented by libscols_line). The data for
 * the extra lines are stored in libscols_column->pending_data_buf and the
 * function print_line() adds extra lines until the buffer is not empty in all
 * columns.
 */

/* set data that will be printed by extra lines */
static int set_pending_data(struct libscols_column *cl, const char *data, size_t sz)
{
	char *p = NULL;

	if (data && *data) {
		DBG(COL, ul_debugobj(cl, "setting pending data"));
		assert(sz);
		p = strdup(data);
		if (!p)
			return -ENOMEM;
	}

	free(cl->pending_data_buf);
	cl->pending_data_buf = p;
	cl->pending_data_sz = sz;
	cl->pending_data = cl->pending_data_buf;
	return 0;
}

/* the next extra line has been printed, move pending data cursor */
static int step_pending_data(struct libscols_column *cl, size_t bytes)
{
	DBG(COL, ul_debugobj(cl, "step pending data %zu -= %zu", cl->pending_data_sz, bytes));

	if (bytes >= cl->pending_data_sz)
		return set_pending_data(cl, NULL, 0);

	cl->pending_data += bytes;
	cl->pending_data_sz -= bytes;
	return 0;
}

/* print next pending data for the column @cl */
static int print_pending_data(
		struct libscols_table *tb,
		struct libscols_column *cl,
		struct libscols_line *ln,	/* optional */
		struct libscols_cell *ce)
{
	const char *color = get_cell_color(tb, cl, ln, ce);
	size_t width = cl->width, bytes;
	size_t len = width, i;
	char *data;
	char *nextchunk = NULL;

	if (!cl->pending_data)
		return 0;
	if (!width)
		return -EINVAL;

	DBG(COL, ul_debugobj(cl, "printing pending data"));

	data = strdup(cl->pending_data);
	if (!data)
		goto err;

	if (scols_column_is_customwrap(cl)
	    && (nextchunk = cl->wrap_nextchunk(cl, data, cl->wrapfunc_data))) {
		bytes = nextchunk - data;

		len = mbs_safe_nwidth(data, bytes, NULL);
	} else
		bytes = mbs_truncate(data, &len);

	if (bytes == (size_t) -1)
		goto err;

	if (bytes)
		step_pending_data(cl, bytes);

	if (color)
		fputs(color, tb->out);
	fputs(data, tb->out);
	if (color)
		fputs(UL_COLOR_RESET, tb->out);
	free(data);

	if (is_last_column(cl))
		return 0;

	for (i = len; i < width; i++)
		fputs(cellpadding_symbol(tb), tb->out);		/* padding */

	fputs(colsep(tb), tb->out);	/* columns separator */
	return 0;
err:
	free(data);
	return -errno;
}

static int print_data(struct libscols_table *tb,
		      struct libscols_column *cl,
		      struct libscols_line *ln,	/* optional */
		      struct libscols_cell *ce,	/* optional */
		      struct libscols_buffer *buf)
{
	size_t len = 0, i, width, bytes;
	const char *color = NULL;
	char *data, *nextchunk;
	int is_last;

	assert(tb);
	assert(cl);

	data = buffer_get_data(buf);
	if (!data)
		data = "";

	is_last = is_last_column(cl);

	switch (tb->format) {
	case SCOLS_FMT_RAW:
		fputs_nonblank(data, tb->out);
		if (!is_last)
			fputs(colsep(tb), tb->out);
		return 0;

	case SCOLS_FMT_EXPORT:
		fprintf(tb->out, "%s=", scols_cell_get_data(&cl->header));
		fputs_quoted(data, tb->out);
		if (!is_last)
			fputs(colsep(tb), tb->out);
		return 0;

	case SCOLS_FMT_JSON:
		fputs_quoted_json_lower(scols_cell_get_data(&cl->header), tb->out);
		fputs(":", tb->out);
		switch (cl->json_type) {
			case SCOLS_JSON_STRING:
				if (!*data)
					fputs("null", tb->out);
				else
					fputs_quoted_json(data, tb->out);
				break;
			case SCOLS_JSON_NUMBER:
				if (!*data)
					fputs("null", tb->out);
				else
					fputs(data, tb->out);
				break;
			case SCOLS_JSON_BOOLEAN:
				fputs(!*data ? "false" :
				      *data == '0' ? "false" :
				      *data == 'N' || *data == 'n' ? "false" : "true",
				      tb->out);
				break;
		}
		if (!is_last)
			fputs(", ", tb->out);
		return 0;

	case SCOLS_FMT_HUMAN:
		break;		/* continue below */
	}

	color = get_cell_color(tb, cl, ln, ce);

	/* Encode. Note that 'len' and 'width' are number of cells, not bytes.
	 */
	data = buffer_get_safe_data(tb, buf, &len, scols_column_get_safechars(cl));
	if (!data)
		data = "";
	bytes = strlen(data);
	width = cl->width;

	/* custom multi-line cell based */
	if (*data && scols_column_is_customwrap(cl)
	    && (nextchunk = cl->wrap_nextchunk(cl, data, cl->wrapfunc_data))) {
		set_pending_data(cl, nextchunk, bytes - (nextchunk - data));
		bytes = nextchunk - data;
		len = mbs_safe_nwidth(data, bytes, NULL);
	}

	if (is_last
	    && len < width
	    && !scols_table_is_maxout(tb)
	    && !scols_column_is_right(cl))
		width = len;

	/* truncate data */
	if (len > width && scols_column_is_trunc(cl)) {
		len = width;
		bytes = mbs_truncate(data, &len);	/* updates 'len' */
	}

	/* standard multi-line cell */
	if (len > width && scols_column_is_wrap(cl)
	    && !scols_column_is_customwrap(cl)) {
		set_pending_data(cl, data, bytes);

		len = width;
		bytes = mbs_truncate(data, &len);
		if (bytes  != (size_t) -1 && bytes > 0)
			step_pending_data(cl, bytes);
	}

	if (bytes == (size_t) -1) {
		bytes = len = 0;
		data = NULL;
	}

	if (data) {
		if (scols_column_is_right(cl)) {
			if (color)
				fputs(color, tb->out);
			for (i = len; i < width; i++)
				fputs(cellpadding_symbol(tb), tb->out);
			fputs(data, tb->out);
			if (color)
				fputs(UL_COLOR_RESET, tb->out);
			len = width;

		} else if (color) {
			char *p = data;
			size_t art = buffer_get_safe_art_size(buf);

			/* we don't want to colorize tree ascii art */
			if (scols_column_is_tree(cl) && art && art < bytes) {
				fwrite(p, 1, art, tb->out);
				p += art;
			}

			fputs(color, tb->out);
			fputs(p, tb->out);
			fputs(UL_COLOR_RESET, tb->out);
		} else
			fputs(data, tb->out);
	}
	for (i = len; i < width; i++)
		fputs(cellpadding_symbol(tb), tb->out);	/* padding */

	if (is_last)
		return 0;

	if (len > width && !scols_column_is_trunc(cl))
		print_newline_padding(tb, cl, ln, buffer_get_size(buf));	/* next column starts on next line */
	else
		fputs(colsep(tb), tb->out);		/* columns separator */

	return 0;
}

int __cell_to_buffer(struct libscols_table *tb,
			  struct libscols_line *ln,
			  struct libscols_column *cl,
			  struct libscols_buffer *buf)
{
	const char *data;
	struct libscols_cell *ce;
	int rc = 0;

	assert(tb);
	assert(ln);
	assert(cl);
	assert(buf);
	assert(cl->seqnum <= tb->ncols);

	buffer_reset_data(buf);

	ce = scols_line_get_cell(ln, cl->seqnum);
	data = ce ? scols_cell_get_data(ce) : NULL;
	if (!data)
		return 0;

	if (!scols_column_is_tree(cl))
		return buffer_set_data(buf, data);

	/*
	 * Tree stuff
	 */
	if (ln->parent && !scols_table_is_json(tb)) {
		rc = line_ascii_art_to_buffer(tb, ln->parent, buf);

		if (!rc && is_last_child(ln))
			rc = buffer_append_data(buf, right_symbol(tb));
		else if (!rc)
			rc = buffer_append_data(buf, branch_symbol(tb));
		if (!rc)
			buffer_set_art_index(buf);
	}

	if (!rc)
		rc = buffer_append_data(buf, data);
	return rc;
}

static void fput_indent(struct libscols_table *tb)
{
	int i;

	for (i = 0; i <= tb->indent; i++)
		fputs("   ", tb->out);
}

static void fput_table_open(struct libscols_table *tb)
{
	tb->indent = 0;

	if (scols_table_is_json(tb)) {
		fputc('{', tb->out);
		fputs(linesep(tb), tb->out);

		fput_indent(tb);
		fputs_quoted(tb->name, tb->out);
		fputs(": [", tb->out);
		fputs(linesep(tb), tb->out);

		tb->indent++;
		tb->indent_last_sep = 1;
	}
}

static void fput_table_close(struct libscols_table *tb)
{
	tb->indent--;

	if (scols_table_is_json(tb)) {
		fput_indent(tb);
		fputc(']', tb->out);
		tb->indent--;
		fputs(linesep(tb), tb->out);
		fputc('}', tb->out);
		tb->indent_last_sep = 1;
	}
}

static void fput_children_open(struct libscols_table *tb)
{
	if (scols_table_is_json(tb)) {
		fputc(',', tb->out);
		fputs(linesep(tb), tb->out);
		fput_indent(tb);
		fputs("\"children\": [", tb->out);
	}
	/* between parent and child is separator */
	fputs(linesep(tb), tb->out);
	tb->indent_last_sep = 1;
	tb->indent++;
	tb->termlines_used++;
}

static void fput_children_close(struct libscols_table *tb)
{
	tb->indent--;

	if (scols_table_is_json(tb)) {
		fput_indent(tb);
		fputc(']', tb->out);
		fputs(linesep(tb), tb->out);
		tb->indent_last_sep = 1;
	}
}

static void fput_line_open(struct libscols_table *tb)
{
	if (scols_table_is_json(tb)) {
		fput_indent(tb);
		fputc('{', tb->out);
		tb->indent_last_sep = 0;
	}
	tb->indent++;
}

static void fput_line_close(struct libscols_table *tb, int last, int last_in_table)
{
	tb->indent--;
	if (scols_table_is_json(tb)) {
		if (tb->indent_last_sep)
			fput_indent(tb);
		fputs(last ? "}" : "},", tb->out);
		if (!tb->no_linesep)
			fputs(linesep(tb), tb->out);

	} else if (tb->no_linesep == 0 && last_in_table == 0) {
		fputs(linesep(tb), tb->out);
		tb->termlines_used++;
	}

	tb->indent_last_sep = 1;
}

/*
 * Prints data. Data can be printed in more formats (raw, NAME=xxx pairs), and
 * control and non-printable characters can be encoded in the \x?? encoding.
 */
static int print_line(struct libscols_table *tb,
		      struct libscols_line *ln,
		      struct libscols_buffer *buf)
{
	int rc = 0, pending = 0;
	struct libscols_column *cl;
	struct libscols_iter itr;

	assert(ln);

	DBG(TAB, ul_debugobj(tb, "printing line"));

	/* regular line */
	scols_reset_iter(&itr, SCOLS_ITER_FORWARD);
	while (rc == 0 && scols_table_next_column(tb, &itr, &cl) == 0) {
		if (scols_column_is_hidden(cl))
			continue;
		rc = __cell_to_buffer(tb, ln, cl, buf);
		if (rc == 0)
			rc = print_data(tb, cl, ln,
					scols_line_get_cell(ln, cl->seqnum),
					buf);
		if (rc == 0 && cl->pending_data)
			pending = 1;
	}

	/* extra lines of the multi-line cells */
	while (rc == 0 && pending) {
		pending = 0;
		fputs(linesep(tb), tb->out);
		tb->termlines_used++;
		scols_reset_iter(&itr, SCOLS_ITER_FORWARD);
		while (rc == 0 && scols_table_next_column(tb, &itr, &cl) == 0) {
			if (scols_column_is_hidden(cl))
				continue;
			if (cl->pending_data) {
				rc = print_pending_data(tb, cl, ln, scols_line_get_cell(ln, cl->seqnum));
				if (rc == 0 && cl->pending_data)
					pending = 1;
			} else
				print_empty_cell(tb, cl, ln, buffer_get_size(buf));
		}
	}

	return 0;
}

static int print_title(struct libscols_table *tb)
{
	int rc, color = 0;
	mbs_align_t align;
	size_t width, len = 0, bufsz, titlesz;
	char *title = NULL, *buf = NULL;

	assert(tb);

	if (!tb->title.data)
		return 0;

	DBG(TAB, ul_debugobj(tb, "printing title"));

	/* encode data */
	if (tb->no_encode) {
		len = bufsz = strlen(tb->title.data) + 1;
		buf = strdup(tb->title.data);
		if (!buf) {
			rc = -ENOMEM;
			goto done;
		}
	} else {
		bufsz = mbs_safe_encode_size(strlen(tb->title.data)) + 1;
		if (bufsz == 1) {
			DBG(TAB, ul_debugobj(tb, "title is empty string -- ignore"));
			return 0;
		}
		buf = malloc(bufsz);
		if (!buf) {
			rc = -ENOMEM;
			goto done;
		}

		if (!mbs_safe_encode_to_buffer(tb->title.data, &len, buf, NULL) ||
		    !len || len == (size_t) -1) {
			rc = -EINVAL;
			goto done;
		}
	}

	/* truncate and align */
	width = tb->is_term ? tb->termwidth : 80;
	titlesz = width + bufsz;

	title = malloc(titlesz);
	if (!title) {
		rc = -EINVAL;
		goto done;
	}

	switch (scols_cell_get_alignment(&tb->title)) {
	case SCOLS_CELL_FL_RIGHT:
		align = MBS_ALIGN_RIGHT;
		break;
	case SCOLS_CELL_FL_CENTER:
		align = MBS_ALIGN_CENTER;
		break;
	case SCOLS_CELL_FL_LEFT:
	default:
		align = MBS_ALIGN_LEFT;
		/*
		 * Don't print extra blank chars after the title if on left
		 * (that's same as we use for the last column in the table).
		 */
		if (len < width
		    && !scols_table_is_maxout(tb)
		    && isblank(*titlepadding_symbol(tb)))
			width = len;
		break;

	}

	/* copy from buf to title and align to width with title_padding */
	rc = mbsalign_with_padding(buf, title, titlesz,
			&width, align,
			0, (int) *titlepadding_symbol(tb));

	if (rc == -1) {
		rc = -EINVAL;
		goto done;
	}

	if (tb->colors_wanted && tb->title.color)
		color = 1;
	if (color)
		fputs(tb->title.color, tb->out);

	fputs(title, tb->out);

	if (color)
		fputs(UL_COLOR_RESET, tb->out);

	fputc('\n', tb->out);
	rc = 0;
done:
	free(buf);
	free(title);
	DBG(TAB, ul_debugobj(tb, "printing title done [rc=%d]", rc));
	return rc;
}

static int print_header(struct libscols_table *tb, struct libscols_buffer *buf)
{
	int rc = 0;
	struct libscols_column *cl;
	struct libscols_iter itr;

	assert(tb);

	if ((tb->header_printed == 1 && tb->header_repeat == 0) ||
	    scols_table_is_noheadings(tb) ||
	    scols_table_is_export(tb) ||
	    scols_table_is_json(tb) ||
	    list_empty(&tb->tb_lines))
		return 0;

	DBG(TAB, ul_debugobj(tb, "printing header"));

	/* set the width according to the size of the data */
	scols_reset_iter(&itr, SCOLS_ITER_FORWARD);
	while (rc == 0 && scols_table_next_column(tb, &itr, &cl) == 0) {
		if (scols_column_is_hidden(cl))
			continue;
		rc = buffer_set_data(buf, scols_cell_get_data(&cl->header));
		if (!rc)
			rc = print_data(tb, cl, NULL, &cl->header, buf);
	}

	if (rc == 0) {
		fputs(linesep(tb), tb->out);
		tb->termlines_used++;
	}

	tb->header_printed = 1;
	tb->header_next = tb->termlines_used + tb->termheight;
	if (tb->header_repeat)
		DBG(TAB, ul_debugobj(tb, "\tnext header: %zu [current=%zu]",
					tb->header_next, tb->termlines_used));
	return rc;
}


static int print_range(	struct libscols_table *tb,
			struct libscols_buffer *buf,
			struct libscols_iter *itr,
			struct libscols_line *end)
{
	int rc = 0;
	struct libscols_line *ln;

	assert(tb);
	DBG(TAB, ul_debugobj(tb, "printing range"));

	while (rc == 0 && scols_table_next_line(tb, itr, &ln) == 0) {

		int last = scols_iter_is_last(itr);

		fput_line_open(tb);
		rc = print_line(tb, ln, buf);
		fput_line_close(tb, last, last);

		if (end && ln == end)
			break;

		if (!last && want_repeat_header(tb))
			print_header(tb, buf);
	}

	return rc;

}

static int print_table(struct libscols_table *tb, struct libscols_buffer *buf)
{
	struct libscols_iter itr;

	scols_reset_iter(&itr, SCOLS_ITER_FORWARD);
	return print_range(tb, buf, &itr, NULL);
}


static int print_tree_line(struct libscols_table *tb,
			   struct libscols_line *ln,
			   struct libscols_buffer *buf,
			   int last,
			   int last_in_table)
{
	int rc;

	/* print the line */
	fput_line_open(tb);
	rc = print_line(tb, ln, buf);
	if (rc)
		goto done;

	/* print children */
	if (!list_empty(&ln->ln_branch)) {
		struct list_head *p;

		fput_children_open(tb);

		/* print all children */
		list_for_each(p, &ln->ln_branch) {
			struct libscols_line *chld =
					list_entry(p, struct libscols_line, ln_children);
			int last_child = p->next == &ln->ln_branch;

			rc = print_tree_line(tb, chld, buf, last_child, last_in_table && last_child);
			if (rc)
				goto done;
		}

		fput_children_close(tb);
	}

	if (list_empty(&ln->ln_branch) || scols_table_is_json(tb))
		fput_line_close(tb, last, last_in_table);
done:
	return rc;
}

static int print_tree(struct libscols_table *tb, struct libscols_buffer *buf)
{
	int rc = 0;
	struct libscols_line *ln, *last = NULL;
	struct libscols_iter itr;

	assert(tb);

	DBG(TAB, ul_debugobj(tb, "printing tree"));

	scols_reset_iter(&itr, SCOLS_ITER_FORWARD);

	while (scols_table_next_line(tb, &itr, &ln) == 0)
		if (!last || !ln->parent)
			last = ln;

	scols_reset_iter(&itr, SCOLS_ITER_FORWARD);
	while (rc == 0 && scols_table_next_line(tb, &itr, &ln) == 0) {
		if (ln->parent)
			continue;
		rc = print_tree_line(tb, ln, buf, ln == last, ln == last);
	}

	return rc;
}

static size_t strlen_line(struct libscols_line *ln)
{
	size_t i, sz = 0;

	assert(ln);

	for (i = 0; i < ln->ncells; i++) {
		struct libscols_cell *ce = scols_line_get_cell(ln, i);
		const char *data = ce ? scols_cell_get_data(ce) : NULL;

		sz += data ? strlen(data) : 0;
	}

	return sz;
}

static void cleanup_printing(struct libscols_table *tb, struct libscols_buffer *buf)
{
	if (!tb)
		return;

	free_buffer(buf);

	if (tb->priv_symbols) {
		scols_table_set_symbols(tb, NULL);
		tb->priv_symbols = 0;
	}
}

static int initialize_printing(struct libscols_table *tb, struct libscols_buffer **buf)
{
	size_t bufsz, extra_bufsz = 0;
	struct libscols_line *ln;
	struct libscols_iter itr;
	int rc;

	DBG(TAB, ul_debugobj(tb, "initialize printing"));
	*buf = NULL;

	if (!tb->symbols) {
		rc = scols_table_set_default_symbols(tb);
		if (rc)
			goto err;
		tb->priv_symbols = 1;
	} else
		tb->priv_symbols = 0;

	if (tb->format == SCOLS_FMT_HUMAN)
		tb->is_term = tb->termforce == SCOLS_TERMFORCE_NEVER  ? 0 :
			      tb->termforce == SCOLS_TERMFORCE_ALWAYS ? 1 :
			      isatty(STDOUT_FILENO);

	if (tb->is_term) {
		size_t width = (size_t) scols_table_get_termwidth(tb);

		if (tb->termreduce > 0 && tb->termreduce < width) {
			width -= tb->termreduce;
			scols_table_set_termwidth(tb, width);
		}
		bufsz = width;
	} else
		bufsz = BUFSIZ;

	if (!tb->is_term || tb->format != SCOLS_FMT_HUMAN || scols_table_is_tree(tb))
		tb->header_repeat = 0;

	/*
	 * Estimate extra space necessary for tree, JSON or another output
	 * decoration.
	 */
	if (scols_table_is_tree(tb))
		extra_bufsz += tb->nlines * strlen(vertical_symbol(tb));

	switch (tb->format) {
	case SCOLS_FMT_RAW:
		extra_bufsz += tb->ncols;			/* separator between columns */
		break;
	case SCOLS_FMT_JSON:
		if (tb->format == SCOLS_FMT_JSON)
			extra_bufsz += tb->nlines * 3;		/* indention */
		/* fallthrough */
	case SCOLS_FMT_EXPORT:
	{
		struct libscols_column *cl;

		scols_reset_iter(&itr, SCOLS_ITER_FORWARD);

		while (scols_table_next_column(tb, &itr, &cl) == 0) {
			if (scols_column_is_hidden(cl))
				continue;
			extra_bufsz += strlen(scols_cell_get_data(&cl->header));	/* data */
			extra_bufsz += 2;						/* separators */
		}
		break;
	}
	case SCOLS_FMT_HUMAN:
		break;
	}

	/*
	 * Enlarge buffer if necessary, the buffer should be large enough to
	 * store line data and tree ascii art (or another decoration).
	 */
	scols_reset_iter(&itr, SCOLS_ITER_FORWARD);
	while (scols_table_next_line(tb, &itr, &ln) == 0) {
		size_t sz;

		sz = strlen_line(ln) + extra_bufsz;
		if (sz > bufsz)
			bufsz = sz;
	}

	*buf = new_buffer(bufsz + 1);	/* data + space for \0 */
	if (!*buf) {
		rc = -ENOMEM;
		goto err;
	}

	if (tb->format == SCOLS_FMT_HUMAN) {
		rc = __scols_calculate(tb, *buf);
		if (rc != 0)
			goto err;
	}

	return 0;
err:
	cleanup_printing(tb, *buf);
	return rc;
}

/**
 * scola_table_print_range:
 * @tb: table
 * @start: first printed line or NULL to print from the begin of the table
 * @end: last printed line or NULL to print all from start.
 *
 * If the start is the first line in the table than prints table header too.
 * The header is printed only once. This does not work for trees.
 *
 * Returns: 0, a negative value in case of an error.
 */
int scols_table_print_range(	struct libscols_table *tb,
				struct libscols_line *start,
				struct libscols_line *end)
{
	struct libscols_buffer *buf = NULL;
	struct libscols_iter itr;
	int rc;

	if (scols_table_is_tree(tb))
		return -EINVAL;

	DBG(TAB, ul_debugobj(tb, "printing range from API"));

	rc = initialize_printing(tb, &buf);
	if (rc)
		return rc;

	if (start) {
		itr.direction = SCOLS_ITER_FORWARD;
		itr.head = &tb->tb_lines;
		itr.p = &start->ln_lines;
	} else
		scols_reset_iter(&itr, SCOLS_ITER_FORWARD);

	if (!start || itr.p == tb->tb_lines.next) {
		rc = print_header(tb, buf);
		if (rc)
			goto done;
	}

	rc = print_range(tb, buf, &itr, end);
done:
	cleanup_printing(tb, buf);
	return rc;
}

/**
 * scols_table_print_range_to_string:
 * @tb: table
 * @start: first printed line or NULL to print from the beginning of the table
 * @end: last printed line or NULL to print all from start.
 * @data: pointer to the beginning of a memory area to print to
 *
 * The same as scols_table_print_range(), but prints to @data instead of
 * stream.
 *
 * Returns: 0, a negative value in case of an error.
 */
#ifdef HAVE_OPEN_MEMSTREAM
int scols_table_print_range_to_string(	struct libscols_table *tb,
					struct libscols_line *start,
					struct libscols_line *end,
					char **data)
{
	FILE *stream, *old_stream;
	size_t sz;
	int rc;

	if (!tb)
		return -EINVAL;

	DBG(TAB, ul_debugobj(tb, "printing range to string"));

	/* create a stream for output */
	stream = open_memstream(data, &sz);
	if (!stream)
		return -ENOMEM;

	old_stream = scols_table_get_stream(tb);
	scols_table_set_stream(tb, stream);
	rc = scols_table_print_range(tb, start, end);
	fclose(stream);
	scols_table_set_stream(tb, old_stream);

	return rc;
}
#else
int scols_table_print_range_to_string(
			struct libscols_table *tb __attribute__((__unused__)),
			struct libscols_line *start __attribute__((__unused__)),
			struct libscols_line *end __attribute__((__unused__)),
			char **data __attribute__((__unused__)))
{
	return -ENOSYS;
}
#endif

static int __scols_print_table(struct libscols_table *tb, int *is_empty)
{
	int rc = 0;
	struct libscols_buffer *buf = NULL;

	if (!tb)
		return -EINVAL;

	DBG(TAB, ul_debugobj(tb, "printing"));
	if (is_empty)
		*is_empty = 0;

	if (list_empty(&tb->tb_columns)) {
		DBG(TAB, ul_debugobj(tb, "error -- no columns"));
		return -EINVAL;
	}
	if (list_empty(&tb->tb_lines)) {
		DBG(TAB, ul_debugobj(tb, "ignore -- no lines"));
		if (is_empty)
			*is_empty = 1;
		return 0;
	}

	tb->header_printed = 0;
	rc = initialize_printing(tb, &buf);
	if (rc)
		return rc;

	fput_table_open(tb);

	if (tb->format == SCOLS_FMT_HUMAN)
		print_title(tb);

	rc = print_header(tb, buf);
	if (rc)
		goto done;

	if (scols_table_is_tree(tb))
		rc = print_tree(tb, buf);
	else
		rc = print_table(tb, buf);

	fput_table_close(tb);
done:
	cleanup_printing(tb, buf);
	return rc;
}

/**
 * scols_print_table:
 * @tb: table
 *
 * Prints the table to the output stream and terminate by \n.
 *
 * Returns: 0, a negative value in case of an error.
 */
int scols_print_table(struct libscols_table *tb)
{
	int empty = 0;
	int rc = __scols_print_table(tb, &empty);

	if (rc == 0 && !empty)
		fputc('\n', tb->out);
	return rc;
}

/**
 * scols_print_table_to_string:
 * @tb: table
 * @data: pointer to the beginning of a memory area to print to
 *
 * Prints the table to @data.
 *
 * Returns: 0, a negative value in case of an error.
 */
#ifdef HAVE_OPEN_MEMSTREAM
int scols_print_table_to_string(struct libscols_table *tb, char **data)
{
	FILE *stream, *old_stream;
	size_t sz;
	int rc;

	if (!tb)
		return -EINVAL;

	DBG(TAB, ul_debugobj(tb, "printing to string"));

	/* create a stream for output */
	stream = open_memstream(data, &sz);
	if (!stream)
		return -ENOMEM;

	old_stream = scols_table_get_stream(tb);
	scols_table_set_stream(tb, stream);
	rc = __scols_print_table(tb, NULL);
	fclose(stream);
	scols_table_set_stream(tb, old_stream);

	return rc;
}
#else
int scols_print_table_to_string(
		struct libscols_table *tb __attribute__((__unused__)),
		char **data  __attribute__((__unused__)))
{
	return -ENOSYS;
}
#endif

