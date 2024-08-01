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

struct libmnt_lsmnt {

	uint64_t	id;		/* node ID (LSMT_ROOT for "/") */
	size_t		stepsiz;	/* how many IDs read in one step */
	uint64_t	*list;		/* buffer for IDs */

	unsigned int	done : 1,		/* we already have all data */
			reverse : 1,		/* current setting */
			reverse_supported : 1,	/* kernel status */
			reverse_verified : 1;	/* kernel status already requested */
};

/* default number of IDs one listmount() call */
#define MNT_LSMNT_STEPSIZ	(BUFSIZ / sizeof(uint64_t))

/**
 * mnt_table_enable_listmount:
 * @tb: mount table
 * @id: 0 for root filesystem, statx(TATX_MNT_ID_UNIQUE) for subdirectory
 * @stepsize: 0 for built-in default, otherwise number of mountpoints to read in one step
 *
 * This function initializes @tb to read data from the listmount() syscall. The
 * default behavior is to read data from the kernel on demand. Usually, the
 * listmount() syscall is called when new data is required by mnt_table_next_fs().
 *
 * If on-demand behavior is not desired, then use mnt_table_fetch_listmount() to
 * get the complete mount table.
 *
 * The direction (forward/backward) in which libmount reads mount IDs from the kernel
 * depends on the direction used for mnt_table_next_fs() and the kernel version. This
 * is because LISTMOUNT_REVERSE is not supported by all kernel versions. If the library
 * detects a mix of forward and backward directions in the mount table, or if
 * LISTMOUNT_REVERSE is unsupported, it will reset the table and read the complete
 * mount table rather then use on-demand incremental way.
 *
 * The table is reset before new data is added.
 *
 * Returns: 0 on sucess, < 0 on error
 * Since: 2.41
 */
int mnt_table_enable_listmount(struct libmnt_table *tb, uint64_t id, size_t stepsiz)
{
	struct libmnt_lsmnt *ls;
	size_t ss = stepsiz == 0 ? MNT_LSMNT_STEPSIZ : stepsiz;

	if (!tb)
		return -EINVAL;

	/* check if supported by current kernel */
	errno = 0;
	if (listmount(0, 0, NULL, 0, 0) != 0 && errno == ENOSYS) {
		DBG(TAB, ul_debugobj(tb, "listmount: unsuppported"));
		return -ENOSYS;
	}

	mnt_reset_table(tb);

	/* reset if allocated for a different size */
	if (tb->lsmnt && tb->lsmnt->stepsiz != ss) {
		free(tb->lsmnt);
		tb->lsmnt = NULL;
	}

	/* alloc buffer and struct libmnt_lsmnt by one calloc() call */
	if (tb->lsmnt)
		ls = tb->lsmnt;
	else {
		char *x = calloc(1, sizeof(tb->lsmnt) + (sizeof(uint64_t) * ss));
		if (!x)
			return -ENOMEM;
		ls = (struct libmnt_lsmnt *) x;
		ls->list = (uint64_t *) (x + sizeof(tb->lsmnt));
		tb->lsmnt = ls;
	}

	DBG(TAB, ul_debugobj(tb, "listmount: init [id=%" PRIu64 ", step=%zu]", id, ss));
	ls->id = id == 0 ? LSMT_ROOT : id;
	ls->stepsiz = ss;

	return 0;
}

/**
 * mnt_table_disable_listmount:
 * @tb: table instance
 *
 * Disables listmount() for the @tb (and deallocates relevant resources).
 *
 */
void mnt_table_disable_listmount(struct libmnt_table *tb)
{
	if (tb && tb->lsmnt) {
		free(tb->lsmnt);
		tb->lsmnt = NULL;
	}
}

