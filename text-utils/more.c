/*
 * Copyright (C) 1980 The Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that the above copyright notice and this paragraph are
 * duplicated in all such forms and that any documentation,
 * advertising materials, and other materials related to such
 * distribution and use acknowledge that the software was developed
 * by the University of California, Berkeley.  The name of the
 * University may not be used to endorse or promote products derived
 * from this software without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

/* more.c - General purpose tty output filter and file perusal program
 *
 * by Eric Shienbrood, UC Berkeley
 *
 * modified by Geoff Peck
 *	UCB to add underlining, single spacing
 * modified by John Foderaro
 *	UCB to add -c and MORE environment variable
 * modified by Erik Troan <ewt@redhat.com>
 *	to be more posix and so compile on linux/axp.
 * modified by Kars de Jong <jongk@cs.utwente.nl>
 *	to use terminfo instead of termcap.
 * 1999-02-22 Arkadiusz Miśkiewicz <misiek@pld.ORG.PL>
 *	added Native Language Support
 * 1999-03-19 Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 *	more nls translatable strings
 * 1999-05-09 aeb
 *	applied a RedHat patch (setjmp->sigsetjmp); without it a second
 *	^Z would fail.
 * 1999-05-09 aeb
 *	undone Kars' work, so that more works without libcurses (and
 *	hence can be in /bin with libcurses being in
 *	/usr/lib which may not be mounted).  However, when termcap is not
 *	present curses can still be used.
 * 2010-10-21 Davidlohr Bueso <dave@gnu.org>
 *	modified mem allocation handling for util-linux
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdarg.h>
#include <sys/param.h>
#include <ctype.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <termios.h>
#include <setjmp.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <regex.h>

#if defined(HAVE_NCURSESW_TERM_H)
# include <ncursesw/term.h>
#elif defined(HAVE_NCURSES_TERM_H)
# include <ncurses/term.h>
#elif defined(HAVE_TERM_H)
# include <term.h>
#endif

#include "strutils.h"
#include "nls.h"
#include "xalloc.h"
#include "widechar.h"
#include "closestream.h"

#ifdef TEST_PROGRAM
# define NON_INTERACTIVE_MORE 1
#endif

#define VI	"vi"	/* found on the user's path */

#define BS		"\b"
#define BSB		"\b \b"
#define CARAT		"^"
#define RINGBELL	'\007'

#define LINSIZ		256	/* minimal Line buffer size */
#define ctrl(letter)	(letter & 077)
#define RUBOUT		'\177'
#define ESC		'\033'
#define QUIT		'\034'
#define SCROLL_LEN	11
#define LINES_PER_PAGE	24
#define NUM_COLUMNS	80
#define TERMINAL_BUF	4096
#define INIT_BUF	80
#define SHELL_LINE	1000
#define COMMAND_BUF	200
#define REGERR_BUF	NUM_COLUMNS

#define TERM_AUTO_RIGHT_MARGIN    "am"
#define TERM_CEOL                 "xhp"
#define TERM_CLEAR                "clear"
#define TERM_CLEAR_TO_LINE_END    "el"
#define TERM_CLEAR_TO_SCREEN_END  "ed"
#define TERM_COLS                 "cols"
#define TERM_CURSOR_ADDRESS       "cup"
#define TERM_EAT_NEW_LINE         "xenl"
#define TERM_ENTER_UNDERLINE      "smul"
#define TERM_EXIT_STANDARD_MODE   "rmso"
#define TERM_EXIT_UNDERLINE       "rmul"
#define TERM_HARD_COPY            "hc"
#define TERM_HOME                 "home"
#define TERM_LINE_DOWN            "cud1"
#define TERM_LINES                "lines"
#define TERM_OVER_STRIKE          "os"
#define TERM_PAD_CHAR             "pad"
#define TERM_STANDARD_MODE        "smso"
#define TERM_STD_MODE_GLITCH      "xmc"
#define TERM_UNDERLINE_CHAR       "uc"
#define TERM_UNDERLINE            "ul"

struct more_control {
	struct termios otty;		/* output terminal */
	struct termios savetty0;	/* original terminal settings */
	long file_pos;			/* file position */
	long file_size;			/* file size */
	int fnum;			/* argv[] position */
	int dlines;			/* screen size in lines */
	int nscroll;			/* number of lines scrolled by 'd' */
	int promptlen;			/* message prompt length */
	int Currline;			/* line we are currently at */
	char **fnames;			/* The list of file names */
	int nfiles;			/* Number of files left to process */
	char *shell;			/* name of the shell to use */
	int shellp;			/* does previous shell command exist */
	sigjmp_buf restore;		/* siglongjmp() destination */
	char *Line;			/* line buffer */
	size_t LineLen;			/* size of Line buffer */
	int Lpp;			/* lines per page */
	char *Clear;			/* clear screen */
	char *eraseln;			/* erase line */
	char *Senter;			/* enter standout mode */
	char *Sexit;			/* exit standout mode */
	char *ULenter;			/* enter underline mode */
	char *ULexit;			/* exit underline mode */
	char *chUL;			/* underline character */
	char *chBS;			/* backspace character */
	char *Home;			/* go to home */
	char *cursorm;			/* cursor movement */
	char cursorhome[40];		/* contains cursor movement to home */
	char *EodClr;			/* clear rest of screen */
	int Mcol;			/* number of columns */
	char *previousre;		/* previous search() buf[] item */
	struct {
		long chrctr;		/* row number */
		long line;		/* line number */
	} context,
	  screen_start;
	int lastcmd;			/* previous more key command */
	int lastarg;			/* previous key command argument */
	int lastcolon;			/* is a colon-prefixed key command */
	char shell_line[SHELL_LINE];
	unsigned int
		bad_so:1,		/* true if overwriting does not turn off standout */
		catch_susp:1,		/* we should catch the SIGTSTP signal */
		clreol:1,		/* do not scroll, paint each screen from the top */
		docrterase:1,		/* is erase previous supported */
		docrtkill:1,		/* is erase input supported */
		dumb:1,			/* is terminal type known */
		dum_opt:1,		/* suppress bell */
		eatnl:1,		/* is newline ignored after 80 cols */
		errors:1,		/* is an error reported */
		firstf:1,		/* is the input file the first in list */
		fold_opt:1,		/* fold long lines */
		hard:1,			/* is this hard copy terminal (a printer or such) */
		hardtabs:1,		/* print spaces instead of '\t' */
		inwait:1,		/* is waiting user input */
		lastp:1,		/* run previous key command */
		no_intty:1,		/* is input in interactive mode */
		noscroll:1,		/* do not scroll, clear the screen and then display text */
		notell:1,		/* suppress quit dialog */
		no_tty:1,		/* is output in interactive mode */
		Pause:1,		/* is output paused */
		pstate:1,		/* current UL state */
		soglitch:1,		/* terminal has standout mode glitch */
		ssp_opt:1,		/* suppress white space */
		startup:1,		/* is startup completed */
		stop_opt:1,		/* stop after form feeds */
		ulglitch:1,		/* terminal has underline mode glitch */
		ul_opt:1,		/* underline as best we can */
		within:1,		/* true if we are within a file, false if we are between files */
		Wrap:1;			/* set if automargins */
};

/* FIXME: global_ctl is used in signal handlers. */
struct more_control *global_ctl;

static void putstring(char *s)
{
	tputs(s, fileno(stdout), putchar);	/* putp(s); */
}

static void __attribute__((__noreturn__)) usage(void)
{
	FILE *out = stdout;
	fputs(USAGE_HEADER, out);
	fprintf(out, _(" %s [options] <file>...\n"), program_invocation_short_name);

	fputs(USAGE_SEPARATOR, out);
	fputs(_("A file perusal filter for CRT viewing.\n"), out);

	fputs(USAGE_OPTIONS, out);
	fputs(_(" -d          display help instead of ringing bell\n"), out);
	fputs(_(" -f          count logical rather than screen lines\n"), out);
	fputs(_(" -l          suppress pause after form feed\n"), out);
	fputs(_(" -c          do not scroll, display text and clean line ends\n"), out);
	fputs(_(" -p          do not scroll, clean screen and display text\n"), out);
	fputs(_(" -s          squeeze multiple blank lines into one\n"), out);
	fputs(_(" -u          suppress underlining\n"), out);
	fputs(_(" -<number>   the number of lines per screenful\n"), out);
	fputs(_(" +<number>   display file beginning from line number\n"), out);
	fputs(_(" +/<string>  display file beginning from search string match\n"), out);

	fputs(USAGE_SEPARATOR, out);
	printf( "     --help     %s\n", USAGE_OPTSTR_HELP);
	printf( " -V, --version  %s\n", USAGE_OPTSTR_VERSION);
	printf(USAGE_MAN_TAIL("more(1)"));
	exit(EXIT_SUCCESS);
}


