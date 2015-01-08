/*-
 * Copyright (c) 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Michael Rendell of the Memorial University of Newfoundland.
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
 * Wed Jun 22 22:15:41 1994, faith@cs.unc.edu: Added internationalization
 *                           patches from Andries.Brouwer@cwi.nl
 * Wed Sep 14 22:31:17 1994: patches from Carl Christofferson
 *                           (cchris@connected.com)
 * 1999-02-22 Arkadiusz Miśkiewicz <misiek@pld.ORG.PL>
 * 	added Native Language Support
 * 1999-09-19 Bruno Haible <haible@clisp.cons.org>
 * 	modified to work correctly in multi-byte locales
 *
 */

/*
 * This command is deprecated.  The utility is in maintenance mode,
 * meaning we keep them in source tree for backward compatibility
 * only.  Do not waste time making this command better, unless the
 * fix is about security or other very critical issue.
 *
 * See Documentation/deprecated.txt for more information.
 */

#include <stdlib.h>
#include <errno.h>
#include <ctype.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <getopt.h>

#include "nls.h"
#include "xalloc.h"
#include "widechar.h"
#include "strutils.h"
#include "closestream.h"

#define	BS	'\b'		/* backspace */
#define	TAB	'\t'		/* tab */
#define	SPACE	' '		/* space */
#define	NL	'\n'		/* newline */
#define	CR	'\r'		/* carriage return */
#define	ESC	'\033'		/* escape */
#define	SI	'\017'		/* shift in to normal character set */
#define	SO	'\016'		/* shift out to alternate character set */
#define	VT	'\013'		/* vertical tab (aka reverse line feed) */
#define	RLF	'\007'		/* ESC-07 reverse line feed */
#define	RHLF	'\010'		/* ESC-010 reverse half-line feed */
#define	FHLF	'\011'		/* ESC-011 forward half-line feed */

/* build up at least this many lines before flushing them out */
#define	BUFFER_MARGIN		32

typedef char CSET;

typedef struct char_str {
#define	CS_NORMAL	1
#define	CS_ALTERNATE	2
	short		c_column;	/* column character is in */
	CSET		c_set;		/* character set (currently only 2) */
	wchar_t		c_char;		/* character in question */
	int		c_width;	/* character width */
} CHAR;

typedef struct line_str LINE;
struct line_str {
	CHAR	*l_line;		/* characters on the line */
	LINE	*l_prev;		/* previous line */
	LINE	*l_next;		/* next line */
	int	l_lsize;		/* allocated sizeof l_line */
	int	l_line_len;		/* strlen(l_line) */
	int	l_needs_sort;		/* set if chars went in out of order */
	int	l_max_col;		/* max column in the line */
};

void free_line(LINE *l);
void flush_line(LINE *l);
void flush_lines(int);
void flush_blanks(void);
LINE *alloc_line(void);

CSET last_set;			/* char_set of last char printed */
LINE *lines;
int compress_spaces;		/* if doing space -> tab conversion */
int fine;			/* if `fine' resolution (half lines) */
unsigned max_bufd_lines;	/* max # lines to keep in memory */
int nblank_lines;		/* # blanks after last flushed line */
int no_backspaces;		/* if not to output any backspaces */
int pass_unknown_seqs;		/* whether to pass unknown control sequences */

#define	PUTC(ch) \
	if (putwchar(ch) == WEOF) \
		wrerr();

static void __attribute__((__noreturn__)) usage(FILE *out)
{
	fprintf(out, _(
		"\nUsage:\n"
		" %s [options]\n"), program_invocation_short_name);

	fputs(USAGE_SEPARATOR, out);
	fputs(_("Filter out reverse line feeds.\n"), out);

	fprintf(out, _(
		"\nOptions:\n"
		" -b, --no-backspaces    do not output backspaces\n"
		" -f, --fine             permit forward half line feeds\n"
		" -p, --pass             pass unknown control sequences\n"
		" -h, --tabs             convert spaces to tabs\n"
		" -x, --spaces           convert tabs to spaces\n"
		" -l, --lines NUM        buffer at least NUM lines\n"
		" -V, --version          output version information and exit\n"
		" -H, --help             display this help and exit\n\n"));

	fprintf(out, _(
		"%s reads from standard input and writes to standard output\n\n"),
		program_invocation_short_name);

	fprintf(out, USAGE_MAN_TAIL("col(1)"));
	exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}

