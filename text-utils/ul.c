/*
 * Copyright (c) 1980, 1993
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
 * modified by Kars de Jong <jongk@cs.utwente.nl>
 *	to use terminfo instead of termcap.
 * 1999-02-22 Arkadiusz Miśkiewicz <misiek@pld.ORG.PL>
 * 	added Native Language Support
 * 1999-09-19 Bruno Haible <haible@clisp.cons.org>
 * 	modified to work correctly in multi-byte locales
 */

#include <stdio.h>
#include <unistd.h>		/* for getopt(), isatty() */
#include <string.h>		/* for memset(), strcpy() */
#include <stdlib.h>		/* for getenv() */
#include <limits.h>		/* for INT_MAX */
#include <signal.h>		/* for signal() */
#include <errno.h>
#include <getopt.h>

#if defined(HAVE_NCURSESW_TERM_H)
# include <ncursesw/term.h>
#elif defined(HAVE_NCURSES_TERM_H)
# include <ncurses/term.h>
#elif defined(HAVE_TERM_H)
# include <term.h>
#endif

#include "nls.h"
#include "xalloc.h"
#include "widechar.h"
#include "c.h"
#include "closestream.h"

#ifdef HAVE_WIDECHAR
/* Output an ASCII character as a wide character */
static int put1wc(int c)
{
	if (putwchar(c) == WEOF)
		return EOF;

	return c;
}

# define putwp(s) tputs(s, STDOUT_FILENO, put1wc)
#else
# define putwp(s) putp(s)
#endif

#define	IESC	'\033'
#define	SO	'\016'
#define	SI	'\017'
#define	HFWD	'9'
#define	HREV	'8'
#define	FREV	'7'

enum {
	NORMAL 	= 0,		/* Must be zero, see initbuf() */
	ALTSET 	= 1 << 0,	/* Reverse */
	SUPERSC = 1 << 1,	/* Dim */
	SUBSC	= 1 << 2,	/* Dim | Ul */
	UNDERL	= 1 << 3,	/* Ul */
	BOLD	= 1 << 4,	/* Bold */
};

struct term_caps {
	char *curs_up;
	char *curs_right;
	char *curs_left;
	char *enter_standout;
	char *exit_standout;
	char *enter_underline;
	char *exit_underline;
	char *enter_dim;
	char *enter_bold;
	char *enter_reverse;
	char *under_char;
	char *exit_attributes;
};

struct ul_char {
	wchar_t	c_char;
	int	c_width;
	char	c_mode;
};

struct ul_ctl {
	int obuflen;
	int col;
	int maxcol;
	int mode;
	int halfpos;
	int upln;
	int curmode;
	struct ul_char *obuf;
	unsigned int
		iflag:1,
		must_use_uc:1,
		must_overstrike:1;
};

static void __attribute__((__noreturn__)) usage(void)
{
	FILE *out = stdout;

	fputs(USAGE_HEADER, out);
	fprintf(out, _(" %s [options] [<file> ...]\n"), program_invocation_short_name);

	fputs(USAGE_SEPARATOR, out);
	fputs(_("Do underlining.\n"), out);

	fputs(USAGE_OPTIONS, out);
	fputs(_(" -t, -T, --terminal TERMINAL  override the TERM environment variable\n"), out);
	fputs(_(" -i, --indicated              underlining is indicated via a separate line\n"), out);
	fputs(USAGE_SEPARATOR, out);
	printf(USAGE_HELP_OPTIONS(30));

	printf(USAGE_MAN_TAIL("ul(1)"));

	exit(EXIT_SUCCESS);
}

