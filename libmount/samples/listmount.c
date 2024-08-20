/*
 * Copyright (C) 2024 Karel Zak <kzak@redhat.com>
 *
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 */
#include <stdlib.h>

#include "c.h"
#include "strutils.h"
#include "timeutils.h"
#include "pathnames.h"

#include "libmount.h"

#include "mount-api-utils.h"	/* fallback for old linux/mount.h */

static void __iter_table(	struct libmnt_table *tb,
			struct libmnt_iter *itr, int output, int reverse)
{
	struct libmnt_fs *fs = NULL;
	int rc;

	mnt_reset_iter(itr, reverse ? MNT_ITER_BACKWARD : MNT_ITER_FORWARD);
	while ((rc = mnt_table_next_fs(tb, itr, &fs)) == 0) {
		const char *tgt = mnt_fs_get_target(fs);
		const char *type = mnt_fs_get_fstype(fs);

		if (output)
			printf (" %15s %s\n", type, tgt);
	}
	if (rc < 0)
		warn("cannot iterate on filesystems");
}

#define fetch_data(t, i)		__iter_table(t, i, 0, 0)
#define print_table(t, i)		__iter_table(t, i, 1, 0)
#define print_table_reverse(t, i)	__iter_table(t, i, 1, 1)


int main(int argc, char *argv[])
{
	struct libmnt_table *tb;
	struct libmnt_statmnt *sm;
	struct libmnt_iter *itr;
	struct timeval start, end;
	uint64_t id = 0;
	double sec_lsmnt, sec_lsstmnt, sec_mountinfo;

	mnt_init_debug(0);


	if (argc == 2) {
		const char *arg = argv[1];
		if (!isdigit_string(arg))
			mnt_id_from_path(arg, &id, NULL);
		else
			id = strtou64_or_err(arg, "cannot ID");
	}

	itr = mnt_new_iter(MNT_ITER_FORWARD);
	if (!itr)
		err(MNT_EX_SYSERR, "failed to initialize libmount iterator");

	tb = mnt_new_table();
	if (!tb)
		err(EXIT_FAILURE, "failed to allocate table handler");
	if (id)
		mnt_table_listmount_set_id(tb, id);

	/*
	 * A) listmount() and statmount() based table
	 */
	sm = mnt_new_statmnt();
	if (!sm)
		err(EXIT_FAILURE, "failed to allocate statmnt handler");

	/* Without this mask setting, the library will make two statmount() calls
	 * for each node. */
#if defined(STATMOUNT_MNT_POINT) && defined(STATMOUNT_FS_TYPE)
	mnt_statmnt_set_mask(sm, STATMOUNT_MNT_POINT | STATMOUNT_FS_TYPE);
#endif

	/* force fetch all data
	mnt_statmnt_set_mask(sm,
			STATMOUNT_SB_BASIC |
			STATMOUNT_MNT_BASIC |
			STATMOUNT_PROPAGATE_FROM |
			STATMOUNT_MNT_ROOT |
			STATMOUNT_MNT_POINT |
			STATMOUNT_FS_TYPE
			STATMOUNT_MNT_OPTS);
	*/

	/* enable don-demand statmount() call for all filesystems in the table */
	mnt_table_refer_statmnt(tb, sm);

	/* listmount() only */
	gettimeofday(&start, NULL);
	if (mnt_table_fetch_listmount(tb) != 0)
		warn("failed to read mount table by listmount()");
	gettimeofday(&end, NULL);
	sec_lsmnt = time_diff(&end, &start);

	/* force statmount() for all nodes */
	fetch_data(tb, itr);
	gettimeofday(&end, NULL);
	sec_lsstmnt = time_diff(&end, &start);

	fprintf(stdout, "listmount() based table:\n");
	print_table(tb, itr);


	/* disable statmount() and listmount(); reset table */
	mnt_statmnt_disable_fetching(sm, 1);
	mnt_table_enable_listmount(tb, 0);
	mnt_reset_table(tb);

	/*
	 * B) /proc/self/mountinfo based table
	 */
	gettimeofday(&start, NULL);
	mnt_table_parse_file(tb, _PATH_PROC_MOUNTINFO);
	gettimeofday(&end, NULL);
	sec_mountinfo = time_diff(&end, &start);

	fprintf(stdout, "\nprocfs based table:\n");
	print_table(tb, itr);

	fprintf(stdout, "\n"
			"%f sec listmount()\n"
			"%f sec listmount()+statmount()\n"
			"%f sec /proc/sef/mountinfo"
			"\n\n",
			sec_lsmnt, sec_lsstmnt, sec_mountinfo);


	mnt_reset_table(tb);

	/*
	 * C) Instead of reading the entire mount table with one listmount() call, read
	 * it in smaller steps. This is particularly useful for systems with large
	 * mount tables, where we may only need to access one specific node (usually the
	 * last one).
	 *
	 * By default, libmount reads 512 nodes in one call. To test this sample
	 * on normal systems, we will reduce this number to 5 nodes. The listmount()
	 * function is used as a backend for regular mnt_table_next_fs().
	 * There is no need to make any changes to the application, just enable
	 * on-demand listmount() by using mnt_table_enable_listmount().
	 */
	if (mnt_table_listmount_set_stepsiz(tb, 5) != 0)
		warn("failed to initialize listmount()");
	mnt_table_enable_listmount(tb, 1);

	/* enable also statmount() */
	mnt_statmnt_disable_fetching(sm, 0);

	fprintf(stdout, "\nlistmount() - small steps (reverse):\n");
	print_table_reverse(tb, itr);


	mnt_unref_table(tb);
	mnt_unref_statmnt(sm);
	mnt_free_iter(itr);

	return EXIT_SUCCESS;
}
