/*
 * Copyright (C) 2008-2010 Karel Zak <kzak@redhat.com>
 *
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 */

/**
 * SECTION: table
 * @title: Table of filesystems
 * @short_description: container for entries from fstab, mtab or mountinfo
 *
 * Note that mnt_table_find_* functions are mount(8) compatible. These functions
 * try to find an entry in more iterations, where the first attempt is always
 * based on comparison with unmodified (non-canonicalized or un-evaluated)
 * paths or tags. For example a fstab with two entries:
 * <informalexample>
 *   <programlisting>
 *	LABEL=foo	/foo	auto   rw
 *	/dev/foo	/foo	auto   rw
 *  </programlisting>
 * </informalexample>
 *
 * where both lines are used for the *same* device, then
 * <informalexample>
 *  <programlisting>
 *	mnt_table_find_source(tb, "/dev/foo", &fs);
 *  </programlisting>
 * </informalexample>
 * will returns the second line, and
 * <informalexample>
 *  <programlisting>
 *	mnt_table_find_source(tb, "LABEL=foo", &fs);
 *  </programlisting>
 * </informalexample>
 * will returns the first entry, and
 * <informalexample>
 *  <programlisting>
 *	mnt_table_find_source(tb, "UUID=anyuuid", &fs);
 *  </programlisting>
 * </informalexample>
 * will return the first entry (if UUID matches with the device).
 */
#include <blkid.h>

#include "mountP.h"
#include "strutils.h"
#include "loopdev.h"

/**
 * mnt_new_table:
 *
 * The tab is a container for struct libmnt_fs entries that usually represents a fstab,
 * mtab or mountinfo file from your system.
 *
 * See also mnt_table_parse_file().
 *
 * Returns: newly allocated tab struct.
 */
struct libmnt_table *mnt_new_table(void)
{
	struct libmnt_table *tb = NULL;

	tb = calloc(1, sizeof(*tb));
	if (!tb)
		return NULL;

	DBG(TAB, mnt_debug_h(tb, "alloc"));
	tb->refcount = 1;
	INIT_LIST_HEAD(&tb->ents);
	return tb;
}

/**
 * mnt_reset_table:
 * @tb: tab pointer
 *
 * Removes all entries (filesystems) from the table. The filesystems with zero
 * reference count will be deallocated.
 *
 * Returns: 0 on success or negative number in case of error.
 */
int mnt_reset_table(struct libmnt_table *tb)
{
	if (!tb)
		return -EINVAL;

	DBG(TAB, mnt_debug_h(tb, "reset"));

	while (!list_empty(&tb->ents)) {
		struct libmnt_fs *fs = list_entry(tb->ents.next,
				                  struct libmnt_fs, ents);
		mnt_table_remove_fs(tb, fs);
	}

	tb->nents = 0;
	return 0;
}

/**
 * mnt_ref_table:
 * @tb: table pointer
 *
 * Increments reference counter.
 */
void mnt_ref_table(struct libmnt_table *tb)
{
	if (tb) {
		tb->refcount++;
		/*DBG(FS, mnt_debug_h(tb, "ref=%d", tb->refcount));*/
	}
}

/**
 * mnt_unref_table:
 * @tb: table pointer
 *
 * De-increments reference counter, on zero the @tb is automatically
 * deallocated by mnt_free_table().
 */
void mnt_unref_table(struct libmnt_table *tb)
{
	if (tb) {
		tb->refcount--;
		/*DBG(FS, mnt_debug_h(tb, "unref=%d", tb->refcount));*/
		if (tb->refcount <= 0)
			mnt_free_table(tb);
	}
}


/**
 * mnt_free_table:
 * @tb: tab pointer
 *
 * Deallocates the table. This function does not care about reference count. Don't
 * use this function directly -- it's better to use use mnt_unref_table().
 *
 * The table entries (filesystems) are unrefrenced by mnt_reset_table() and
 * cache by mnt_unref_cache().
 */
void mnt_free_table(struct libmnt_table *tb)
{
	if (!tb)
		return;

	mnt_reset_table(tb);

	WARN_REFCOUNT(TAB, tb, tb->refcount);
	DBG(TAB, mnt_debug_h(tb, "free"));

	mnt_unref_cache(tb->cache);
	free(tb->comm_intro);
	free(tb->comm_tail);
	free(tb);
}

/**
 * mnt_table_get_nents:
 * @tb: pointer to tab
 *
 * Returns: number of entries in table.
 */
int mnt_table_get_nents(struct libmnt_table *tb)
{
	return tb ? tb->nents : 0;
}

/**
 * mnt_table_is_empty:
 * @tb: pointer to tab
 *
 * Returns: 1 if the table is without filesystems, or 0.
 */
int mnt_table_is_empty(struct libmnt_table *tb)
{
	assert(tb);
	return tb == NULL || list_empty(&tb->ents) ? 1 : 0;
}

/**
 * mnt_table_set_userdata:
 * @tb: pointer to tab
 * @data: pointer to user data
 *
 * Sets pointer to the private user data.
 *
 * Returns: 0 on success or negative number in case of error.
 */
int mnt_table_set_userdata(struct libmnt_table *tb, void *data)
{
	assert(tb);
	if (!tb)
		return -EINVAL;

	tb->userdata = data;
	return 0;
}

/**
 * mnt_table_get_userdata:
 * @tb: pointer to tab
 *
 * Returns: pointer to user's data.
 */
void *mnt_table_get_userdata(struct libmnt_table *tb)
{
	assert(tb);
	return tb ? tb->userdata : NULL;
}

/**
 * mnt_table_enable_comments:
 * @tb: pointer to tab
 * @enable: TRUE or FALSE
 *
 * Enables parsing of comments.
 *
 * The initial (intro) file comment is accessible by
 * mnt_table_get_intro_comment(). The intro and the comment of the first fstab
 * entry has to be separated by blank line.  The filesystem comments are
 * accessible by mnt_fs_get_comment(). The trailing fstab comment is accessible
 * by mnt_table_get_trailing_comment().
 *
 * <informalexample>
 *  <programlisting>
 *	#
 *	# Intro comment
 *	#
 *
 *	# this comments belongs to the first fs
 *	LABEL=foo /mnt/foo auto defaults 1 2
 *	# this comments belongs to the second fs
 *	LABEL=bar /mnt/bar auto defaults 1 2 
 *	# tailing comment
 *  </programlisting>
 * </informalexample>
 */
