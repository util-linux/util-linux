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
 * 1999-02-22 Arkadiusz Miśkiewicz <misiek@pld.ORG.PL>
 * 	added Native Language Support
 * 1999-09-19 Bruno Haible <haible@clisp.cons.org>
 * 	modified to work correctly in multi-byte locales
 */

#include <sys/types.h>
#include <sys/ioctl.h>

#include <ctype.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>

#include "nls.h"
#include "widechar.h"
#include "c.h"
#include "xalloc.h"
#include "strutils.h"
#include "closestream.h"
#include "ttyutils.h"

#ifdef HAVE_WIDECHAR
static wchar_t *mbs_to_wcs(const char *);
#else
#define mbs_to_wcs(s) xstrdup(s)
static char *mtsafe_strtok(char *, const char *, char **);
#define wcstok mtsafe_strtok
#endif

#define DEFCOLS     25
#define TAB         8
#define DEFNUM      1000
#define MAXLINELEN  (LINE_MAX + 1)

static int input(FILE *fp, int *maxlength, wchar_t ***list, int *entries);
static void c_columnate(int maxlength, long termwidth, wchar_t **list, int entries);
static void r_columnate(int maxlength, long termwidth, wchar_t **list, int entries);
static wchar_t *local_wcstok(wchar_t *p, const wchar_t *separator, int greedy, wchar_t **wcstok_state);
static void maketbl(wchar_t **list, int entries, wchar_t *separator, int greedy, wchar_t *colsep);
static void print(wchar_t **list, int entries);

typedef struct _tbl {
	wchar_t **list;
	int cols, *len;
} TBL;


#ifdef HAVE_WIDECHAR
/* Don't use wcswidth(), we need to ignore non-printable chars. */
static int width(const wchar_t *str)
{
	int x, width = 0;

	for (; *str != '\0'; str++) {
		x = wcwidth(*str);
		if (x > 0)
			width += x;
	}
	return width;
}
#else
static int width(const char *str)
{
	int width = 0;

	for (; *str != '\0'; str++) {
		if (isprint(*str))
			width++;
	}
	return width;
}
#endif

static void __attribute__((__noreturn__)) usage(int rc)
{
	FILE *out = rc == EXIT_FAILURE ? stderr : stdout;

	fputs(USAGE_HEADER, out);
	fprintf(out, _(" %s [options] [<file>...]\n"), program_invocation_short_name);

	fputs(USAGE_SEPARATOR, out);
	fputs(_("Columnate lists.\n"), out);

	fputs(USAGE_OPTIONS, out);
	fputs(_(" -c, --columns <width>    width of output in number of characters\n"), out);
	fputs(_(" -t, --table              create a table\n"), out);
	fputs(_(" -s, --separator <string> possible table delimiters\n"), out);
	fputs(_(" -o, --output-separator <string>\n"
	        "                          columns separator for table output; default is two spaces\n"), out);
	fputs(_(" -x, --fillrows           fill rows before columns\n"), out);
	fputs(USAGE_SEPARATOR, out);
	fputs(USAGE_HELP, out);
	fputs(USAGE_VERSION, out);
	fprintf(out, USAGE_MAN_TAIL("column(1)"));

	exit(rc);
}

