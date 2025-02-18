/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * This file is part of libmount from util-linux project.
 *
 * Copyright (C) 2024 Karel Zak <kzak@redhat.com>
 *
 * libmount is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 */
#include "mountP.h"

#ifndef HAVE_STATMOUNT_API

int mnt_table_listmount_set_id(
		struct libmnt_table *tb __attribute__((__unused__)),
		uint64_t id __attribute__((__unused__)))
{
	return -ENOSYS;
}

int mnt_table_listmount_set_ns(
		struct libmnt_table *tb __attribute__((__unused__)),
		uint64_t ns __attribute__((__unused__)))
{
	return -ENOSYS;
}

int mnt_table_listmount_set_stepsiz(
		struct libmnt_table *tb __attribute__((__unused__)),
		size_t sz __attribute__((__unused__)))
{
	return -ENOSYS;
}

int mnt_table_enable_listmount(
		struct libmnt_table *tb __attribute__((__unused__)),
		int enable __attribute__((__unused__)))
{
	return -ENOSYS;
}

int mnt_table_fetch_listmount(struct libmnt_table *tb __attribute__((__unused__)))
{
	return -ENOSYS;
}

int mnt_table_reset_listmount(struct libmnt_table *tb __attribute__((__unused__)))
{
	return -ENOSYS;
}

int mnt_table_next_lsmnt(struct libmnt_table *tb __attribute__((__unused__)),
			int direction __attribute__((__unused__)))
{
	return -ENOSYS;
}

#else /* HAVE_STATMOUNT_API */

/*
* This struct is not shared between multiple tables, so reference counting is
* not used for it.
 */
struct libmnt_listmnt {

	uint64_t	id;		/* node ID (LSMT_ROOT for "/") */
	uint64_t	ns;		/* namespce ID or zero for the current */
	uint64_t	last;		/* last ID from previous listmount() call */
	size_t		stepsiz;	/* how many IDs read in one step */
	uint64_t	*list;		/* buffer for IDs */

	unsigned int	enabled : 1,	/* on-demand listmount status */
			done : 1,	/* we already have all data */
			reverse : 1;	/* current setting */
};

/* default number of IDs read by one listmount() call */
#define MNT_LSMNT_STEPSIZ	512

static int table_init_listmount(struct libmnt_table *tb, size_t stepsiz)
{
	struct libmnt_listmnt *ls;

	if (!tb)
		return -EINVAL;
	if (!stepsiz)
		stepsiz = MNT_LSMNT_STEPSIZ;

	ls = tb->lsmnt;

	/* check if supported by current kernel */
	if (!ls) {
		uint64_t dummy;

		errno = 0;
		if (ul_listmount(LSMT_ROOT, 0, 0, &dummy, 1, LISTMOUNT_REVERSE) != 1) {
			if (errno == ENOSYS)
				DBG(TAB, ul_debugobj(tb, "listmount: unsuppported"));
			if (errno == EINVAL)
				DBG(TAB, ul_debugobj(tb, "listmount: reverse unsuppported"));
			errno = ENOSYS;
			return -ENOSYS;
		}
	}

	/* reset if allocated for a different size */
	if (ls && ls->stepsiz != stepsiz)
		ls = NULL;

	/* alloc libmnt_listmnt together with list buffer */
	if (!ls) {
		char *x = calloc(1, sizeof(struct libmnt_listmnt)
				    + (sizeof(uint64_t) * stepsiz));
		if (!x)
			return -ENOMEM;

		ls = (struct libmnt_listmnt *) x;
		ls->list = (uint64_t *) (x + sizeof(struct libmnt_listmnt));
		ls->stepsiz = stepsiz;
		ls->id = LSMT_ROOT;	/* default */

		/* reuse old setting */
		if (tb->lsmnt) {
			ls->id = tb->lsmnt->id;
			ls->ns = tb->lsmnt->ns;
			ls->last = tb->lsmnt->last;
			ls->enabled = tb->lsmnt->enabled;
			ls->reverse = tb->lsmnt->reverse;

			free(tb->lsmnt);
		}
		tb->lsmnt = ls;;
	}

	DBG(TAB, ul_debugobj(tb, "listmount: init [step=%zu]", ls->stepsiz));
	return 0;
}