void mnt_table_enable_comments(struct libmnt_table *tb, int enable)
{
	assert(tb);
	if (tb)
		tb->comms = enable;
}

/**
 * mnt_table_with_comments:
 * @tb: pointer to table
 *
 * Returns: 1 if comments parsing is enabled, or 0.
 */
int mnt_table_with_comments(struct libmnt_table *tb)
{
	assert(tb);
	return tb ? tb->comms : 0;
}

/**
 * mnt_table_get_intro_comment:
 * @tb: pointer to tab
 *
 * Returns: initial comment in tb
 */
const char *mnt_table_get_intro_comment(struct libmnt_table *tb)
{
	assert(tb);
	return tb ? tb->comm_intro : NULL;
}

/**
 * mnt_table_set_into_comment:
 * @tb: pointer to tab
 * @comm: comment or NULL
 *
 * Sets the initial comment in tb.
 *
 * Returns: 0 on success or negative number in case of error.
 */
int mnt_table_set_intro_comment(struct libmnt_table *tb, const char *comm)
{
	char *p = NULL;

	assert(tb);
	if (!tb)
		return -EINVAL;
	if (comm) {
		p = strdup(comm);
		if (!p)
			return -ENOMEM;
	}
	free(tb->comm_intro);
	tb->comm_intro = p;
	return 0;
}

/**
 * mnt_table_append_into_comment:
 * @tb: pointer to tab
 * @comm: comment of NULL
 *
 * Appends the initial comment in tb.
 *
 * Returns: 0 on success or negative number in case of error.
 */
int mnt_table_append_intro_comment(struct libmnt_table *tb, const char *comm)
{
	assert(tb);
	if (!tb)
		return -EINVAL;
	return append_string(&tb->comm_intro, comm);
}

/**
 * mnt_table_get_trailing_comment:
 * @tb: pointer to tab
 *
 * Returns: table trailing comment
 */
const char *mnt_table_get_trailing_comment(struct libmnt_table *tb)
{
	assert(tb);
	return tb ? tb->comm_tail : NULL;
}

/**
 * mnt_table_set_trailing_comment
 * @tb: pointer to tab
 * @comm: comment string
 *
 * Sets the trailing comment in table.
 *
 * Returns: 0 on success or negative number in case of error.
 */
int mnt_table_set_trailing_comment(struct libmnt_table *tb, const char *comm)
{
	char *p = NULL;

	assert(tb);
	if (!tb)
		return -EINVAL;
	if (comm) {
		p = strdup(comm);
		if (!p)
			return -ENOMEM;
	}
	free(tb->comm_tail);
	tb->comm_tail = p;
	return 0;
}

/**
 * mnt_table_append_trailing_comment:
 * @tb: pointer to tab
 * @comm: comment of NULL
 *
 * Appends to the trailing table comment.
 *
 * Returns: 0 on success or negative number in case of error.
 */
int mnt_table_append_trailing_comment(struct libmnt_table *tb, const char *comm)
{
	assert(tb);
	if (!tb)
		return -EINVAL;
	return append_string(&tb->comm_tail, comm);
}

/**
 * mnt_table_set_cache:
 * @tb: pointer to tab
 * @mpc: pointer to struct libmnt_cache instance
 *
 * Sets up a cache for canonicalized paths and evaluated tags (LABEL/UUID). The
 * cache is recommended for mnt_table_find_*() functions.
 *
 * The cache could be shared between more tabs. Be careful when you share the
 * same cache between more threads -- currently the cache does not provide any
 * locking method.
 *
 * This function increments cache refrence counter. It's recomented to use
 * mnt_unref_cache() after mnt_table_set_cache() if you want to keep the cache
 * referenced by @tb only.
 *
 * See also mnt_new_cache().
 *
 * Returns: 0 on success or negative number in case of error.
 */
int mnt_table_set_cache(struct libmnt_table *tb, struct libmnt_cache *mpc)
{
	assert(tb);
	if (!tb)
		return -EINVAL;

	mnt_ref_cache(mpc);			/* new */
	mnt_unref_cache(tb->cache);		/* old */
	tb->cache = mpc;
	return 0;
}

/**
 * mnt_table_get_cache:
 * @tb: pointer to tab
 *
 * Returns: pointer to struct libmnt_cache instance or NULL.
 */
struct libmnt_cache *mnt_table_get_cache(struct libmnt_table *tb)
{
	assert(tb);
	return tb ? tb->cache : NULL;
}

/**
 * mnt_table_add_fs:
 * @tb: tab pointer
 * @fs: new entry
 *
 * Adds a new entry to tab and increment @fs reference counter. Don't forget to
 * use mnt_unref_fs() after mnt_table_add_fs() you want to keep the @fs
 * referenced by the table only.
 *
 * Returns: 0 on success or negative number in case of error.
 */
int mnt_table_add_fs(struct libmnt_table *tb, struct libmnt_fs *fs)
{
	assert(tb);
	assert(fs);

	if (!tb || !fs)
		return -EINVAL;

	mnt_ref_fs(fs);
	list_add_tail(&fs->ents, &tb->ents);
	tb->nents++;

	DBG(TAB, mnt_debug_h(tb, "add entry: %s %s",
			mnt_fs_get_source(fs), mnt_fs_get_target(fs)));
	return 0;
}

/**
 * mnt_table_remove_fs:
 * @tb: tab pointer
 * @fs: new entry
 *
 * Removes the @fs from the table and de-increment reference counter of the @fs. The
 * filesystem with zero reference counter will be deallocated. Don't forget to use
 * mnt_ref_fs() before call mnt_table_remove_fs() if you want to use @fs later.
 *
 * Returns: 0 on success or negative number in case of error.
 */
int mnt_table_remove_fs(struct libmnt_table *tb, struct libmnt_fs *fs)
{
	assert(tb);
	assert(fs);

	if (!tb || !fs)
		return -EINVAL;

	list_del(&fs->ents);
	INIT_LIST_HEAD(&fs->ents);	/* otherwise FS still points to the list */

	mnt_unref_fs(fs);
	tb->nents--;
	return 0;
}