static void argscan(struct more_control *ctl, char *s)
{
	int seen_num = 0;

	while (*s != '\0') {
		switch (*s) {
		case '0':
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
		case '8':
		case '9':
			if (!seen_num) {
				ctl->dlines = 0;
				seen_num = 1;
			}
			ctl->dlines = ctl->dlines * 10 + *s - '0';
			break;
		case 'd':
			ctl->dum_opt = 1;
			break;
		case 'l':
			ctl->stop_opt = 0;
			break;
		case 'f':
			ctl->fold_opt = 0;
			break;
		case 'p':
			ctl->noscroll = 1;
			break;
		case 'c':
			ctl->clreol = 1;
			break;
		case 's':
			ctl->ssp_opt = 1;
			break;
		case 'u':
			ctl->ul_opt = 0;
			break;
		case '-':
		case ' ':
		case '\t':
			break;
		case 'V':
			printf(UTIL_LINUX_VERSION);
			exit(EXIT_SUCCESS);
			break;
		default:
			warnx(_("unknown option -%s"), s);
			errtryhelp(EXIT_FAILURE);
			break;
		}
		s++;
	}
}

static void more_fseek(struct more_control *ctl, FILE *stream, long pos)
{
	ctl->file_pos = pos;
	fseek(stream, pos, 0);
}

static int more_getc(struct more_control *ctl, FILE *stream)
{
	ctl->file_pos++;
	return getc(stream);
}

static int more_ungetc(struct more_control *ctl, int c, FILE *stream)
{
	ctl->file_pos--;
	return ungetc(c, stream);
}

/* force clear to end of line */
static void cleareol(struct more_control *ctl)
{
	putstring(ctl->eraseln);
}

/* magic --
 *	check for file magic numbers.  This code would best be shared
 *	with the file(1) program or, perhaps, more should not try to be
 *	so smart. */
static int magic(FILE *f, char *fs)
{
	signed char twobytes[2];

	/* don't try to look ahead if the input is unseekable */
	if (fseek(f, 0L, SEEK_SET))
		return 0;

	if (fread(twobytes, 2, 1, f) == 1) {
		switch (twobytes[0] + (twobytes[1] << 8)) {
		case 0407:	/* a.out obj */
		case 0410:	/* a.out exec */
		case 0413:	/* a.out demand exec */
		case 0405:
		case 0411:
		case 0177545:
		case 0x457f:	/* simple ELF detection */
			printf(_("\n******** %s: Not a text file ********\n\n"),
			       fs);
			return 1;
		}
	}
	fseek(f, 0L, SEEK_SET);	/* rewind() not necessary */
	return 0;
}

/* Check whether the file named by fs is an ASCII file which the user may
 * access.  If it is, return the opened file.  Otherwise return NULL. */
static FILE *checkf(struct more_control *ctl, register char *fs, int *clearfirst)
{
	struct stat stbuf;
	register FILE *f;
	int c;

	if (stat(fs, &stbuf) == -1) {
		fflush(stdout);
		if (ctl->clreol)
			cleareol(ctl);
		warn(_("stat of %s failed"), fs);
		return NULL;
	}
	if ((stbuf.st_mode & S_IFMT) == S_IFDIR) {
		printf(_("\n*** %s: directory ***\n\n"), fs);
		return NULL;
	}
	ctl->Currline = 0;
	ctl->file_pos = 0;
	if ((f = fopen(fs, "r")) == NULL) {
		fflush(stdout);
		warn(_("cannot open %s"), fs);
		return NULL;
	}
	if (magic(f, fs)) {
		fclose(f);
		return NULL;
	}
	fcntl(fileno(f), F_SETFD, FD_CLOEXEC);
	c = more_getc(ctl, f);
	*clearfirst = (c == '\f');
	more_ungetc(ctl, c, f);
	if ((ctl->file_size = stbuf.st_size) == 0)
		ctl->file_size = LONG_MAX;
	return f;
}

static void prepare_line_buffer(struct more_control *ctl)
{
	size_t nsz = ctl->Mcol * 4;

	if (ctl->LineLen >= nsz)
		return;

	if (nsz < LINSIZ)
		nsz = LINSIZ;

	/* alloc nsz and extra space for \n\0 */
	ctl->Line = xrealloc(ctl->Line, nsz + 2);
	ctl->LineLen = nsz;
}

/* Get a logical line */
static int get_line(struct more_control *ctl, register FILE *f, int *length)
{
	int c;
	char *p;
	int column;
	static int colflg;

#ifdef HAVE_WIDECHAR
	size_t i;
	wchar_t wc;
	int wc_width;
	mbstate_t state, state_bak;	/* Current status of the stream. */
	char mbc[MB_LEN_MAX];		/* Buffer for one multibyte char. */
	size_t mblength;		/* Byte length of multibyte char. */
	size_t mbc_pos = 0;		/* Position of the MBC. */
	int use_mbc_buffer_flag = 0;	/* If 1, mbc has data. */
	int break_flag = 0;		/* If 1, exit while(). */
	long file_pos_bak = ctl->file_pos;

	memset(&state, '\0', sizeof(mbstate_t));
#endif

	prepare_line_buffer(ctl);

	p = ctl->Line;
	column = 0;
	c = more_getc(ctl, f);
	if (colflg && c == '\n') {
		ctl->Currline++;
		c = more_getc(ctl, f);
	}
	while (p < &ctl->Line[ctl->LineLen - 1]) {
#ifdef HAVE_WIDECHAR
		if (ctl->fold_opt && use_mbc_buffer_flag && MB_CUR_MAX > 1) {
			use_mbc_buffer_flag = 0;
			state_bak = state;
			mbc[mbc_pos++] = c;
 process_mbc:
			mblength = mbrtowc(&wc, mbc, mbc_pos, &state);

			switch (mblength) {
			case (size_t)-2:	/* Incomplete multibyte character. */
				use_mbc_buffer_flag = 1;
				state = state_bak;
				break;

			case (size_t)-1:	/* Invalid as a multibyte character. */
				*p++ = mbc[0];
				state = state_bak;
				column++;
				file_pos_bak++;

				if (column >= ctl->Mcol) {
					more_fseek(ctl, f, file_pos_bak);
				} else {
					memmove(mbc, mbc + 1, --mbc_pos);
					if (mbc_pos > 0) {
						mbc[mbc_pos] = '\0';
						goto process_mbc;
					}
				}
				break;

			default:
				wc_width = wcwidth(wc);

				if (column + wc_width > ctl->Mcol) {
					more_fseek(ctl, f, file_pos_bak);
					break_flag = 1;
				} else {
					for (i = 0; p < &ctl->Line[ctl->LineLen - 1] &&
						    i < mbc_pos; i++)
						*p++ = mbc[i];
					if (wc_width > 0)
						column += wc_width;
				}
			}

			if (break_flag || column >= ctl->Mcol)
				break;

			c = more_getc(ctl, f);
			continue;
		}
#endif	/* HAVE_WIDECHAR */
		if (c == EOF) {
			if (p > ctl->Line) {
				*p = '\0';
				*length = p - ctl->Line;
				return column;
			}
			*length = p - ctl->Line;
			return EOF;
		}
		if (c == '\n') {
			ctl->Currline++;
			break;
		}

		*p++ = c;
		if (c == '\t') {
			if (!ctl->hardtabs || (column < ctl->promptlen && !ctl->hard)) {
				if (ctl->hardtabs && ctl->eraseln && !ctl->dumb) {
					column = 1 + (column | 7);
					putp(ctl->eraseln);
					ctl->promptlen = 0;
				} else {
					for (--p; p < &ctl->Line[ctl->LineLen - 1];) {
						*p++ = ' ';
						if ((++column & 7) == 0)
							break;
					}
					if (column >= ctl->promptlen)
						ctl->promptlen = 0;
				}
			} else
				column = 1 + (column | 7);
		} else if (c == '\b' && column > 0) {
			column--;
		} else if (c == '\r') {
			int next = more_getc(ctl, f);
			if (next == '\n') {
				p--;
				ctl->Currline++;
				break;
			}
			more_ungetc(ctl, c, f);
			column = 0;
		} else if (c == '\f' && ctl->stop_opt) {
			p[-1] = '^';
			*p++ = 'L';
			column += 2;
			ctl->Pause = 1;
		} else if (c == EOF) {
			*length = p - ctl->Line;
			return column;
		} else {
#ifdef HAVE_WIDECHAR
			if (ctl->fold_opt && MB_CUR_MAX > 1) {
				memset(mbc, '\0', MB_LEN_MAX);
				mbc_pos = 0;
				mbc[mbc_pos++] = c;
				state_bak = state;

				mblength = mbrtowc(&wc, mbc, mbc_pos, &state);
				/* The value of mblength is always less than 2 here. */
				switch (mblength) {
				case (size_t)-2:
					p--;
					file_pos_bak = ctl->file_pos - 1;
					state = state_bak;
					use_mbc_buffer_flag = 1;
					break;

				case (size_t)-1:
					state = state_bak;
					column++;
					break;

				default:
					wc_width = wcwidth(wc);
					if (wc_width > 0)
						column += wc_width;
				}
			} else
#endif	/* HAVE_WIDECHAR */
			{
				if (isprint(c))
					column++;
			}
		}

		if (column >= ctl->Mcol && ctl->fold_opt)
			break;
#ifdef HAVE_WIDECHAR
		if (use_mbc_buffer_flag == 0 && p >= &ctl->Line[ctl->LineLen - 1 - 4])
			/* don't read another char if there is no space for
			 * whole multibyte sequence */
			break;
#endif
		c = more_getc(ctl, f);
	}
	if (column >= ctl->Mcol && ctl->Mcol > 0) {
		if (!ctl->Wrap) {
			*p++ = '\n';
		}
	}
	colflg = column == ctl->Mcol && ctl->fold_opt;
	if (colflg && ctl->eatnl && ctl->Wrap) {
		*p++ = '\n';	/* simulate normal wrap */
	}
	*length = p - ctl->Line;
	*p = 0;
	return column;
}

