/*
 * Copyright (C) 2023 Karel Zak <kzak@redhat.com>
 *
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 *
 *
 * This sample reads the mountpoint entry from /etc/fstab and mounts it to the
 * different (on the command line specified) mountpoint. The mount options
 * settings are read from fstab.
 */
#include <stdlib.h>

#include "c.h"
#include "strutils.h"
#include "libmount.h"

int main(int argc, char *argv[])
{
        struct libmnt_fs *fs;
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

	if (mnt_fs_fetch_statmount(fs))
		err(EXIT_FAILURE, "cannot fetch FS infomation");

	mnt_fs_print_debug(fs, stdout);

	mnt_unref_fs(fs);

	return EXIT_SUCCESS;
}
