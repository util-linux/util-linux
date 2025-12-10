/*
 * No copyright is claimed.  This code is in the public domain; do with
 * it what you wish.
 *
 * Authors: Christian Goeschel Ndjomouo <cgoesc2@wgu.edu> [2025]
 */
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
int pfd_is_pidfs(int pidfd)
{
	struct statfs stfs;
	int rc;

	rc = fstatfs(pidfd, &stfs);
	if (rc < 0)
		return 0;

	return F_TYPE_EQUAL(stfs.f_type, STATFS_PIDFS_MAGIC);
}

#ifdef USE_PIDFD_INO_SUPPORT
uint64_t pidfd_get_inode(int pidfd)
{
	struct statx stx;
	int rc;

	rc = statx(pidfd, "", AT_EMPTY_PATH, STATX_INO, &stx);
	if (rc < 0) {
		close(pidfd);
		err(EXIT_FAILURE, N_("failed to statx() pidfd"));
	}
	return stx.stx_ino;
}
#endif

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
#ifdef USE_PIDFD_INO_SUPPORT
int ul_get_valid_pidfd_or_err(pid_t pid, uint64_t pidfd_ino)
#else
int ul_get_valid_pidfd_or_err(pid_t pid, uint64_t pidfd_ino __attribute__((__unused__)))
#endif
{
	int pfd;

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

#ifdef USE_PIDFD_INO_SUPPORT
	uint64_t real_pidfd_ino;
	if (pidfd_ino) {
		real_pidfd_ino = pidfd_get_inode(pfd);
		if (real_pidfd_ino != pidfd_ino) {
			close(pfd);
			errx(EXIT_FAILURE, N_("pidfd inode %"PRIu64" not found for pid %d"),
				pidfd_ino, pid);
		}
	}
#endif
	return pfd;
}