static void clreos(struct more_control *ctl)
{
	putstring(ctl->EodClr);
}

/* Erase the rest of the prompt, assuming we are starting at column col. */
static void erasep(struct more_control *ctl, register int col)
{

	if (ctl->promptlen == 0)
		return;
	if (ctl->hard) {
		putchar('\n');
	} else {
		if (col == 0)
			putchar('\r');
		if (!ctl->dumb && ctl->eraseln)
			putstring(ctl->eraseln);
		else
			printf("%*s", ctl->promptlen - col, "");
	}
	ctl->promptlen = 0;
}

#ifdef HAVE_WIDECHAR
static UL_ASAN_BLACKLIST size_t xmbrtowc(wchar_t *wc, const char *s, size_t n,
				  mbstate_t *mbstate)
{
	const size_t mblength = mbrtowc(wc, s, n, mbstate);
	if (mblength == (size_t)-2 || mblength == (size_t)-1)
		return 1;
	return mblength;
}
#endif

static int wouldul(char *s, int n)
{
	if (n < 2)
		return 0;
	if ((s[0] == '_' && s[1] == '\b') || (s[1] == '\b' && s[2] == '_'))
		return 1;
	return 0;
}

/* Print a buffer of n characters */
static void prbuf(struct more_control *ctl, register char *s, register int n)
{
	register char c;	/* next output character */
	register int state;	/* next output char's UL state */

	while (--n >= 0)
		if (!ctl->ul_opt)
			putchar(*s++);
		else {
			if (*s == ' ' && ctl->pstate == 0 && ctl->ulglitch
			    && wouldul(s + 1, n - 1)) {
				s++;
				continue;
			}
			if ((state = wouldul(s, n)) != 0) {
				c = (*s == '_') ? s[2] : *s;
				n -= 2;
				s += 3;
			} else
				c = *s++;
			if (state != ctl->pstate) {
				if (c == ' ' && state == 0 && ctl->ulglitch
				    && wouldul(s, n - 1))
					state = 1;
				else
					putp(state ? ctl->ULenter : ctl->ULexit);
			}
			if (c != ' ' || ctl->pstate == 0 || state != 0
			    || ctl->ulglitch == 0)
#ifdef HAVE_WIDECHAR
			{
				wchar_t wc;
				size_t mblength;
				mbstate_t mbstate;
				memset(&mbstate, '\0', sizeof(mbstate_t));
				s--;
				n++;
				mblength = xmbrtowc(&wc, s, n, &mbstate);
				while (mblength--)
					putchar(*s++);
				n += mblength;
			}
#else
				putchar(c);
#endif				/* HAVE_WIDECHAR */
			if (state && *ctl->chUL) {
				fputs(ctl->chBS, stdout);
				putstring(ctl->chUL);
			}
			ctl->pstate = state;
		}
}

/* Erase the current line entirely */
static void kill_line(struct more_control *ctl)
{
	erasep(ctl, 0);
	if (!ctl->eraseln || ctl->dumb)
		putchar('\r');
}

static void prompt(struct more_control *ctl, char *filename)
{
	if (ctl->clreol)
		cleareol(ctl);
	else if (ctl->promptlen > 0)
		kill_line(ctl);
	if (!ctl->hard) {
		ctl->promptlen = 0;
		if (ctl->Senter && ctl->Sexit) {
			putstring(ctl->Senter);
			ctl->promptlen += (2 * ctl->soglitch);
		}
		if (ctl->clreol)
			cleareol(ctl);
		ctl->promptlen += printf(_("--More--"));
		if (filename != NULL) {
			ctl->promptlen += printf(_("(Next file: %s)"), filename);
		} else if (!ctl->no_intty) {
			ctl->promptlen +=
			    printf("(%d%%)",
				   (int)((ctl->file_pos * 100) / ctl->file_size));
		}
		if (ctl->dum_opt) {
			ctl->promptlen +=
			    printf(_("[Press space to continue, 'q' to quit.]"));
		}
		if (ctl->Senter && ctl->Sexit)
			putstring(ctl->Sexit);
		if (ctl->clreol)
			clreos(ctl);
		fflush(stdout);
	} else
		fputc(RINGBELL, stderr);
	ctl->inwait++;
}

static int ourputch(int c)
{
	return putc(c, stdout);
}

static void reset_tty(void)
{
	if (global_ctl->no_tty)
		return;
	if (global_ctl->pstate) {
		tputs(global_ctl->ULexit, fileno(stdout), ourputch);
		fflush(stdout);
		global_ctl->pstate = 0;
	}
	global_ctl->otty.c_lflag |= ICANON | ECHO;
	global_ctl->otty.c_cc[VMIN] = global_ctl->savetty0.c_cc[VMIN];
	global_ctl->otty.c_cc[VTIME] = global_ctl->savetty0.c_cc[VTIME];
	tcsetattr(STDERR_FILENO, TCSANOW, &global_ctl->savetty0);
}

/* Clean up terminal state and exit. Also come here if interrupt signal received */
static void __attribute__((__noreturn__)) end_it(int dummy __attribute__((__unused__)))
{
	/* May be executed as a signal handler as well as by main process.
	 *
	 * The _exit() may wait for pending I/O for really long time, be sure
	 * that signal handler is not executed in this time to avoid double
	 * de-initialization (free() calls, etc.).
	 */
	signal(SIGINT, SIG_IGN);

	reset_tty();
	if (global_ctl->clreol) {
		putchar('\r');
		clreos(global_ctl);
		fflush(stdout);
	} else if (!global_ctl->clreol && (global_ctl->promptlen > 0)) {
		kill_line(global_ctl);
		fflush(stdout);
	} else
		fputc('\n', stderr);
	free(global_ctl->previousre);
	free(global_ctl->Line);
	_exit(EXIT_SUCCESS);
}