/* add new entries from list[] to table */
static int lsmnt_to_table(
		struct libmnt_table *tb, int reverse,
		uint64_t list[], size_t nitems)
{
	int rc = 0;
	size_t i;
	struct libmnt_fs *prev = NULL;

	if (reverse)
		mnt_table_first_fs(tb, &prev);
	else
		mnt_table_last_fs(tb, &prev);
	if (prev)
		mnt_ref_fs(prev);

	DBG(TAB, ul_debugobj(tb, "listmount: insert %zu", nitems));

	for (i = 0; rc == 0 && i < nitems; i++) {
		struct libmnt_fs *fs;
		uint64_t id = list[i];

		if (!id)
			continue;

		fs = mnt_new_fs();
		if (fs) {
			mnt_fs_set_uniq_id(fs, id);
			rc = mnt_table_insert_fs(tb, reverse, prev, fs);
		} else
			rc = -ENOMEM;

		mnt_unref_fs(prev);
		prev = fs;
	}

	mnt_unref_fs(prev);

	return rc;
}

static inline int is_reverse_supported(struct libmnt_lsmnt *ls)
{
	if (!ls->reverse_verified) {
		ls->reverse_verified = 1;
		if (listmount(0, 0, NULL, 0, LISTMOUNT_REVERSE) == 0) {
			ls->reverse_supported = 1;
			DBG(TAB, ul_debug("listmount: reverse supported"));
		} else
			DBG(TAB, ul_debug("listmount: reverse unsupported"));
	}

	return ls->reverse_supported;
}

/*
 * Private function, backed of mnt_table_next_fs().
 *
 * Return: 0 on success, 1 if not more data, <0 on error.
 */
int mnt_table_next_lsmnt(struct libmnt_table *tb, int direction)
{
	struct libmnt_lsmnt *ls;
	ssize_t n;
	uint64_t last = 0;
	int reverse = direction == MNT_ITER_BACKWARD;

	if (!tb || !tb->lsmnt)
		return -EINVAL;
	if (tb->lsmnt->done)
		return 1;
	ls = tb->lsmnt;

	/* read all if reverse table is not supported by kernel */
	if (reverse && !is_reverse_supported(ls))
		return mnt_table_fetch_listmount(tb, 0);

	/* read all to avoid mixing order in the table */
	if (!mnt_table_is_empty(tb) && ls->reverse != reverse)
		return mnt_table_fetch_listmount(tb, 0);

	/* last ID; zero first time (due to calloc()) */
	last = ls->list[ ls->stepsiz - 1 ];

	DBG(TAB, ul_debugobj(tb, "listmount: call [id=%" PRIu64", last=%" PRIu64" %s]",
				ls->id, last, reverse ? "reverse" : ""));

	n = listmount(ls->id, last, ls->list, ls->stepsiz,
			reverse ? LISTMOUNT_REVERSE : 0);
	if (n < 0)
		return -errno;

	if (n < (ssize_t) ls->stepsiz)
		ls->done = 1;
	if (n > 0)
		return lsmnt_to_table(tb, reverse, ls->list, n);

	return 1;	/* nothing */
}

/**
 * mnt_table_fetch_listmount:
 * @tb: table instance
 *
 * Reads all mount nodes from the kernel and adds them to the @tb table. The
 * table is reset before new data is added.
 *
 * Return: 0 on success, <0 on error.
 */
int mnt_table_fetch_listmount(struct libmnt_table *tb, uint64_t id)
{
	size_t stepsiz = 0;
	uint64_t *list = NULL;
	ssize_t n;
	int rc = 0;

	if (!tb)
		return -EINVAL;
	if (!id)
		id = LSMT_ROOT;

	/* default to stuff defined by mnt_table_enable_listmount() */
	if (tb->lsmnt) {
		stepsiz = tb->lsmnt->stepsiz;
		list = tb->lsmnt->list;
	}

	if (!stepsiz)
		stepsiz = MNT_LSMNT_STEPSIZ;
	if (!list) {
		list = calloc(stepsiz, sizeof(uint64_t));
		if (!list)
			return -ENOMEM;
	}

	mnt_reset_table(tb);

	do {
		n = listmount(id, 0, list, stepsiz, 0);
		if (n < 0) {
			rc = -errno;
			break;
		}
		rc = lsmnt_to_table(tb, 0, list, n);
	} while (rc == 0 && n == (ssize_t) stepsiz);

	if (tb->lsmnt)
		/* Avoid using on-demand mnt_table_next_lsmnt() if we already
		 * have all the necessary data (or on error) */
		tb->lsmnt->done = 1;

	if (!tb->lsmnt || !tb->lsmnt->list)
		free(list);

	return rc;
}


