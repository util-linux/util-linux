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
			struct libmnt_iter *itr, int output)
{
	struct libmnt_fs *fs = NULL;;

	mnt_reset_iter(itr, MNT_ITER_FORWARD);
	while (mnt_table_next_fs(tb, itr, &fs) == 0) {
		const char *tgt = mnt_fs_get_target(fs);
		const char *type = mnt_fs_get_fstype(fs);

		if (output)
			printf (" %15s %s\n", type, tgt);
	}
}

#define fetch_data(t, i)	__iter_table(t, i, 0)
#define print_table(t, i)	__iter_table(t, i, 1)

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
		if (isdigit_string(arg))
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

	/*
	 * listmount() and statmount() based table
	 */
	sm = mnt_new_statmnt();
	if (!sm)
		err(EXIT_FAILURE, "failed to allocate statmnt handler");

	/* Without this mask setting, the library will make two statmount() calls
	 * for each node. */
	mnt_statmnt_set_mask(sm, STATMOUNT_MNT_POINT | STATMOUNT_FS_TYPE);

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
	mnt_table_fetch_listmount(tb, id);
	gettimeofday(&end, NULL);
	sec_lsmnt = time_diff(&end, &start);

	/* statmount() for all nodes */
	fetch_data(tb, itr);
	gettimeofday(&end, NULL);
	sec_lsstmnt = time_diff(&end, &start);

	fprintf(stdout, "listmount() based table:\n");
	print_table(tb, itr);

	/* diable listmount() and statmount(), reset table */
	mnt_table_disable_listmount(tb);
	mnt_table_refer_statmnt(tb, NULL);
	mnt_reset_table(tb);

	/*
	 * /proc/self/mountinfo based table
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

	mnt_unref_table(tb);
	mnt_unref_statmnt(sm);
	mnt_free_iter(itr);

	return EXIT_SUCCESS;
}
