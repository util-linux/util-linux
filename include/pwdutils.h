/*
 * No copyright is claimed.  This code is in the public domain; do with
 * it what you wish.
 */
#ifndef UTIL_LINUX_PWDUTILS_H
#define UTIL_LINUX_PWDUTILS_H

#include <sys/types.h>
#include <pwd.h>
#include <grp.h>

extern struct passwd *xgetpwuid(uid_t uid, char **pwdbuf);
extern struct passwd *xgetuserpw(const char *str, char **pwdbuf);
extern struct group *xgetgroup(const char *str, char **pwdbuf);
extern char *xgetlogin(void);
extern struct group *ul_getgrp_str(const char *str);
extern struct passwd *ul_getuserpw_str(const char *str);

#endif /* UTIL_LINUX_PWDUTILS_H */

