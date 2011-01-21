/*
 * Copyright (C) 2008-2010 Karel Zak <kzak@redhat.com>
 *
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 */

/**
 * SECTION: tab
 * @title: Table of filesystems
 * @short_description: container for entries from fstab/mtab/mountinfo
 *
 *
 * Note that mnt_table_find_* functions are mount(8) compatible. These functions
 * try to found an entry in more iterations where the first attempt is always
 * based on comparison with unmodified (non-canonicalized or un-evaluated)
 * paths or tags. For example fstab with two entries:
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
 * will returns the first entry (if UUID matches with the device).
 */

#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <blkid.h>

#include "nls.h"
#include "mountP.h"
#include "c.h"

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

	INIT_LIST_HEAD(&tb->ents);
	return tb;
}

/**
 * mnt_free_table:
 * @tb: tab pointer
 *
 * Deallocates tab struct and all entries.
 */
void mnt_free_table(struct libmnt_table *tb)
{
	if (!tb)
		return;

	DBG(TAB, mnt_debug_h(tb, "free"));

	while (!list_empty(&tb->ents)) {
		struct libmnt_fs *fs = list_entry(tb->ents.next,
				                  struct libmnt_fs, ents);
		mnt_free_fs(fs);
	}

	free(tb);
}

/**
 * mnt_table_get_nents:
 * @tb: pointer to tab
 *
 * Returns: number of valid entries in tab.
 */
int mnt_table_get_nents(struct libmnt_table *tb)
{
	assert(tb);
	return tb ? tb->nents : 0;
}

/**
 * mnt_table_set_cache:
 * @tb: pointer to tab
 * @mpc: pointer to struct libmnt_cache instance
 *
 * Setups a cache for canonicalized paths and evaluated tags (LABEL/UUID). The
 * cache is recommended for mnt_table_find_*() functions.
 *
 * The cache could be shared between more tabs. Be careful when you share the
 * same cache between more threads -- currently the cache does not provide any
 * locking method.
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
 * Adds a new entry to tab.
 *
 * Returns: 0 on success or negative number in case of error.
 */
int mnt_table_add_fs(struct libmnt_table *tb, struct libmnt_fs *fs)
{
	assert(tb);
	assert(fs);

	if (!tb || !fs)
		return -EINVAL;

	list_add_tail(&fs->ents, &tb->ents);

	DBG(TAB, mnt_debug_h(tb, "add entry: %s %s",
			mnt_fs_get_source(fs), mnt_fs_get_target(fs)));
	tb->nents++;
	return 0;
}

/**
 * mnt_table_remove_fs:
 * @tb: tab pointer
 * @fs: new entry
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
	tb->nents--;
	return 0;
}

/**
 * mnt_table_get_root_fs:
 * @tb: mountinfo file (/proc/self/mountinfo)
 * @root: returns pointer to the root filesystem (/)
 *
 * Returns: 0 on success or -1 case of error.
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
			break;		/* @tab is not mountinfo file? */

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
 * Note that filesystems are returned in the order how was mounted (according to
 * IDs in /proc/self/mountinfo).
 *
 * Returns: 0 on success, negative number in case of error or 1 at end of list.
 */
int mnt_table_next_child_fs(struct libmnt_table *tb, struct libmnt_iter *itr,
			struct libmnt_fs *parent, struct libmnt_fs **chld)
{
	struct libmnt_fs *fs;
	int parent_id, lastchld_id = 0, chld_id = 0;

	if (!tb || !itr || !parent)
		return -EINVAL;

