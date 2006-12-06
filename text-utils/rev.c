/*-
 * Copyright (c) 1987, 1992 The Regents of the University of California.
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
 *
 * Modified for Linux by Charles Hannum (mycroft@gnu.ai.mit.edu)
 *                   and Brian Koehmstedt (bpk@gnu.ai.mit.edu)
 *
 * Wed Sep 14 22:26:00 1994: Patch from bjdouma <bjdouma@xs4all.nl> to handle
 *                           last line that has no newline correctly.
 * 3-Jun-1998: Patched by Nicolai Langfeldt to work better on Linux:
 * 	Handle any-length-lines.  Code copied from util-linux' setpwnam.c
 * 1999-02-22 Arkadiusz Mi¶kiewicz <misiek@pld.ORG.PL>
 * 	added Native Language Support
 * 1999-09-19 Bruno Haible <haible@clisp.cons.org>
 * 	modified to work correctly in multi-byte locales
 *
 */

#include <stdarg.h>
#include <sys/types.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "nls.h"

#include "widechar.h"

void usage(void);
void warn(const char *, ...);

int
main(int argc, char *argv[])
{
  register char *filename;
  register wchar_t *t;
  size_t buflen = 512;
  wchar_t *p = malloc(buflen*sizeof(wchar_t));
  size_t len;
  FILE *fp;
  int ch, rval;

  setlocale(LC_ALL, "");
  bindtextdomain(PACKAGE, LOCALEDIR);
  textdomain(PACKAGE);

  while ((ch = getopt(argc, argv, "")) != -1)
    switch(ch) {
    case '?':
    default:
      usage();
    }

  argc -= optind;
  argv += optind;

  fp = stdin;
  filename = "stdin";
  rval = 0;
  do {
    if (*argv) {
      if ((fp = fopen(*argv, "r")) == NULL) {
	warn("%s: %s", *argv, strerror(errno));
	rval = 1;
	++argv;
	continue;
      }
      filename = *argv++;
    }

    while (fgetws(p, buflen, fp)) {

      len = wcslen(p);

      /* This is my hack from setpwnam.c -janl */
      while (p[len-1] != '\n' && !feof(fp)) {
	/* Extend input buffer if it failed getting the whole line */

	/* So now we double the buffer size */
	buflen *= 2;

	p = realloc(p, buflen*sizeof(wchar_t));
	if (p == NULL) {
	  fprintf(stderr,_("Unable to allocate bufferspace\n"));
	  exit(1);
	}

	/* And fill the rest of the buffer */
	if (fgetws(&p[len], buflen/2, fp) == NULL) break;

	len = wcslen(p);
      
	/* That was a lot of work for nothing.  Gimme perl! */
      }
		  
      t = p + len - 1 - (*(p+len-1)=='\r' || *(p+len-1)=='\n');
      for ( ; t >= p; --t)
	if (*t != 0)
	  putwchar(*t);
      putwchar('\n');
    }
    fflush(fp);
    if (ferror(fp)) {
      warn("%s: %s", filename, strerror(errno));
      rval = 1;
    }
    if (fclose(fp))
      rval = 1;
  } while(*argv);
  exit(rval);
}

void
warn(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	(void)fprintf(stderr, "rev: ");
	(void)vfprintf(stderr, fmt, ap);
	va_end(ap);
	(void)fprintf(stderr, "\n");
}

void
usage(void)
{
	(void)fprintf(stderr, _("usage: rev [file ...]\n"));
	exit(1);
}
