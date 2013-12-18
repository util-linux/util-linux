
#include "fdiskP.h"

/**
 * fdisk_new_table:
 *
 * The table is a container for struct fdisk_partition entries. The container
 * does not have any real connection with label (partition table) and with
 * real on-disk data.
 *
 * Returns: newly allocated table struct.
 */
struct fdisk_table *fdisk_new_table(void)
{
	struct fdisk_table *tb = NULL;

	tb = calloc(1, sizeof(*tb));
	if (!tb)
		return NULL;

	DBG(TAB, dbgprint("alloc"));
	tb->refcount = 1;
	INIT_LIST_HEAD(&tb->parts);
	return tb;
}

/**
 * fdisk_reset_table:
 * @tb: tab pointer
 *
 * Removes all entries (filesystems) from the table. The filesystems with zero
 * reference count will be deallocated.
 *
 * Returns: 0 on success or negative number in case of error.
 */
int fdisk_reset_table(struct fdisk_table *tb)
{
	if (!tb)
		return -EINVAL;

	DBG(TAB, dbgprint("reset"));

	while (!list_empty(&tb->parts)) {
		struct fdisk_partition *pa = list_entry(tb->parts.next,
				                  struct fdisk_partition, parts);
		fdisk_table_remove_partition(tb, pa);
	}

	return 0;
}

/**
 * fdisk_ref_table:
 * @tb: table pointer
 *
 * Incremparts reference counter.
 */
void fdisk_ref_table(struct fdisk_table *tb)
{
	if (tb)
		tb->refcount++;
}

/**
 * fdisk_unref_table:
 * @tb: table pointer
 *
 * De-incremparts reference counter, on zero the @tb is automatically
 * deallocated by fdisk_free_table().
 */
void fdisk_unref_table(struct fdisk_table *tb)
{
	if (!tb)
		return;

	tb->refcount--;
	if (tb->refcount <= 0) {
		fdisk_reset_table(tb);

		DBG(TAB, dbgprint("free"));
		free(tb);
	}
}

/**
 * fdisk_table_is_empty:
 * @tb: pointer to tab
 *
 * Returns: 1 if the table is without filesystems, or 0.
 */
int fdisk_table_is_empty(struct fdisk_table *tb)
{
	assert(tb);
	return tb == NULL || list_empty(&tb->parts) ? 1 : 0;
}


/**
 * fdisk_table_next_partition:
 * @tb: tab pointer
 * @itr: iterator
 * @pa: returns the next tab entry
 *
 * Returns: 0 on success, negative number in case of error or 1 at the end of list.
 *
 * Example:
 * <informalexample>
 *   <programlisting>
 *	while(fdisk_table_next_partition(tb, itr, &pa) == 0) {
 *		...
 *	}
 *   </programlisting>
 * </informalexample>
 */
int fdisk_table_next_partition(
			struct fdisk_table *tb,
			struct fdisk_iter *itr,
			struct fdisk_partition **pa)
{
	int rc = 1;

	assert(tb);
	assert(itr);
	assert(pa);

	if (!tb || !itr || !pa)
		return -EINVAL;
	*pa = NULL;

	if (!itr->head)
		FDISK_ITER_INIT(itr, &tb->parts);
	if (itr->p != itr->head) {
		FDISK_ITER_ITERATE(itr, *pa, struct fdisk_partition, parts);
		rc = 0;
	}

	return rc;
}

/**
 * fdisk_table_add_partition
 * @tb: tab pointer
 * @pa: new entry
 *
 * Adds a new entry to table and increment @pa reference counter. Don't forget to
 * use fdisk_unref_pa() after fdisk_table_add_partition() if you want to keep
 * the @pa referenced by the table only.
 *
 * Returns: 0 on success or negative number in case of error.
 */
int fdisk_table_add_partition(struct fdisk_table *tb, struct fdisk_partition *pa)
{
	assert(tb);
	assert(pa);

	if (!tb || !pa)
		return -EINVAL;

	fdisk_ref_partition(pa);
	list_add_tail(&pa->parts, &tb->parts);

	DBG(TAB, dbgprint("add entry %p [start=%ju, size=%ju, freespace=%s]",
				pa, pa->start, pa->size,
				pa->freespace ? "yes" : "no"));
	return 0;
}

/**
 * fdisk_table_remove_partition
 * @tb: tab pointer
 * @pa: new entry
 *
 * Removes the @pa from the table and de-increment reference counter of the @pa. The
 * partition with zero reference counter will be deallocated. Don't forget to use
 * fdisk_ref_partition() before call fdisk_table_remove_partition() if you want
 * to use @pa later.
 *
 * Returns: 0 on success or negative number in case of error.
 */
int fdisk_table_remove_partition(struct fdisk_table *tb, struct fdisk_partition *pa)
{
	assert(tb);
	assert(pa);

	if (!tb || !pa)
		return -EINVAL;

	DBG(TAB, dbgprint("remove entry %p", pa));
	list_del(&pa->parts);
	INIT_LIST_HEAD(&pa->parts);

	fdisk_unref_partition(pa);
	return 0;
}