static void needcol(struct ul_ctl *ctl, int acol)
{
	ctl->maxcol = acol;

	/* If col >= obuflen, expand obuf until obuflen > col. */
	while (acol >= ctl->obuflen) {
		/* Paranoid check for obuflen == INT_MAX. */
		if (ctl->obuflen == INT_MAX)
			errx(EXIT_FAILURE, _("Input line too long."));

		/* Similar paranoia: double only up to INT_MAX. */
		if (ctl->obuflen < (INT_MAX / 2))
			ctl->obuflen *= 2;
		else
			ctl->obuflen = INT_MAX;

		/* Now we can try to expand obuf. */
		ctl->obuf = xrealloc(ctl->obuf, sizeof(struct ul_char) * ctl->obuflen);
	}
}

static void setcol(struct ul_ctl *ctl, int newcol)
{
	ctl->col = newcol;

	if (ctl->col < 0)
		ctl->col = 0;
	else if (ctl->col > ctl->maxcol)
		needcol(ctl, ctl->col);
}

static void initbuf(struct ul_ctl *ctl)
{
	if (ctl->obuf == NULL) {
		/* First time. */
		ctl->obuflen = BUFSIZ;
		ctl->obuf = xcalloc(ctl->obuflen, sizeof(struct ul_char));
	} else
		/* assumes NORMAL == 0 */
		memset(ctl->obuf, 0, sizeof(struct ul_char) * ctl->maxcol);

	setcol(ctl, 0);
	ctl->maxcol = 0;
	ctl->mode &= ALTSET;
}

static void initinfo(struct ul_ctl *ctl, struct term_caps *const tcs)
{
	tcs->curs_up		= tigetstr("cuu1");
	tcs->curs_right		= tigetstr("cuf1");
	tcs->curs_left		= tigetstr("cub1");
	if (tcs->curs_left == NULL)
		tcs->curs_left = "\b";

	tcs->enter_standout	= tigetstr("smso");
	tcs->exit_standout	= tigetstr("rmso");
	tcs->enter_underline	= tigetstr("smul");
	tcs->exit_underline	= tigetstr("rmul");
	tcs->enter_dim		= tigetstr("dim");
	tcs->enter_bold		= tigetstr("bold");
	tcs->enter_reverse	= tigetstr("rev");
	tcs->exit_attributes	= tigetstr("sgr0");

	if (!tcs->enter_bold && tcs->enter_reverse)
		tcs->enter_bold	= tcs->enter_reverse;

	if (!tcs->enter_bold && tcs->enter_standout)
		tcs->enter_bold	= tcs->enter_standout;

	if (!tcs->enter_underline && tcs->enter_standout) {
		tcs->enter_underline	= tcs->enter_standout;
		tcs->exit_underline	= tcs->exit_standout;
	}

	if (!tcs->enter_dim && tcs->enter_standout)
		tcs->enter_dim 	= tcs->enter_standout;

	if (!tcs->enter_reverse && tcs->enter_standout)
		tcs->enter_reverse	= tcs->enter_standout;

	if (!tcs->exit_attributes && tcs->exit_standout)
		tcs->exit_attributes	= tcs->exit_standout;

	/*
	 * Note that we use REVERSE for the alternate character set,
	 * not the as/ae capabilities.  This is because we are modeling
	 * the model 37 teletype (since that's what nroff outputs) and
	 * the typical as/ae is more of a graphics set, not the greek
	 * letters the 37 has.
	 */
	tcs->under_char = tigetstr("uc");
	ctl->must_use_uc = (tcs->under_char && !tcs->enter_underline);

	if ((tigetflag("os") && tcs->enter_bold == NULL) ||
	    (tigetflag("ul") && tcs->enter_underline == NULL && tcs->under_char == NULL))
		ctl->must_overstrike = 1;
}

static void sig_handler(int signo __attribute__((__unused__)))
{
	_exit(EXIT_SUCCESS);
}

static void print_out(char *line)
{
	if (line == NULL)
		return;

	putwp(line);
}