int main(int argc, char **argv)
{
	int ch, tflag = 0, xflag = 0;
	int i;
	int termwidth = 80;
	int entries = 0;		/* number of records */
	unsigned int eval = 0;		/* exit value */
	int maxlength = 0;		/* longest record */
	wchar_t **list = NULL;		/* array of pointers to records */
	int greedy = 1;
	wchar_t *colsep;		/* table column output separator */

	/* field separator for table option */
	wchar_t default_separator[] = { '\t', ' ', 0 };
	wchar_t *separator = default_separator;

	static const struct option longopts[] =
	{
		{ "help",	0, 0, 'h' },
		{ "version",    0, 0, 'V' },
		{ "columns",	1, 0, 'c' },
		{ "table",	0, 0, 't' },
		{ "separator",	1, 0, 's' },
		{ "output-separator", 1, 0, 'o' },
		{ "fillrows",	0, 0, 'x' },
		{ NULL,		0, 0, 0 },
	};

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	termwidth = get_terminal_width(80);
	colsep = mbs_to_wcs("  ");

	while ((ch = getopt_long(argc, argv, "hVc:s:txo:", longopts, NULL)) != -1)
		switch(ch) {
		case 'h':
			usage(EXIT_SUCCESS);
			break;
		case 'V':
			printf(UTIL_LINUX_VERSION);
			return EXIT_SUCCESS;
		case 'c':
			termwidth = strtou32_or_err(optarg, _("invalid columns argument"));
			break;
		case 's':
			separator = mbs_to_wcs(optarg);
			greedy = 0;
			break;
		case 'o':
			free(colsep);
			colsep = mbs_to_wcs(optarg);
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
		eval += input(stdin, &maxlength, &list, &entries);
	else
		for (; *argv; ++argv) {
			FILE *fp;

			if ((fp = fopen(*argv, "r")) != NULL) {
				eval += input(fp, &maxlength, &list, &entries);
				fclose(fp);
			} else {
				warn("%s", *argv);
				eval += EXIT_FAILURE;
			}
		}

	if (!entries)
		exit(eval);

	if (tflag)
		maketbl(list, entries, separator, greedy, colsep);
	else if (maxlength >= termwidth)
		print(list, entries);
	else if (xflag)
		c_columnate(maxlength, termwidth, list, entries);
	else
		r_columnate(maxlength, termwidth, list, entries);

	for (i = 0; i < entries; i++)
		free(list[i]);
	free(list);

	if (eval == 0)
		return EXIT_SUCCESS;
	else
		return EXIT_FAILURE;
}

static void c_columnate(int maxlength, long termwidth, wchar_t **list, int entries)
{
	int chcnt, col, cnt, endcol, numcols;
	wchar_t **lp;

	maxlength = (maxlength + TAB) & ~(TAB - 1);
	numcols = termwidth / maxlength;
	endcol = maxlength;
	for (chcnt = col = 0, lp = list;; ++lp) {
		fputws(*lp, stdout);
		chcnt += width(*lp);
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

static void r_columnate(int maxlength, long termwidth, wchar_t **list, int entries)
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
			chcnt += width(list[base]);
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

static void print(wchar_t **list, int entries)
{
	int cnt;
	wchar_t **lp;

	for (cnt = entries, lp = list; cnt--; ++lp) {
		fputws(*lp, stdout);
		putwchar('\n');
	}
}

wchar_t *local_wcstok(wchar_t * p, const wchar_t * separator, int greedy,
		      wchar_t ** wcstok_state)
{
	wchar_t *result;
	if (greedy)
		return wcstok(p, separator, wcstok_state);

	if (p == NULL) {
		if (*wcstok_state == NULL)
			return NULL;
		else
			p = *wcstok_state;
	}
	result = p;
	p = wcspbrk(result, separator);
	if (p == NULL)
		*wcstok_state = NULL;
	else {
		*p = '\0';
		*wcstok_state = p + 1;
	}
	return result;
}

static void maketbl(wchar_t **list, int entries, wchar_t *separator, int greedy, wchar_t *colsep)
{
	TBL *t;
	int cnt;
	wchar_t *p, **lp;
	ssize_t *lens;
	ssize_t maxcols = DEFCOLS, coloff;
	TBL *tbl;
	wchar_t **cols;
	wchar_t *wcstok_state = NULL;

	t = tbl = xcalloc(entries, sizeof(TBL));
	cols = xcalloc(maxcols, sizeof(wchar_t *));
	lens = xcalloc(maxcols, sizeof(ssize_t));

	for (lp = list, cnt = 0; cnt < entries; ++cnt, ++lp, ++t) {
		coloff = 0;
		p = *lp;
		while ((cols[coloff] = local_wcstok(p, separator, greedy, &wcstok_state)) != NULL) {
			if (++coloff == maxcols) {
				maxcols += DEFCOLS;
				cols = xrealloc(cols, maxcols * sizeof(wchar_t *));
				lens = xrealloc(lens, maxcols * sizeof(ssize_t));
				/* zero fill only new memory */
				memset(lens + (maxcols - DEFCOLS), 0,
				       DEFCOLS * sizeof(*lens));
			}
			p = NULL;
		}
		t->list = xcalloc(coloff, sizeof(wchar_t *));
		t->len = xcalloc(coloff, sizeof(int));
		for (t->cols = coloff; --coloff >= 0;) {
			t->list[coloff] = cols[coloff];
			t->len[coloff] = width(cols[coloff]);
			if (t->len[coloff] > lens[coloff])
				lens[coloff] = t->len[coloff];
		}
	}

	for (t = tbl, cnt = 0; cnt < entries; ++cnt, ++t) {
		for (coloff = 0; coloff < t->cols - 1; ++coloff) {
			fputws(t->list[coloff], stdout);
#ifdef HAVE_WIDECHAR
			wprintf(L"%*s", lens[coloff] - t->len[coloff], "");
#else
			printf("%*s", (int) lens[coloff] - t->len[coloff], "");
#endif
			fputws(colsep, stdout);
		}
		if (coloff < t->cols) {
			fputws(t->list[coloff], stdout);
			putwchar('\n');
		}
	}

	for (cnt = 0; cnt < entries; ++cnt) {
		free((tbl+cnt)->list);
		free((tbl+cnt)->len);
	}
	free(cols);
	free(lens);
	free(tbl);
}

static int input(FILE *fp, int *maxlength, wchar_t ***list, int *entries)
{
	static int maxentry = DEFNUM;
	int len, lineno = 1, reportedline = 0, eval = 0;
	wchar_t *p, buf[MAXLINELEN];
	wchar_t **local_list = *list;
	int local_entries = *entries;

	if (!local_list)
		local_list = xcalloc(maxentry, sizeof(wchar_t *));

	while (1) {
		if (fgetws(buf, MAXLINELEN, fp) == NULL) {
			if (feof(fp))
				break;
			else
				err(EXIT_FAILURE, _("read failed"));
		}
		for (p = buf; *p && iswspace(*p); ++p)
			;
		if (!*p)
			continue;
		if (!(p = wcschr(p, '\n')) && !feof(fp)) {
			if (reportedline < lineno) {
				warnx(_("line %d is too long, output will be truncated"),
					lineno);
				reportedline = lineno;
			}
			eval = 1;
			continue;
		}
		lineno++;
		if (!feof(fp) && p)
			*p = '\0';
		len = width(buf);	/* len = p - buf; */
		if (*maxlength < len)
			*maxlength = len;
		if (local_entries == maxentry) {
			maxentry += DEFNUM;
			local_list = xrealloc(local_list,
				(u_int)maxentry * sizeof(wchar_t *));
		}
		local_list[local_entries++] = wcsdup(buf);
	}

	*list = local_list;
	*entries = local_entries;

	return eval;
}

#ifdef HAVE_WIDECHAR
static wchar_t *mbs_to_wcs(const char *s)
{
	ssize_t n;
	wchar_t *wcs;

	n = mbstowcs((wchar_t *)0, s, 0);
	if (n < 0)
		return NULL;
	wcs = xmalloc((n + 1) * sizeof(wchar_t));
	n = mbstowcs(wcs, s, n + 1);
	if (n < 0) {
		free(wcs);
		return NULL;
	}
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
