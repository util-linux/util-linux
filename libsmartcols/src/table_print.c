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

#define colsep(tb)	((tb)->colsep ? (tb)->colsep : " ")
#define linesep(tb)	((tb)->linesep ? (tb)->linesep : "\n")

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


/* This is private struct to work with output data */
struct libscols_buffer {
	char	*begin;		/* begin of the buffer */
	char	*cur;		/* current end of  the buffer */
	char	*encdata;	/* encoded buffer mbs_safe_encode() */

	size_t	bufsz;		/* size of the buffer */
	size_t	art_idx;	/* begin of the tree ascii art or zero */
};

static struct libscols_buffer *new_buffer(size_t sz)
{
	struct libscols_buffer *buf = malloc(sz + sizeof(struct libscols_buffer));

	if (!buf)
		return NULL;

	buf->cur = buf->begin = ((char *) buf) + sizeof(struct libscols_buffer);
	buf->encdata = NULL;
	buf->bufsz = sz;

	DBG(BUFF, ul_debugobj(buf, "alloc (size=%zu)", sz));
	return buf;
}

static void free_buffer(struct libscols_buffer *buf)
{
	if (!buf)
		return;
	DBG(BUFF, ul_debugobj(buf, "dealloc"));
	free(buf->encdata);
	free(buf);
}

static int buffer_reset_data(struct libscols_buffer *buf)
{
	if (!buf)
		return -EINVAL;

	/*DBG(BUFF, ul_debugobj(buf, "reset data"));*/
	buf->begin[0] = '\0';
	buf->cur = buf->begin;
	buf->art_idx = 0;
	return 0;
}

static int buffer_append_data(struct libscols_buffer *buf, const char *str)
{
	size_t maxsz, sz;

	if (!buf)
		return -EINVAL;
	if (!str || !*str)
		return 0;

	sz = strlen(str);
	maxsz = buf->bufsz - (buf->cur - buf->begin);

	if (maxsz <= sz)
		return -EINVAL;
	memcpy(buf->cur, str, sz + 1);
	buf->cur += sz;
	return 0;
}

static int buffer_set_data(struct libscols_buffer *buf, const char *str)
{
	int rc = buffer_reset_data(buf);
	return rc ? rc : buffer_append_data(buf, str);
}

/* save the current buffer position to art_idx */
static void buffer_set_art_index(struct libscols_buffer *buf)
{
	if (buf) {
		buf->art_idx = buf->cur - buf->begin;
		/*DBG(BUFF, ul_debugobj(buf, "art index: %zu", buf->art_idx));*/
	}
}

static char *buffer_get_data(struct libscols_buffer *buf)
{
	return buf ? buf->begin : NULL;
}

/* encode data by mbs_safe_encode() to avoid control and non-printable chars */
static char *buffer_get_safe_data(struct libscols_table *tb,
				  struct libscols_buffer *buf,
				  size_t *cells,
				  const char *safechars)
{
	char *data = buffer_get_data(buf);
	char *res = NULL;

	if (!data)
		goto nothing;

	if (!buf->encdata) {
		buf->encdata = malloc(mbs_safe_encode_size(buf->bufsz) + 1);
		if (!buf->encdata)
			goto nothing;
	}

	if (tb->no_encode) {
		*cells = mbs_safe_width(data);
		strcpy(buf->encdata, data);
		res = buf->encdata;
	} else {
		res = mbs_safe_encode_to_buffer(data, cells, buf->encdata, safechars);
	}

	if (!res || !*cells || *cells == (size_t) -1)
		goto nothing;
	return res;
nothing:
	*cells = 0;
	return NULL;
}

/* returns size in bytes of the ascii art (according to art_idx) in safe encoding */
static size_t buffer_get_safe_art_size(struct libscols_buffer *buf)
{
	char *data = buffer_get_data(buf);
	size_t bytes = 0;

	if (!data || !buf->art_idx)
		return 0;

	mbs_safe_nwidth(data, buf->art_idx, &bytes);
	return bytes;
}

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

	if (list_entry_is_last(&ln->ln_children, &ln->parent->ln_branch))
		art = "  ";
	else
		art = vertical_symbol(tb);

	return buffer_append_data(buf, art);
}