/**
 * mnt_table_get_root_fs:
 * @tb: mountinfo file (/proc/self/mountinfo)
 * @root: returns pointer to the root filesystem (/)
 *
 * The function uses the parent ID from the mountinfo file to determine the root filesystem
 * (the filesystem with the smallest ID). The function is designed mostly for
 * applications where it is necessary to sort mountpoints by IDs to get the tree
 * of the mountpoints (e.g. findmnt default output).
 *
 * If you're not sure, then use
 *
 *	mnt_table_find_target(tb, "/", MNT_ITER_BACKWARD);
 *
 * this is more robust and usable for arbitrary tab files (including fstab).
 *
 * Returns: 0 on success or negative number in case of error.
 */
int mnt_table_get_root_fs(struct libmnt_table *tb, struct libmnt_fs **root)
{
	struct libmnt_iter itr;
	struct libmnt_fs *fs;
	int root_id = 0;

	assert(tb);
	assert(root);

	if (!tb || !root)
		return -EINVAL;

	DBG(TAB, mnt_debug_h(tb, "lookup root fs"));

	mnt_reset_iter(&itr, MNT_ITER_FORWARD);
	while(mnt_table_next_fs(tb, &itr, &fs) == 0) {
		int id = mnt_fs_get_parent_id(fs);
		if (!id)
			break;		/* @tab is not a mountinfo file? */

		if (!*root || id < root_id) {
			*root = fs;
			root_id = id;
		}
	}

	return root_id ? 0 : -EINVAL;
}

/**
 * mnt_table_next_child_fs:
 * @tb: mountinfo file (/proc/self/mountinfo)
 * @itr: iterator
 * @parent: parental FS
 * @chld: returns the next child filesystem
 *
 * Note that filesystems are returned in the order of mounting (according to
 * IDs in /proc/self/mountinfo).
 *
 * Returns: 0 on success, negative number in case of error or 1 at the end of list.
 */
int mnt_table_next_child_fs(struct libmnt_table *tb, struct libmnt_iter *itr,
			struct libmnt_fs *parent, struct libmnt_fs **chld)
{
	struct libmnt_fs *fs;
	int parent_id, lastchld_id = 0, chld_id = 0;

	if (!tb || !itr || !parent)
		return -EINVAL;

	DBG(TAB, mnt_debug_h(tb, "lookup next child of '%s'",
				mnt_fs_get_target(parent)));

	parent_id = mnt_fs_get_id(parent);
	if (!parent_id)
		return -EINVAL;

	/* get ID of the previously returned child */
	if (itr->head && itr->p != itr->head) {
		MNT_ITER_ITERATE(itr, fs, struct libmnt_fs, ents);
		lastchld_id = mnt_fs_get_id(fs);
	}

	*chld = NULL;

	mnt_reset_iter(itr, MNT_ITER_FORWARD);
	while(mnt_table_next_fs(tb, itr, &fs) == 0) {
		int id;

		if (mnt_fs_get_parent_id(fs) != parent_id)
			continue;

		id = mnt_fs_get_id(fs);

		/* avoid an infinite loop. This only happens in rare cases
		 * such as in early userspace when the rootfs is its own parent */
		if (id == parent_id)
			continue;

		if ((!lastchld_id || id > lastchld_id) &&
		    (!*chld || id < chld_id)) {
			*chld = fs;
			chld_id = id;
		}
	}

	if (!chld_id)
		return 1;	/* end of iterator */

	/* set the iterator to the @chld for the next call */
	mnt_table_set_iter(tb, itr, *chld);

	return 0;
}

/**
 * mnt_table_next_fs:
 * @tb: tab pointer
 * @itr: iterator
 * @fs: returns the next tab entry
 *
 * Returns: 0 on success, negative number in case of error or 1 at the end of list.
 *
 * Example:
 * <informalexample>
 *   <programlisting>
 *	while(mnt_table_next_fs(tb, itr, &fs) == 0) {
 *		const char *dir = mnt_fs_get_target(fs);
 *		printf("mount point: %s\n", dir);
 *	}
 *   </programlisting>
 * </informalexample>
 *
 * lists all mountpoints from fstab in reverse order.
 */
int mnt_table_next_fs(struct libmnt_table *tb, struct libmnt_iter *itr, struct libmnt_fs **fs)
{
	int rc = 1;

	assert(tb);
	assert(itr);
	assert(fs);

	if (!tb || !itr || !fs)
		return -EINVAL;
	*fs = NULL;

	if (!itr->head)
		MNT_ITER_INIT(itr, &tb->ents);
	if (itr->p != itr->head) {
		MNT_ITER_ITERATE(itr, *fs, struct libmnt_fs, ents);
		rc = 0;
	}

	return rc;
}

/**
 * mnt_table_first_fs:
 * @tb: tab pointer
 * @fs: returns the first tab entry
 *
 * Returns: 0 on success, negative number in case of error or 1 at the end of list.
 */
int mnt_table_first_fs(struct libmnt_table *tb, struct libmnt_fs **fs)
{
	assert(tb);
	assert(fs);

	if (!tb || !fs)
		return -EINVAL;
	if (list_empty(&tb->ents))
		return 1;
	*fs = list_first_entry(&tb->ents, struct libmnt_fs, ents);
	return 0;
}

/**
 * mnt_table_last_fs:
 * @tb: tab pointer
 * @fs: returns the last tab entry
 *
 * Returns: 0 on success, negative number in case of error or 1 at the end of list.
 */
int mnt_table_last_fs(struct libmnt_table *tb, struct libmnt_fs **fs)
{
	assert(tb);
	assert(fs);

	if (!tb || !fs)
		return -EINVAL;
	if (list_empty(&tb->ents))
		return 1;
	*fs = list_last_entry(&tb->ents, struct libmnt_fs, ents);
	return 0;
}