static void xsetmode(struct ul_ctl *ctl, struct term_caps const *const tcs, int newmode)
{
	if (!ctl->iflag) {
		if (ctl->curmode != NORMAL && newmode != NORMAL)
			xsetmode(ctl, tcs, NORMAL);
		switch (newmode) {
		case NORMAL:
			switch (ctl->curmode) {
			case NORMAL:
				break;
			case UNDERL:
				print_out(tcs->exit_underline);
				break;
			default:
				/* This includes standout */
				print_out(tcs->exit_attributes);
				break;
			}
			break;
		case ALTSET:
			print_out(tcs->enter_reverse);
			break;
		case SUPERSC:
			/*
			 * This only works on a few terminals.
			 * It should be fixed.
			 */
			print_out(tcs->enter_underline);
			print_out(tcs->enter_dim);
			break;
		case SUBSC:
			print_out(tcs->enter_dim);
			break;
		case UNDERL:
			print_out(tcs->enter_underline);
			break;
		case BOLD:
			print_out(tcs->enter_bold);
			break;
		default:
			/*
			 * We should have some provision here for multiple modes
			 * on at once.  This will have to come later.
			 */
			print_out(tcs->enter_standout);
			break;
		}
	}
	ctl->curmode = newmode;
}

static void iattr(struct ul_ctl *ctl)
{
	int i;
	wchar_t *lbuf = xcalloc(ctl->maxcol + 1, sizeof(wchar_t));
	wchar_t *cp = lbuf;

	for (i = 0; i < ctl->maxcol; i++)
		switch (ctl->obuf[i].c_mode) {
		case NORMAL:	*cp++ = ' '; break;
		case ALTSET:	*cp++ = 'g'; break;
		case SUPERSC:	*cp++ = '^'; break;
		case SUBSC:	*cp++ = 'v'; break;
		case UNDERL:	*cp++ = '_'; break;
		case BOLD:	*cp++ = '!'; break;
		default:	*cp++ = 'X'; break;
		}
	for (*cp = ' '; *cp == ' '; cp--)
		*cp = 0;
	fputws(lbuf, stdout);
	putwchar('\n');
	free(lbuf);
}

static void outc(struct ul_ctl *ctl, struct term_caps const *const tcs, wint_t c, int width)
{
	int i;

	putwchar(c);
	if (ctl->must_use_uc && (ctl->curmode & UNDERL)) {
		for (i = 0; i < width; i++)
			print_out(tcs->curs_left);
		for (i = 0; i < width; i++)
			print_out(tcs->under_char);
	}
}

/*
 * For terminals that can overstrike, overstrike underlines and bolds.
 * We don't do anything with halfline ups and downs, or Greek.
 */
static void overstrike(struct ul_ctl *ctl)
{
	int i;
	wchar_t *lbuf = xcalloc(ctl->maxcol + 1, sizeof(wchar_t));
	wchar_t *cp = lbuf;
	int hadbold = 0;

	/* Set up overstrike buffer */
	for (i = 0; i < ctl->maxcol; i++)
		switch (ctl->obuf[i].c_mode) {
		case NORMAL:
		default:
			*cp++ = ' ';
			break;
		case UNDERL:
			*cp++ = '_';
			break;
		case BOLD:
			*cp++ = ctl->obuf[i].c_char;
			if (ctl->obuf[i].c_width > 1)
				i += ctl->obuf[i].c_width - 1;
			hadbold = 1;
			break;
		}
	putwchar('\r');
	for (*cp = ' '; *cp == ' '; cp--)
		*cp = 0;
	fputws(lbuf, stdout);
	if (hadbold) {
		putwchar('\r');
		for (cp = lbuf; *cp; cp++)
			putwchar(*cp == '_' ? ' ' : *cp);
		putwchar('\r');
		for (cp = lbuf; *cp; cp++)
			putwchar(*cp == '_' ? ' ' : *cp);
	}
	free(lbuf);
}

