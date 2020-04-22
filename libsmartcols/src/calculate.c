#include "smartcolsP.h"
#include "mbsalign.h"

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

static int count_cell_width(struct libscols_table *tb,
		struct libscols_line *ln,
		struct libscols_column *cl,
		struct libscols_buffer *buf)
{
	size_t len;
	char *data;
	int rc;

	rc = __cell_to_buffer(tb, ln, cl, buf);
	if (rc)
		return rc;

	data = buffer_get_data(buf);
	if (!data)
		len = 0;
	else if (scols_column_is_customwrap(cl))
		len = cl->wrap_chunksize(cl, data, cl->wrapfunc_data);
	else if (scols_table_is_noencoding(tb))
		len = mbs_width(data);
	else
		len = mbs_safe_width(data);

	if (len == (size_t) -1)		/* ignore broken multibyte strings */
		len = 0;
	cl->width_max = max(len, cl->width_max);

	if (cl->is_extreme && cl->width_avg && len > cl->width_avg * 2)
		return 0;

	if (scols_column_is_noextremes(cl)) {
		cl->extreme_sum += len;
		cl->extreme_count++;
	}
	cl->width = max(len, cl->width);
	if (scols_column_is_tree(cl)) {
		size_t treewidth = buffer_get_safe_art_size(buf);
		cl->width_treeart = max(cl->width_treeart, treewidth);
	}
	return 0;
}


static int walk_count_cell_width(struct libscols_table *tb,
		struct libscols_line *ln,
		struct libscols_column *cl,
		void *data)
{
	return count_cell_width(tb, ln, cl, (struct libscols_buffer *) data);
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
	int rc = 0, no_header = 0;

	assert(tb);
	assert(cl);

	cl->width = 0;
	if (!cl->width_min) {
		const char *data;

		if (cl->width_hint < 1 && scols_table_is_maxout(tb) && tb->is_term) {
			cl->width_min = (size_t) (cl->width_hint * tb->termwidth);
			if (cl->width_min && !is_last_column(cl))
				cl->width_min--;
		}

		data = scols_cell_get_data(&cl->header);
		if (data) {
			size_t len = scols_table_is_noencoding(tb) ?
					mbs_width(data) : mbs_safe_width(data);
			cl->width_min = max(cl->width_min, len);
		} else
			no_header = 1;

		if (!cl->width_min)
			cl->width_min = 1;
	}

	if (scols_table_is_tree(tb)) {
		/* Count width for tree */
		rc = scols_walk_tree(tb, cl, walk_count_cell_width, (void *) buf);
		if (rc)
			goto done;
	} else {
		/* Count width for list */
		struct libscols_iter itr;
		struct libscols_line *ln;

		scols_reset_iter(&itr, SCOLS_ITER_FORWARD);
		while (scols_table_next_line(tb, &itr, &ln) == 0) {
			rc = count_cell_width(tb, ln, cl, buf);
			if (rc)
				goto done;
		}
	}

	if (scols_column_is_tree(cl) && has_groups(tb)) {
		/* We don't fill buffer with groups tree ascii art during width
		 * calculation. The print function only enlarge grpset[] and we
		 * calculate final width from grpset_size.
		 */
		size_t gprwidth = tb->grpset_size + 1;
		cl->width_treeart += gprwidth;
		cl->width_max += gprwidth;
		cl->width += gprwidth;
		if (cl->extreme_count)
			cl->extreme_sum += gprwidth;
	}

	if (cl->extreme_count && cl->width_avg == 0) {
		cl->width_avg = cl->extreme_sum / cl->extreme_count;
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
int __scols_calculate(struct libscols_table *tb, struct libscols_buffer *buf)
{
	struct libscols_column *cl;
	struct libscols_iter itr;
	size_t width = 0, width_min = 0;	/* output width */
	int stage, rc = 0;
	int extremes = 0, group_ncolumns = 0;
	size_t colsepsz;


	DBG(TAB, ul_debugobj(tb, "-----calculate-(termwidth=%zu)-----", tb->termwidth));
	tb->is_dummy_print = 1;

	colsepsz = scols_table_is_noencoding(tb) ?
			mbs_width(colsep(tb)) :
			mbs_safe_width(colsep(tb));

	if (has_groups(tb))
		group_ncolumns = 1;

	/* set basic columns width
	 */
	scols_reset_iter(&itr, SCOLS_ITER_FORWARD);
	while (scols_table_next_column(tb, &itr, &cl) == 0) {
		int is_last;

		if (scols_column_is_hidden(cl))
			continue;

		/* we print groups chart only for the for the first tree column */
		if (scols_column_is_tree(cl) && group_ncolumns == 1) {
			cl->is_groups = 1;
			group_ncolumns++;
		}

		rc = count_column_width(tb, cl, buf);
		if (rc)
			goto done;

		is_last = is_last_column(cl);

		width += cl->width + (is_last ? 0 : colsepsz);		/* separator for non-last column */
		width_min += cl->width_min + (is_last ? 0 : colsepsz);
		if (cl->is_extreme)
			extremes++;
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
			if (cl->width == 0)
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
	tb->is_dummy_print = 0;
	DBG(TAB, ul_debugobj(tb, "-----final width: %zu (rc=%d)-----", width, rc));
	ON_DBG(TAB, dbg_columns(tb));

	return rc;
}
