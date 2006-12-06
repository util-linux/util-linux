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
 * 1999-02-22 Arkadiusz Mi¶kiewicz <misiek@pld.ORG.PL>
 * 	added Native Language Support
 * 1999-09-19 Bruno Haible <haible@clisp.cons.org>
 * 	modified to work correctly in multi-byte locales
 */

#include <stdio.h>
#include <unistd.h>		/* for getopt(), isatty() */
#include <string.h>		/* for bzero(), strcpy() */
#include <term.h>		/* for setupterm() */
#include <stdlib.h>		/* for getenv() */
#include <limits.h>		/* for INT_MAX */
#include "nls.h"

#include "widechar.h"

#ifdef ENABLE_WIDECHAR
static int put1wc(int c) /* Output an ASCII character as a wide character */
{
  if (putwchar(c) == WEOF)
    return EOF;
  else
    return c;
}
#define putwp(s) tputs(s,1,put1wc)
#else
#define putwp(s) putp(s)
#endif

void filter(FILE *f);
void flushln(void);
void overstrike(void);
void iattr(void);
void initbuf(void);
void fwd(void);
void reverse(void);
void initinfo(void);
void outc(wint_t c, int width);
void setmode(int newmode);
static void setcol(int newcol);
static void needcol(int col);

#define	IESC	'\033'
#define	SO	'\016'
#define	SI	'\017'
#define	HFWD	'9'
#define	HREV	'8'
#define	FREV	'7'

#define	NORMAL	000
#define	ALTSET	001	/* Reverse */
#define	SUPERSC	002	/* Dim */
#define	SUBSC	004	/* Dim | Ul */
#define	UNDERL	010	/* Ul */
#define	BOLD	020	/* Bold */
#define	INITBUF	512

int	must_use_uc, must_overstrike;
char	*CURS_UP, *CURS_RIGHT, *CURS_LEFT,
	*ENTER_STANDOUT, *EXIT_STANDOUT, *ENTER_UNDERLINE, *EXIT_UNDERLINE,
	*ENTER_DIM, *ENTER_BOLD, *ENTER_REVERSE, *UNDER_CHAR, *EXIT_ATTRIBUTES;

struct	CHAR	{
	char	c_mode;
	wchar_t	c_char;
	int	c_width;
} ;

struct	CHAR	*obuf;
int	obuflen;		/* Tracks number of elements in obuf. */
int	col, maxcol;
int	mode;
int	halfpos;
int	upln;
int	iflag;

#define	PRINT(s)	if (s == NULL) /* void */; else putwp(s)

int main(int argc, char **argv)
{
	int c, ret;
	char *termtype;
	FILE *f;

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

	termtype = getenv("TERM");
	if (termtype == NULL || (argv[0][0] == 'c' && !isatty(1)))
		termtype = "lpr";
	while ((c = getopt(argc, argv, "it:T:")) != -1)
		switch(c) {

		case 't':
		case 'T': /* for nroff compatibility */
				termtype = optarg;
			break;
		case 'i':
			iflag = 1;
			break;

		default:
			fprintf(stderr,
				_("usage: %s [ -i ] [ -tTerm ] file...\n"),
				argv[0]);
			exit(1);
		}
	setupterm(termtype, 1, &ret);
	switch(ret) {

	case 1:
		break;

	default:
		fprintf(stderr,_("trouble reading terminfo"));
		/* fall through to ... */

	case 0:
		/* No such terminal type - assume dumb */
	        setupterm("dumb", 1, (int *)0);
		break;
	}
	initinfo();
	if (    (tigetflag("os") && ENTER_BOLD==NULL ) ||
		(tigetflag("ul") && ENTER_UNDERLINE==NULL && UNDER_CHAR==NULL))
			must_overstrike = 1;
	initbuf();
	if (optind == argc)
		filter(stdin);
	else for (; optind<argc; optind++) {
		f = fopen(argv[optind],"r");
		if (f == NULL) {
			perror(argv[optind]);
			exit(1);
		} else
			filter(f);
	}
	if (ferror(stdout) || fclose(stdout))
		return 1;
	return 0;
}