static void __attribute__((__noreturn__)) wrerr(void)
{
	errx(EXIT_FAILURE, _("write error"));
}

int main(int argc, char **argv)
{
	register wint_t ch;
	CHAR *c;
	CSET cur_set;			/* current character set */
	LINE *l;			/* current line */
	int extra_lines;		/* # of lines above first line */
	int cur_col;			/* current column */
	int cur_line;			/* line number of current position */
	int max_line;			/* max value of cur_line */
	int this_line;			/* line l points to */
	int nflushd_lines;		/* number of lines that were flushed */
	int adjust, opt, warned;
	int ret = EXIT_SUCCESS;

	static const struct option longopts[] = {
		{ "no-backspaces", no_argument,		0, 'b' },
		{ "fine",	   no_argument,		0, 'f' },
		{ "pass",	   no_argument,		0, 'p' },
		{ "tabs",	   no_argument,		0, 'h' },
		{ "spaces",	   no_argument,		0, 'x' },
		{ "lines",	   required_argument,	0, 'l' },
		{ "version",	   no_argument,		0, 'V' },
		{ "help",	   no_argument,		0, 'H' },
		{ NULL, 0, 0, 0 }
	};

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	max_bufd_lines = 128 * 2;
	compress_spaces = 1;		/* compress spaces into tabs */
	pass_unknown_seqs = 0;          /* remove unknown escape sequences */

	while ((opt = getopt_long(argc, argv, "bfhl:pxVH", longopts, NULL)) != -1)
		switch (opt) {
		case 'b':		/* do not output backspaces */
			no_backspaces = 1;
			break;
		case 'f':		/* allow half forward line feeds */
			fine = 1;
			break;
		case 'h':		/* compress spaces into tabs */
			compress_spaces = 1;
			break;
		case 'l':
			/*
			 * Buffered line count, which is a value in half
			 * lines e.g. twice the amount specified.
			 */
			max_bufd_lines = strtou32_or_err(optarg, _("bad -l argument")) * 2;
			break;
		case 'p':
			pass_unknown_seqs = 1;
			break;
		case 'x':		/* do not compress spaces into tabs */
			compress_spaces = 0;
			break;
		case 'V':
			printf(UTIL_LINUX_VERSION);
			return EXIT_SUCCESS;
		case 'H':
			usage(stdout);
		default:
			usage(stderr);
		}

	if (optind != argc)
		usage(stderr);

	adjust = cur_col = extra_lines = warned = 0;
	cur_line = max_line = nflushd_lines = this_line = 0;
	cur_set = last_set = CS_NORMAL;
	lines = l = alloc_line();

	while (feof(stdin) == 0) {
		errno = 0;
		if ((ch = getwchar()) == WEOF) {
			if (errno == EILSEQ) {
				warn(NULL);
				ret = EXIT_FAILURE;
			}
			break;
		}
		if (!iswgraph(ch)) {
			switch (ch) {
			case BS:		/* can't go back further */
				if (cur_col == 0)
					continue;
				--cur_col;
				continue;
			case CR:
				cur_col = 0;
				continue;
			case ESC:		/* just ignore EOF */
				switch(getwchar()) {
				case RLF:
					cur_line -= 2;
					break;
				case RHLF:
					cur_line--;
					break;
				case FHLF:
					cur_line++;
					if (cur_line > max_line)
						max_line = cur_line;
				}
				continue;
			case NL:
				cur_line += 2;
				if (cur_line > max_line)
					max_line = cur_line;
				cur_col = 0;
				continue;
			case SPACE:
				++cur_col;
				continue;
			case SI:
				cur_set = CS_NORMAL;
				continue;
			case SO:
				cur_set = CS_ALTERNATE;
				continue;
			case TAB:		/* adjust column */
				cur_col |= 7;
				++cur_col;
				continue;
			case VT:
				cur_line -= 2;
				continue;
			}
			if (iswspace(ch)) {
				if (wcwidth(ch) > 0)
					cur_col += wcwidth(ch);
				continue;
			}
			if (!pass_unknown_seqs)
				continue;
		}

		/* Must stuff ch in a line - are we at the right one? */
		if (cur_line != this_line - adjust) {
			LINE *lnew;
			int nmove;

			adjust = 0;
			nmove = cur_line - this_line;
			if (!fine) {
				/* round up to next line */
				if (cur_line & 1) {
					adjust = 1;
					nmove++;
				}
			}
			if (nmove < 0) {
				for (; nmove < 0 && l->l_prev; nmove++)
					l = l->l_prev;
				if (nmove) {
					if (nflushd_lines == 0) {
						/*
						 * Allow backup past first
						 * line if nothing has been
						 * flushed yet.
						 */
						for (; nmove < 0; nmove++) {
							lnew = alloc_line();
							l->l_prev = lnew;
							lnew->l_next = l;
							l = lines = lnew;
							extra_lines++;
						}
					} else {
						if (!warned++)
							warnx(
			_("warning: can't back up %s."), cur_line < 0 ?
			_("past first line") : _("-- line already flushed"));
						cur_line -= nmove;
					}
				}
			} else {
				/* may need to allocate here */
				for (; nmove > 0 && l->l_next; nmove--)
					l = l->l_next;
				for (; nmove > 0; nmove--) {
					lnew = alloc_line();
					lnew->l_prev = l;
					l->l_next = lnew;
					l = lnew;
				}
			}
			this_line = cur_line + adjust;
			nmove = this_line - nflushd_lines;
			if (nmove > 0
			    && (unsigned) nmove >= max_bufd_lines + BUFFER_MARGIN) {
				nflushd_lines += nmove - max_bufd_lines;
				flush_lines(nmove - max_bufd_lines);
			}
		}
		/* grow line's buffer? */
		if (l->l_line_len + 1 >= l->l_lsize) {
			int need;

			need = l->l_lsize ? l->l_lsize * 2 : 90;
			l->l_line = (CHAR *)xrealloc((void *) l->l_line,
						    (unsigned) need * sizeof(CHAR));
			l->l_lsize = need;
		}
		c = &l->l_line[l->l_line_len++];
		c->c_char = ch;
		c->c_set = cur_set;
		c->c_column = cur_col;
		c->c_width = wcwidth(ch);
		/*
		 * If things are put in out of order, they will need sorting
		 * when it is flushed.
		 */
		if (cur_col < l->l_max_col)
			l->l_needs_sort = 1;
		else
			l->l_max_col = cur_col;
		if (c->c_width > 0)
			cur_col += c->c_width;
	}
	/* goto the last line that had a character on it */
	for (; l->l_next; l = l->l_next)
		this_line++;
	flush_lines(this_line - nflushd_lines + extra_lines + 1);

	/* make sure we leave things in a sane state */
	if (last_set != CS_NORMAL)
		PUTC('\017');

	/* flush out the last few blank lines */
	nblank_lines = max_line - this_line;
	if (max_line & 1)
		nblank_lines++;
	else if (!nblank_lines)
		/* missing a \n on the last line? */
		nblank_lines = 2;
	flush_blanks();
	return ret;
}

