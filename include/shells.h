/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef UTIL_LINUX_SHELLS_H
#define UTIL_LINUX_SHELLS_H

#include <stdio.h>

#if defined (HAVE_LIBECONF) && defined (USE_VENDORDIR)
# include <libeconf.h>
econf_file *open_etc_shells(void);
#endif

extern void print_shells(FILE *out, const char *format);
extern int is_known_shell(const char *shell_name);

#endif /* UTIL_LINUX_SHELLS_H */
