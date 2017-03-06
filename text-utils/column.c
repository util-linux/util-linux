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
#include "strv.h"

#include "libsmartcols.h"

#define TABCHAR_CELLS         8

enum {
	COLUMN_MODE_FILLCOLS = 0,
	COLUMN_MODE_FILLROWS,
	COLUMN_MODE_TABLE,
	COLUMN_MODE_SIMPLE
};

struct column_control {
	int	mode;		/* COLUMN_MODE_* */
	size_t	termwidth;

	struct libscols_table *tab;

	char **tab_colnames;	/* parsed -H option */
	const char *tab_name;

	wchar_t *input_separator;
	const char *output_separator;

	wchar_t	**ents;		/* input entries */
	size_t	nents;		/* number of entries */
	size_t	maxlength;	/* longest input record (line) */

	unsigned int greedy :1,
		     json :1;
};

static size_t width(const wchar_t *str)
{
	size_t width = 0;

	for (; *str != '\0'; str++) {
#ifdef HAVE_WIDECHAR
		int x = wcwidth(*str);	/* don't use wcswidth(), need to ignore non-printable */
		if (x > 0)
			width += x;
#else
		if (isprint(*str))
			width++;
#endif
	}
	return width;
}

static wchar_t *mbs_to_wcs(const char *s)
{
#ifdef HAVE_WIDECHAR
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
#else
	return xstrdup(s)
#endif
}

static char *wcs_to_mbs(const wchar_t *s)
{
#ifdef HAVE_WIDECHAR
	size_t n;
	char *str;

	n = wcstombs(NULL, s, 0);
	if (n == (size_t) -1)
		return NULL;

	str = xmalloc((n + 1));
	if (wcstombs(str, s, n) == (size_t) -1) {
		free(str);
		return NULL;
	}
	return str;
#else
	return xstrdup(s)
#endif
}

static wchar_t *local_wcstok(wchar_t *p, const wchar_t *separator, int greedy, wchar_t **state)
{
	wchar_t *result = NULL;

	if (greedy)
		return wcstok(p, separator, state);
	if (!p) {
		if (!*state || !**state)
			return NULL;
		p = *state;
	}
	result = p;
	p = wcspbrk(result, separator);
	if (!p)
		*state = NULL;
	else {
		*p = '\0';
		*state = p + 1;
	}
	return result;
}

static int parse_table_colnames(struct column_control *ctl, const char *names)
{
	ctl->tab_colnames = strv_split(names, ",");
	if (!ctl->tab_colnames) {
		if (errno == ENOMEM)
			err_oom();
		errx(EXIT_FAILURE, _("failed to parse column names"));
	}
	return 0;
}

static void init_table(struct column_control *ctl)
{
	scols_init_debug(0);

	ctl->tab = scols_new_table();
	if (!ctl->tab)
		err(EXIT_FAILURE, _("failed to allocate output table"));

	scols_table_set_column_separator(ctl->tab, ctl->output_separator);
	if (ctl->json) {
		scols_table_enable_json(ctl->tab, 1);
		scols_table_set_name(ctl->tab, ctl->tab_name ? : "table");
	}
	if (ctl->tab_colnames) {
		char **name;

		STRV_FOREACH(name, ctl->tab_colnames)
			scols_table_new_column(ctl->tab, *name, 0, 0);
	} else
		scols_table_enable_noheadings(ctl->tab, 1);
}

static int add_line_to_table(struct column_control *ctl, wchar_t *wcs)
{
	wchar_t *wcdata, *sv = NULL;
	size_t n = 0;
	struct libscols_line *ln = NULL;

	if (!ctl->tab)
		init_table(ctl);

	while ((wcdata = local_wcstok(wcs, ctl->input_separator, ctl->greedy, &sv))) {
		char *data;

		if (scols_table_get_ncols(ctl->tab) < n + 1)
			scols_table_new_column(ctl->tab, NULL, 0, 0);
		if (!ln) {
			ln = scols_table_new_line(ctl->tab, NULL);
			if (!ln)
				err(EXIT_FAILURE, _("failed to allocate output line"));
		}
		data = wcs_to_mbs(wcdata);
		if (!data)
			err(EXIT_FAILURE, _("failed to allocate output data"));
		if (scols_line_refer_data(ln, n, data))
			err(EXIT_FAILURE, _("failed to add output data"));
		n++;
		wcs = NULL;
	}

	return 0;
}