/**
 * mnt_table_listmount_set_id:
 * @tb: mount table
 * @id: root ID
 *
 * Set root ID for the table if the table is read from kernel by
 * listmount() syscall. The default is to read all filesystems; use
 * statx(STATX_MNT_ID_UNIQUE) for subdirectory.
 *
 * Returns: 0 on sucess, < 0 on error
 * Since: 2.41
 */
int mnt_table_listmount_set_id(struct libmnt_table *tb, uint64_t id)
{
	int rc = 0;

	if (!tb)
		return -EINVAL;
	if (!tb->lsmnt && (rc = table_init_listmount(tb, 0)) != 0)
		return rc;
	tb->lsmnt->id = id;
	return 0;
}

/**
 * mnt_table_listmount_set_ns:
 * @tb: mount table
 * @id: namespace ID
 *
 * Set namespace ID for listmount().
 *
 * Returns: 0 on sucess, < 0 on error
 * Since: 2.41
 */
int mnt_table_listmount_set_ns(struct libmnt_table *tb, uint64_t ns)
{
	int rc = 0;

	if (!tb)
		return -EINVAL;
	if (!tb->lsmnt && (rc = table_init_listmount(tb, 0)) != 0)
		return rc;
	tb->lsmnt->ns = ns;
	return 0;
}

/**
 * mnt_table_listmount_set_stepsiz:
 * @tb: mount table
 * @sz: number of nodes read by one libmount() call
 *
 * Returns: 0 on sucess, < 0 on error
 * Since: 2.41
 */
int mnt_table_listmount_set_stepsiz(struct libmnt_table *tb, size_t sz)
{
	if (!tb)
		return -EINVAL;

	return table_init_listmount(tb, sz);
}

/*
 * This function is called by mnt_reset_table() and the table must already be
 * empty.
 *
 * Private; not export to library API!
 **/
int mnt_table_reset_listmount(struct libmnt_table *tb)
{
	if (!tb || !tb->lsmnt)
	       return 0;
	if (tb->nents)
		return -EINVAL;

	tb->lsmnt->done = 0;
	tb->lsmnt->reverse = 0;
	tb->lsmnt->last = 0;
	return 0;
}

/**
 * mnt_table_enable_listmount:
 * @tb: table
 * @enable: 0 or 1
 *
 * Enable or disable on-demand listmount() to make it usable by
 * mnt_table_next_fs(). This function does not affect
 * mnt_table_fetch_listmont().
 *
 * Returns: old status (1 or 0)
 * Since: 2.41
 */
int mnt_table_enable_listmount(struct libmnt_table *tb, int enable)
{
	int old = 0;

	if (tb && tb->lsmnt) {
		old = tb->lsmnt->enabled;
		tb->lsmnt->enabled = enable;
		DBG(TAB, ul_debugobj(tb, "listmount() %s",
					enable ? "on" : "off"));
	}
	return old;
}

/* private; returns 1 if on-demand listmount() possible */
int mnt_table_want_listmount(struct libmnt_table *tb)
{
	return tb && tb->lsmnt && tb->lsmnt->enabled;
}

/* add new entries from list[] to table */
static int lsmnt_to_table(
		struct libmnt_table *tb, struct libmnt_listmnt *ls,
		size_t nitems, int reverse)
{
	int rc = 0;
	size_t i;
	struct libmnt_fs *prev = NULL;

	if (!ls)
		return -EINVAL;
	if (reverse)
		mnt_table_first_fs(tb, &prev);
	else
		mnt_table_last_fs(tb, &prev);
	if (prev)
		mnt_ref_fs(prev);

	DBG(TAB, ul_debugobj(tb, "listmount: insert %zu", nitems));

	for (i = 0; rc == 0 && i < nitems; i++) {
		struct libmnt_fs *fs;
		uint64_t id = ls->list[i];

		if (!id)
			continue;

		fs = mnt_new_fs();
		if (fs) {
			fs->flags |= MNT_FS_KERNEL;
			mnt_fs_set_uniq_id(fs, id);
			if (ls->ns)
				mnt_fs_set_ns(fs, ls->ns);

			rc = mnt_table_insert_fs(tb, reverse, prev, fs);
		} else
			rc = -ENOMEM;

		mnt_unref_fs(prev);
		prev = fs;
	}

	mnt_unref_fs(prev);
	return rc;
}

