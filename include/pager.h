/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file may be redistributed under the terms of the
 * GNU General Public License.
 */
#ifndef UTIL_LINUX_PAGER
#define UTIL_LINUX_PAGER

#include <stdbool.h>

/* --pager / --nopager */
enum ul_pagermode {
	UL_PAGER_AUTO = 0,	/* unspecified by user; obey PAGER_ENABLE */
	UL_PAGER_NEVER,		/* --nopager (or --json/--raw) */
	UL_PAGER_ALWAYS		/* --pager */
};

void pager_open(void);
void pager_open_header(int header_lines, int first_col_width);
void pager_close(void);
bool pager_is_enabled(enum ul_pagermode mode);

#endif