static void flushln(struct ul_ctl *ctl, struct term_caps const *const tcs)
{
	int lastmode;
	int i;
	int hadmodes = 0;

	lastmode = NORMAL;
	for (i = 0; i < ctl->maxcol; i++) {
		if (ctl->obuf[i].c_mode != lastmode) {
			hadmodes++;
			xsetmode(ctl, tcs, ctl->obuf[i].c_mode);
			lastmode = ctl->obuf[i].c_mode;
		}
		if (ctl->obuf[i].c_char == '\0') {
			if (ctl->upln)
				print_out(tcs->curs_right);
			else
				outc(ctl, tcs, ' ', 1);
		} else
			outc(ctl, tcs, ctl->obuf[i].c_char, ctl->obuf[i].c_width);
		if (ctl->obuf[i].c_width > 1)
			i += ctl->obuf[i].c_width - 1;
	}
	if (lastmode != NORMAL)
		xsetmode(ctl, tcs, NORMAL);
	if (ctl->must_overstrike && hadmodes)
		overstrike(ctl);
	putwchar('\n');
	if (ctl->iflag && hadmodes)
		iattr(ctl);
	fflush(stdout);
	if (ctl->upln)
		ctl->upln--;
	initbuf(ctl);
}

static void fwd(struct ul_ctl *ctl, struct term_caps const *const tcs)
{
	int oldcol, oldmax;

	oldcol = ctl->col;
	oldmax = ctl->maxcol;
	flushln(ctl, tcs);
	setcol(ctl, oldcol);
	ctl->maxcol = oldmax;
}

static void reverse(struct ul_ctl *ctl, struct term_caps const *const tcs)
{
	ctl->upln++;
	fwd(ctl, tcs);
	print_out(tcs->curs_up);
	print_out(tcs->curs_up);
	ctl->upln++;
}

static int handle_escape(struct ul_ctl *ctl, struct term_caps const *const tcs, FILE *f)
{
	wint_t c;

	switch (c = getwc(f)) {
	case HREV:
		if (ctl->halfpos == 0) {
			ctl->mode |= SUPERSC;
			ctl->halfpos--;
		} else if (ctl->halfpos > 0) {
			ctl->mode &= ~SUBSC;
			ctl->halfpos--;
		} else {
			ctl->halfpos = 0;
			reverse(ctl, tcs);
		}
		return 0;
	case HFWD:
		if (ctl->halfpos == 0) {
			ctl->mode |= SUBSC;
			ctl->halfpos++;
		} else if (ctl->halfpos < 0) {
			ctl->mode &= ~SUPERSC;
			ctl->halfpos++;
		} else {
			ctl->halfpos = 0;
			fwd(ctl, tcs);
		}
		return 0;
	case FREV:
		reverse(ctl, tcs);
		return 0;
	default:
		/* unknown escape */
		ungetwc(c, f);
		return 1;
	}
}