/**
 * mnt_table_find_next_fs:
 * @tb: table
 * @itr: iterator
 * @match_func: function returning 1 or 0
 * @userdata: extra data for match_func
 * @fs: returns pointer to the next matching table entry
 *
 * This function allows searching in @tb.
 *
 * Returns: negative number in case of error, 1 at end of table or 0 o success.
 */
int mnt_table_find_next_fs(struct libmnt_table *tb, struct libmnt_iter *itr,
		int (*match_func)(struct libmnt_fs *, void *), void *userdata,
		struct libmnt_fs **fs)
{
	if (!tb || !itr || !fs || !match_func)
		return -EINVAL;

	DBG(TAB, mnt_debug_h(tb, "lookup next fs"));

	if (!itr->head)
		MNT_ITER_INIT(itr, &tb->ents);

	do {
		if (itr->p != itr->head)
			MNT_ITER_ITERATE(itr, *fs, struct libmnt_fs, ents);
		else
			break;			/* end */

		if (match_func(*fs, userdata))
			return 0;
	} while(1);

	*fs = NULL;
	return 1;
}

/**
 * mnt_table_set_iter:
 * @tb: tab pointer
 * @itr: iterator
 * @fs: tab entry
 *
 * Sets @iter to the position of @fs in the file @tb.
 *
 * Returns: 0 on success, negative number in case of error.
 */
int mnt_table_set_iter(struct libmnt_table *tb, struct libmnt_iter *itr, struct libmnt_fs *fs)
{
	assert(tb);
	assert(itr);
	assert(fs);

	if (!tb || !itr || !fs)
		return -EINVAL;

	MNT_ITER_INIT(itr, &tb->ents);
	itr->p = &fs->ents;

	return 0;
}

/**
 * mnt_table_find_mountpoint:
 * @tb: tab pointer
 * @path: directory
 * @direction: MNT_ITER_{FORWARD,BACKWARD}
 *
 * Same as mnt_get_mountpoint(), except this function does not rely on
 * st_dev numbers.
 *
 * Returns: a tab entry or NULL.
 */
struct libmnt_fs *mnt_table_find_mountpoint(struct libmnt_table *tb,
					    const char *path,
					    int direction)
{
	char *mnt;

	if (!tb || !path || !*path)
		return NULL;
	if (direction != MNT_ITER_FORWARD && direction != MNT_ITER_BACKWARD)
		return NULL;

	DBG(TAB, mnt_debug_h(tb, "lookup MOUNTPOINT: '%s'", path));

	mnt = strdup(path);
	if (!mnt)
		return NULL;

	do {
		char *p;
		struct libmnt_fs *fs;

		fs = mnt_table_find_target(tb, mnt, direction);
		if (fs) {
			free(mnt);
			return fs;
		}

		p = stripoff_last_component(mnt);
		if (!p || !*p)
			break;
	} while (mnt && *(mnt + 1) != '\0');

	free(mnt);
	return mnt_table_find_target(tb, "/", direction);
}

/**
 * mnt_table_find_target:
 * @tb: tab pointer
 * @path: mountpoint directory
 * @direction: MNT_ITER_{FORWARD,BACKWARD}
 *
 * Try to lookup an entry in the given tab, three iterations are possible, the first
 * with @path, the second with realpath(@path) and the third with realpath(@path)
 * against realpath(fs->target). The 2nd and 3rd iterations are not performed
 * when the @tb cache is not set (see mnt_table_set_cache()).
 *
 * Returns: a tab entry or NULL.
 */
struct libmnt_fs *mnt_table_find_target(struct libmnt_table *tb, const char *path, int direction)
{
	struct libmnt_iter itr;
	struct libmnt_fs *fs = NULL;
	char *cn;

	assert(tb);
	assert(path);

	if (!tb || !path || !*path)
		return NULL;
	if (direction != MNT_ITER_FORWARD && direction != MNT_ITER_BACKWARD)
		return NULL;

	DBG(TAB, mnt_debug_h(tb, "lookup TARGET: '%s'", path));

	/* native @target */
	mnt_reset_iter(&itr, direction);
	while(mnt_table_next_fs(tb, &itr, &fs) == 0) {
		if (mnt_fs_streq_target(fs, path))
			return fs;
	}
	if (!tb->cache || !(cn = mnt_resolve_path(path, tb->cache)))
		return NULL;

	DBG(TAB, mnt_debug_h(tb, "lookup canonical TARGET: '%s'", cn));

	/* canonicalized paths in struct libmnt_table */
	mnt_reset_iter(&itr, direction);
	while(mnt_table_next_fs(tb, &itr, &fs) == 0) {
		if (mnt_fs_streq_target(fs, cn))
			return fs;
	}

	/* non-canonicaled path in struct libmnt_table
	 * -- note that mountpoint in /proc/self/mountinfo is already
	 *    canonicalized by the kernel
	 */
	mnt_reset_iter(&itr, direction);
	while(mnt_table_next_fs(tb, &itr, &fs) == 0) {
		char *p;

		if (!fs->target
		    || mnt_fs_is_swaparea(fs)
		    || mnt_fs_is_kernel(fs)
		    || (*fs->target == '/' && *(fs->target + 1) == '\0'))
		       continue;

		p = mnt_resolve_path(fs->target, tb->cache);
		/* both canonicalized, strcmp() is fine here */
		if (p && strcmp(cn, p) == 0)
			return fs;
	}
	return NULL;
}

/**
 * mnt_table_find_srcpath:
 * @tb: tab pointer
 * @path: source path (devname or dirname) or NULL
 * @direction: MNT_ITER_{FORWARD,BACKWARD}
 *
 * Try to lookup an entry in the given tab, four iterations are possible, the first
 * with @path, the second with realpath(@path), the third with tags (LABEL, UUID, ..)
 * from @path and the fourth with realpath(@path) against realpath(entry->srcpath).
 *
 * The 2nd, 3rd and 4th iterations are not performed when the @tb cache is not
 * set (see mnt_table_set_cache()).
 *
 * Note that NULL is a valid source path; it will be replaced with "none". The
 * "none" is used in /proc/{mounts,self/mountinfo} for pseudo filesystems.
 *
 * Returns: a tab entry or NULL.
 */
