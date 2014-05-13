
#include <libsmartcols.h>
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

	DBG(TAB, ul_debugobj(tb, "alloc"));
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

	DBG(TAB, ul_debugobj(tb, "reset"));

	while (!list_empty(&tb->parts)) {
		struct fdisk_partition *pa = list_entry(tb->parts.next,
				                  struct fdisk_partition, parts);
		fdisk_table_remove_partition(tb, pa);
	}

	tb->nents = 0;
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

		DBG(TAB, ul_debugobj(tb, "free"));
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
 * fdisk_table_get_nents:
 * @tb: pointer to tab
 *
 * Returns: number of entries in table.
 */
int fdisk_table_get_nents(struct fdisk_table *tb)
{
	return tb ? tb->nents : 0;
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

struct fdisk_partition *fdisk_table_get_partition(
			struct fdisk_table *tb,
			size_t n)
{
	struct fdisk_partition *pa = NULL;
	struct fdisk_iter itr;

	if (!tb)
		return NULL;

	fdisk_reset_iter(&itr, FDISK_ITER_FORWARD);

	while (fdisk_table_next_partition(tb, &itr, &pa) == 0) {
		if (n == 0)
			return pa;
		n--;
	}

	return NULL;
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
	tb->nents++;

	DBG(TAB, ul_debugobj(tb, "add entry %p [start=%ju, end=%ju, size=%ju, %s %s %s]",
			pa, pa->start, pa->end, pa->size,
			fdisk_partition_is_freespace(pa) ? "freespace" : "",
			fdisk_partition_is_nested(pa)    ? "nested"    : "",
			fdisk_partition_is_container(pa) ? "container" : "primary"));
	return 0;
}

/* inserts @pa after @poz */
static int table_insert_partition(
			struct fdisk_table *tb,
			struct fdisk_partition *poz,
			struct fdisk_partition *pa)
{
	assert(tb);
	assert(pa);

	fdisk_ref_partition(pa);
	if (poz)
		list_add(&pa->parts, &poz->parts);
	else
		list_add(&pa->parts, &tb->parts);
	tb->nents++;

	DBG(TAB, ul_debugobj(tb, "insert entry %p pre=%p [start=%ju, end=%ju, size=%ju, %s %s %s]",
			pa, poz ? poz : NULL, pa->start, pa->end, pa->size,
			fdisk_partition_is_freespace(pa) ? "freespace" : "",
			fdisk_partition_is_nested(pa)    ? "nested"    : "",
			fdisk_partition_is_container(pa) ? "container" : "primary"));
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

	DBG(TAB, ul_debugobj(tb, "remove entry %p", pa));
	list_del(&pa->parts);
	INIT_LIST_HEAD(&pa->parts);

	fdisk_unref_partition(pa);
	tb->nents--;

	return 0;
}

/**
 * fdisk_get_partitions
 * @cxt: fdisk context
 * @tb: returns table
 *
 * This function adds partitions from disklabel to @table, it allocates a new
 * table if if @table points to NULL.
 *
 * Returns 0 on success, otherwise, a corresponding error.
 */
int fdisk_get_partitions(struct fdisk_context *cxt, struct fdisk_table **tb)
{
	size_t i;

	if (!cxt || !cxt->label || !tb)
		return -EINVAL;
	if (!cxt->label->op->get_part)
		return -ENOSYS;

	DBG(CXT, ul_debugobj(cxt, "get table"));

	if (!*tb && !(*tb = fdisk_new_table()))
		return -ENOMEM;

	for (i = 0; i < cxt->label->nparts_max; i++) {
		struct fdisk_partition *pa = NULL;

		if (fdisk_get_partition(cxt, i, &pa) != 0)
			continue;
		if (fdisk_partition_is_used(pa))
			fdisk_table_add_partition(*tb, pa);
		fdisk_unref_partition(pa);
	}

	return 0;
}

int fdisk_dump_table(struct fdisk_table *tb, FILE *f)
{
	struct fdisk_partition *pa;
	struct fdisk_iter itr;
	int i = 0;

	assert(tb);
	assert(f);

	fdisk_reset_iter(&itr, FDISK_ITER_FORWARD);

	fprintf(f, "--table--%p\n", tb);
	while (fdisk_table_next_partition(tb, &itr, &pa) == 0) {
		fprintf(f, "%d: ", i++);
		fdisk_dump_partition(pa, f);
	}
	fputs("-----\n", f);
	return 0;
}


typedef	int (*fdisk_partcmp_t)(struct fdisk_partition *, struct fdisk_partition *);

static int cmp_parts_wrapper(struct list_head *a, struct list_head *b, void *data)
{
	struct fdisk_partition *pa = list_entry(a, struct fdisk_partition, parts),
			       *pb = list_entry(b, struct fdisk_partition, parts);

	fdisk_partcmp_t cmp = (fdisk_partcmp_t) data;

	return cmp(pa, pb);
}

int fdisk_table_sort_partitions(struct fdisk_table *tb,
			int (*cmp)(struct fdisk_partition *,
				   struct fdisk_partition *))
{
	if (!tb)
		return -EINVAL;

	list_sort(&tb->parts, cmp_parts_wrapper, (void *) cmp);
	return 0;
}

/* allocates a new freespace description */
static int new_freespace(struct fdisk_context *cxt,
			 uint64_t start,
			 uint64_t end,
			 struct fdisk_partition *parent,
			 struct fdisk_partition **pa)
{
	assert(cxt);
	assert(pa);

	*pa = NULL;

	if (start == end)
		return 0;
	*pa = fdisk_new_partition();
	if (!*pa)
		return -ENOMEM;

	(*pa)->freespace = 1;
	(*pa)->start = fdisk_align_lba_in_range(cxt, start, start, end);
	(*pa)->end = end;
	(*pa)->size = (*pa)->end - (*pa)->start + 1ULL;

	if (parent)
		(*pa)->parent_partno = parent->partno;
	return 0;
}

/* add freespace description to the right place within @tb */
static int table_add_freespace(
			struct fdisk_context *cxt,
			struct fdisk_table *tb,
			uint64_t start,
			uint64_t end,
			struct fdisk_partition *parent)
{
	struct fdisk_partition *pa, *x, *real_parent = NULL, *best = NULL;
	struct fdisk_iter itr;
	int rc = 0;

	assert(tb);

	rc = new_freespace(cxt, start, end, parent, &pa);
	if (rc)
		return -ENOMEM;
	if (!pa)
		return 0;
	fdisk_reset_iter(&itr, FDISK_ITER_FORWARD);
	if (parent) {
		while (fdisk_table_next_partition(tb, &itr, &x) == 0) {
			if (x->partno == parent->partno) {
				real_parent = x;
				break;
			}
		}
		if (!real_parent) {
			DBG(TAB, ul_debugobj(tb, "not found freespace parent (partno=%zu)",
					parent->partno));
			fdisk_reset_iter(&itr, FDISK_ITER_FORWARD);
		}
	}

	while (fdisk_table_next_partition(tb, &itr, &x) == 0) {
		if (x->end < pa->start && (!best || best->end < x->end))
			best = x;
	}

	if (!best && real_parent)
		best = real_parent;
	rc = table_insert_partition(tb, best, pa);

	fdisk_unref_partition(pa);
	return rc;
}

/* analyze @cont(ainer) in @parts and add all detected freespace into @tb, note
 * that @parts has to be sorted by partition starts */
static int check_container_freespace(struct fdisk_context *cxt,
				     struct fdisk_table *parts,
				     struct fdisk_table *tb,
				     struct fdisk_partition *cont)
{
	struct fdisk_iter itr;
	struct fdisk_partition *pa;
	uint64_t x, last, grain;
	uint64_t lastplusoff;
	int rc = 0;

	assert(cxt);
	assert(parts);
	assert(tb);
	assert(cont);

	last = fdisk_partition_get_start(cont);
	grain = cxt->grain > cxt->sector_size ?	cxt->grain / cxt->sector_size : 1;
	fdisk_reset_iter(&itr, FDISK_ITER_FORWARD);

	while (fdisk_table_next_partition(parts, &itr, &pa) == 0) {
		if (!pa->used || !fdisk_partition_is_nested(pa))
			continue;

		lastplusoff = last + cxt->first_lba;
		if (pa->start > lastplusoff && pa->start - lastplusoff > grain)
			rc = table_add_freespace(cxt, tb, lastplusoff, pa->start, cont);
		if (rc)
			return rc;
		last = pa->end;
	}

	/* free-space remaining in extended partition */
	x = fdisk_partition_get_start(cont) + fdisk_partition_get_size(cont) - 1;
	lastplusoff = last + cxt->first_lba;
	if (lastplusoff < x && x - lastplusoff > grain)
		rc = table_add_freespace(cxt, tb, lastplusoff, x, cont);
	return rc;
}


/**
 * fdisk_get_freespaces
 * @cxt: fdisk context
 * @tb: returns table
 *
 * This function adds freespace (described by fdisk_partition) to @table, it
 * allocates a new table if the @table points to NULL.
 *
 * Note that free space smaller than grain (see fdisk_topology_get_grain()) is
 * ignored.

 * Returns 0 on success, otherwise, a corresponding error.
 */
int fdisk_get_freespaces(struct fdisk_context *cxt, struct fdisk_table **tb)
{
	int rc = 0;
	uint64_t last, grain;
	struct fdisk_table *parts = NULL;
	struct fdisk_partition *pa;
	struct fdisk_iter itr;

	DBG(CXT, ul_debugobj(cxt, "get freespace"));

	if (!cxt || !cxt->label || !tb)
		return -EINVAL;
	if (!*tb && !(*tb = fdisk_new_table()))
		return -ENOMEM;

	rc = fdisk_get_partitions(cxt, &parts);
	if (rc)
		goto done;
	fdisk_table_sort_partitions(parts, fdisk_partition_cmp_start);
	fdisk_reset_iter(&itr, FDISK_ITER_FORWARD);
	last = cxt->first_lba;
	grain = cxt->grain > cxt->sector_size ?	cxt->grain / cxt->sector_size : 1;

	/* analyze gaps between partitions */
	while (rc == 0 && fdisk_table_next_partition(parts, &itr, &pa) == 0) {
		if (!pa->used || pa->wholedisk || fdisk_partition_is_nested(pa))
			continue;
		DBG(CXT, ul_debugobj(cxt, "freespace analyze: partno=%zu, start=%ju, end=%ju",
					pa->partno, pa->start, pa->end));
		if (last + grain <= pa->start) {
			rc = table_add_freespace(cxt, *tb,
				last + (last > cxt->first_lba ? 1 : 0),
				pa->start - 1, NULL);
		}
		/* add gaps between logical partitions */
		if (fdisk_partition_is_container(pa))
			rc = check_container_freespace(cxt, parts, *tb, pa);
		last = pa->end;
	}

	/* add free-space behind last partition to the end of the table (so
	 * don't use table_add_freespace()) */
	if (rc == 0 && last + grain < cxt->total_sectors - 1) {
		rc = new_freespace(cxt,
			last + (last > cxt->first_lba ? 1 : 0),
			cxt->last_lba, NULL, &pa);
		if (pa) {
			fdisk_table_add_partition(*tb, pa);
			fdisk_unref_partition(pa);
		}
	}

	DBG(LABEL, fdisk_dump_table(*tb, stderr));
done:
	fdisk_unref_table(parts);
	return rc;
}

/**
 * fdisk_table_wrong_order:
 * @tb: table
 *
 * Returns: 1 of the table is not in disk order
 */
int fdisk_table_wrong_order(struct fdisk_table *tb)
{
	struct fdisk_partition *pa;
	struct fdisk_iter itr;
	sector_t last = 0;

	DBG(TAB, ul_debugobj(tb, "wrong older check"));

	fdisk_reset_iter(&itr, FDISK_ITER_FORWARD);
	while (tb && fdisk_table_next_partition(tb, &itr, &pa) == 0) {
		if (pa->start < last)
			return 1;
		last = pa->start;
	}
	return 0;
}

/**
 * fdisk_table_to_string
 * @tb: table
 * @cxt: fdisk context
 * @cols: array with wanted FDISK_COL_* columns
 * @ncols: number of items in the cols array
 * @data: returns table as a newlly allocated string or NULL for empty PT
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
	struct libscols_table *table = NULL;
	const struct fdisk_column *col;
	struct fdisk_partition *pa = NULL;
	struct fdisk_iter itr;
	size_t j;

	if (!cxt || !tb || !data)
		return -EINVAL;

	DBG(TAB, ul_debugobj(tb, "generate string"));
	*data = NULL;

	if (!fdisk_table_get_nents(tb))
		return 0;

	if (!cols || !ncols) {
		rc = fdisk_get_columns(cxt, 0, &cols, &ncols);
		if (rc)
			return rc;
	}

	table = scols_new_table();
	if (!table) {
		rc = -ENOMEM;
		goto done;
	}

	/* define columns */
	for (j = 0; j < ncols; j++) {
		col = fdisk_label_get_column(cxt->label, cols[j]);
		if (col)
			if (!scols_table_new_column(table, col->name, col->width, col->scols_flags))
				goto done;
	}

	fdisk_reset_iter(&itr, FDISK_ITER_FORWARD);

	/* convert partition to string and add to table */
	while (fdisk_table_next_partition(tb, &itr, &pa) == 0) {
		struct libscols_line *ln = scols_table_new_line(table, NULL);
		if (!ln) {
			rc = -ENOMEM;
			goto done;
		}

		DBG(TAB, ul_debugobj(tb, "  string from part #%zu [%p]",
					pa->partno + 1, pa));

		/* set data for the columns */
		for (j = 0; j < ncols; j++) {
			char *cdata = NULL;

			col = fdisk_label_get_column(cxt->label, cols[j]);
			if (!col)
				continue;
			if (fdisk_partition_to_string(pa, cxt, col->id, &cdata))
				continue;
			scols_line_refer_data(ln, j, cdata);
		}
	}

	rc = 0;
	if (!scols_table_is_empty(table))
		rc = scols_print_table_to_string(table, data);
	else
		DBG(TAB, ul_debugobj(tb, "table empty"));
done:
	if (org_cols != cols)
		free(cols);
	scols_unref_table(table);
	return rc;
}