	DBG(TAB, mnt_debug_h(tb, "lookup next child of %s",
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
 * Returns: 0 on success, negative number in case of error or 1 at end of list.
 *
 * Example:
 * <informalexample>
 *   <programlisting>
 *	while(mnt_table_next_fs(tb, itr, &fs) == 0) {
 *		const char *dir = mnt_fs_get_target(fs);
 *		printf("mount point: %s\n", dir);
 *	}
 *	mnt_free_table(fi);
 *   </programlisting>
 * </informalexample>
 *
 * lists all mountpoints from fstab in backward order.
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
 * mnt_table_find_next_fs:
 * @tb: table
 * @itr: iterator
 * @match_func: function returns 1 or 0
 * @userdata: extra data for match_func
 * @fs: returns pointer to the next matching table entry
 *
 * This function allows search in @tb.
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
 * mnt_table_find_target:
 * @tb: tab pointer
 * @path: mountpoint directory
 * @direction: MNT_ITER_{FORWARD,BACKWARD}
 *
 * Try to lookup an entry in given tab, possible are three iterations, first
 * with @path, second with realpath(@path) and third with realpath(@path)
 * against realpath(fs->target). The 2nd and 3rd iterations are not performed
 * when @tb cache is not set (see mnt_table_set_cache()).
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

	if (!tb || !path)
		return NULL;

	DBG(TAB, mnt_debug_h(tb, "lookup target: %s", path));

	/* native @target */
	mnt_reset_iter(&itr, direction);
	while(mnt_table_next_fs(tb, &itr, &fs) == 0)
		if (fs->target && strcmp(fs->target, path) == 0)
			return fs;

	if (!tb->cache || !(cn = mnt_resolve_path(path, tb->cache)))
		return NULL;

	/* canonicalized paths in struct libmnt_table */
	mnt_reset_iter(&itr, direction);
	while(mnt_table_next_fs(tb, &itr, &fs) == 0) {
		if (fs->target && strcmp(fs->target, cn) == 0)
			return fs;
	}

	/* non-canonicaled path in struct libmnt_table */
	mnt_reset_iter(&itr, direction);
	while(mnt_table_next_fs(tb, &itr, &fs) == 0) {
		char *p;

		if (!fs->target || !(fs->flags & MNT_FS_SWAP) ||
		    (*fs->target == '/' && *(fs->target + 1) == '\0'))
		       continue;

		p = mnt_resolve_path(fs->target, tb->cache);
		if (strcmp(cn, p) == 0)
			return fs;
	}
	return NULL;
}

/**
 * mnt_table_find_srcpath:
 * @tb: tab pointer
 * @path: source path (devname or dirname)
 * @direction: MNT_ITER_{FORWARD,BACKWARD}
 *
 * Try to lookup an entry in given tab, possible are four iterations, first
 * with @path, second with realpath(@path), third with tags (LABEL, UUID, ..)
 * from @path and fourth with realpath(@path) against realpath(entry->srcpath).
 *
 * The 2nd, 3rd and 4th iterations are not performed when @tb cache is not
 * set (see mnt_table_set_cache()).
 *
 * Returns: a tab entry or NULL.
 */
struct libmnt_fs *mnt_table_find_srcpath(struct libmnt_table *tb, const char *path, int direction)
{
	struct libmnt_iter itr;
	struct libmnt_fs *fs = NULL;
	int ntags = 0;
	char *cn;
	const char *p;

	assert(tb);
	assert(path);

	DBG(TAB, mnt_debug_h(tb, "lookup srcpath: %s", path));

	/* native paths */
	mnt_reset_iter(&itr, direction);
	while(mnt_table_next_fs(tb, &itr, &fs) == 0) {
		p = mnt_fs_get_srcpath(fs);
		if (p && strcmp(p, path) == 0)
			return fs;
		if (!p)
			/* mnt_fs_get_srcpath() returs nothing, it's TAG */
			ntags++;
	}

	if (!tb->cache || !(cn = mnt_resolve_path(path, tb->cache)))
		return NULL;

	/* canonicalized paths in struct libmnt_table */
	if (ntags < mnt_table_get_nents(tb)) {
		mnt_reset_iter(&itr, direction);
		while(mnt_table_next_fs(tb, &itr, &fs) == 0) {
			p = mnt_fs_get_srcpath(fs);
			if (p && strcmp(p, cn) == 0)
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
			/* @path is unaccessible, try evaluate all TAGs in @tb
			 * by udev symlinks -- this could be expensive on systems
			 * with huge fstab/mtab */
			 while(mnt_table_next_fs(tb, &itr, &fs) == 0) {
				 const char *t, *v, *x;
				 if (mnt_fs_get_tag(fs, &t, &v))
					 continue;
				 x = mnt_resolve_tag(t, v, tb->cache);
				 if (x && !strcmp(x, cn))
					 return fs;
			 }
		}
	}

	/* non-canonicalized paths in struct libmnt_table */
	if (ntags <= mnt_table_get_nents(tb)) {
		mnt_reset_iter(&itr, direction);
		while(mnt_table_next_fs(tb, &itr, &fs) == 0) {
			if (fs->flags & (MNT_FS_NET | MNT_FS_PSEUDO))
				continue;
			p = mnt_fs_get_srcpath(fs);
			if (p)
				p = mnt_resolve_path(p, tb->cache);
			if (p && strcmp(cn, p) == 0)
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
 * Try to lookup an entry in given tab, first attempt is lookup by @tag and
 * @val, for the second attempt the tag is evaluated (converted to the device
 * name) and mnt_table_find_srcpath() is preformed. The second attempt is not
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

	if (!tb || !tag || !val)
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
 * This is high-level API for mnt_table_find_{srcpath,tag}. You needn't to care
 * about @source format (device, LABEL, UUID, ...). This function parses @source
 * and calls mnt_table_find_tag() or mnt_table_find_srcpath().
 *
 * Returns: a tab entry or NULL.
 */
struct libmnt_fs *mnt_table_find_source(struct libmnt_table *tb, const char *source, int direction)
{
	struct libmnt_fs *fs = NULL;

	assert(tb);
	assert(source);

	if (!tb || !source)
		return NULL;

	DBG(TAB, mnt_debug_h(tb, "lookup SOURCE: %s", source));

	if (strchr(source, '=')) {
		char *tag, *val;

		if (blkid_parse_tag_string(source, &tag, &val) == 0) {

			fs = mnt_table_find_tag(tb, tag, val, direction);

			free(tag);
			free(val);
		}
	} else
		fs = mnt_table_find_srcpath(tb, source, direction);

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
 * mnt_fs_match_target() functions. It means that this is more expensive that
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
	assert(source);
	assert(target);

	if (!tb || !source || !target)
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

#ifdef TEST_PROGRAM

static int parser_errcb(struct libmnt_table *tb, const char *filename, int line)
{
	fprintf(stderr, "%s:%d: parse error\n", filename, line);

	return 1;	/* all errors are recoverable -- this is default */
}

struct libmnt_table *create_table(const char *file)
{
	struct libmnt_table *tb;

	if (!file)
		return NULL;
	tb = mnt_new_table();
	if (!tb)
		goto err;

	mnt_table_set_parser_errcb(tb, parser_errcb);

	if (mnt_table_parse_file(tb, file) != 0)
		goto err;
	return tb;
err:
	fprintf(stderr, "%s: parsing failed\n", file);
	mnt_free_table(tb);
	return NULL;
}

int test_copy_fs(struct libmnt_test *ts, int argc, char *argv[])
{
	struct libmnt_table *tb;
	struct libmnt_fs *fs;
	int rc = -1;

	tb = create_table(argv[1]);
	if (!tb)
		return -1;

	fs = mnt_table_find_target(tb, "/", MNT_ITER_FORWARD);
	if (!fs)
		goto done;

	printf("ORIGINAL:\n");
	mnt_fs_print_debug(fs, stdout);

	fs = mnt_copy_fs(fs);
	if (!fs)
		goto done;

	printf("COPY:\n");
	mnt_fs_print_debug(fs, stdout);
	mnt_free_fs(fs);
	rc = 0;
done:
	mnt_free_table(tb);
	return rc;
}

int test_parse(struct libmnt_test *ts, int argc, char *argv[])
{
	struct libmnt_table *tb = NULL;
	struct libmnt_iter *itr = NULL;
	struct libmnt_fs *fs;
	int rc = -1;

	tb = create_table(argv[1]);
	if (!tb)
		return -1;

	itr = mnt_new_iter(MNT_ITER_FORWARD);
	if (!itr)
		goto done;

	while(mnt_table_next_fs(tb, itr, &fs) == 0)
		mnt_fs_print_debug(fs, stdout);
	rc = 0;
done:
	mnt_free_iter(itr);
	mnt_free_table(tb);
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

	tb = create_table(file);
	if (!tb)
		goto done;

	/* create a cache for canonicalized paths */
	mpc = mnt_new_cache();
	if (!mpc)
		goto done;
	mnt_table_set_cache(tb, mpc);

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
	mnt_free_table(tb);
	mnt_free_cache(mpc);
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
	int rc = -1;

	tb = create_table(argv[1]);
	if (!tb)
		return -1;

	fs = mnt_table_find_pair(tb, argv[2], argv[3], MNT_ITER_FORWARD);
	if (!fs)
		goto done;

	mnt_fs_print_debug(fs, stdout);
	rc = 0;
done:
	mnt_free_table(tb);
	return rc;
}

int main(int argc, char *argv[])
{
	struct libmnt_test tss[] = {
	{ "--parse",    test_parse,        "<file>  parse and print tab" },
	{ "--find-forward",  test_find_fw, "<file> <source|target> <string>" },
	{ "--find-backward", test_find_bw, "<file> <source|target> <string>" },
	{ "--find-pair",     test_find_pair, "<file> <source> <target>" },
	{ "--copy-fs",       test_copy_fs, "<file>  copy root FS from the file" },
	{ NULL }
	};

	return mnt_run_test(tss, argc, argv);
}

#endif /* TEST_PROGRAM */
