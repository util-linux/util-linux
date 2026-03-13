/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file may be redistributed under the terms of the
 * GNU General Public License.
 */
#ifndef UTIL_LINUX_PAGER
#define UTIL_LINUX_PAGER

#include <stddef.h>

void pager_open(void);
void pager_open_header(int header_lines, size_t first_col_width);
void pager_close(void);

#endif