static void filter(struct ul_ctl *ctl, struct term_caps const *const tcs, FILE *f)
{
	wint_t c;
	int i, w;

	while ((c = getwc(f)) != WEOF) {
		switch (c) {
		case '\b':
			setcol(ctl, ctl->col - 1);
			continue;
		case '\t':
			setcol(ctl, (ctl->col + 8) & ~07);
			continue;
		case '\r':
			setcol(ctl, 0);
			continue;
		case SO:
			ctl->mode |= ALTSET;
			continue;
		case SI:
			ctl->mode &= ~ALTSET;
			continue;
		case IESC:
			if (handle_escape(ctl, tcs, f)) {
				c = getwc(f);
				errx(EXIT_FAILURE,
				     _("unknown escape sequence in input: %o, %o"), IESC, c);
			}
			continue;
		case '_':
			if (ctl->obuf[ctl->col].c_char || ctl->obuf[ctl->col].c_width < 0) {
				while (ctl->col > 0 && ctl->obuf[ctl->col].c_width < 0)
					ctl->col--;
				w = ctl->obuf[ctl->col].c_width;
				for (i = 0; i < w; i++)
					ctl->obuf[ctl->col++].c_mode |= UNDERL | ctl->mode;
				setcol(ctl, ctl->col);
				continue;
			}
			ctl->obuf[ctl->col].c_char = '_';
			ctl->obuf[ctl->col].c_width = 1;
			/* fallthrough */
		case ' ':
			setcol(ctl, ctl->col + 1);
			continue;
		case '\n':
			flushln(ctl, tcs);
			continue;
		case '\f':
			flushln(ctl, tcs);
			putwchar('\f');
			continue;
		default:
			if (!iswprint(c))
				/* non printable */
				continue;
			w = wcwidth(c);
			needcol(ctl, ctl->col + w);
			if (ctl->obuf[ctl->col].c_char == '\0') {
				ctl->obuf[ctl->col].c_char = c;
				for (i = 0; i < w; i++)
					ctl->obuf[ctl->col + i].c_mode = ctl->mode;
				ctl->obuf[ctl->col].c_width = w;
				for (i = 1; i < w; i++)
					ctl->obuf[ctl->col + i].c_width = -1;
			} else if (ctl->obuf[ctl->col].c_char == '_') {
				ctl->obuf[ctl->col].c_char = c;
				for (i = 0; i < w; i++)
					ctl->obuf[ctl->col + i].c_mode |= UNDERL | ctl->mode;
				ctl->obuf[ctl->col].c_width = w;
				for (i = 1; i < w; i++)
					ctl->obuf[ctl->col + i].c_width = -1;
			} else if ((wint_t) ctl->obuf[ctl->col].c_char == c) {
				for (i = 0; i < w; i++)
					ctl->obuf[ctl->col + i].c_mode |= BOLD | ctl->mode;
			} else {
				w = ctl->obuf[ctl->col].c_width;
				for (i = 0; i < w; i++)
					ctl->obuf[ctl->col + i].c_mode = ctl->mode;
			}
			setcol(ctl, ctl->col + w);
			continue;
		}
	}
	if (ctl->maxcol)
		flushln(ctl, tcs);
}

int main(int argc, char **argv)
{
	int c, ret, tflag = 0;
	char *termtype;
	struct term_caps tcs = { 0 };
	struct ul_ctl ctl = { .curmode = NORMAL };
	FILE *f;

	static const struct option longopts[] = {
		{ "terminal",	required_argument,	NULL, 't' },
		{ "indicated",	no_argument,		NULL, 'i' },
		{ "version",	no_argument,		NULL, 'V' },
		{ "help",	no_argument,		NULL, 'h' },
		{ NULL, 0, NULL, 0 }
	};

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	close_stdout_atexit();

	signal(SIGINT, sig_handler);
	signal(SIGTERM, sig_handler);

	termtype = getenv("TERM");

	while ((c = getopt_long(argc, argv, "it:T:Vh", longopts, NULL)) != -1)
		switch (c) {

		case 't':
		case 'T':
			/* for nroff compatibility */
			termtype = optarg;
			tflag = 1;
			break;
		case 'i':
			ctl.iflag = 1;
			break;

		case 'V':
			print_version(EXIT_SUCCESS);
		case 'h':
			usage();
		default:
			errtryhelp(EXIT_FAILURE);
		}
	setupterm(termtype, STDOUT_FILENO, &ret);
	switch (ret) {

	case 1:
		break;

	default:
		warnx(_("trouble reading terminfo"));
		/* fallthrough */

	case 0:
		if (tflag)
			warnx(_("terminal `%s' is not known, defaulting to `dumb'"),
				termtype);
		setupterm("dumb", STDOUT_FILENO, (int *)0);
		break;
	}

	initinfo(&ctl, &tcs);
	initbuf(&ctl);
	if (optind == argc)
		filter(&ctl, &tcs, stdin);
	else
		for (; optind < argc; optind++) {
			f = fopen(argv[optind], "r");
			if (!f)
				err(EXIT_FAILURE, _("cannot open %s"), argv[optind]);
			filter(&ctl, &tcs, f);
			fclose(f);
		}
	free(ctl.obuf);
	return EXIT_SUCCESS;
}
