/*
 * No copyright is claimed.  This code is in the public domain; do with
 * it what you wish.
 *
 * Authors: Christian Goeschel Ndjomouo <cgoesc2@wgu.edu> [2025]
 */
#define _GNU_SOURCE 1

#include <stdlib.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <sys/types.h>
#include <errno.h>
#include <err.h>

#include "c.h"
#include "nls.h"
#include "strutils.h"
#include "pidfd-utils.h"
#include "statfs_magic.h"

/*
 * Returns 1, if the pidfd has the pidfs file system type, otherwise 0.
 */
int pfd_is_pidfs(int pfd)
{
	struct statfs stfs;
	int rc;

	rc = fstatfs(pfd, &stfs);
	if (rc < 0)
		return 0;

	return F_TYPE_EQUAL(stfs.f_type, STATFS_PIDFS_MAGIC);
}

/*
 * ul_get_valid_pidfd_or_err() - Return a valid file descriptor for a PID
 *                               or exit the process with an error message.
 *
 * @pid:     PID number for which to get a file descriptor
 * @pfd_ino: A pidfd inode number that is expected to be the
 *           same as for the new file descriptor.
 *
 * Pass @pfd_ino as NULL, if the pidfd should not be validated.
 *
 * Return: On success, a file descriptor is returned.
 *         On failure, err() or errx() is called to
 *         print an error message and kill the program.
 *
 */
int ul_get_valid_pidfd_or_err(pid_t pid, ino_t pfd_ino)
{
        int pfd, rc;
        struct statx stx;

        pfd = pidfd_open(pid, 0);
        if (pfd < 0)
                err(EXIT_FAILURE, N_("pidfd_open() failed"));

	/* the file descriptor has to have the pidfs file system type
	 * otherwise the inode assigned to it will not be useful.
	 */
	if (!pfd_is_pidfs(pfd)) {
		close(pfd);
		errx(EXIT_FAILURE, N_("pidfd needs to have the pidfs file system type"));
	}

        if (pfd_ino) {
                rc = statx(pfd, NULL, AT_EMPTY_PATH, STATX_INO, &stx);
                if (rc < 0)
                        err(EXIT_FAILURE, N_("failed to statx() pidfd"));

                if (stx.stx_ino != pfd_ino) {
                        close(pfd);
                        errx(EXIT_FAILURE, N_("pidfd inode %"PRIu64" not found for pid %d"),
                                pfd_ino, pid);
                }
        }
        return pfd;
}