static int readch(struct more_control *ctl)
{
	unsigned char c;

	errno = 0;
	if (read(fileno(stderr), &c, 1) <= 0) {
		if (errno != EINTR)
			end_it(0);
		else
			c = ctl->otty.c_cc[VKILL];
	}
	return c;
}

/* Read a decimal number from the terminal.  Set cmd to the non-digit
 * which terminates the number. */
static int number(struct more_control *ctl, char *cmd)
{
	register int i;
	char ch;

	i = 0;
	ch = ctl->otty.c_cc[VKILL];
	for (;;) {
		ch = readch(ctl);
		if (isdigit(ch))
			i = i * 10 + ch - '0';
		else if ((cc_t) ch == ctl->otty.c_cc[VKILL])
			i = 0;
		else {
			*cmd = ch;
			break;
		}
	}
	return i;
}

/* Skip nskip files in the file list (from the command line).  Nskip may
 * be negative. */
static void skipf(struct more_control *ctl, register int nskip)
{
	if (nskip == 0)
		return;
	if (nskip > 0) {
		if (ctl->fnum + nskip > ctl->nfiles - 1)
			nskip = ctl->nfiles - ctl->fnum - 1;
	} else if (ctl->within)
		ctl->fnum++;
	ctl->fnum += nskip;
	if (ctl->fnum < 0)
		ctl->fnum = 0;
	puts(_("\n...Skipping "));
	if (ctl->clreol)
		cleareol(ctl);
	if (nskip > 0)
		fputs(_("...Skipping to file "), stdout);
	else
		fputs(_("...Skipping back to file "), stdout);
	puts(ctl->fnames[ctl->fnum]);
	if (ctl->clreol)
		cleareol(ctl);
	putchar('\n');
	ctl->fnum--;
}

static void show(struct more_control *ctl, char c)
{
	if ((c < ' ' && c != '\n' && c != ESC) || c == RUBOUT) {
		c += (c == RUBOUT) ? -0100 : 0100;
		fputs(CARAT, stderr);
		ctl->promptlen++;
	}
	fputc(c, stderr);
	ctl->promptlen++;
}

static void more_error(struct more_control *ctl, char *mess)
{
	if (ctl->clreol)
		cleareol(ctl);
	else
		kill_line(ctl);
	ctl->promptlen += strlen(mess);
	if (ctl->Senter && ctl->Sexit) {
		putp(ctl->Senter);
		fputs(mess, stdout);
		putp(ctl->Sexit);
	} else
		fputs(mess, stdout);
	fflush(stdout);
	ctl->errors++;
	siglongjmp(ctl->restore, 1);
}

static void erase_one_column(struct more_control *ctl)
{
	if (ctl->docrterase)
		fputs(BSB, stderr);
	else
		fputs(BS, stderr);
}

static void ttyin(struct more_control *ctl, char buf[], register int nmax, char pchar)
{
	char *sp;
	int c;
	int slash = 0;
	int maxlen;

	sp = buf;
	maxlen = 0;
	while (sp - buf < nmax) {
		if (ctl->promptlen > maxlen)
			maxlen = ctl->promptlen;
		c = readch(ctl);
		if (c == '\\') {
			slash++;
		} else if (((cc_t) c == ctl->otty.c_cc[VERASE]) && !slash) {
			if (sp > buf) {
#ifdef HAVE_WIDECHAR
				if (MB_CUR_MAX > 1) {
					wchar_t wc;
					size_t pos = 0, mblength;
					mbstate_t state, state_bak;

					memset(&state, '\0', sizeof(mbstate_t));

					while (1) {
						state_bak = state;
						mblength =
						    mbrtowc(&wc, buf + pos,
							    sp - buf, &state);

						state = (mblength == (size_t)-2
							 || mblength ==
							 (size_t)-1) ? state_bak
						    : state;
						mblength =
						    (mblength == (size_t)-2
						     || mblength == (size_t)-1
						     || mblength ==
						     0) ? 1 : mblength;

						if (buf + pos + mblength >= sp)
							break;

						pos += mblength;
					}

					if (mblength == 1) {
						erase_one_column(ctl);
					} else {
						int wc_width;
						wc_width = wcwidth(wc);
						wc_width =
						    (wc_width <
						     1) ? 1 : wc_width;
						while (wc_width--) {
							erase_one_column(ctl);
						}
					}

					while (mblength--) {
						--ctl->promptlen;
						--sp;
					}
				} else
#endif	/* HAVE_WIDECHAR */
				{
					--ctl->promptlen;
					erase_one_column(ctl);
					--sp;
				}

				if ((*sp < ' ' && *sp != '\n') || *sp == RUBOUT) {
					--ctl->promptlen;
					erase_one_column(ctl);
				}
				continue;
			} else {
				if (!ctl->eraseln)
					ctl->promptlen = maxlen;
				siglongjmp(ctl->restore, 1);
			}
		} else if (((cc_t) c == ctl->otty.c_cc[VKILL]) && !slash) {
			if (ctl->hard) {
				show(ctl, c);
				putchar('\n');
				putchar(pchar);
			} else {
				putchar('\r');
				putchar(pchar);
				if (ctl->eraseln)
					erasep(ctl, 1);
				else if (ctl->docrtkill)
					while (ctl->promptlen-- > 1)
						fputs(BSB, stderr);
				ctl->promptlen = 1;
			}
			sp = buf;
			fflush(stdout);
			continue;
		}
		if (slash && ((cc_t) c == ctl->otty.c_cc[VKILL]
			      || (cc_t) c == ctl->otty.c_cc[VERASE])) {
			erase_one_column(ctl);
			--sp;
		}
		if (c != '\\')
			slash = 0;
		*sp++ = c;
		if ((c < ' ' && c != '\n' && c != ESC) || c == RUBOUT) {
			c += (c == RUBOUT) ? -0100 : 0100;
			fputs(CARAT, stderr);
			ctl->promptlen++;
		}
		if (c != '\n' && c != ESC) {
			fputc(c, stderr);
			ctl->promptlen++;
		} else
			break;
	}
	*--sp = '\0';
	if (!ctl->eraseln)
		ctl->promptlen = maxlen;
	if (sp - buf >= nmax - 1)
		more_error(ctl, _("Line too long"));
}

static int expand(struct more_control *ctl, char **outbuf, char *inbuf)
{
	char *inpstr;
	char *outstr;
	char c;
	char *temp;
	int changed = 0;
	int tempsz, xtra, offset;

	xtra = strlen(ctl->fnames[ctl->fnum]) + strlen(ctl->shell_line) + 1;
	tempsz = 200 + xtra;
	temp = xmalloc(tempsz);
	inpstr = inbuf;
	outstr = temp;
	while ((c = *inpstr++) != '\0') {
		offset = outstr - temp;
		if (tempsz - offset - 1 < xtra) {
			tempsz += 200 + xtra;
			temp = xrealloc(temp, tempsz);
			outstr = temp + offset;
		}
		switch (c) {
		case '%':
			if (!ctl->no_intty) {
				strcpy(outstr, ctl->fnames[ctl->fnum]);
				outstr += strlen(ctl->fnames[ctl->fnum]);
				changed++;
			} else
				*outstr++ = c;
			break;
		case '!':
			if (!ctl->shellp)
				more_error(ctl, _
					   ("No previous command to substitute for"));
			strcpy(outstr, ctl->shell_line);
			outstr += strlen(ctl->shell_line);
			changed++;
			break;
		case '\\':
			if (*inpstr == '%' || *inpstr == '!') {
				*outstr++ = *inpstr++;
				break;
			}
			/* fallthrough */
		default:
			*outstr++ = c;
		}
	}
	*outstr++ = '\0';
	*outbuf = temp;
	return changed;
}

static void set_tty(struct more_control *ctl)
{
	ctl->otty.c_lflag &= ~(ICANON | ECHO);
	ctl->otty.c_cc[VMIN] = 1;	/* read at least 1 char */
	ctl->otty.c_cc[VTIME] = 0;	/* no timeout */
	tcsetattr(STDERR_FILENO, TCSANOW, &ctl->otty);
}

