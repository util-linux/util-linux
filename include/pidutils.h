/*
 * No copyright is claimed.  This code is in the public domain; do with
 * it what you wish.
 *
 * Authors: Christian Goeschel Ndjomouo <cgoesc2@wgu.edu> [2025]
 */
#ifndef UTIL_LINUX_PIDUTILS_H
#define UTIL_LINUX_PIDUTILS_H

#include <sys/types.h>

extern int ul_parse_pid_str(char *pidstr, pid_t *pid_num, ino_t *pfd_ino);

#endif /* UTIL_LINUX_PIDUTILS_H */