struct libmnt_fs *mnt_table_find_srcpath(struct libmnt_table *tb, const char *path, int direction)
{
	struct libmnt_iter itr;
	struct libmnt_fs *fs = NULL;
	int ntags = 0, nents;
	char *cn;
	const char *p;

	assert(tb);
	if (!tb || !path || !*path)
		return NULL;
	if (direction != MNT_ITER_FORWARD && direction != MNT_ITER_BACKWARD)
		return NULL;

	DBG(TAB, mnt_debug_h(tb, "lookup SRCPATH: '%s'", path));

	/* native paths */
	mnt_reset_iter(&itr, direction);
	while(mnt_table_next_fs(tb, &itr, &fs) == 0) {
		if (mnt_fs_streq_srcpath(fs, path))
			return fs;
		if (mnt_fs_get_tag(fs, NULL, NULL) == 0)
			ntags++;
	}

	if (!path || !tb->cache || !(cn = mnt_resolve_path(path, tb->cache)))
		return NULL;

	DBG(TAB, mnt_debug_h(tb, "lookup canonical SRCPATH: '%s'", cn));

	nents = mnt_table_get_nents(tb);

	/* canonicalized paths in struct libmnt_table */
	if (ntags < nents) {
		mnt_reset_iter(&itr, direction);
		while(mnt_table_next_fs(tb, &itr, &fs) == 0) {
			if (mnt_fs_streq_srcpath(fs, cn))
				return fs;
		}
	}

	/* evaluated tag */
	if (ntags) {
		int rc = mnt_cache_read_tags(tb->cache, cn);

		mnt_reset_iter(&itr, direction);

		if (rc == 0) {
			/* @path's TAGs are in the cache */
			while(mnt_table_next_fs(tb, &itr, &fs) == 0) {
				const char *t, *v;

				if (mnt_fs_get_tag(fs, &t, &v))
					continue;

				if (mnt_cache_device_has_tag(tb->cache, cn, t, v))
					return fs;
			}
		} else if (rc < 0 && errno == EACCES) {
			/* @path is inaccessible, try evaluating all TAGs in @tb
			 * by udev symlinks -- this could be expensive on systems
			 * with a huge fstab/mtab */
			 while(mnt_table_next_fs(tb, &itr, &fs) == 0) {
				 const char *t, *v, *x;
				 if (mnt_fs_get_tag(fs, &t, &v))
					 continue;
				 x = mnt_resolve_tag(t, v, tb->cache);

				 /* both canonicalized, strcmp() is fine here */
				 if (x && strcmp(x, cn) == 0)
					 return fs;
			 }
		}
	}

	/* non-canonicalized paths in struct libmnt_table */
	if (ntags <= nents) {
		mnt_reset_iter(&itr, direction);
		while(mnt_table_next_fs(tb, &itr, &fs) == 0) {
			if (mnt_fs_is_netfs(fs) || mnt_fs_is_pseudofs(fs))
				continue;
			p = mnt_fs_get_srcpath(fs);
			if (p)
				p = mnt_resolve_path(p, tb->cache);

			/* both canonicalized, strcmp() is fine here */
			if (p && strcmp(p, cn) == 0)
				return fs;
		}
	}

	return NULL;
}


/**
 * mnt_table_find_tag:
 * @tb: tab pointer
 * @tag: tag name (e.g "LABEL", "UUID", ...)
 * @val: tag value
 * @direction: MNT_ITER_{FORWARD,BACKWARD}
 *
 * Try to lookup an entry in the given tab, the first attempt is to lookup by @tag and
 * @val, for the second attempt the tag is evaluated (converted to the device
 * name) and mnt_table_find_srcpath() is performed. The second attempt is not
 * performed when @tb cache is not set (see mnt_table_set_cache()).

 * Returns: a tab entry or NULL.
 */
struct libmnt_fs *mnt_table_find_tag(struct libmnt_table *tb, const char *tag,
			const char *val, int direction)
{
	struct libmnt_iter itr;
	struct libmnt_fs *fs = NULL;

	assert(tb);
	assert(tag);
	assert(val);

	if (!tb || !tag || !*tag || !val)
		return NULL;
	if (direction != MNT_ITER_FORWARD && direction != MNT_ITER_BACKWARD)
		return NULL;

	DBG(TAB, mnt_debug_h(tb, "lookup by TAG: %s %s", tag, val));

	/* look up by TAG */
	mnt_reset_iter(&itr, direction);
	while(mnt_table_next_fs(tb, &itr, &fs) == 0) {
		if (fs->tagname && fs->tagval &&
		    strcmp(fs->tagname, tag) == 0 &&
		    strcmp(fs->tagval, val) == 0)
			return fs;
	}

	if (tb->cache) {
		/* look up by device name */
		char *cn = mnt_resolve_tag(tag, val, tb->cache);
		if (cn)
			return mnt_table_find_srcpath(tb, cn, direction);
	}
	return NULL;
}

/**
 * mnt_table_find_source:
 * @tb: tab pointer
 * @source: TAG or path
 * @direction: MNT_ITER_{FORWARD,BACKWARD}
 *
 * This is a high-level API for mnt_table_find_{srcpath,tag}. You needn't care
 * about the @source format (device, LABEL, UUID, ...). This function parses
 * the @source and calls mnt_table_find_tag() or mnt_table_find_srcpath().
 *
 * Returns: a tab entry or NULL.
 */
struct libmnt_fs *mnt_table_find_source(struct libmnt_table *tb,
					const char *source, int direction)
{
	struct libmnt_fs *fs;
	char *t = NULL, *v = NULL;

	assert(tb);

	if (!tb)
		return NULL;
	if (direction != MNT_ITER_FORWARD && direction != MNT_ITER_BACKWARD)
		return NULL;

	DBG(TAB, mnt_debug_h(tb, "lookup SOURCE: '%s'", source));

	if (blkid_parse_tag_string(source, &t, &v) || !mnt_valid_tagname(t))
		fs = mnt_table_find_srcpath(tb, source, direction);
	else
		fs = mnt_table_find_tag(tb, t, v, direction);

	free(t);
	free(v);

	return fs;
}