static int is_last_column(struct libscols_column *cl)
{
	int rc = list_entry_is_last(&cl->cl_columns, &cl->table->tb_columns);
	struct libscols_column *next;

	if (rc)
		return 1;

	next = list_entry(cl->cl_columns.next, struct libscols_column, cl_columns);
	if (next && scols_column_is_hidden(next) && is_last_column(next))
		return 1;
	return 0;
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
		print_newline_padding(tb, cl, ln, buf->bufsz);	/* next column starts on next line */
	else
		fputs(colsep(tb), tb->out);		/* columns separator */

	return 0;
}

static int cell_to_buffer(struct libscols_table *tb,
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

		if (!rc && list_entry_is_last(&ln->ln_children, &ln->parent->ln_branch))
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
		rc = cell_to_buffer(tb, ln, cl, buf);
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
				print_empty_cell(tb, cl, ln, buf->bufsz);
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

static void dbg_column(struct libscols_table *tb, struct libscols_column *cl)
{
	if (scols_column_is_hidden(cl)) {
		DBG(COL, ul_debugobj(cl, "%s (hidden) ignored", cl->header.data));
		return;
	}

	DBG(COL, ul_debugobj(cl, "%15s seq=%zu, width=%zd, "
				 "hint=%d, avg=%zu, max=%zu, min=%zu, "
				 "extreme=%s %s",

		cl->header.data, cl->seqnum, cl->width,
		cl->width_hint > 1 ? (int) cl->width_hint :
				     (int) (cl->width_hint * tb->termwidth),
		cl->width_avg,
		cl->width_max,
		cl->width_min,
		cl->is_extreme ? "yes" : "not",
		cl->flags & SCOLS_FL_TRUNC ? "trunc" : ""));
}

static void dbg_columns(struct libscols_table *tb)
{
	struct libscols_iter itr;
	struct libscols_column *cl;

	scols_reset_iter(&itr, SCOLS_ITER_FORWARD);
	while (scols_table_next_column(tb, &itr, &cl) == 0)
		dbg_column(tb, cl);
}


/*
 * This function counts column width.
 *
 * For the SCOLS_FL_NOEXTREMES columns it is possible to call this function
 * two times. The first pass counts the width and average width. If the column
 * contains fields that are too large (a width greater than 2 * average) then
 * the column is marked as "extreme". In the second pass all extreme fields
 * are ignored and the column width is counted from non-extreme fields only.
 */
static int count_column_width(struct libscols_table *tb,
			      struct libscols_column *cl,
			      struct libscols_buffer *buf)
{
	struct libscols_line *ln;
	struct libscols_iter itr;
	int extreme_count = 0, rc = 0, no_header = 0;
	size_t extreme_sum = 0;

	assert(tb);
	assert(cl);

	cl->width = 0;
	if (!cl->width_min) {
		if (cl->width_hint < 1 && scols_table_is_maxout(tb) && tb->is_term) {
			cl->width_min = (size_t) (cl->width_hint * tb->termwidth);
			if (cl->width_min && !is_last_column(cl))
				cl->width_min--;
		}
		if (scols_cell_get_data(&cl->header)) {
			size_t len = mbs_safe_width(scols_cell_get_data(&cl->header));
			cl->width_min = max(cl->width_min, len);
		} else
			no_header = 1;

		if (!cl->width_min)
			cl->width_min = 1;
	}

	scols_reset_iter(&itr, SCOLS_ITER_FORWARD);
	while (scols_table_next_line(tb, &itr, &ln) == 0) {
		size_t len;
		char *data;

		rc = cell_to_buffer(tb, ln, cl, buf);
		if (rc)
			goto done;

		data = buffer_get_data(buf);

		if (!data)
			len = 0;
		else if (scols_column_is_customwrap(cl))
			len = cl->wrap_chunksize(cl, data, cl->wrapfunc_data);
		else
			len = mbs_safe_width(data);

		if (len == (size_t) -1)		/* ignore broken multibyte strings */
			len = 0;
		cl->width_max = max(len, cl->width_max);

		if (cl->is_extreme && cl->width_avg && len > cl->width_avg * 2)
			continue;
		else if (scols_column_is_noextremes(cl)) {
			extreme_sum += len;
			extreme_count++;
		}
		cl->width = max(len, cl->width);
		if (scols_column_is_tree(cl)) {
			size_t treewidth = buffer_get_safe_art_size(buf);
			cl->width_treeart = max(cl->width_treeart, treewidth);
		}
	}

	if (extreme_count && cl->width_avg == 0) {
		cl->width_avg = extreme_sum / extreme_count;
		if (cl->width_avg && cl->width_max > cl->width_avg * 2)
			cl->is_extreme = 1;
	}

	/* enlarge to minimal width */
	if (cl->width < cl->width_min && !scols_column_is_strict_width(cl))
		cl->width = cl->width_min;

	/* use absolute size for large columns */
	else if (cl->width_hint >= 1 && cl->width < (size_t) cl->width_hint
		 && cl->width_min < (size_t) cl->width_hint)

		cl->width = (size_t) cl->width_hint;


	/* Column without header and data, set minimal size to zero (default is 1) */
	if (cl->width_max == 0 && no_header && cl->width_min == 1 && cl->width <= 1)
		cl->width = cl->width_min = 0;

done:
	ON_DBG(COL, dbg_column(tb, cl));
	return rc;
}

/*
 * This is core of the scols_* voodoo...
 */
static int recount_widths(struct libscols_table *tb, struct libscols_buffer *buf)
{
	struct libscols_column *cl;
	struct libscols_iter itr;
	size_t width = 0, width_min = 0;	/* output width */
	int stage, rc = 0;
	int extremes = 0;
	size_t colsepsz;


	DBG(TAB, ul_debugobj(tb, "recounting widths (termwidth=%zu)", tb->termwidth));

	colsepsz = mbs_safe_width(colsep(tb));

	/* set basic columns width
	 */
	scols_reset_iter(&itr, SCOLS_ITER_FORWARD);
	while (scols_table_next_column(tb, &itr, &cl) == 0) {
		int is_last;

		if (scols_column_is_hidden(cl))
			continue;
		rc = count_column_width(tb, cl, buf);
		if (rc)
			goto done;

		is_last = is_last_column(cl);

		width += cl->width + (is_last ? 0 : colsepsz);		/* separator for non-last column */
		width_min += cl->width_min + (is_last ? 0 : colsepsz);
		extremes += cl->is_extreme;
	}

	if (!tb->is_term) {
		DBG(TAB, ul_debugobj(tb, " non-terminal output"));
		goto done;
	}

	/* be paranoid */
	if (width_min > tb->termwidth && scols_table_is_maxout(tb)) {
		DBG(TAB, ul_debugobj(tb, " min width larger than terminal! [width=%zu, term=%zu]", width_min, tb->termwidth));

		scols_reset_iter(&itr, SCOLS_ITER_FORWARD);
		while (width_min > tb->termwidth
		       && scols_table_next_column(tb, &itr, &cl) == 0) {
			if (scols_column_is_hidden(cl))
				continue;
			width_min--;
			cl->width_min--;
		}
		DBG(TAB, ul_debugobj(tb, " min width reduced to %zu", width_min));
	}

	/* reduce columns with extreme fields */
	if (width > tb->termwidth && extremes) {
		DBG(TAB, ul_debugobj(tb, " reduce width (extreme columns)"));

		scols_reset_iter(&itr, SCOLS_ITER_FORWARD);
		while (scols_table_next_column(tb, &itr, &cl) == 0) {
			size_t org_width;

			if (!cl->is_extreme || scols_column_is_hidden(cl))
				continue;

			org_width = cl->width;
			rc = count_column_width(tb, cl, buf);
			if (rc)
				goto done;

			if (org_width > cl->width)
				width -= org_width - cl->width;
			else
				extremes--;	/* hmm... nothing reduced */
		}
	}

	if (width < tb->termwidth) {
		if (extremes) {
			DBG(TAB, ul_debugobj(tb, " enlarge width (extreme columns)"));

			/* enlarge the first extreme column */
			scols_reset_iter(&itr, SCOLS_ITER_FORWARD);
			while (scols_table_next_column(tb, &itr, &cl) == 0) {
				size_t add;

				if (!cl->is_extreme || scols_column_is_hidden(cl))
					continue;

				/* this column is too large, ignore?
				if (cl->width_max - cl->width >
						(tb->termwidth - width))
					continue;
				*/

				add = tb->termwidth - width;
				if (add && cl->width + add > cl->width_max)
					add = cl->width_max - cl->width;

				cl->width += add;
				width += add;

				if (width == tb->termwidth)
					break;
			}
		}

		if (width < tb->termwidth && scols_table_is_maxout(tb)) {
			DBG(TAB, ul_debugobj(tb, " enlarge width (max-out)"));

			/* try enlarging all columns */
			while (width < tb->termwidth) {
				scols_reset_iter(&itr, SCOLS_ITER_FORWARD);
				while (scols_table_next_column(tb, &itr, &cl) == 0) {
					if (scols_column_is_hidden(cl))
						continue;
					cl->width++;
					width++;
					if (width == tb->termwidth)
						break;
				}
			}
		} else if (width < tb->termwidth) {
			/* enlarge the last column */
			struct libscols_column *col = list_entry(
				tb->tb_columns.prev, struct libscols_column, cl_columns);

			DBG(TAB, ul_debugobj(tb, " enlarge width (last column)"));

			if (!scols_column_is_right(col) && tb->termwidth - width > 0) {
				col->width += tb->termwidth - width;
				width = tb->termwidth;
			}
		}
	}

	/* bad, we have to reduce output width, this is done in three stages:
	 *
	 * 1) trunc relative with trunc flag if the column width is greater than
	 *    expected column width (it means "width_hint * terminal_width").
	 *
	 * 2) trunc all with trunc flag
	 *
	 * 3) trunc relative without trunc flag
	 *
	 * Note that SCOLS_FL_WRAP (if no custom wrap function is specified) is
	 * interpreted as SCOLS_FL_TRUNC.
	 */
	for (stage = 1; width > tb->termwidth && stage <= 3; ) {
		size_t org_width = width;

		DBG(TAB, ul_debugobj(tb, " reduce width - #%d stage (current=%zu, wanted=%zu)",
				stage, width, tb->termwidth));

		scols_reset_iter(&itr, SCOLS_ITER_FORWARD);
		while (scols_table_next_column(tb, &itr, &cl) == 0) {

			int trunc_flag = 0;

			DBG(TAB, ul_debugobj(cl, "   checking %s (width=%zu, treeart=%zu)",
						cl->header.data, cl->width, cl->width_treeart));
			if (scols_column_is_hidden(cl))
				continue;
			if (width <= tb->termwidth)
				break;

			/* never truncate if already minimal width */
			if (cl->width == cl->width_min)
				continue;

			/* never truncate the tree */
			if (scols_column_is_tree(cl) && width <= cl->width_treeart)
				continue;

			/* nothing to truncate */
			if (cl->width == 0 || width == 0)
				continue;

			trunc_flag = scols_column_is_trunc(cl)
				    || (scols_column_is_wrap(cl) && !scols_column_is_customwrap(cl));

			switch (stage) {
			/* #1 stage - trunc relative with TRUNC flag */
			case 1:
				if (!trunc_flag)		/* ignore: missing flag */
					break;
				if (cl->width_hint <= 0 || cl->width_hint >= 1)	/* ignore: no relative */
					break;
				if (cl->width < (size_t) (cl->width_hint * tb->termwidth)) /* ignore: smaller than expected width */
					break;

				DBG(TAB, ul_debugobj(tb, "     reducing (relative with flag)"));
				cl->width--;
				width--;
				break;

			/* #2 stage - trunc all with TRUNC flag */
			case 2:
				if (!trunc_flag)		/* ignore: missing flag */
					break;

				DBG(TAB, ul_debugobj(tb, "     reducing (all with flag)"));
				cl->width--;
				width--;
				break;

			/* #3 stage - trunc relative without flag */
			case 3:
				if (cl->width_hint <= 0 || cl->width_hint >= 1)	/* ignore: no relative */
					break;

				DBG(TAB, ul_debugobj(tb, "     reducing (relative without flag)"));
				cl->width--;
				width--;
				break;
			}

			/* hide zero width columns */
			if (cl->width == 0)
				cl->flags |= SCOLS_FL_HIDDEN;
		}

		/* the current stage is without effect, go to the next */
		if (org_width == width)
			stage++;
	}

	/* ignore last column(s) or force last column to be truncated if
	 * nowrap mode enabled */
	if (tb->no_wrap && width > tb->termwidth) {
		scols_reset_iter(&itr, SCOLS_ITER_BACKWARD);
		while (scols_table_next_column(tb, &itr, &cl) == 0) {

			if (scols_column_is_hidden(cl))
				continue;
			if (width <= tb->termwidth)
				break;
			if (width - cl->width < tb->termwidth) {
				size_t r =  width - tb->termwidth;

				cl->flags |= SCOLS_FL_TRUNC;
				cl->width -= r;
				width -= r;
			} else {
				cl->flags |= SCOLS_FL_HIDDEN;
				width -= cl->width + colsepsz;
			}
		}
	}
done:
	DBG(TAB, ul_debugobj(tb, " final width: %zu (rc=%d)", width, rc));
	ON_DBG(TAB, dbg_columns(tb));

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
		rc = recount_widths(tb, *buf);
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