/* Come here if a quit signal is received */
static void onquit(int dummy __attribute__((__unused__)))
{
	signal(SIGQUIT, SIG_IGN);
	if (!global_ctl->inwait) {
		putchar('\n');
		if (!global_ctl->startup) {
			signal(SIGQUIT, onquit);
			siglongjmp(global_ctl->restore, 1);
		} else
			global_ctl->Pause = 1;
	} else if (!global_ctl->dum_opt && global_ctl->notell) {
		global_ctl->promptlen += fprintf(stderr, _("[Use q or Q to quit]"));
		global_ctl->notell = 0;
	}
	signal(SIGQUIT, onquit);
}

/* Come here when we get a suspend signal from the terminal */
static void onsusp(int dummy __attribute__((__unused__)))
{
	sigset_t signals, oldmask;

	/* ignore SIGTTOU so we don't get stopped if csh grabs the tty */
	signal(SIGTTOU, SIG_IGN);
	reset_tty();
	fflush(stdout);
	signal(SIGTTOU, SIG_DFL);
	/* Send the TSTP signal to suspend our process group */
	signal(SIGTSTP, SIG_DFL);

	/* unblock SIGTSTP or we won't be able to suspend ourself */
	sigemptyset(&signals);
	sigaddset(&signals, SIGTSTP);
	sigprocmask(SIG_UNBLOCK, &signals, &oldmask);

	kill(0, SIGTSTP);
	/* Pause for station break */

	sigprocmask(SIG_SETMASK, &oldmask, NULL);

	/* We're back */
	signal(SIGTSTP, onsusp);
	set_tty(global_ctl);
	if (global_ctl->inwait)
		siglongjmp(global_ctl->restore, 1);
}

static void execute(struct more_control *ctl, char *filename, char *cmd, ...)
{
	int id;
	int n;
	va_list argp;
	char *arg;
	char **args;
	int argcount;

	fflush(stdout);
	reset_tty();
	for (n = 10; (id = fork()) < 0 && n > 0; n--)
		sleep(5);
	if (id == 0) {
		int errsv;
		if (!isatty(STDIN_FILENO)) {
			close(STDIN_FILENO);
			open("/dev/tty", 0);
		}

		va_start(argp, cmd);
		arg = va_arg(argp, char *);
		argcount = 0;
		while (arg) {
			argcount++;
			arg = va_arg(argp, char *);
		}
		va_end(argp);

		args = alloca(sizeof(char *) * (argcount + 1));
		args[argcount] = NULL;

		va_start(argp, cmd);
		arg = va_arg(argp, char *);
		argcount = 0;
		while (arg) {
			args[argcount] = arg;
			argcount++;
			arg = va_arg(argp, char *);
		}
		va_end(argp);

		execvp(cmd, args);
		errsv = errno;
		fputs(_("exec failed\n"), stderr);
		exit(errsv == ENOENT ? EX_EXEC_ENOENT : EX_EXEC_FAILED);
	}
	if (id > 0) {
		signal(SIGINT, SIG_IGN);
		signal(SIGQUIT, SIG_IGN);
		if (ctl->catch_susp)
			signal(SIGTSTP, SIG_DFL);
		while (wait(NULL) > 0)
			/* nothing */ ;
		signal(SIGINT, end_it);
		signal(SIGQUIT, onquit);
		if (ctl->catch_susp)
			signal(SIGTSTP, onsusp);
	} else
		fputs(_("can't fork\n"), stderr);
	set_tty(ctl);
	puts("------------------------");
	prompt(ctl, filename);
}

static void do_shell(struct more_control *ctl, char *filename)
{
	char cmdbuf[COMMAND_BUF];
	int rc;
	char *expanded;

	kill_line(ctl);
	putchar('!');
	fflush(stdout);
	ctl->promptlen = 1;
	if (ctl->lastp)
		fputs(ctl->shell_line, stdout);
	else {
		ttyin(ctl, cmdbuf, sizeof(cmdbuf) - 2, '!');
		expanded = NULL;
		rc = expand(ctl, &expanded, cmdbuf);
		if (expanded) {
			if (strlen(expanded) < sizeof(ctl->shell_line))
				strcpy(ctl->shell_line, expanded);
			else
				rc = -1;
			free(expanded);
		}
		if (rc < 0) {
			fputs(_("  Overflow\n"), stderr);
			prompt(ctl, filename);
			return;
		} else if (rc > 0) {
			kill_line(ctl);
			ctl->promptlen = printf("!%s", ctl->shell_line);
		}
	}
	fflush(stdout);
	fputc('\n', stderr);
	ctl->promptlen = 0;
	ctl->shellp = 1;
	execute(ctl, filename, ctl->shell, ctl->shell, "-c", ctl->shell_line, 0);
}

/* Execute a colon-prefixed command.  Returns <0 if not a command that
 * should cause more of the file to be printed. */
static int colon(struct more_control *ctl, char *filename, int cmd, int nlines)
{
	char ch;

	if (cmd == 0)
		ch = readch(ctl);
	else
		ch = cmd;
	ctl->lastcolon = ch;
	switch (ch) {
	case 'f':
		kill_line(ctl);
		if (!ctl->no_intty)
			ctl->promptlen =
			    printf(_("\"%s\" line %d"), ctl->fnames[ctl->fnum], ctl->Currline);
		else
			ctl->promptlen = printf(_("[Not a file] line %d"), ctl->Currline);
		fflush(stdout);
		return -1;
	case 'n':
		if (nlines == 0) {
			if (ctl->fnum >= ctl->nfiles - 1)
				end_it(0);
			nlines++;
		}
		putchar('\r');
		erasep(ctl, 0);
		skipf(ctl, nlines);
		return 0;
	case 'p':
		if (ctl->no_intty) {
			fputc(RINGBELL, stderr);
			return -1;
		}
		putchar('\r');
		erasep(ctl, 0);
		if (nlines == 0)
			nlines++;
		skipf(ctl, -nlines);
		return 0;
	case '!':
		do_shell(ctl, filename);
		return -1;
	case 'q':
	case 'Q':
		end_it(0);
	default:
		fputc(RINGBELL, stderr);
		return -1;
	}
}

/* Skip n lines in the file f */
static void skiplns(struct more_control *ctl, register int n, register FILE *f)
{
	register int c;

	while (n > 0) {
		while ((c = more_getc(ctl, f)) != '\n')
			if (c == EOF)
				return;
		n--;
		ctl->Currline++;
	}
}

/*  Clear the screen */
static void doclear(struct more_control *ctl)
{
	if (ctl->Clear && !ctl->hard) {
		putp(ctl->Clear);
		/* Put out carriage return so that system doesn't get
		 * confused by escape sequences when expanding tabs */
		putchar('\r');
		ctl->promptlen = 0;
	}
}

static void rdline(struct more_control *ctl, register FILE *f)
{
	register int c;
	register char *p;

	prepare_line_buffer(ctl);

	p = ctl->Line;
	while ((c = more_getc(ctl, f)) != '\n' && c != EOF
	       && (size_t)(p - ctl->Line) < ctl->LineLen - 1)
		*p++ = c;
	if (c == '\n')
		ctl->Currline++;
	*p = '\0';
}

/* Go to home position */
static void home(struct more_control *ctl)
{
	putp(ctl->Home);
}

/* Search for nth occurrence of regular expression contained in buf in
 * the file */
static void search(struct more_control *ctl, char buf[], FILE *file, register int n)
{
	long startline = ctl->file_pos;
	register long line1 = startline;
	register long line2 = startline;
	register long line3;
	register int lncount;
	int saveln, rc;
	regex_t re;

	ctl->context.line = saveln = ctl->Currline;
	ctl->context.chrctr = startline;
	lncount = 0;
	if (!buf)
		goto notfound;
	if ((rc = regcomp(&re, buf, REG_NOSUB)) != 0) {
		char s[REGERR_BUF];
		regerror(rc, &re, s, sizeof s);
		more_error(ctl, s);
	}
	while (!feof(file)) {
		line3 = line2;
		line2 = line1;
		line1 = ctl->file_pos;
		rdline(ctl, file);
		lncount++;
		if (regexec(&re, ctl->Line, 0, NULL, 0) == 0) {
			if (--n == 0) {
				if (lncount > 3 || (lncount > 1 && ctl->no_intty)) {
					putchar('\n');
					if (ctl->clreol)
						cleareol(ctl);
					fputs(_("...skipping\n"), stdout);
				}
				if (!ctl->no_intty) {
					ctl->Currline -=
					    (lncount >= 3 ? 3 : lncount);
					more_fseek(ctl, file, line3);
					if (ctl->noscroll) {
						if (ctl->clreol) {
							home(ctl);
							cleareol(ctl);
						} else
							doclear(ctl);
					}
				} else {
					kill_line(ctl);
					if (ctl->noscroll) {
						if (ctl->clreol) {
							home(ctl);
							cleareol(ctl);
						} else
							doclear(ctl);
					}
					puts(ctl->Line);
				}
				break;
			}
		}
	}
	regfree(&re);
	if (feof(file)) {
		if (!ctl->no_intty) {
			ctl->Currline = saveln;
			more_fseek(ctl, file, startline);
		} else {
			fputs(_("\nPattern not found\n"), stdout);
			end_it(0);
		}
		free(ctl->previousre);
		ctl->previousre = NULL;
notfound:
		more_error(ctl, _("Pattern not found"));
	}
}

