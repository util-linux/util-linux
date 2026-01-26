/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef UTIL_LINUX_SHELLS_H
#define UTIL_LINUX_SHELLS_H

#include <pwd.h>

#define UL_SHELL_NOENV  (1 << 0)
#define UL_SHELL_NOPWD  (1 << 1)

extern void print_shells(FILE *out, const char *format);
extern int is_known_shell(const char *shell_name);

const char *ul_default_shell(int flags, const struct passwd *pw);

#endif /* UTIL_LINUX_SHELLS_H */
