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

#include <sys/prctl.h>
#include <stdio.h>
#include <limits.h>

#include "caputils.h"
#include "procutils.h"
#include "pathnames.h"

static int test_cap(unsigned int cap)
{
	/* prctl returns 0 or 1 for valid caps, -1 otherwise */
	return prctl(PR_CAPBSET_READ, cap, 0, 0, 0) >= 0;
}

int cap_last_cap(void)
{
	static int cap = -1;
	FILE *f;

	if (cap != -1)
		return cap;

	/* try to read value from kernel, check that the path is
	 * indeed in a procfs mount */
	f = fopen(_PATH_PROC_CAPLASTCAP, "r");
	if (f) {
		int matched = 0;

		if (proc_is_procfs(fileno(f))) {
			matched = fscanf(f, "%d", &cap);
		}
		fclose(f);

		/* we check if the cap after this one really isn't valid */
		if (matched == 1 && cap < INT_MAX && !test_cap(cap + 1))
			return cap;
	}

	/* if it wasn't possible to read the file in /proc,
	 * fall back to binary search over capabilities */

	/* starting with cap=INT_MAX means we always know
	 * that cap1 is invalid after the first iteration */
	unsigned int cap0 = 0, cap1 = INT_MAX;
	cap = INT_MAX;
	while ((int)cap0 < cap) {
		if (test_cap(cap)) {
			cap0 = cap;
		} else {
			cap1 = cap;
		}
		cap = (cap0 + cap1) / 2U;
	}

	return cap;
}
