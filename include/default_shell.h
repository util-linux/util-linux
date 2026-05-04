/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef UTIL_LINUX_DEFAULT_SHELL_H
#define UTIL_LINUX_DEFAULT_SHELL_H

#include <pwd.h>

#define UL_SHELL_NOENV  (1 << 0)
#define UL_SHELL_NOPWD  (1 << 1)

const char *ul_default_shell(int flags, const struct passwd *pw);

#endif /* UTIL_LINUX_DEFAULT_SHELL_H */