/**
 * mnt_table_find_pair
 * @tb: tab pointer
 * @source: TAG or path
 * @target: mountpoint
 * @direction: MNT_ITER_{FORWARD,BACKWARD}
 *
 * This function is implemented by mnt_fs_match_source() and
 * mnt_fs_match_target() functions. It means that this is more expensive than
 * others mnt_table_find_* function, because every @tab entry is fully evaluated.
 *
 * Returns: a tab entry or NULL.
 */
struct libmnt_fs *mnt_table_find_pair(struct libmnt_table *tb, const char *source,
				      const char *target, int direction)
{
	struct libmnt_fs *fs = NULL;
	struct libmnt_iter itr;

	assert(tb);
	assert(target);

	if (!tb || !target || !*target || !source || !*source)
		return NULL;
	if (direction != MNT_ITER_FORWARD && direction != MNT_ITER_BACKWARD)
		return NULL;

	DBG(TAB, mnt_debug_h(tb, "lookup SOURCE: %s TARGET: %s", source, target));

	mnt_reset_iter(&itr, direction);
	while(mnt_table_next_fs(tb, &itr, &fs) == 0) {

		if (mnt_fs_match_target(fs, target, tb->cache) &&
		    mnt_fs_match_source(fs, source, tb->cache))
			return fs;
	}

	return NULL;
}

/**
 * mnt_table_find_devno
 * @tb: /proc/self/mountinfo
 * @devno: device number
 * @direction: MNT_ITER_{FORWARD,BACKWARD}
 *
 * Note that zero could be a valid device number for the root pseudo filesystem (e.g.
 * tmpfs).
 *
 * Returns: a tab entry or NULL.
 */
struct libmnt_fs *mnt_table_find_devno(struct libmnt_table *tb,
				       dev_t devno, int direction)
{
	struct libmnt_fs *fs = NULL;
	struct libmnt_iter itr;

	assert(tb);

	if (!tb)
		return NULL;
	if (direction != MNT_ITER_FORWARD && direction != MNT_ITER_BACKWARD)
		return NULL;

	DBG(TAB, mnt_debug_h(tb, "lookup DEVNO: %d", (int) devno));

	mnt_reset_iter(&itr, direction);

	while(mnt_table_next_fs(tb, &itr, &fs) == 0) {
		if (mnt_fs_get_devno(fs) == devno)
			return fs;
	}

	return NULL;
}

/*
 * tb: /proc/self/mountinfo
 * fs: filesystem
 * mountflags: MS_BIND or 0
 * fsroot: fs-root that will probably be used in the mountinfo file
 *          for @fs after mount(2)
 *
 * For btrfs subvolumes this function returns NULL, but @fsroot properly set.
 *
 * Returns: entry from @tb that will be used as a source for @fs if the @fs is
 *          bindmount.
 *
 * Don't export to library API!
 */
struct libmnt_fs *mnt_table_get_fs_root(struct libmnt_table *tb,
					struct libmnt_fs *fs,
					unsigned long mountflags,
					char **fsroot)
{
	char *root = NULL, *mnt = NULL;
	const char *fstype;
	struct libmnt_fs *src_fs = NULL;

	assert(fs);
	assert(fsroot);

	DBG(TAB, mnt_debug("lookup fs-root for '%s'", mnt_fs_get_source(fs)));

	fstype = mnt_fs_get_fstype(fs);

	if (tb && (mountflags & MS_BIND)) {
		const char *src, *src_root;
		char *xsrc = NULL;

		DBG(TAB, mnt_debug("fs-root for bind"));

		src = xsrc = mnt_resolve_spec(mnt_fs_get_source(fs), tb->cache);
		if (src)
			mnt = mnt_get_mountpoint(src);
		if (mnt)
			root = mnt_get_fs_root(src, mnt);

		if (xsrc && !tb->cache) {
			free(xsrc);
			src = NULL;
		}
		if (!mnt)
			goto err;

		src_fs = mnt_table_find_target(tb, mnt, MNT_ITER_BACKWARD);
		if (!src_fs)  {
			DBG(TAB, mnt_debug("not found '%s' in mountinfo -- using default", mnt));
			goto dflt;
		}

		/* on btrfs the subvolume is used as fs-root in
		 * /proc/self/mountinfo, so we have to get the original subvolume
		 * name from src_fs and prepend the subvolume name to the
		 * fs-root path
		 */
		src_root = mnt_fs_get_root(src_fs);
		if (src_root && !startswith(root, src_root)) {
			size_t sz = strlen(root) + strlen(src_root) + 1;
			char *tmp = malloc(sz);

			if (!tmp)
				goto err;
			snprintf(tmp, sz, "%s%s", src_root, root);
			free(root);
			root = tmp;
		}
	}

	/*
	 * btrfs-subvolume mount -- get subvolume name and use it as a root-fs path
	 */
	else if (fstype && !strcmp(fstype, "btrfs")) {
		char *vol = NULL, *p;
		size_t sz, volsz = 0;

		if (mnt_fs_get_option(fs, "subvol", &vol, &volsz))
			goto dflt;

		DBG(TAB, mnt_debug("setting FS root: btrfs subvol"));

		sz = volsz;
		if (*vol != '/')
			sz++;
		root = malloc(sz + 1);
		if (!root)
			goto err;
		p = root;
		if (*vol != '/')
			*p++ = '/';
		memcpy(p, vol, volsz);
		*(root + sz) = '\0';
	}
dflt:
	if (!root) {
		root = strdup("/");
		if (!root)
			goto err;
	}
	*fsroot = root;

	DBG(TAB, mnt_debug("FS root result: %s", root));

	free(mnt);
	return src_fs;
err:
	free(root);
	free(mnt);
	return NULL;
}

static int is_mountinfo(struct libmnt_table *tb)
{
	struct libmnt_fs *fs;

	if (!tb)
		return 0;

	fs = list_first_entry(&tb->ents, struct libmnt_fs, ents);
	if (fs && mnt_fs_is_kernel(fs) && mnt_fs_get_root(fs))
		return 1;

	return 0;
}