static int fdisk_table_add_freespace(
			struct fdisk_table *tb,
			uint64_t start,
			uint64_t end)
{
	struct fdisk_partition *pa = fdisk_new_partition();
	int rc;

	if (!pa)
		return -ENOMEM;

	assert(tb);

	pa->freespace = 1;

	pa->start = start;
	pa->end = end;
	pa->size = pa->end - pa->start + 1ULL;

	rc = fdisk_table_add_partition(tb, pa);
	fdisk_unref_partition(pa);
	return rc;
}
/**
 * fdisk_get_table
 * @cxt: fdisk context
 * @tb: returns table (allocate a new if not allocate yet)
 *
 * Returns 0 on success, otherwise, a corresponding error.
 */
int fdisk_get_table(struct fdisk_context *cxt, struct fdisk_table **tb)
{
	struct fdisk_partition *pa = NULL;
	size_t i;
	uint64_t last, grain;

	if (!cxt || !cxt->label || !tb)
		return -EINVAL;
	if (!cxt->label->op->get_part)
		return -ENOSYS;

	DBG(LABEL, dbgprint("get table [freespace=%s]",
		fdisk_context_display_freespace(cxt) ? "yes" : "no"));

	if (!*tb) {
		*tb = fdisk_new_table();
		if (!*tb)
			return -ENOMEM;
	}

	last = cxt->first_lba;
	grain = cxt->grain / cxt->sector_size;

	for (i = 0; i < cxt->label->nparts_max; i++) {
		if (fdisk_get_partition(cxt, i, &pa))
			continue;
		if (!fdisk_partition_is_used(pa))
			continue;

		/* add free-space (before partition) to the list */
		if (fdisk_context_display_freespace(cxt) &&
		    last + grain < pa->start) {
			fdisk_table_add_freespace(*tb,
				last + (last > cxt->first_lba ? 1 : 0),
				pa->start - 1);
		}
		last = pa->end;
		fdisk_table_add_partition(*tb, pa);
		fdisk_unref_partition(pa);
		pa = NULL;
	}

	/* add free-space (behind last partition) to the list */
	if (fdisk_context_display_freespace(cxt) &&
	    last + grain < cxt->total_sectors - 1) {
		fdisk_table_add_freespace(*tb,
			last + (last > cxt->first_lba ? 1 : 0),
			cxt->total_sectors - 1);
	}

	return 0;
}

/**
 * fdisk_table_to_string
 * @tb: table
 * @cxt: fdisk context
 * @cols: array with wanted FDISK_COL_* columns
 * @ncols: number of items in the cols array
 * @data: returns table as a newlly allocated string
 *
 * If no @cols are specified then the default is printed (see
 * fdisk_get_columns() for the default columns).

 * Returns 0 on success, otherwise, a corresponding error.
 */
int fdisk_table_to_string(struct fdisk_table *tb,
			  struct fdisk_context *cxt,
			  int *cols,
			  size_t ncols,
			  char **data)
{
	int *org_cols = cols, rc = 0;
	struct tt *tt = NULL;
	const struct fdisk_column *col;
	struct fdisk_partition *pa = NULL;
	struct fdisk_iter itr;
	size_t j;

	if (!cxt || !tb || !data)
		return -EINVAL;

	DBG(TAB, dbgprint("generate string"));

	if (!cols || !ncols) {
		rc = fdisk_get_columns(cxt, 0, &cols, &ncols);
		if (rc)
			return rc;
	}

	tt = tt_new_table(TT_FL_FREEDATA);
	if (!tt) {
		rc = -ENOMEM;
		goto done;
	}

	/* define columns */
	for (j = 0; j < ncols; j++) {
		col = fdisk_label_get_column(cxt->label, cols[j]);
		if (!col)
			continue;
		tt_define_column(tt,
			col->id == FDISK_COL_SECTORS &&
			fdisk_context_use_cylinders(cxt) ?
				_("Cylinders") : col->name,
			col->width, col->tt_flags);
	}

	fdisk_reset_iter(&itr, FDISK_ITER_FORWARD);

	/* convert partition to string and add to tt */
	while (fdisk_table_next_partition(tb, &itr, &pa) == 0) {
		struct tt_line *ln = tt_add_line(tt, NULL);
		if (!ln) {
			rc = -ENOMEM;
			goto done;
		}

		DBG(TAB, dbgprint("  string from part #%zu", pa->partno + 1));

		/* set data for the columns */
		for (j = 0; j < ncols; j++) {
			const struct fdisk_column *col;
			char *cdata = NULL;
			int id;

			col = fdisk_label_get_column(cxt->label, cols[j]);
			if (!col)
				continue;
			id = (col->id == FDISK_COL_SECTORS &&
			      fdisk_context_use_cylinders(cxt)) ?
					FDISK_COL_CYLINDERS :
					col->id;

			if (fdisk_partition_to_string(pa, cxt, id, &cdata))
				continue;
			tt_line_set_data(ln, j, cdata);
		}
	}

	rc = 0;
	*data = NULL;
	if (!tt_is_empty(tt))
		rc = tt_print_table_to_string(tt, data);
	else
		DBG(TAB, dbgprint("tt empty"));
done:
	if (org_cols != cols)
		free(cols);
	tt_free_table(tt);
	return rc;
}