/*
 * Private function, backed of mnt_table_next_fs().
 *
 * Return: 0 on success, 1 if not more data, <0 on error.
 */
int mnt_table_next_lsmnt(struct libmnt_table *tb, int direction)
{
	ssize_t n;
	int reverse = direction == MNT_ITER_BACKWARD;
	struct libmnt_listmnt *ls = NULL;
	int rc = 0;

	if (!tb || !tb->lsmnt)
		return -EINVAL;
	if (tb->lsmnt->done || !tb->lsmnt->enabled)
		return 1;

	ls = tb->lsmnt;

	/* disable on-demand fetching */
	mnt_table_enable_listmount(tb, 0);

	/* read all to avoid mixing order in the table */
	if (!mnt_table_is_empty(tb) && ls->reverse != reverse) {
		rc = mnt_table_fetch_listmount(tb);
		goto done;
	}

	ls->reverse = reverse;

	DBG(TAB, ul_debugobj(tb, "listmount: call "
				 "[id=%" PRIu64", ns=%" PRIu64
				 "last=%" PRIu64", sz=%zu %s]",
				ls->id, ls->ns,
				ls->last, ls->stepsiz,
				ls->reverse ? "reverse" : ""));

	n = ul_listmount(ls->id, ls->ns, ls->last, ls->list, ls->stepsiz,
			reverse ? LISTMOUNT_REVERSE : 0);
	if (n < 0) {
		rc = -errno;
		goto done;
	}

	if (n < (ssize_t) ls->stepsiz)
		ls->done = 1;
	if (n > 0) {
		ls->last = ls->list[ n - 1 ];
		rc = lsmnt_to_table(tb, ls, n, reverse);
	} else
		rc = 0;
done:
	mnt_table_enable_listmount(tb, 1);

	DBG(TAB, ul_debugobj(tb, "listmount: on-demand done [rc=%d]", rc));
	return rc;		/* nothing */
}

/**
 * mnt_table_fetch_listmount:
 * @tb: table instance
 *
 * By default, this function reads all mount nodes in the current namespace
 * from the kernel and adds them to the @tb table. This default behavior can
 * be modified using mnt_table_listmount_set_...().
 *
 * The table is reset (all file systems removed) before new data is added.
 *
 * Return: 0 on success, <0 on error.
 * Since: 2.41
 */
int mnt_table_fetch_listmount(struct libmnt_table *tb)
{
	int rc = 0, stmnt_status = 0, lsmnt_status = 0;
	struct libmnt_listmnt *ls = NULL;
	ssize_t n;

	if (!tb)
		return -EINVAL;

	DBG(TAB, ul_debugobj(tb, "listmount: fetching all"));

	if (!tb->lsmnt && (rc = table_init_listmount(tb, 0)) != 0)
		return rc;

	/* disable on-demand statmount() */
	if (tb->stmnt)
		stmnt_status = mnt_statmnt_disable_fetching(tb->stmnt, 1);
	/* disable on-demand listmount() */
	lsmnt_status = mnt_table_enable_listmount(tb, 0);

	mnt_reset_table(tb);

	ls = tb->lsmnt;

	do {
		DBG(TAB, ul_debugobj(tb, "listmount: call "
				 "[id=%" PRIu64", ns=%" PRIu64
				 "last=%" PRIu64", sz=%zu]",
				ls->id, ls->ns,
				ls->last, ls->stepsiz));

		n = ul_listmount(ls->id, ls->ns, ls->last,
				 ls->list, ls->stepsiz, 0);
		if (n < 0) {
			rc = -errno;
			break;
		}
		ls->last = ls->list[ n - 1 ];
		rc = lsmnt_to_table(tb, ls, n, 0);

	} while (rc == 0 && n == (ssize_t) ls->stepsiz);

	/* Avoid using on-demand mnt_table_next_lsmnt() if we already
	 * have all the necessary data (or on error) */
	tb->lsmnt->done = 1;

	/* restore */
	if (tb->stmnt)
		mnt_statmnt_disable_fetching(tb->stmnt, stmnt_status);
	mnt_table_enable_listmount(tb, lsmnt_status);

	DBG(TAB, ul_debugobj(tb, "listmount: fetching done [rc=%d]", rc));

	return rc;
}

#endif /* HAVE_STATMOUNT_API */
