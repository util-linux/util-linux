/*
 * Copyright (c) 1989, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
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

#include <sys/types.h>
#include <sys/ioctl.h>

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <err.h>
#include <errno.h>
#include <getopt.h>
#include "nls.h"

#include "widechar.h"

#ifdef HAVE_WIDECHAR
#define wcs_width(s) wcswidth(s,wcslen(s))
static wchar_t *mbs_to_wcs(const char *);
#else
#define wcs_width(s) strlen(s)
#define mbs_to_wcs(s) strdup(s)
static char *mtsafe_strtok(char *, const char *, char **);
#define wcstok mtsafe_strtok
#endif

#define DEFCOLS     25
#define TAB         8
#define DEFNUM      1000
#define MAXLINELEN  (LINE_MAX + 1)

static void  c_columnate __P((void));
static void *emalloc __P((int));
static void  input __P((FILE *));
static void  maketbl __P((void));
static void  print __P((void));
static void  r_columnate __P((void));

typedef struct _tbl {
	wchar_t **list;
	int cols, *len;
} TBL;

int termwidth = 80;		/* default terminal width */

int entries;			/* number of records */
int eval;			/* exit value */
int maxlength;			/* longest record */
wchar_t **list;			/* array of pointers to records */
wchar_t default_separator[] = { '\t', ' ', 0 };
wchar_t *separator = default_separator;	/* field separator for table option */

struct option longopts[] =
{
	{ "help",	0, 0, 'h' },
	{ "columns",	0, 0, 'c' },
	{ "table",	0, 0, 't' },
	{ "separator",	0, 0, 's' },
	{ "fillrows",	0, 0, 'x' },
	{ NULL,		0, 0, 0 },
};

static void __attribute__((__noreturn__)) usage(int rc)
{
	FILE *out = rc == EXIT_FAILURE ? stderr : stdout;

	fprintf(out, _("\nUsage: %s [options] [file ...]\n"),
				program_invocation_short_name);
	fprintf(out, _("\nOptions:\n"));

	fprintf(out, _(
	" -h, --help               displays this help text\n"
	" -c, --columns <width>    width of output in number of characters\n"
	" -t, --table              create a table\n"
	" -s, --separator <string> table delimeter\n"
	" -x, --fillrows           fill rows before columns\n"));

	fprintf(out, _("\nFor more information see column(1).\n"));
	exit(rc);
}
int
main(int argc, char **argv)
{
	struct winsize win;
	FILE *fp;
	int ch, tflag, xflag;
	char *p;

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

	if (ioctl(1, TIOCGWINSZ, &win) == -1 || !win.ws_col) {
		if ((p = getenv("COLUMNS")) != NULL)
			termwidth = atoi(p);
	} else
		termwidth = win.ws_col;

	tflag = xflag = 0;
	while ((ch = getopt_long(argc, argv, "h?c:s:tx", longopts, NULL)) != -1)
		switch(ch) {
		case 'h':
		case '?':
			usage(EXIT_SUCCESS);
			break;
		case 'c':
			termwidth = atoi(optarg);
			break;
		case 's':
			separator = mbs_to_wcs(optarg);
			break;
		case 't':
			tflag = 1;
			break;
		case 'x':
			xflag = 1;
			break;
		default:
			usage(EXIT_FAILURE);
        }
	argc -= optind;
	argv += optind;

	if (!*argv)
		input(stdin);
	else {
		for (; *argv; ++argv) {
			if ((fp = fopen(*argv, "r")) != NULL) {
				input(fp);
				(void)fclose(fp);
			} else {
				warn("%s", *argv);
				eval = EXIT_FAILURE;
			}
		}
	}

	if (!entries)
		exit(eval);

	if (tflag)
		maketbl();
	else if (maxlength >= termwidth)
		print();
	else if (xflag)
		c_columnate();
	else
		r_columnate();
	if (ferror(stdout) || fclose(stdout))
		eval = EXIT_FAILURE;
	exit(eval);
}

static void
c_columnate()
{
	int chcnt, col, cnt, endcol, numcols;
	wchar_t **lp;

	maxlength = (maxlength + TAB) & ~(TAB - 1);
	numcols = termwidth / maxlength;
	endcol = maxlength;
	for (chcnt = col = 0, lp = list;; ++lp) {
		fputws(*lp, stdout);
		chcnt += wcs_width(*lp);
		if (!--entries)
			break;
		if (++col == numcols) {
			chcnt = col = 0;
			endcol = maxlength;
			putwchar('\n');
		} else {
			while ((cnt = ((chcnt + TAB) & ~(TAB - 1))) <= endcol) {
				putwchar('\t');
				chcnt = cnt;
			}
			endcol += maxlength;
		}
	}
	if (chcnt)
		putwchar('\n');
}

