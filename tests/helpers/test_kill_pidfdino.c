/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * test_kill_pidfdino - return a pidfd inode for a process using its pid
 *
 * Written by Christian Goeschel Ndjomouo <cgoesc2@wgu.edu> [2025]
 */
#include <errno.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "c.h"
#include "exitcodes.h"
#include "strutils.h"
#include "pidfd-utils.h"

int main(int argc, char **argv)
{
        int pfd, rc = 0;
        pid_t pid;
        struct stat f;

        if (argc != 2)
                err(EXIT_FAILURE, "usage: %s PID", *argv);

        pid = strtopid_or_err(argv[1], "invalid pid");
        pfd = pidfd_open(pid, 0);
        if (pfd < 0)
                err_nosys(EXIT_FAILURE, "pidfd_open() failed %d", pid);

        rc = fstat(pfd, &f);
        if (rc != 0)
                err(EXIT_FAILURE, "fstat() failed: %d", pfd);

        printf("%"PRIu64"\n", f.st_ino);
        return EXIT_SUCCESS;
}