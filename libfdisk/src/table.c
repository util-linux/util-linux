
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

	DBG(TAB, dbgprint("add entry %p", pa));
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
