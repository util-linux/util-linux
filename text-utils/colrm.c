/*
 * Copyright (c) 1980 Regents of the University of California.
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

/*
 * 1999-02-22 Arkadiusz Mi¶kiewicz <misiek@pld.ORG.PL>
 * 	added Native Language Support
 * 1999-09-19 Bruno Haible <haible@clisp.cons.org>
 * 	modified to work correctly in multi-byte locales
 */

#include <stdio.h>
#include <stdlib.h>
#include <locale.h>

#include "widechar.h"

/*
COLRM removes unwanted columns from a file
	Jeff Schriebman  UC Berkeley 11-74
*/

int
main(int argc, char **argv)
{
	register int ct, first, last;
	register wint_t c;
	int i, w;
	int padding;

	setlocale(LC_ALL, "");

	first = 0;
	last = 0;
	if (argc > 1)
		first = atoi(*++argv);
	if (argc > 2)
		last = atoi(*++argv);

start:
	ct = 0;
loop1:
	c = getwc(stdin);
	if (c == WEOF)
		goto fin;
	if (c == '\t')
		w = ((ct + 8) & ~7) - ct;
	else if (c == '\b')
		w = (ct ? ct - 1 : 0) - ct;
	else {
		w = wcwidth(c);
		if (w < 0)
			w = 0;
	}
	ct += w;
	if (c == '\n') {
		putwc(c, stdout);
		goto start;
	}
	if (!first || ct < first) {
		putwc(c, stdout);
		goto loop1;
	}
	for (i = ct-w+1; i < first; i++)
		putwc(' ', stdout);

/* Loop getting rid of characters */
	while (!last || ct < last) {
		c = getwc(stdin);
		if (c == WEOF)
			goto fin;
		if (c == '\n') {
			putwc(c, stdout);
			goto start;
		}
		if (c == '\t')
			ct = (ct + 8) & ~7;
		else if (c == '\b')
			ct = ct ? ct - 1 : 0;
		else {
			w = wcwidth(c);
			if (w < 0)
				w = 0;
			ct += w;
		}
	}

	padding = 0;

/* Output last of the line */
	for (;;) {
		c = getwc(stdin);
		if (c == WEOF)
			break;
		if (c == '\n') {
			putwc(c, stdout);
			goto start;
		}
		if (padding == 0 && last < ct) {
			for (i = last; i <ct; i++)
				putwc(' ', stdout);
			padding = 1;
		}
		putwc(c, stdout);
	}
fin:
	fflush(stdout);
	if (ferror(stdout) || fclose(stdout))
		return 1;
	return 0;
}
