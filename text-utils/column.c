/*
 * Copyright (c) 1989, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * Copyright (C) 2017 Karel Zak <kzak@redhat.com>
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
#include "c.h"
#include "widechar.h"
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
#define MAXLINELEN  (LINE_MAX + 1)

typedef struct _tbl {
	wchar_t **list;
	int cols, *len;
} TBL;


enum {
	COLUMN_MODE_FILLCOLS = 0,
	COLUMN_MODE_FILLROWS,
	COLUMN_MODE_TABLE,
	COLUMN_MODE_SIMPLE
};

struct column_control {
	int	mode;		/* COLUMN_MODE_* */
	size_t	termwidth;

	wchar_t	**ents;		/* input entries */
	size_t	nents;		/* number of entries */
	size_t	maxlength;	/* longest input record (line) */
};

static int input(struct column_control *ctl, FILE *fp);
static void columnate_fillrows(struct column_control *ctl);
static void columnate_fillcols(struct column_control *ctl);
static wchar_t *local_wcstok(wchar_t *p, const wchar_t *separator, int greedy, wchar_t **wcstok_state);
static void maketbl(struct column_control *ctl, wchar_t *separator, int greedy, wchar_t *colsep);
static void print(struct column_control *ctl);

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
	fputs(_(" -t, --table                      create a table\n"), out);
	fputs(_(" -s, --separator <string>         possible table delimiters\n"), out);
	fputs(_(" -o, --output-separator <string>  columns separator for table output\n"
		"                                    (default is two spaces)\n"), out);
	fputs(_(" -c, --output-width <width>       width of output in number of characters\n"), out);
	fputs(_(" -x, --fillrows                   fill rows before columns\n"), out);
	fputs(USAGE_SEPARATOR, out);
	fputs(USAGE_HELP, out);
	fputs(USAGE_VERSION, out);
	fprintf(out, USAGE_MAN_TAIL("column(1)"));

	exit(rc);
}

int main(int argc, char **argv)
{
	struct column_control ctl = {
		.mode = COLUMN_MODE_FILLCOLS,
		.termwidth = 80
	};

	int ch;
	unsigned int eval = 0;		/* exit value */
	int greedy = 1;
	wchar_t *colsep;		/* table column output separator */

	/* field separator for table option */
	wchar_t default_separator[] = { '\t', ' ', 0 };
	wchar_t *separator = default_separator;

	static const struct option longopts[] =
	{
		{ "columns",          required_argument, NULL, 'c' }, /* deprecated */
		{ "fillrows",         no_argument,       NULL, 'x' },
		{ "help",             no_argument,       NULL, 'h' },
		{ "output-separator", required_argument, NULL, 'o' },
		{ "output-width",     required_argument, NULL, 'c' },
		{ "separator",        required_argument, NULL, 's' },
		{ "table",            no_argument,       NULL, 't' },
		{ "version",          no_argument,       NULL, 'V' },
		{ NULL,	0, NULL, 0 },
	};

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	ctl.termwidth = get_terminal_width(80);
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
			ctl.termwidth = strtou32_or_err(optarg, _("invalid columns argument"));
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
			ctl.mode = COLUMN_MODE_TABLE;
			break;
		case 'x':
			ctl.mode = COLUMN_MODE_FILLROWS;
			break;
		default:
			errtryhelp(EXIT_FAILURE);
	}
	argc -= optind;
	argv += optind;

	if (!*argv)
		eval += input(&ctl, stdin);
	else
		for (; *argv; ++argv) {
			FILE *fp;

			if ((fp = fopen(*argv, "r")) != NULL) {
				eval += input(&ctl, fp);
				fclose(fp);
			} else {
				warn("%s", *argv);
				eval += EXIT_FAILURE;
			}
		}

	if (!ctl.nents)
		exit(eval);

	if (ctl.mode != COLUMN_MODE_TABLE && ctl.maxlength >= ctl.termwidth)
		ctl.mode = COLUMN_MODE_SIMPLE;

	switch (ctl.mode) {
	case COLUMN_MODE_TABLE:
		maketbl(&ctl, separator, greedy, colsep);
		break;
	case COLUMN_MODE_FILLCOLS:
		columnate_fillcols(&ctl);
		break;
	case COLUMN_MODE_FILLROWS:
		columnate_fillrows(&ctl);
		break;
	case COLUMN_MODE_SIMPLE:
		print(&ctl);
		break;
	}

	return eval == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