/* Read a command and do it.  A command consists of an optional integer
 * argument followed by the command character.  Return the number of
 * lines to display in the next screenful.  If there is nothing more to
 * display in the current file, zero is returned. */
static int command(struct more_control *ctl, char *filename, register FILE *f)
{
	register int nlines;
	register int retval = 0;
	register int c;
	char colonch;
	int done;
	char comchar, cmdbuf[INIT_BUF];

	done = 0;
	if (!ctl->errors)
		prompt(ctl, filename);
	else
		ctl->errors = 0;
	for (;;) {
		nlines = number(ctl, &comchar);
		ctl->lastp = colonch = 0;
		if (comchar == '.') {	/* Repeat last command */
			ctl->lastp++;
			comchar = ctl->lastcmd;
			nlines = ctl->lastarg;
			if (ctl->lastcmd == ':')
				colonch = ctl->lastcolon;
		}
		ctl->lastcmd = comchar;
		ctl->lastarg = nlines;
		if ((cc_t) comchar == ctl->otty.c_cc[VERASE]) {
			kill_line(ctl);
			prompt(ctl, filename);
			continue;
		}
		switch (comchar) {
		case ':':
			retval = colon(ctl, filename, colonch, nlines);
			if (retval >= 0)
				done++;
			break;
		case 'b':
		case ctrl('B'):
			{
				register int initline;

				if (ctl->no_intty) {
					fputc(RINGBELL, stderr);
					return -1;
				}

				if (nlines == 0)
					nlines++;

				putchar('\r');
				erasep(ctl, 0);
				putchar('\n');
				if (ctl->clreol)
					cleareol(ctl);
				printf(P_("...back %d page",
					"...back %d pages", nlines),
					nlines);
				if (ctl->clreol)
					cleareol(ctl);
				putchar('\n');

				initline = ctl->Currline - ctl->dlines * (nlines + 1);
				if (!ctl->noscroll)
					--initline;
				if (initline < 0)
					initline = 0;
				more_fseek(ctl, f, 0L);
				ctl->Currline = 0;	/* skiplns() will make Currline correct */
				skiplns(ctl, initline, f);
				if (!ctl->noscroll) {
					retval = ctl->dlines + 1;
				} else {
					retval = ctl->dlines;
				}
				done = 1;
				break;
			}
		case ' ':
		case 'z':
			if (nlines == 0)
				nlines = ctl->dlines;
			else if (comchar == 'z')
				ctl->dlines = nlines;
			retval = nlines;
			done = 1;
			break;
		case 'd':
		case ctrl('D'):
			if (nlines != 0)
				ctl->nscroll = nlines;
			retval = ctl->nscroll;
			done = 1;
			break;
		case 'q':
		case 'Q':
			end_it(0);
		case 's':
		case 'f':
		case ctrl('F'):
			if (nlines == 0)
				nlines++;
			if (comchar == 'f')
				nlines *= ctl->dlines;
			putchar('\r');
			erasep(ctl, 0);
			putchar('\n');
			if (ctl->clreol)
				cleareol(ctl);
			printf(P_("...skipping %d line",
				"...skipping %d lines", nlines),
				nlines);

			if (ctl->clreol)
				cleareol(ctl);
			putchar('\n');

			while (nlines > 0) {
				while ((c = more_getc(ctl, f)) != '\n')
					if (c == EOF) {
						retval = 0;
						done++;
						goto endsw;
					}
				ctl->Currline++;
				nlines--;
			}
			retval = ctl->dlines;
			done = 1;
			break;
		case '\n':
			if (nlines != 0)
				ctl->dlines = nlines;
			else
				nlines = 1;
			retval = nlines;
			done = 1;
			break;
		case '\f':
			if (!ctl->no_intty) {
				doclear(ctl);
				more_fseek(ctl, f, ctl->screen_start.chrctr);
				ctl->Currline = ctl->screen_start.line;
				retval = ctl->dlines;
				done = 1;
				break;
			} else {
				fputc(RINGBELL, stderr);
				break;
			}
		case '\'':
			if (!ctl->no_intty) {
				kill_line(ctl);
				fputs(_("\n***Back***\n\n"), stdout);
				more_fseek(ctl, f, ctl->context.chrctr);
				ctl->Currline = ctl->context.line;
				retval = ctl->dlines;
				done = 1;
				break;
			} else {
				fputc(RINGBELL, stderr);
				break;
			}
		case '=':
			kill_line(ctl);
			ctl->promptlen = printf("%d", ctl->Currline);
			fflush(stdout);
			break;
		case 'n':
			if (!ctl->previousre) {
				more_error(ctl, _("No previous regular expression"));
				break;
			}
			ctl->lastp++;
			/* fallthrough */
		case '/':
			if (nlines == 0)
				nlines++;
			kill_line(ctl);
			putchar('/');
			ctl->promptlen = 1;
			fflush(stdout);
			if (ctl->lastp) {
				fputc('\r', stderr);
				search(ctl, ctl->previousre, f, nlines);
			} else {
				ttyin(ctl, cmdbuf, sizeof(cmdbuf) - 2, '/');
				fputc('\r', stderr);
				free(ctl->previousre);
				ctl->previousre = xstrdup(cmdbuf);
				search(ctl, cmdbuf, f, nlines);
			}
			retval = ctl->dlines - 1;
			done = 1;
			break;
		case '!':
			do_shell(ctl, filename);
			break;
		case '?':
		case 'h':
			if (ctl->noscroll)
				doclear(ctl);
			fputs(_("\n"
				  "Most commands optionally preceded by integer argument k.  "
				  "Defaults in brackets.\n"
				  "Star (*) indicates argument becomes new default.\n"), stdout);
			puts("---------------------------------------"
			     "----------------------------------------");
			fputs(_
				("<space>                 Display next k lines of text [current screen size]\n"
				 "z                       Display next k lines of text [current screen size]*\n"
				 "<return>                Display next k lines of text [1]*\n"
				 "d or ctrl-D             Scroll k lines [current scroll size, initially 11]*\n"
				 "q or Q or <interrupt>   Exit from more\n"
				 "s                       Skip forward k lines of text [1]\n"
				 "f                       Skip forward k screenfuls of text [1]\n"
				 "b or ctrl-B             Skip backwards k screenfuls of text [1]\n"
				 "'                       Go to place where previous search started\n"
				 "=                       Display current line number\n"
				 "/<regular expression>   Search for kth occurrence of regular expression [1]\n"
				 "n                       Search for kth occurrence of last r.e [1]\n"
				 "!<cmd> or :!<cmd>       Execute <cmd> in a subshell\n"
				 "v                       Start up /usr/bin/vi at current line\n"
				 "ctrl-L                  Redraw screen\n"
				 ":n                      Go to kth next file [1]\n"
				 ":p                      Go to kth previous file [1]\n"
				 ":f                      Display current file name and line number\n"
				 ".                       Repeat previous command\n"), stdout);
			puts("---------------------------------------"
			     "----------------------------------------");
			prompt(ctl, filename);
			break;
		case 'v':	/* This case should go right before default */
			if (!ctl->no_intty) {
				/* Earlier: call vi +n file. This also
				 * works for emacs.  POSIX: call vi -c n
				 * file (when editor is vi or ex). */
				char *editor, *p;
				int n = (ctl->Currline - ctl->dlines <= 0 ? 1 :
					 ctl->Currline - (ctl->dlines + 1) / 2);
				int split = 0;

				editor = getenv("VISUAL");
				if (editor == NULL || *editor == '\0')
					editor = getenv("EDITOR");
				if (editor == NULL || *editor == '\0')
					editor = VI;

				p = strrchr(editor, '/');
				if (p)
					p++;
				else
					p = editor;
				if (!strcmp(p, "vi") || !strcmp(p, "ex")) {
					sprintf(cmdbuf, "-c %d", n);
					split = 1;
				} else {
					sprintf(cmdbuf, "+%d", n);
				}

				kill_line(ctl);
				printf("%s %s %s", editor, cmdbuf,
				       ctl->fnames[ctl->fnum]);
				if (split) {
					cmdbuf[2] = 0;
					execute(ctl, filename, editor, editor,
						cmdbuf, cmdbuf + 3,
						ctl->fnames[ctl->fnum], (char *)0);
				} else
					execute(ctl, filename, editor, editor,
						cmdbuf, ctl->fnames[ctl->fnum],
						(char *)0);
				break;
			}
			/* fallthrough */
		default:
			if (ctl->dum_opt) {
				kill_line(ctl);
				if (ctl->Senter && ctl->Sexit) {
					putstring(ctl->Senter);
					ctl->promptlen =
					    printf(_
						   ("[Press 'h' for instructions.]"))
					    + 2 * ctl->soglitch;
					putstring(ctl->Sexit);
				} else
					ctl->promptlen =
					    printf(_
						   ("[Press 'h' for instructions.]"));
				fflush(stdout);
			} else
				fputc(RINGBELL, stderr);
			break;
		}
		if (done)
			break;
	}
	putchar('\r');
 endsw:
	ctl->inwait = 0;
	ctl->notell = 1;
	return retval;
}

