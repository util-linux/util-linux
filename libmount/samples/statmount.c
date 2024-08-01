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

#include <linux/mount.h>	/* STATMOUNT_* masks */

#include "mount-api-utils.h"	/* fallback for old linux/mount.h */

int main(int argc, char *argv[])
{
        struct libmnt_fs *fs;
	struct libmnt_statmnt *sm;
	const char *mnt;

	if (argc != 2)
		errx(EXIT_FAILURE, "usage: %s <mountpoint | id>",
				program_invocation_short_name);

	mnt_init_debug(0);

	fs = mnt_new_fs();
	if (!fs)
		err(EXIT_FAILURE, "failed to allocate fs handler");

	mnt = argv[1];
	if (isdigit_string(mnt))
		mnt_fs_set_uniq_id(fs, strtou64_or_err(mnt, "cannot ID"));
	else
		mnt_fs_set_target(fs, mnt);

	sm = mnt_new_statmnt();
	if (!sm)
		err(EXIT_FAILURE, "failed to allocate statmount handler");

	/* enable on-demand fetching from kernel */
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