static void columnate_fillrows(struct column_control *ctl)
{
	size_t chcnt, col, cnt, endcol, numcols;
	wchar_t **lp;

	ctl->maxlength = (ctl->maxlength + TAB) & ~(TAB - 1);
	numcols = ctl->termwidth / ctl->maxlength;
	endcol = ctl->maxlength;
	for (chcnt = col = 0, lp = ctl->ents;; ++lp) {
		fputws(*lp, stdout);
		chcnt += width(*lp);
		if (!--ctl->nents)
			break;
		if (++col == numcols) {
			chcnt = col = 0;
			endcol = ctl->maxlength;
			putwchar('\n');
		} else {
			while ((cnt = ((chcnt + TAB) & ~(TAB - 1))) <= endcol) {
				putwchar('\t');
				chcnt = cnt;
			}
			endcol += ctl->maxlength;
		}
	}
	if (chcnt)
		putwchar('\n');
}

static void columnate_fillcols(struct column_control *ctl)
{
	size_t base, chcnt, cnt, col, endcol, numcols, numrows, row;

	ctl->maxlength = (ctl->maxlength + TAB) & ~(TAB - 1);
	numcols = ctl->termwidth / ctl->maxlength;
	if (!numcols)
		numcols = 1;
	numrows = ctl->nents / numcols;
	if (ctl->nents % numcols)
		++numrows;

	for (row = 0; row < numrows; ++row) {
		endcol = ctl->maxlength;
		for (base = row, chcnt = col = 0; col < numcols; ++col) {
			fputws(ctl->ents[base], stdout);
			chcnt += width(ctl->ents[base]);
			if ((base += numrows) >= ctl->nents)
				break;
			while ((cnt = ((chcnt + TAB) & ~(TAB - 1))) <= endcol) {
				putwchar('\t');
				chcnt = cnt;
			}
			endcol += ctl->maxlength;
		}
		putwchar('\n');
	}
}

static void print(struct column_control *ctl)
{
	int cnt;
	wchar_t **lp;

	for (cnt = ctl->nents, lp = ctl->ents; cnt--; ++lp) {
		fputws(*lp, stdout);
		putwchar('\n');
	}
}

static wchar_t *local_wcstok(wchar_t *p, const wchar_t *separator, int greedy,
			     wchar_t **wcstok_state)
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

static void maketbl(struct column_control *ctl, wchar_t *separator, int greedy, wchar_t *colsep)
{
	TBL *t;
	size_t cnt;
	wchar_t *p, **lp;
	ssize_t *lens;
	ssize_t maxcols = DEFCOLS, coloff;
	TBL *tbl;
	wchar_t **cols;
	wchar_t *wcstok_state = NULL;

	t = tbl = xcalloc(ctl->nents, sizeof(TBL));
	cols = xcalloc(maxcols, sizeof(wchar_t *));
	lens = xcalloc(maxcols, sizeof(ssize_t));

	for (lp = ctl->ents, cnt = 0; cnt < ctl->nents; ++cnt, ++lp, ++t) {
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

	for (t = tbl, cnt = 0; cnt < ctl->nents; ++cnt, ++t) {
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

	for (cnt = 0; cnt < ctl->nents; ++cnt) {
		free((tbl+cnt)->list);
		free((tbl+cnt)->len);
	}
	free(cols);
	free(lens);
	free(tbl);
}

static int input(struct column_control *ctl, FILE *fp)
{
	char *buf = NULL;
	size_t bufsz = 0;
	size_t maxents = 0;

	do {
		char *str, *p;
		size_t len;

		if (getline(&buf, &bufsz, fp) < 0) {
			if (feof(fp))
				break;
			else
				err(EXIT_FAILURE, _("read failed"));
		}
		str = (char *) skip_space(buf);
		if (str) {
			p = strchr(str, '\n');
			if (p)
				*p = '\0';
		}
		if (!str || !*str)
			continue;

		switch (ctl->mode) {
		case COLUMN_MODE_TABLE:
			/* TODO: fill libsmartcols */

		case COLUMN_MODE_FILLCOLS:
		case COLUMN_MODE_FILLROWS:
			if (ctl->nents <= maxents) {
				maxents += 1000;
				ctl->ents = xrealloc(ctl->ents,
						maxents * sizeof(wchar_t *));
			}
			ctl->ents[ctl->nents] = mbs_to_wcs(str);
			if (!ctl->ents[ctl->nents])
				err(EXIT_FAILURE, _("read failed"));
			len = width(ctl->ents[ctl->nents]);
			if (ctl->maxlength < len)
				ctl->maxlength = len;
			ctl->nents++;
			break;
		}
	} while (1);

	return 0;
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