/**
 * mnt_table_is_fs__mounted:
 * @tb: /proc/self/mountinfo file
 * @fstab_fs: /etc/fstab entry
 *
 * Checks if the @fstab_fs entry is already in the @tb table. The "swap" is
 * ignored. This function explicitly compares the source, target and root of the
 * filesystems.
 *
 * Note that source and target are canonicalized only if a cache for @tb is
 * defined (see mnt_table_set_cache()). The target canonicalization may
 * trigger automount on autofs mountpoints!
 *
 * Don't use it if you want to know if a device is mounted, just use
 * mnt_table_find_source() on the device.
 *
 * This function is designed mostly for "mount -a".
 *
 * Returns: 0 or 1
 */
int mnt_table_is_fs_mounted(struct libmnt_table *tb, struct libmnt_fs *fstab_fs)
{
	struct libmnt_iter itr;
	struct libmnt_fs *fs;

	char *root = NULL;
	const char *src = NULL, *tgt = NULL;
	char *xtgt = NULL;
	int rc = 0;
	dev_t devno = 0;

	assert(tb);
	assert(fstab_fs);

	DBG(FS, mnt_debug_h(fstab_fs, "is FS mounted? [target=%s]",
				mnt_fs_get_target(fstab_fs)));

	if (mnt_fs_is_swaparea(fstab_fs) || mnt_table_is_empty(tb)) {
		DBG(FS, mnt_debug_h(fstab_fs, "- ignore (swap or no data)"));
		return 0;
	}

	if (is_mountinfo(tb)) {
		/* @tb is mountinfo, so we can try to use fs-roots */
		struct libmnt_fs *rootfs;
		int flags = 0;

		if (mnt_fs_get_option(fstab_fs, "bind", NULL, NULL) == 0)
			flags = MS_BIND;

		rootfs = mnt_table_get_fs_root(tb, fstab_fs, flags, &root);
		if (rootfs)
			src = mnt_fs_get_srcpath(rootfs);
	}

	if (!src)
		src = mnt_fs_get_source(fstab_fs);

	if (src && tb->cache && !mnt_fs_is_pseudofs(fstab_fs))
		src = mnt_resolve_spec(src, tb->cache);

	if (src && root) {
		struct stat st;

		devno = mnt_fs_get_devno(fstab_fs);
		if (!devno && stat(src, &st) == 0 && S_ISBLK(st.st_mode))
			devno = st.st_rdev;
	}

	tgt = mnt_fs_get_target(fstab_fs);

	if (!tgt || !src) {
		DBG(FS, mnt_debug_h(fstab_fs, "- ignore (no source/target)"));
		goto done;
	}
	mnt_reset_iter(&itr, MNT_ITER_FORWARD);

	while (mnt_table_next_fs(tb, &itr, &fs) == 0) {

		int eq = mnt_fs_streq_srcpath(fs, src);

		if (!eq && devno && mnt_fs_get_devno(fs) == devno)
			eq = 1;

		if (!eq) {
			/* The source does not match. Maybe the source is a loop
			 * device backing file.
			 */
			uint64_t offset = 0;
			char *val;
			size_t len;
			int flags;

			if (!mnt_fs_is_kernel(fs) ||
			    !mnt_fs_get_srcpath(fs) ||
			    !startswith(mnt_fs_get_srcpath(fs), "/dev/loop"))
				continue;	/* does not look like loopdev */

			if (mnt_fs_get_option(fstab_fs, "offset", &val, &len) == 0 &&
			    mnt_parse_offset(val, len, &offset)) {
				DBG(FS, mnt_debug_h(fstab_fs, "failed to parse offset="));
				continue;
			} else
				flags = LOOPDEV_FL_OFFSET;

			if (loopdev_is_used(mnt_fs_get_srcpath(fs), src, offset, flags))
				break;
		}

		if (root) {
			const char *r = mnt_fs_get_root(fs);
			if (!r || strcmp(r, root) != 0)
				continue;
		}

		/*
		 * Compare target, try to minimize the number of situations when we
		 * need to canonicalize the path to avoid readlink() on
		 * mountpoints.
		 */
		if (!xtgt) {
			if (mnt_fs_streq_target(fs, tgt))
				break;
			if (tb->cache)
				xtgt = mnt_resolve_path(tgt, tb->cache);
		}
		if (xtgt && mnt_fs_streq_target(fs, xtgt))
			break;
	}

	if (fs)
		rc = 1;		/* success */
done:
	free(root);

	DBG(TAB, mnt_debug_h(tb, "mnt_table_is_fs_mounted: %s [rc=%d]", src, rc));
	return rc;
}

#ifdef TEST_PROGRAM
#include "pathnames.h"

static int parser_errcb(struct libmnt_table *tb, const char *filename, int line)
{
	fprintf(stderr, "%s:%d: parse error\n", filename, line);

	return 1;	/* all errors are recoverable -- this is the default */
}

struct libmnt_table *create_table(const char *file, int comments)
{
	struct libmnt_table *tb;

	if (!file)
		return NULL;
	tb = mnt_new_table();
	if (!tb)
		goto err;

	mnt_table_enable_comments(tb, comments);
	mnt_table_set_parser_errcb(tb, parser_errcb);

	if (mnt_table_parse_file(tb, file) != 0)
		goto err;
	return tb;
err:
	fprintf(stderr, "%s: parsing failed\n", file);
	mnt_unref_table(tb);
	return NULL;
}

int test_copy_fs(struct libmnt_test *ts, int argc, char *argv[])
{
	struct libmnt_table *tb;
	struct libmnt_fs *fs;
	int rc = -1;

	tb = create_table(argv[1], FALSE);
	if (!tb)
		return -1;

	fs = mnt_table_find_target(tb, "/", MNT_ITER_FORWARD);
	if (!fs)
		goto done;

	printf("ORIGINAL:\n");
	mnt_fs_print_debug(fs, stdout);

	fs = mnt_copy_fs(NULL, fs);
	if (!fs)
		goto done;

	printf("COPY:\n");
	mnt_fs_print_debug(fs, stdout);
	mnt_unref_fs(fs);
	rc = 0;
done:
	mnt_unref_table(tb);
	return rc;
}