static void
r_columnate()
{
	int base, chcnt, cnt, col, endcol, numcols, numrows, row;

	maxlength = (maxlength + TAB) & ~(TAB - 1);
	numcols = termwidth / maxlength;
	if (!numcols) 
		numcols = 1;
	numrows = entries / numcols;
	if (entries % numcols)
		++numrows;

	for (row = 0; row < numrows; ++row) {
		endcol = maxlength;
		for (base = row, chcnt = col = 0; col < numcols; ++col) {
			fputws(list[base], stdout);
			chcnt += wcs_width(list[base]);
			if ((base += numrows) >= entries)
				break;
			while ((cnt = ((chcnt + TAB) & ~(TAB - 1))) <= endcol) {
				putwchar('\t');
				chcnt = cnt;
			}
			endcol += maxlength;
		}
		putwchar('\n');
	}
}

static void
print()
{
	int cnt;
	wchar_t **lp;

	for (cnt = entries, lp = list; cnt--; ++lp) {
		fputws(*lp, stdout);
		putwchar('\n');
	}
}

static void
maketbl()
{
	TBL *t;
	int coloff, cnt, i;
	wchar_t *p, **lp;
	int *lens, maxcols;
	TBL *tbl;
	wchar_t **cols;
	wchar_t *wcstok_state;

	t = tbl = emalloc(entries * sizeof(TBL));
	cols = emalloc((maxcols = DEFCOLS) * sizeof(wchar_t *));
	lens = emalloc(maxcols * sizeof(int));
	for (cnt = 0, lp = list; cnt < entries; ++cnt, ++lp, ++t) {
		for (coloff = 0, p = *lp;
		    (cols[coloff] = wcstok(p, separator, &wcstok_state)) != NULL;
		    p = NULL) {
			if (++coloff == maxcols) {
				cols = realloc(cols, ((u_int)maxcols + DEFCOLS)
					 * sizeof(wchar_t *));
				lens = realloc(lens, ((u_int)maxcols + DEFCOLS)
					* sizeof(int));
				if (!cols || !lens)
					err(EXIT_FAILURE, _("out of memory?"));
				memset((char *)lens + maxcols * sizeof(int),
					0, DEFCOLS * sizeof(int));
				maxcols += DEFCOLS;
			}
		}
		t->list = emalloc(coloff * sizeof(wchar_t *));
		t->len = emalloc(coloff * sizeof(int));
		for (t->cols = coloff; --coloff >= 0;) {
			t->list[coloff] = cols[coloff];
			t->len[coloff] = wcs_width(cols[coloff]);
			if (t->len[coloff] > lens[coloff])
				lens[coloff] = t->len[coloff];
		}
	}
	for (cnt = 0, t = tbl; cnt < entries; ++cnt, ++t) {
		for (coloff = 0; coloff < t->cols - 1; ++coloff) {
			fputws(t->list[coloff], stdout);
			for (i = lens[coloff] - t->len[coloff] + 2; i > 0; i--)
				putwchar(' ');
		}
		if (coloff < t->cols) {
			fputws(t->list[coloff], stdout);
			putwchar('\n');
		}
	}
}

static void
input(fp)
	FILE *fp;
{
	static int maxentry;
	int len, lineno = 1, reportedline = 0;
	wchar_t *p, buf[MAXLINELEN];

	if (!list)
		list = emalloc((maxentry = DEFNUM) * sizeof(wchar_t *));
	while (fgetws(buf, MAXLINELEN, fp)) {
		for (p = buf; *p && iswspace(*p); ++p);
		if (!*p)
			continue;
		if (!(p = wcschr(p, '\n')) && !feof(fp)) {
			if (reportedline < lineno) {
				warnx(_("line %d is too long, output will be truncated"),
					lineno);
				reportedline = lineno;
			}
			eval = EXIT_FAILURE;
			continue;
		}
		lineno++;
		if (!feof(fp))
			*p = '\0';
		len = wcs_width(buf);	/* len = p - buf; */
		if (maxlength < len)
			maxlength = len;
		if (entries == maxentry) {
			maxentry += DEFNUM;
			list = realloc(list, (u_int)maxentry * sizeof(wchar_t *));
			if (!list)
				err(EXIT_FAILURE, _("out of memory?"));
		}
		list[entries++] = wcsdup(buf);
	}
}

#ifdef HAVE_WIDECHAR
static wchar_t *mbs_to_wcs(const char *s)
{
	ssize_t n;
	wchar_t *wcs;

	n = mbstowcs((wchar_t *)0, s, 0);
	if (n < 0)
		return NULL;
	wcs = malloc((n + 1) * sizeof(wchar_t));
	if (!wcs)
		return NULL;
	n = mbstowcs(wcs, s, n + 1);
	if (n < 0)
		return NULL;
	return wcs;
}
#endif

#ifndef HAVE_WIDECHAR
static char *mtsafe_strtok(char *str, const char *delim, char **ptr)
{
	if (str == NULL) {
		str = *ptr;
		if (str == NULL)
			return NULL;
	}
	str += strspn(str, delim);
	if (*str == '\0') {
		*ptr = NULL;
		return NULL;
	} else {
		char *token_end = strpbrk(str, delim);
		if (token_end) {
			*token_end = '\0';
			*ptr = token_end + 1;
		} else
			*ptr = NULL;
		return str;
	}
}
#endif

static void *
emalloc(size)
        int size;
{
	char *p;

	if (!(p = malloc(size)))
		err(EXIT_FAILURE, _("out of memory?"));
	memset(p, 0, size);
	return (p);
}
