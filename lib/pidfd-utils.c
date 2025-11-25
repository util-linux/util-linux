/*
 * No copyright is claimed.  This code is in the public domain; do with
 * it what you wish.
 *
 * Authors: Christian Goeschel Ndjomouo <cgoesc2@wgu.edu> [2025]
 */
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <err.h>

#include "c.h"
#include "nls.h"
#include "strutils.h"
#include "pidfd-utils.h"

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
        struct stat f;

        pfd = pidfd_open(pid, 0);
        if (pfd < 0)
                err(EXIT_FAILURE, _("pidfd_open() failed"));

        if (pfd_ino) {
                rc = fstat(pfd, &f);
                if (rc < 0)
                        err(EXIT_FAILURE, _("failed to fstat() pidfd"));

                if (f.st_ino != pfd_ino) {
                        close(pfd);
                        errx(EXIT_FAILURE, _("pidfd inode %"PRIu64" not found for pid %d"),
                                pfd_ino, pid);
                }
        }
        return pfd;
}