int test_parse(struct libmnt_test *ts, int argc, char *argv[])
{
	struct libmnt_table *tb = NULL;
	struct libmnt_iter *itr = NULL;
	struct libmnt_fs *fs;
	int rc = -1;
	int parse_comments = FALSE;

	if (argc == 3 && !strcmp(argv[2], "--comments"))
		parse_comments = TRUE;

	tb = create_table(argv[1], parse_comments);
	if (!tb)
		return -1;

	itr = mnt_new_iter(MNT_ITER_FORWARD);
	if (!itr)
		goto done;

	if (mnt_table_get_intro_comment(tb))
		fprintf(stdout, "Initial comment:\n\"%s\"\n",
				mnt_table_get_intro_comment(tb));

	while(mnt_table_next_fs(tb, itr, &fs) == 0)
		mnt_fs_print_debug(fs, stdout);

	if (mnt_table_get_trailing_comment(tb))
		fprintf(stdout, "Trailing comment:\n\"%s\"\n",
				mnt_table_get_trailing_comment(tb));
	rc = 0;
done:
	mnt_free_iter(itr);
	mnt_unref_table(tb);
	return rc;
}

int test_find(struct libmnt_test *ts, int argc, char *argv[], int dr)
{
	struct libmnt_table *tb;
	struct libmnt_fs *fs = NULL;
	struct libmnt_cache *mpc = NULL;
	const char *file, *find, *what;
	int rc = -1;

	if (argc != 4) {
		fprintf(stderr, "try --help\n");
		return -EINVAL;
	}

	file = argv[1], find = argv[2], what = argv[3];

	tb = create_table(file, FALSE);
	if (!tb)
		goto done;

	/* create a cache for canonicalized paths */
	mpc = mnt_new_cache();
	if (!mpc)
		goto done;
	mnt_table_set_cache(tb, mpc);
	mnt_unref_cache(mpc);

	if (strcasecmp(find, "source") == 0)
		fs = mnt_table_find_source(tb, what, dr);
	else if (strcasecmp(find, "target") == 0)
		fs = mnt_table_find_target(tb, what, dr);

	if (!fs)
		fprintf(stderr, "%s: not found %s '%s'\n", file, find, what);
	else {
		mnt_fs_print_debug(fs, stdout);
		rc = 0;
	}
done:
	mnt_unref_table(tb);
	return rc;
}

int test_find_bw(struct libmnt_test *ts, int argc, char *argv[])
{
	return test_find(ts, argc, argv, MNT_ITER_BACKWARD);
}

int test_find_fw(struct libmnt_test *ts, int argc, char *argv[])
{
	return test_find(ts, argc, argv, MNT_ITER_FORWARD);
}

int test_find_pair(struct libmnt_test *ts, int argc, char *argv[])
{
	struct libmnt_table *tb;
	struct libmnt_fs *fs;
	struct libmnt_cache *mpc = NULL;
	int rc = -1;

	tb = create_table(argv[1], FALSE);
	if (!tb)
		return -1;
	mpc = mnt_new_cache();
	if (!mpc)
		goto done;
	mnt_table_set_cache(tb, mpc);
	mnt_unref_cache(mpc);

	fs = mnt_table_find_pair(tb, argv[2], argv[3], MNT_ITER_FORWARD);
	if (!fs)
		goto done;

	mnt_fs_print_debug(fs, stdout);
	rc = 0;
done:
	mnt_unref_table(tb);
	return rc;
}

int test_find_mountpoint(struct libmnt_test *ts, int argc, char *argv[])
{
	struct libmnt_table *tb;
	struct libmnt_fs *fs;
	struct libmnt_cache *mpc = NULL;
	int rc = -1;

	tb = mnt_new_table_from_file(_PATH_PROC_MOUNTINFO);
	if (!tb)
		return -1;
	mpc = mnt_new_cache();
	if (!mpc)
		goto done;
	mnt_table_set_cache(tb, mpc);
	mnt_unref_cache(mpc);

	fs = mnt_table_find_mountpoint(tb, argv[1], MNT_ITER_BACKWARD);
	if (!fs)
		goto done;

	mnt_fs_print_debug(fs, stdout);
	rc = 0;
done:
	mnt_unref_table(tb);
	return rc;
}

static int test_is_mounted(struct libmnt_test *ts, int argc, char *argv[])
{
	struct libmnt_table *tb = NULL, *fstab = NULL;
	struct libmnt_fs *fs;
	struct libmnt_iter *itr = NULL;
	struct libmnt_cache *mpc = NULL;
	int rc;

	tb = mnt_new_table_from_file("/proc/self/mountinfo");
	if (!tb) {
		fprintf(stderr, "failed to parse mountinfo\n");
		return -1;
	}

	fstab = create_table(argv[1], FALSE);
	if (!fstab)
		goto done;

	itr = mnt_new_iter(MNT_ITER_FORWARD);
	if (!itr)
		goto done;

	mpc = mnt_new_cache();
	if (!mpc)
		goto done;
	mnt_table_set_cache(tb, mpc);
	mnt_unref_cache(mpc);

	while(mnt_table_next_fs(fstab, itr, &fs) == 0) {
		if (mnt_table_is_fs_mounted(tb, fs))
			printf("%s already mounted on %s\n",
					mnt_fs_get_source(fs),
					mnt_fs_get_target(fs));
		else
			printf("%s not mounted on %s\n",
					mnt_fs_get_source(fs),
					mnt_fs_get_target(fs));
	}

	rc = 0;
done:
	mnt_unref_table(tb);
	mnt_unref_table(fstab);
	mnt_free_iter(itr);
	return rc;
}

int main(int argc, char *argv[])
{
	struct libmnt_test tss[] = {
	{ "--parse",    test_parse,        "<file> [--comments] parse and print tab" },
	{ "--find-forward",  test_find_fw, "<file> <source|target> <string>" },
	{ "--find-backward", test_find_bw, "<file> <source|target> <string>" },
	{ "--find-pair",     test_find_pair, "<file> <source> <target>" },
	{ "--find-mountpoint", test_find_mountpoint, "<path>" },
	{ "--copy-fs",       test_copy_fs, "<file>  copy root FS from the file" },
	{ "--is-mounted",    test_is_mounted, "<fstab> check what from <file> are already mounted" },
	{ NULL }
	};

	return mnt_run_test(tss, argc, argv);
}

#endif /* TEST_PROGRAM */
