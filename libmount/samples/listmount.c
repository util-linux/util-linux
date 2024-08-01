/*
 * Copyright (C) 2024 Karel Zak <kzak@redhat.com>
 *
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 */
#include <stdlib.h>

#include "c.h"
#include "strutils.h"
#include "libmount.h"

#include <linux/mount.h>	/* STATMOUNT_* masks */

#include "mount-api-utils.h"	/* fallback for old linux/mount.h */

static void print_table(struct libmnt_table *tb)
{
	struct libmnt_iter *itr = NULL;
	struct libmnt_fs *fs = NULL;;

	itr = mnt_new_iter(MNT_ITER_FORWARD);
	if (!itr)
		err(MNT_EX_SYSERR, "failed to initialize libmount iterator");

	while (mnt_table_next_fs(tb, itr, &fs) == 0) {
		printf ("id: %" PRIu64 "\n", mnt_fs_get_uniq_id(fs));
	}

	mnt_free_iter(itr);
}

int main(int argc, char *argv[])
{
        struct libmnt_table *tb;
	uint64_t id = 0;

	mnt_init_debug(0);

	tb = mnt_new_table();
	if (!tb)
		err(EXIT_FAILURE, "failed to allocate table handler");

	if (argc == 2) {
		const char *arg = argv[1];
		if (isdigit_string(arg))
			mnt_id_from_path(arg, &id, NULL);
		else
			id = strtou64_or_err(arg, "cannot ID");
	}


	/* fetch complete table */
	mnt_table_fetch_listmount(tb, id);
	print_table(tb);

	/* enable on-demand fetching from kernel */
	//mnt_table_enable_listmount(tb, id, 0);

	mnt_unref_table(tb);

	return EXIT_SUCCESS;
}