void filter(FILE *f)
{
	wint_t c;
	int i, w;

	while ((c = getwc(f)) != WEOF) switch(c) {

	case '\b':
		setcol(col - 1);
		continue;

	case '\t':
		setcol((col+8) & ~07);
		continue;

	case '\r':
		setcol(0);
		continue;

	case SO:
		mode |= ALTSET;
		continue;

	case SI:
		mode &= ~ALTSET;
		continue;

	case IESC:
		switch (c = getwc(f)) {

		case HREV:
			if (halfpos == 0) {
				mode |= SUPERSC;
				halfpos--;
			} else if (halfpos > 0) {
				mode &= ~SUBSC;
				halfpos--;
			} else {
				halfpos = 0;
				reverse();
			}
			continue;

		case HFWD:
			if (halfpos == 0) {
				mode |= SUBSC;
				halfpos++;
			} else if (halfpos < 0) {
				mode &= ~SUPERSC;
				halfpos++;
			} else {
				halfpos = 0;
				fwd();
			}
			continue;

		case FREV:
			reverse();
			continue;

		default:
			fprintf(stderr,
				_("Unknown escape sequence in input: %o, %o\n"),
				IESC, c);
			exit(1);
		}
		continue;

	case '_':
		if (obuf[col].c_char || obuf[col].c_width < 0) {
			while(col > 0 && obuf[col].c_width < 0)
				col--;
			w = obuf[col].c_width;
			for (i = 0; i < w; i++)
				obuf[col++].c_mode |= UNDERL | mode;
			setcol(col);
			continue;
		}
		obuf[col].c_char = '_';
		obuf[col].c_width = 1;
		/* fall through */
	case ' ':
		setcol(col + 1);
		continue;

	case '\n':
		flushln();
		continue;

	case '\f':
		flushln();
		putwchar('\f');
		continue;

	default:
		if (!iswprint(c))	/* non printing */
			continue;
		w = wcwidth(c);
		needcol(col + w);
		if (obuf[col].c_char == '\0') {
			obuf[col].c_char = c;
			for (i = 0; i < w; i++)
				obuf[col+i].c_mode = mode;
			obuf[col].c_width = w;
			for (i = 1; i < w; i++)
				obuf[col+i].c_width = -1;
		} else if (obuf[col].c_char == '_') {
			obuf[col].c_char = c;
			for (i = 0; i < w; i++)
				obuf[col+i].c_mode |= UNDERL|mode;
			obuf[col].c_width = w;
			for (i = 1; i < w; i++)
				obuf[col+i].c_width = -1;
		} else if (obuf[col].c_char == c) {
			for (i = 0; i < w; i++)
				obuf[col+i].c_mode |= BOLD|mode;
		} else {
			w = obuf[col].c_width;
			for (i = 0; i < w; i++)
				obuf[col+i].c_mode = mode;
		}
		setcol(col + w);
		continue;
	}
	if (maxcol)
		flushln();
}

void flushln(void)
{
	int lastmode;
	int i;
	int hadmodes = 0;

	lastmode = NORMAL;
	for (i=0; i<maxcol; i++) {
		if (obuf[i].c_mode != lastmode) {
			hadmodes++;
			setmode(obuf[i].c_mode);
			lastmode = obuf[i].c_mode;
		}
		if (obuf[i].c_char == '\0') {
			if (upln) {
				PRINT(CURS_RIGHT);
			} else
				outc(' ', 1);
		} else
			outc(obuf[i].c_char, obuf[i].c_width);
		if (obuf[i].c_width > 1)
			i += obuf[i].c_width -1;
	}
	if (lastmode != NORMAL) {
		setmode(0);
	}
	if (must_overstrike && hadmodes)
		overstrike();
	putwchar('\n');
	if (iflag && hadmodes)
		iattr();
	(void)fflush(stdout);
	if (upln)
		upln--;
	initbuf();
}