void flush_lines(int nflush)
{
	LINE *l;

	while (--nflush >= 0) {
		l = lines;
		lines = l->l_next;
		if (l->l_line) {
			flush_blanks();
			flush_line(l);
		}
		nblank_lines++;
		free((void *)l->l_line);
		free_line(l);
	}
	if (lines)
		lines->l_prev = NULL;
}

/*
 * Print a number of newline/half newlines.  If fine flag is set, nblank_lines
 * is the number of half line feeds, otherwise it is the number of whole line
 * feeds.
 */
void flush_blanks(void)
{
	int half, i, nb;

	half = 0;
	nb = nblank_lines;
	if (nb & 1) {
		if (fine)
			half = 1;
		else
			nb++;
	}
	nb /= 2;
	for (i = nb; --i >= 0;)
		PUTC('\n');
	if (half) {
		PUTC('\033');
		PUTC('9');
		if (!nb)
			PUTC('\r');
	}
	nblank_lines = 0;
}

/*
 * Write a line to stdout taking care of space to tab conversion (-h flag)
 * and character set shifts.
 */
void flush_line(LINE *l)
{
	CHAR *c, *endc;
	int nchars, last_col, this_col;

	last_col = 0;
	nchars = l->l_line_len;

	if (l->l_needs_sort) {
		static CHAR *sorted;
		static int count_size, *count, i, save, sorted_size, tot;

		/*
		 * Do an O(n) sort on l->l_line by column being careful to
		 * preserve the order of characters in the same column.
		 */
		if (l->l_lsize > sorted_size) {
			sorted_size = l->l_lsize;
			sorted = (CHAR *)xrealloc((void *)sorted,
						  (unsigned)sizeof(CHAR) * sorted_size);
		}
		if (l->l_max_col >= count_size) {
			count_size = l->l_max_col + 1;
			count = (int *)xrealloc((void *)count,
			    (unsigned)sizeof(int) * count_size);
		}
		memset(count, 0, sizeof(int) * l->l_max_col + 1);
		for (i = nchars, c = l->l_line; --i >= 0; c++)
			count[c->c_column]++;

		/*
		 * calculate running total (shifted down by 1) to use as
		 * indices into new line.
		 */
		for (tot = 0, i = 0; i <= l->l_max_col; i++) {
			save = count[i];
			count[i] = tot;
			tot += save;
		}

		for (i = nchars, c = l->l_line; --i >= 0; c++)
			sorted[count[c->c_column]++] = *c;
		c = sorted;
	} else
		c = l->l_line;
	while (nchars > 0) {
		this_col = c->c_column;
		endc = c;
		do {
			++endc;
		} while (--nchars > 0 && this_col == endc->c_column);

		/* if -b only print last character */
		if (no_backspaces) {
			c = endc - 1;
			if (nchars > 0 &&
			    this_col + c->c_width > endc->c_column)
				continue;
		}

		if (this_col > last_col) {
			int nspace = this_col - last_col;

			if (compress_spaces && nspace > 1) {
				int ntabs;

				ntabs = this_col / 8 - last_col / 8;
				if (ntabs > 0) {
					nspace = this_col & 7;
					while (--ntabs >= 0)
						PUTC('\t');
				}
			}
			while (--nspace >= 0)
				PUTC(' ');
			last_col = this_col;
		}

		for (;;) {
			if (c->c_set != last_set) {
				switch (c->c_set) {
				case CS_NORMAL:
					PUTC('\017');
					break;
				case CS_ALTERNATE:
					PUTC('\016');
				}
				last_set = c->c_set;
			}
			PUTC(c->c_char);
			if ((c + 1) < endc) {
				int i;
				for (i=0; i < c->c_width; i++)
					PUTC('\b');
			}
			if (++c >= endc)
				break;
		}
		last_col += (c - 1)->c_width;
	}
}

#define	NALLOC 64

static LINE *line_freelist;

LINE *
alloc_line(void)
{
	LINE *l;
	int i;

	if (!line_freelist) {
		l = xmalloc(sizeof(LINE) * NALLOC);
		line_freelist = l;
		for (i = 1; i < NALLOC; i++, l++)
			l->l_next = l + 1;
		l->l_next = NULL;
	}
	l = line_freelist;
	line_freelist = l->l_next;

	memset(l, 0, sizeof(LINE));
	return l;
}

void free_line(LINE *l)
{
	l->l_next = line_freelist;
	line_freelist = l;
}
