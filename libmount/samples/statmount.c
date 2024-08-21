/*
 * Copyright (C) 2023 Karel Zak <kzak@redhat.com>
 *
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 */
#include <stdlib.h>

#include "c.h"
#include "strutils.h"
#include "libmount.h"

#include "mount-api-utils.h"	/* fallback for old linux/mount.h */

int main(int argc, char *argv[])
{
        struct libmnt_fs *fs;
	struct libmnt_statmnt *sm;
	const char *mnt;
	uint64_t id = 0;

	if (argc != 2)
		errx(EXIT_FAILURE, "usage: %s <mountpoint | id>",
				program_invocation_short_name);

	mnt_init_debug(0);

	fs = mnt_new_fs();
	if (!fs)
		err(EXIT_FAILURE, "failed to allocate fs handler");

	/* define target (mountpoint) or mount ID */
	mnt = argv[1];
	if (isdigit_string(mnt)) {
		id = strtou64_or_err(mnt, "cannot ID");
		mnt_fs_set_uniq_id(fs, id);
	} else
		mnt_fs_set_target(fs, mnt);

	/*
	 * A) fetch all data without reference to libmnt_statmnt
	 */
	if (mnt_fs_fetch_statmount(fs, 0) != 0)
		warn("failed to read data by statmount()");
	mnt_fs_print_debug(fs, stdout);

	/* reset */
	id = mnt_fs_get_uniq_id(fs);
	mnt_reset_fs(fs);
	mnt_fs_set_uniq_id(fs, id);

	/*
	 * B) fetch data by on-demand way
	 */
	sm = mnt_new_statmnt();
	if (!sm)
		err(EXIT_FAILURE, "failed to allocate statmount handler");

	mnt_fs_refer_statmnt(fs, sm);

	/* read fs type, but nothing else */
	mnt_fs_get_fstype(fs);
	mnt_fs_print_debug(fs, stdout);

	/* read fs root, but nothing else */
        mnt_fs_get_root(fs);
        mnt_fs_print_debug(fs, stdout);

	/* read all mising data */
	mnt_fs_fetch_statmount(fs, 0);
	mnt_fs_print_debug(fs, stdout);

	/* see debug, this is no-op for statmount() */
	mnt_fs_get_fstype(fs);

	mnt_unref_fs(fs);
	mnt_unref_statmnt(sm);

	return EXIT_SUCCESS;
}