/*
 * For terminals that can overstrike, overstrike underlines and bolds.
 * We don't do anything with halfline ups and downs, or Greek.
 */
void overstrike(void)
{
	register int i;
#ifdef __GNUC__
	register wchar_t *lbuf = __builtin_alloca((maxcol+1)*sizeof(wchar_t));
#else
	wchar_t lbuf[256];
#endif
	register wchar_t *cp = lbuf;
	int hadbold=0;

	/* Set up overstrike buffer */
	for (i=0; i<maxcol; i++)
		switch (obuf[i].c_mode) {
		case NORMAL:
		default:
			*cp++ = ' ';
			break;
		case UNDERL:
			*cp++ = '_';
			break;
		case BOLD:
			*cp++ = obuf[i].c_char;
			if (obuf[i].c_width > 1)
				i += obuf[i].c_width - 1;
			hadbold=1;
			break;
		}
	putwchar('\r');
	for (*cp=' '; *cp==' '; cp--)
		*cp = 0;
	for (cp=lbuf; *cp; cp++)
		putwchar(*cp);
	if (hadbold) {
		putwchar('\r');
		for (cp=lbuf; *cp; cp++)
			putwchar(*cp=='_' ? ' ' : *cp);
		putwchar('\r');
		for (cp=lbuf; *cp; cp++)
			putwchar(*cp=='_' ? ' ' : *cp);
	}
}

void iattr(void)
{
	register int i;
#ifdef __GNUC__
	register char *lbuf = __builtin_alloca((maxcol+1)*sizeof(char));
#else
	char lbuf[256];
#endif
	register char *cp = lbuf;

	for (i=0; i<maxcol; i++)
		switch (obuf[i].c_mode) {
		case NORMAL:	*cp++ = ' '; break;
		case ALTSET:	*cp++ = 'g'; break;
		case SUPERSC:	*cp++ = '^'; break;
		case SUBSC:	*cp++ = 'v'; break;
		case UNDERL:	*cp++ = '_'; break;
		case BOLD:	*cp++ = '!'; break;
		default:	*cp++ = 'X'; break;
		}
	for (*cp=' '; *cp==' '; cp--)
		*cp = 0;
	for (cp=lbuf; *cp; cp++)
		putwchar(*cp);
	putwchar('\n');
}

void initbuf(void)
{
	if (obuf == NULL) {	/* First time. */
		obuflen = INITBUF;
		obuf = malloc(sizeof(struct CHAR) * obuflen);
		if (obuf == NULL) {
			fprintf(stderr, _("Unable to allocate buffer.\n"));
			exit(1);
		}
	}

	/* assumes NORMAL == 0 */
	memset(obuf, 0, sizeof(struct CHAR) * obuflen);
	setcol(0);
	maxcol = 0;
	mode &= ALTSET;
}

void fwd(void)
{
	int oldcol, oldmax;

	oldcol = col;
	oldmax = maxcol;
	flushln();
	setcol(oldcol);
	maxcol = oldmax;
}

void reverse(void)
{
	upln++;
	fwd();
	PRINT(CURS_UP);
	PRINT(CURS_UP);
	upln++;
}