static int read_input(struct column_control *ctl, FILE *fp)
{
	char *buf = NULL;
	size_t bufsz = 0;
	size_t maxents = 0;
	int rc = 0;

	do {
		char *str, *p;
		wchar_t *wcs = NULL;
		size_t len;

		if (getline(&buf, &bufsz, fp) < 0) {
			if (feof(fp))
				break;
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

		wcs = mbs_to_wcs(str);
		if (!wcs)
			err(EXIT_FAILURE, _("read failed"));

		switch (ctl->mode) {
		case COLUMN_MODE_TABLE:
			rc = add_line_to_table(ctl, wcs);
			free(wcs);
			break;

		case COLUMN_MODE_FILLCOLS:
		case COLUMN_MODE_FILLROWS:
			if (ctl->nents <= maxents) {
				maxents += 1000;
				ctl->ents = xrealloc(ctl->ents,
						maxents * sizeof(wchar_t *));
			}
			ctl->ents[ctl->nents] = wcs;
			len = width(ctl->ents[ctl->nents]);
			if (ctl->maxlength < len)
				ctl->maxlength = len;
			ctl->nents++;
			break;
		}
	} while (rc == 0);

	return rc;
}


static void columnate_fillrows(struct column_control *ctl)
{
	size_t chcnt, col, cnt, endcol, numcols;
	wchar_t **lp;

	ctl->maxlength = (ctl->maxlength + TABCHAR_CELLS) & ~(TABCHAR_CELLS - 1);
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
			while ((cnt = ((chcnt + TABCHAR_CELLS) & ~(TABCHAR_CELLS - 1))) <= endcol) {
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

	ctl->maxlength = (ctl->maxlength + TABCHAR_CELLS) & ~(TABCHAR_CELLS - 1);
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
			while ((cnt = ((chcnt + TABCHAR_CELLS) & ~(TABCHAR_CELLS - 1))) <= endcol) {
				putwchar('\t');
				chcnt = cnt;
			}
			endcol += ctl->maxlength;
		}
		putwchar('\n');
	}
}

static void simple_print(struct column_control *ctl)
{
	int cnt;
	wchar_t **lp;

	for (cnt = ctl->nents, lp = ctl->ents; cnt--; ++lp) {
		fputws(*lp, stdout);
		putwchar('\n');
	}
}

static void __attribute__((__noreturn__)) usage(int rc)
{
	FILE *out = rc == EXIT_FAILURE ? stderr : stdout;

	fputs(USAGE_HEADER, out);
	fprintf(out, _(" %s [options] [<file>...]\n"), program_invocation_short_name);

	fputs(USAGE_SEPARATOR, out);
	fputs(_("Columnate lists.\n"), out);

	fputs(USAGE_OPTIONS, out);
	fputs(_(" -J, --json                       use JSON output format for table\n"), out);
	fputs(_(" -t, --table                      create a table\n"), out);
	fputs(_(" -N, --table-colnames <names>     comma separated columns names\n"), out);
	fputs(_(" -n, --table-name <name>          table name for JSON output\n"), out);
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

	static const struct option longopts[] =
	{
		{ "columns",             required_argument, NULL, 'c' }, /* deprecated */
		{ "json",                no_argument,       NULL, 'J' },
		{ "fillrows",            no_argument,       NULL, 'x' },
		{ "help",                no_argument,       NULL, 'h' },
		{ "output-separator",    required_argument, NULL, 'o' },
		{ "output-width",        required_argument, NULL, 'c' },
		{ "separator",           required_argument, NULL, 's' },
		{ "table",               no_argument,       NULL, 't' },
		{ "table-colnames",      required_argument, NULL, 'N' },
		{ "table-name",          required_argument, NULL, 'n' },
		{ "version",             no_argument,       NULL, 'V' },
		{ NULL,	0, NULL, 0 },
	};

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	ctl.termwidth = get_terminal_width(80);
	ctl.output_separator = "  ";
	ctl.input_separator = mbs_to_wcs("\t ");

	while ((ch = getopt_long(argc, argv, "hVc:Jn:N:s:txo:", longopts, NULL)) != -1)
		switch(ch) {
		case 'J':
			ctl.json = 1;
			ctl.mode = COLUMN_MODE_TABLE;
			break;
		case 'n':
			ctl.tab_name = optarg;
			break;
		case 'N':
			parse_table_colnames(&ctl, optarg);
			break;
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
			free(ctl.input_separator);
			ctl.input_separator = mbs_to_wcs(optarg);
			ctl.greedy = 0;
			break;
		case 'o':
			ctl.output_separator = optarg;
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

	if (ctl.tab_colnames == NULL && ctl.json)
		errx(EXIT_FAILURE, _("option --table-colnames required for --json"));

	if (!*argv)
		eval += read_input(&ctl, stdin);
	else
		for (; *argv; ++argv) {
			FILE *fp;

			if ((fp = fopen(*argv, "r")) != NULL) {
				eval += read_input(&ctl, fp);
				fclose(fp);
			} else {
				warn("%s", *argv);
				eval += EXIT_FAILURE;
			}
		}

	if (ctl.mode != COLUMN_MODE_TABLE) {
		if (!ctl.nents)
			exit(eval);
		if (ctl.maxlength >= ctl.termwidth)
			ctl.mode = COLUMN_MODE_SIMPLE;
	}

	switch (ctl.mode) {
	case COLUMN_MODE_TABLE:
		eval = scols_print_table(ctl.tab);
		break;
	case COLUMN_MODE_FILLCOLS:
		columnate_fillcols(&ctl);
		break;
	case COLUMN_MODE_FILLROWS:
		columnate_fillrows(&ctl);
		break;
	case COLUMN_MODE_SIMPLE:
		simple_print(&ctl);
		break;
	}

	return eval == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