/* Print out the contents of the file f, one screenful at a time. */
static void screen(struct more_control *ctl, register FILE *f, register int num_lines)
{
	register int c;
	register int nchars;
	int length;			/* length of current line */
	static int prev_len = 1;	/* length of previous line */

	for (;;) {
		while (num_lines > 0 && !ctl->Pause) {
			if ((nchars = get_line(ctl, f, &length)) == EOF) {
				if (ctl->clreol)
					clreos(ctl);
				return;
			}
			if (ctl->ssp_opt && length == 0 && prev_len == 0)
				continue;
			prev_len = length;
			if (ctl->bad_so
			    || ((ctl->Senter && *ctl->Senter == ' ') && (ctl->promptlen > 0)))
				erasep(ctl, 0);
			/* must clear before drawing line since tabs on
			 * some terminals do not erase what they tab
			 * over. */
			if (ctl->clreol)
				cleareol(ctl);
			prbuf(ctl, ctl->Line, length);
			if (nchars < ctl->promptlen)
				erasep(ctl, nchars);	/* erasep () sets promptlen to 0 */
			else
				ctl->promptlen = 0;
			if (nchars < ctl->Mcol || !ctl->fold_opt)
				prbuf(ctl, "\n", 1);	/* will turn off UL if necessary */
			num_lines--;
		}
		if (ctl->pstate) {
			putp(ctl->ULexit);
			ctl->pstate = 0;
		}
		fflush(stdout);
		if ((c = more_getc(ctl, f)) == EOF) {
			if (ctl->clreol)
				clreos(ctl);
			return;
		}

		if (ctl->Pause && ctl->clreol)
			clreos(ctl);
		more_ungetc(ctl, c, f);
		sigsetjmp(ctl->restore, 1);
		ctl->Pause = 0;
		ctl->startup = 0;
		if ((num_lines = command(ctl, NULL, f)) == 0)
			return;
		if (ctl->hard && ctl->promptlen > 0)
			erasep(ctl, 0);
		if (ctl->noscroll && num_lines >= ctl->dlines) {
			if (ctl->clreol)
				home(ctl);
			else
				doclear(ctl);
		}
		ctl->screen_start.line = ctl->Currline;
		ctl->screen_start.chrctr = ctl->file_pos;
	}
}

/* Come here if a signal for a window size change is received */
#ifdef SIGWINCH
static void chgwinsz(int dummy __attribute__((__unused__)))
{
	struct winsize win;

	signal(SIGWINCH, SIG_IGN);
	if (ioctl(fileno(stdout), TIOCGWINSZ, &win) != -1) {
		if (win.ws_row != 0) {
			global_ctl->Lpp = win.ws_row;
			global_ctl->nscroll = global_ctl->Lpp / 2 - 1;
			if (global_ctl->nscroll <= 0)
				global_ctl->nscroll = 1;
			global_ctl->dlines = global_ctl->Lpp - 1;
		}
		if (win.ws_col != 0)
			global_ctl->Mcol = win.ws_col;
	}
	signal(SIGWINCH, chgwinsz);
}
#endif				/* SIGWINCH */

static void copy_file(register FILE *f)
{
	char buf[BUFSIZ];
	size_t sz;

	while ((sz = fread(&buf, sizeof(char), sizeof(buf), f)) > 0)
		fwrite(&buf, sizeof(char), sz, stdout);
}


static void initterm(struct more_control *ctl)
{
	int ret;
	char *term;
	struct winsize win;

#ifndef NON_INTERACTIVE_MORE
	ctl->no_tty = tcgetattr(fileno(stdout), &ctl->otty);
#endif
	if (!ctl->no_tty) {
		ctl->docrterase = (ctl->otty.c_cc[VERASE] != 255);
		ctl->docrtkill = (ctl->otty.c_cc[VKILL] != 255);
		if ((term = getenv("TERM")) == NULL) {
			ctl->dumb = 1;
			ctl->ul_opt = 0;
		}
		setupterm(term, 1, &ret);
		if (ret <= 0) {
			ctl->dumb = 1;
			ctl->ul_opt = 0;
		} else {
#ifdef TIOCGWINSZ
			if (ioctl(fileno(stdout), TIOCGWINSZ, &win) < 0) {
#endif
				ctl->Lpp = tigetnum(TERM_LINES);
				ctl->Mcol = tigetnum(TERM_COLS);
#ifdef TIOCGWINSZ
			} else {
				if ((ctl->Lpp = win.ws_row) == 0)
					ctl->Lpp = tigetnum(TERM_LINES);
				if ((ctl->Mcol = win.ws_col) == 0)
					ctl->Mcol = tigetnum(TERM_COLS);
			}
#endif
			if ((ctl->Lpp <= 0) || tigetflag(TERM_HARD_COPY)) {
				ctl->hard = 1;
				ctl->Lpp = LINES_PER_PAGE;
			}

			if (tigetflag(TERM_EAT_NEW_LINE))
				/* Eat newline at last column + 1; dec, concept */
				ctl->eatnl++;
			if (ctl->Mcol <= 0)
				ctl->Mcol = NUM_COLUMNS;

			ctl->Wrap = tigetflag(TERM_AUTO_RIGHT_MARGIN);
			ctl->bad_so = tigetflag(TERM_CEOL);
			ctl->eraseln = tigetstr(TERM_CLEAR_TO_LINE_END);
			ctl->Clear = tigetstr(TERM_CLEAR);
			ctl->Senter = tigetstr(TERM_STANDARD_MODE);
			ctl->Sexit = tigetstr(TERM_EXIT_STANDARD_MODE);
			if (0 < tigetnum(TERM_STD_MODE_GLITCH))
				ctl->soglitch = 1;

			/* Set up for underlining:  some terminals don't
			 * need it; others have start/stop sequences,
			 * still others have an underline char sequence
			 * which is assumed to move the cursor forward
			 * one character.  If underline sequence isn't
			 * available, settle for standout sequence. */
			if (tigetflag(TERM_UNDERLINE)
			    || tigetflag(TERM_OVER_STRIKE))
				ctl->ul_opt = 0;
			if ((ctl->chUL = tigetstr(TERM_UNDERLINE_CHAR)) == NULL)
				ctl->chUL = "";
			if (((ctl->ULenter =
			      tigetstr(TERM_ENTER_UNDERLINE)) == NULL
			     || (ctl->ULexit =
				 tigetstr(TERM_EXIT_UNDERLINE)) == NULL)
			    && !*ctl->chUL) {
				if ((ctl->ULenter = ctl->Senter) == NULL
				    || (ctl->ULexit = ctl->Sexit) == NULL) {
					ctl->ULenter = "";
					ctl->ULexit = "";
				} else
					ctl->ulglitch = ctl->soglitch;
			} else {
				ctl->ulglitch = 0;
			}
			ctl->Home = tigetstr(TERM_HOME);
			if (ctl->Home == NULL || *ctl->Home == '\0') {
				if ((ctl->cursorm =
				     tigetstr(TERM_CURSOR_ADDRESS)) != NULL) {
					const char *t =
					    (const char *)tparm(ctl->cursorm, 0,
								   0);
					xstrncpy(ctl->cursorhome, t,
						 sizeof(ctl->cursorhome));
					ctl->Home = ctl->cursorhome;
				}
			}
			ctl->EodClr = tigetstr(TERM_CLEAR_TO_SCREEN_END);
			if ((ctl->chBS = tigetstr(TERM_LINE_DOWN)) == NULL)
				ctl->chBS = BS;

		}
		if ((ctl->shell = getenv("SHELL")) == NULL)
			ctl->shell = "/bin/sh";
	}
	ctl->no_intty = tcgetattr(fileno(stdin), &ctl->otty);
	tcgetattr(fileno(stderr), &ctl->otty);
	ctl->savetty0 = ctl->otty;
	ctl->hardtabs = (ctl->otty.c_oflag & TABDLY) != TAB3;
	if (!ctl->no_tty) {
		ctl->otty.c_lflag &= ~(ICANON | ECHO);
		ctl->otty.c_cc[VMIN] = 1;
		ctl->otty.c_cc[VTIME] = 0;
	}
}

