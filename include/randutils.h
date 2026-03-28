/*
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef UTIL_LINUX_RANDUTILS
#define UTIL_LINUX_RANDUTILS

/* getrandom(2) based with fallback to /dev/(u)random and libc randomness */
extern int ul_random_get_bytes(void *buf, size_t nbytes);
extern const char *random_tell_source(void);

#endif
