/* GNU's read utmp module.

	 Copyright (C) 1992-2001, 2003-2006, 2009-2014 Free Software Foundation, Inc.

	 This program is free software: you can redistribute it and/or modify
	 it under the terms of the GNU General Public License as published by
	 the Free Software Foundation; either version 3 of the License, or
	 (at your option) any later version.

	 This program is distributed in the hope that it will be useful,
	 but WITHOUT ANY WARRANTY; without even the implied warranty of
	 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the
	 GNU General Public License for more details.

	 You should have received a copy of the GNU General Public License
	 along with this program.	If not, see <http://www.gnu.org/licenses/>.	*/

/* Written by jla; revised by djm */
/* extracted for util-linux by ooprala */

#include <errno.h>
#include <stdio.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#include "xalloc.h"
#include "readutmp.h"

/* Read the utmp entries corresponding to file FILE into freshly-
	 malloc'd storage, set *UTMP_BUF to that pointer, set *N_ENTRIES to
	 the number of entries, and return zero.	If there is any error,
	 return -1, setting errno, and don't modify the parameters.
	 If OPTIONS & READ_UTMP_CHECK_PIDS is nonzero, omit entries whose
	 process-IDs do not currently exist.	*/
int
read_utmp (char const *file, size_t *n_entries, struct utmp **utmp_buf)
{
	size_t n_read = 0;
	size_t n_alloc = 0;
	struct utmp *utmp = NULL;
	struct utmp *u;

	/* Ignore the return value for now.
		 Solaris' utmpname returns 1 upon success -- which is contrary
		 to what the GNU libc version does.	In addition, older GNU libc
		 versions are actually void.	 */
	utmpname(file);

	setutent();

	errno = 0;
	while ((u = getutent()) != NULL) {
		if (n_read == n_alloc) {
			n_alloc += 32;
			utmp = xrealloc(utmp, n_alloc * sizeof (struct utmp));
			if (!utmp)
				return -1;
		}
		utmp[n_read++] = *u;
	}
	if (!u && errno) {
		free(utmp);
		return -1;
	}

	endutent();

	*n_entries = n_read;
	*utmp_buf = utmp;

	return 0;
}