int main(int argc, char **argv)
{
	FILE *f;
	char *s;
	int chr;
	int left;
	int prnames = 0;
	int initopt = 0;
	int srchopt = 0;
	int clearit = 0;
	int initline = 0;
	char *initbuf = NULL;
	struct more_control ctl = {
		.firstf = 1,
		.fold_opt = 1,
		.notell = 1,
		.startup = 1,
		.stop_opt = 1,
		.ul_opt = 1,
		.Wrap = 1,
		.Lpp = LINES_PER_PAGE,
		.Mcol = NUM_COLUMNS,
		.nscroll = SCROLL_LEN,
		0
	};
	global_ctl = &ctl;

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	if (argc > 1) {
		/* first arg may be one of our standard longopts */
		if (!strcmp(argv[1], "--help"))
			usage();
		if (!strcmp(argv[1], "--version")) {
			printf(UTIL_LINUX_VERSION);
			exit(EXIT_SUCCESS);
		}
	}

	ctl.nfiles = argc;
	ctl.fnames = argv;
	setlocale(LC_ALL, "");
	initterm(&ctl);

	/* Auto set no scroll on when binary is called page */
	if (!(strcmp(program_invocation_short_name, "page")))
		ctl.noscroll++;

	prepare_line_buffer(&ctl);

	ctl.nscroll = ctl.Lpp / 2 - 1;
	if (ctl.nscroll <= 0)
		ctl.nscroll = 1;

	if ((s = getenv("MORE")) != NULL)
		argscan(&ctl, s);

	while (--ctl.nfiles > 0) {
		if ((chr = (*++ctl.fnames)[0]) == '-') {
			argscan(&ctl, *ctl.fnames + 1);
		} else if (chr == '+') {
			s = *ctl.fnames;
			if (*++s == '/') {
				srchopt++;
				initbuf = xstrdup(s + 1);
			} else {
				initopt++;
				for (initline = 0; *s != '\0'; s++)
					if (isdigit(*s))
						initline =
						    initline * 10 + *s - '0';
				--initline;
			}
		} else
			break;
	}
	/* allow clreol only if Home and eraseln and EodClr strings are
	 * defined, and in that case, make sure we are in noscroll mode */
	if (ctl.clreol) {
		if ((ctl.Home == NULL) || (*ctl.Home == '\0') ||
		    (ctl.eraseln == NULL) || (*ctl.eraseln == '\0') ||
		    (ctl.EodClr == NULL) || (*ctl.EodClr == '\0'))
			ctl.clreol = 0;
		else
			ctl.noscroll = 1;
	}
	if (ctl.dlines == 0)
		ctl.dlines = ctl.Lpp - 1;
	left = ctl.dlines;
	if (ctl.nfiles > 1)
		prnames++;
	if (!ctl.no_intty && ctl.nfiles == 0) {
		warnx(_("bad usage"));
		errtryhelp(EXIT_FAILURE);
	} else
		f = stdin;
	if (!ctl.no_tty) {
		signal(SIGQUIT, onquit);
		signal(SIGINT, end_it);
#ifdef SIGWINCH
		signal(SIGWINCH, chgwinsz);
#endif
		if (signal(SIGTSTP, SIG_IGN) == SIG_DFL) {
			signal(SIGTSTP, onsusp);
			ctl.catch_susp++;
		}
		tcsetattr(STDERR_FILENO, TCSANOW, &ctl.otty);
	}
	if (ctl.no_intty) {
		if (ctl.no_tty)
			copy_file(stdin);
		else {
			if ((chr = getc(f)) == '\f')
				doclear(&ctl);
			else {
				ungetc(chr, f);
				if (ctl.noscroll && (chr != EOF)) {
					if (ctl.clreol)
						home(&ctl);
					else
						doclear(&ctl);
				}
			}
			if (srchopt) {
				free(ctl.previousre);
				ctl.previousre = xstrdup(initbuf);
				search(&ctl, initbuf, stdin, 1);
				if (ctl.noscroll)
					left--;
			} else if (initopt)
				skiplns(&ctl, initline, stdin);
			screen(&ctl, stdin, left);
		}
		ctl.no_intty = 0;
		prnames++;
		ctl.firstf = 0;
	}

	while (ctl.fnum < ctl.nfiles) {
		if ((f = checkf(&ctl, ctl.fnames[ctl.fnum], &clearit)) != NULL) {
			ctl.context.line = ctl.context.chrctr = 0;
			ctl.Currline = 0;
			if (ctl.firstf)
				sigsetjmp(ctl.restore, 1);
			if (ctl.firstf) {
				ctl.firstf = 0;
				if (srchopt) {
					free(ctl.previousre);
					ctl.previousre = xstrdup(initbuf);
					search(&ctl, initbuf, f, 1);
					if (ctl.noscroll)
						left--;
				} else if (initopt)
					skiplns(&ctl, initline, f);
			} else if (ctl.fnum < ctl.nfiles && !ctl.no_tty) {
				sigsetjmp(ctl.restore, 1);
				left = command(&ctl, ctl.fnames[ctl.fnum], f);
			}
			if (left != 0) {
				if ((ctl.noscroll || clearit)
				    && (ctl.file_size != LONG_MAX)) {
					if (ctl.clreol)
						home(&ctl);
					else
						doclear(&ctl);
				}
				if (prnames) {
					if (ctl.bad_so)
						erasep(&ctl, 0);
					if (ctl.clreol)
						cleareol(&ctl);
					fputs("::::::::::::::", stdout);
					if (ctl.promptlen > 14)
						erasep(&ctl, 14);
					putchar('\n');
					if (ctl.clreol)
						cleareol(&ctl);
					puts(ctl.fnames[ctl.fnum]);
					if (ctl.clreol)
						cleareol(&ctl);
					fputs("::::::::::::::\n", stdout);
					if (left > ctl.Lpp - 4)
						left = ctl.Lpp - 4;
				}
				if (ctl.no_tty)
					copy_file(f);
				else {
					ctl.within = 1;
					screen(&ctl, f, left);
					ctl.within = 0;
				}
			}
			sigsetjmp(ctl.restore, 1);
			fflush(stdout);
			fclose(f);
			ctl.screen_start.line = ctl.screen_start.chrctr = 0L;
			ctl.context.line = ctl.context.chrctr = 0L;
		}
		ctl.fnum++;
		ctl.firstf = 0;
	}
	free(ctl.previousre);
	free(initbuf);
	free(ctl.Line);
	reset_tty();
	exit(EXIT_SUCCESS);
}
