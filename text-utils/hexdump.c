/*
 * Copyright (c) 1989 The Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

 /* 1999-02-22 Arkadiusz Miśkiewicz <misiek@pld.ORG.PL>
  * - added Native Language Support
  */

#include <sys/types.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "hexdump.h"

#include "list.h"
#include "nls.h"
#include "c.h"
#include "closestream.h"

struct list_head fshead;				/* head of format strings */
ssize_t blocksize;			/* data block size */
int exitval;				/* final exit value */
ssize_t length = -1;			/* max bytes to read */

int main(int argc, char **argv)
{
	struct list_head *p;
	FS *tfs;
	char *c;
	INIT_LIST_HEAD(&fshead);

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	if (!(c = strrchr(argv[0], 'o')) || strcmp(c, "od")) {
		newsyntax(argc, &argv);
	} else
		errx(EXIT_FAILURE, _("calling hexdump as od has been deprecated "
				     "in favour to GNU coreutils od."));

	/* figure out the data block size */
	blocksize = 0;
	list_for_each(p, &fshead) {
		tfs = list_entry(p, FS, nextfs);
		if ((tfs->bcnt = block_size(tfs)) > blocksize)
			blocksize = tfs->bcnt;
	}

	/* rewrite the rules, do syntax checking */
	list_for_each(p, &fshead) {
		tfs = list_entry(p, FS, nextfs);
		rewrite(tfs);
	}

	(void)next(argv);
	display();
	return exitval;
}