void initinfo(void)
{
	CURS_UP =		tigetstr("cuu1");
	CURS_RIGHT =		tigetstr("cuf1");
	CURS_LEFT =		tigetstr("cub1");
	if (CURS_LEFT == NULL)
		CURS_LEFT =	"\b";

	ENTER_STANDOUT =	tigetstr("smso");
	EXIT_STANDOUT =		tigetstr("rmso");
	ENTER_UNDERLINE =	tigetstr("smul");
	EXIT_UNDERLINE =	tigetstr("rmul");
	ENTER_DIM =		tigetstr("dim");
	ENTER_BOLD =		tigetstr("bold");
	ENTER_REVERSE =		tigetstr("rev");
	EXIT_ATTRIBUTES =	tigetstr("sgr0");

	if (!ENTER_BOLD && ENTER_REVERSE)
		ENTER_BOLD = ENTER_REVERSE;
	if (!ENTER_BOLD && ENTER_STANDOUT)
		ENTER_BOLD = ENTER_STANDOUT;
	if (!ENTER_UNDERLINE && ENTER_STANDOUT) {
		ENTER_UNDERLINE = ENTER_STANDOUT;
		EXIT_UNDERLINE = EXIT_STANDOUT;
	}
	if (!ENTER_DIM && ENTER_STANDOUT)
		ENTER_DIM = ENTER_STANDOUT;
	if (!ENTER_REVERSE && ENTER_STANDOUT)
		ENTER_REVERSE = ENTER_STANDOUT;
	if (!EXIT_ATTRIBUTES && EXIT_STANDOUT)
		EXIT_ATTRIBUTES = EXIT_STANDOUT;
	
	/*
	 * Note that we use REVERSE for the alternate character set,
	 * not the as/ae capabilities.  This is because we are modelling
	 * the model 37 teletype (since that's what nroff outputs) and
	 * the typical as/ae is more of a graphics set, not the greek
	 * letters the 37 has.
	 */

	UNDER_CHAR =		tigetstr("uc");
	must_use_uc = (UNDER_CHAR && !ENTER_UNDERLINE);
}

static int curmode = 0;

void
outc(wint_t c, int width) {
	int i;

	putwchar(c);
	if (must_use_uc && (curmode&UNDERL)) {
		for (i=0; i<width; i++)
			PRINT(CURS_LEFT);
		for (i=0; i<width; i++)
			PRINT(UNDER_CHAR);
	}
}

void setmode(int newmode)
{
	if (!iflag) {
		if (curmode != NORMAL && newmode != NORMAL)
			setmode(NORMAL);
		switch (newmode) {
		case NORMAL:
			switch(curmode) {
			case NORMAL:
				break;
			case UNDERL:
				PRINT(EXIT_UNDERLINE);
				break;
			default:
				/* This includes standout */
				PRINT(EXIT_ATTRIBUTES);
				break;
			}
			break;
		case ALTSET:
			PRINT(ENTER_REVERSE);
			break;
		case SUPERSC:
			/*
			 * This only works on a few terminals.
			 * It should be fixed.
			 */
			PRINT(ENTER_UNDERLINE);
			PRINT(ENTER_DIM);
			break;
		case SUBSC:
			PRINT(ENTER_DIM);
			break;
		case UNDERL:
			PRINT(ENTER_UNDERLINE);
			break;
		case BOLD:
			PRINT(ENTER_BOLD);
			break;
		default:
			/*
			 * We should have some provision here for multiple modes
			 * on at once.  This will have to come later.
			 */
			PRINT(ENTER_STANDOUT);
			break;
		}
	}
	curmode = newmode;
}

static void
setcol(int newcol) {
	col = newcol;

	if (col < 0)
		col = 0;
	else if (col > maxcol)
		needcol(col);
}

static void
needcol(int col) {
	maxcol = col;

	/* If col >= obuflen, expand obuf until obuflen > col. */
	while (col >= obuflen) {
		/* Paranoid check for obuflen == INT_MAX. */
		if (obuflen == INT_MAX) {
			fprintf(stderr,
				_("Input line too long.\n"));
			exit(1);
		}

		/* Similar paranoia: double only up to INT_MAX. */
		obuflen = ((INT_MAX / 2) < obuflen)
			? INT_MAX
			: obuflen * 2;

		/* Now we can try to expand obuf. */
		obuf = realloc(obuf, sizeof(struct CHAR) * obuflen);
		if (obuf == NULL) {
			fprintf(stderr,
				_("Out of memory when growing buffer.\n"));
			exit(1);
		}
	}
}
