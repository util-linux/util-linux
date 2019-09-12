/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <stdio.h>

#include "caputils.h"
#include "pathnames.h"

int cap_last_cap(void)
{
	/* CAP_LAST_CAP is untrustworthy. */
	static int ret = -1;
	int matched;
	FILE *f;

	if (ret != -1)
		return ret;

	f = fopen(_PATH_PROC_CAPLASTCAP, "r");
	if (!f) {
		ret = CAP_LAST_CAP;	/* guess */
		return ret;
	}

	matched = fscanf(f, "%d", &ret);
	fclose(f);

	if (matched != 1)
		ret = CAP_LAST_CAP;	/* guess */

	return ret;
}
