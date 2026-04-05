/*
 * No copyright is claimed.  This code is in the public domain; do with
 * it what you wish.
 *
 * Authors: Christian Goeschel Ndjomouo <cgoesc2@wgu.edu> [2025]
 */
#ifndef UTIL_LINUX_PIDUTILS_H
#define UTIL_LINUX_PIDUTILS_H

#include <sys/types.h>

#define UL_PID_ZERO	(1 << 1)
#define UL_PID_NEGATIVE	(1 << 2)

extern int ul_parse_pid_str(char *pidstr, pid_t *pid_num, uint64_t *pfd_ino, int flags);
extern void ul_parse_pid_str_or_err(char *pidstr, pid_t *pid_num, uint64_t *pfd_ino, int flags);

#endif /* UTIL_LINUX_PIDUTILS_H */
