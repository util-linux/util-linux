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

#define MIN_LINE_SZ	256	/* minimal line_buf buffer size */
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
	struct termios output_tty;	/* output terminal */
	struct termios original_tty;	/* original terminal settings */
	long file_position;		/* file position */
	long file_size;			/* file size */
	int argv_position;		/* argv[] position */
	int lines_per_screen;		/* screen size in lines */
	int d_scroll_len;		/* number of lines scrolled by 'd' */
	int prompt_len;			/* message prompt length */
	int current_line;		/* line we are currently at */
	char **file_names;		/* The list of file names */
	int num_files;			/* Number of files left to process */
	char *shell;			/* name of the shell to use */
	int previous_shell;		/* does previous shell command exist */
	sigjmp_buf destination;		/* siglongjmp() destination */
	char *line_buf;			/* line buffer */
	size_t line_sz;			/* size of line_buf buffer */
	int lines_per_page;		/* lines per page */
	char *clear;			/* clear screen */
	char *erase_line;		/* erase line */
	char *enter_std;		/* enter standout mode */
	char *exit_std;			/* exit standout mode */
	char *enter_underline;		/* enter underline mode */
	char *exit_underline;		/* exit underline mode */
	char *underline_ch;		/* underline character */
	char *backspace_ch;		/* backspace character */
	char *go_home;			/* go to home */
	char *cursor_addr;		/* cursor movement */
	char home_position[40];		/* contains cursor movement to home */
	char *clear_rest;		/* clear rest of screen */
	int num_columns;		/* number of columns */
	char *previous_search;		/* previous search() buf[] item */
	struct {
		long row_num;		/* row number */
		long line_num;		/* line number */
	} context,
	  screen_start;
	int last_key_command;		/* previous more key command */
	int last_key_arg;		/* previous key command argument */
	int last_colon_command;		/* is a colon-prefixed key command */
	char shell_line[SHELL_LINE];
	unsigned int
		bad_stdout:1,		/* true if overwriting does not turn off standout */
		catch_suspend:1,	/* we should catch the SIGTSTP signal */
		clear_line_ends:1,	/* do not scroll, paint each screen from the top */
		dumb_tty:1,		/* is terminal type known */
		eat_newline:1,		/* is newline ignored after 80 cols */
		enable_underlining:1,	/* underline as best we can */
		erase_input_ok:1,	/* is erase input supported */
		erase_previous_ok:1,	/* is erase previous supported */
		first_file:1,		/* is the input file the first in list */
		fold_long_lines:1,	/* fold long lines */
		hard_tabs:1,		/* print spaces instead of '\t' */
		hard_tty:1,		/* is this hard copy terminal (a printer or such) */
		is_paused:1,		/* is output paused */
		no_quit_dialog:1,	/* suppress quit dialog */
		no_scroll:1,		/* do not scroll, clear the screen and then display text */
		no_tty_in:1,		/* is input in interactive mode */
		no_tty_out:1,		/* is output in interactive mode */
		report_errors:1,	/* is an error reported */
		run_previous_command:1,	/* run previous key command */
		squeeze_spaces:1,	/* suppress white space */
		starting_up:1,		/* is startup completed */
		stdout_glitch:1,	/* terminal has standout mode glitch */
		stop_after_formfeed:1,	/* stop after form feeds */
		suppress_bell:1,	/* suppress bell */
		underline_glitch:1,	/* terminal has underline mode glitch */
		underline_state:1,	/* current UL state */
		waiting_input:1,	/* is waiting user input */
		within_file:1,		/* true if we are within a file, false if we are between files */
		wrap_margin:1;		/* set if automargins */
};

/* FIXME: global_ctl is used in signal handlers. */
struct more_control *global_ctl;

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


static void arg_parser(struct more_control *ctl, char *s)
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
				ctl->lines_per_screen = 0;
				seen_num = 1;
			}
			ctl->lines_per_screen = ctl->lines_per_screen * 10 + *s - '0';
			break;
		case 'd':
			ctl->suppress_bell = 1;
			break;
		case 'l':
			ctl->stop_after_formfeed = 0;
			break;
		case 'f':
			ctl->fold_long_lines = 0;
			break;
		case 'p':
			ctl->no_scroll = 1;
			break;
		case 'c':
			ctl->clear_line_ends = 1;
			break;
		case 's':
			ctl->squeeze_spaces = 1;
			break;
		case 'u':
			ctl->enable_underlining = 0;
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
	ctl->file_position = pos;
	fseek(stream, pos, 0);
}

static int more_getc(struct more_control *ctl, FILE *stream)
{
	ctl->file_position++;
	return getc(stream);
}

static int more_ungetc(struct more_control *ctl, int c, FILE *stream)
{
	ctl->file_position--;
	return ungetc(c, stream);
}

/* magic --
 *	check for file magic numbers.  This code would best be shared
 *	with the file(1) program or, perhaps, more should not try to be
 *	so smart. */
static int check_magic(FILE *f, char *fs)
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
static FILE *checkf(struct more_control *ctl, char *fs, int *clearfirst)
{
	struct stat st;
	FILE *f;
	int c;

	if (stat(fs, &st) == -1) {
		fflush(stdout);
		if (ctl->clear_line_ends)
			putp(ctl->erase_line);
		warn(_("stat of %s failed"), fs);
		return NULL;
	}
	if ((st.st_mode & S_IFMT) == S_IFDIR) {
		printf(_("\n*** %s: directory ***\n\n"), fs);
		return NULL;
	}
	ctl->current_line = 0;
	ctl->file_position = 0;
	if ((f = fopen(fs, "r")) == NULL) {
		fflush(stdout);
		warn(_("cannot open %s"), fs);
		return NULL;
	}
	if (check_magic(f, fs)) {
		fclose(f);
		return NULL;
	}
	fcntl(fileno(f), F_SETFD, FD_CLOEXEC);
	c = more_getc(ctl, f);
	*clearfirst = (c == '\f');
	more_ungetc(ctl, c, f);
	if ((ctl->file_size = st.st_size) == 0)
		ctl->file_size = LONG_MAX;
	return f;
}

static void prepare_line_buffer(struct more_control *ctl)
{
	size_t sz = ctl->num_columns * 4;

	if (ctl->line_sz >= sz)
		return;

	if (sz < MIN_LINE_SZ)
		sz = MIN_LINE_SZ;

	/* alloc sz and extra space for \n\0 */
	ctl->line_buf = xrealloc(ctl->line_buf, sz + 2);
	ctl->line_sz = sz;
}

/* Get a logical line */
static int get_line(struct more_control *ctl, FILE *f, int *length)
{
	int c;
	char *p;
	int column;
	static int column_wrap;

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
	long file_position_bak = ctl->file_position;

	memset(&state, '\0', sizeof(mbstate_t));
#endif

	prepare_line_buffer(ctl);

	p = ctl->line_buf;
	column = 0;
	c = more_getc(ctl, f);
	if (column_wrap && c == '\n') {
		ctl->current_line++;
		c = more_getc(ctl, f);
	}
	while (p < &ctl->line_buf[ctl->line_sz - 1]) {
#ifdef HAVE_WIDECHAR
		if (ctl->fold_long_lines && use_mbc_buffer_flag && MB_CUR_MAX > 1) {
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
				file_position_bak++;
				if (column >= ctl->num_columns) {
					more_fseek(ctl, f, file_position_bak);
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
				if (column + wc_width > ctl->num_columns) {
					more_fseek(ctl, f, file_position_bak);
					break_flag = 1;
				} else {
					for (i = 0; p < &ctl->line_buf[ctl->line_sz - 1] &&
						    i < mbc_pos; i++)
						*p++ = mbc[i];
					if (wc_width > 0)
						column += wc_width;
				}
			}

			if (break_flag || column >= ctl->num_columns)
				break;

			c = more_getc(ctl, f);
			continue;
		}
#endif	/* HAVE_WIDECHAR */
		if (c == EOF) {
			if (p > ctl->line_buf) {
				*p = '\0';
				*length = p - ctl->line_buf;
				return column;
			}
			*length = p - ctl->line_buf;
			return EOF;
		}
		if (c == '\n') {
			ctl->current_line++;
			break;
		}

		*p++ = c;
		if (c == '\t') {
			if (!ctl->hard_tabs || (column < ctl->prompt_len && !ctl->hard_tty)) {
				if (ctl->hard_tabs && ctl->erase_line && !ctl->dumb_tty) {
					column = 1 + (column | 7);
					putp(ctl->erase_line);
					ctl->prompt_len = 0;
				} else {
					for (--p; p < &ctl->line_buf[ctl->line_sz - 1];) {
						*p++ = ' ';
						if ((++column & 7) == 0)
							break;
					}
					if (column >= ctl->prompt_len)
						ctl->prompt_len = 0;
				}
			} else
				column = 1 + (column | 7);
		} else if (c == '\b' && column > 0) {
			column--;
		} else if (c == '\r') {
			int next = more_getc(ctl, f);
			if (next == '\n') {
				p--;
				ctl->current_line++;
				break;
			}
			more_ungetc(ctl, c, f);
			column = 0;
		} else if (c == '\f' && ctl->stop_after_formfeed) {
			p[-1] = '^';
			*p++ = 'L';
			column += 2;
			ctl->is_paused = 1;
		} else if (c == EOF) {
			*length = p - ctl->line_buf;
			return column;
		} else {
#ifdef HAVE_WIDECHAR
			if (ctl->fold_long_lines && MB_CUR_MAX > 1) {
				memset(mbc, '\0', MB_LEN_MAX);
				mbc_pos = 0;
				mbc[mbc_pos++] = c;
				state_bak = state;

				mblength = mbrtowc(&wc, mbc, mbc_pos, &state);
				/* The value of mblength is always less than 2 here. */
				switch (mblength) {
				case (size_t)-2:
					p--;
					file_position_bak = ctl->file_position - 1;
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

		if (column >= ctl->num_columns && ctl->fold_long_lines)
			break;
#ifdef HAVE_WIDECHAR
		if (use_mbc_buffer_flag == 0 && p >= &ctl->line_buf[ctl->line_sz - 1 - 4])
			/* don't read another char if there is no space for
			 * whole multibyte sequence */
			break;
#endif
		c = more_getc(ctl, f);
	}
	if (column >= ctl->num_columns && ctl->num_columns > 0) {
		if (!ctl->wrap_margin) {
			*p++ = '\n';
		}
	}
	column_wrap = column == ctl->num_columns && ctl->fold_long_lines;
	if (column_wrap && ctl->eat_newline && ctl->wrap_margin) {
		*p++ = '\n';	/* simulate normal wrap */
	}
	*length = p - ctl->line_buf;
	*p = 0;
	return column;
}

/* Erase the rest of the prompt, assuming we are starting at column col. */
static void erase_prompt(struct more_control *ctl, int col)
{

	if (ctl->prompt_len == 0)
		return;
	if (ctl->hard_tty) {
		putchar('\n');
	} else {
		if (col == 0)
			putchar('\r');
		if (!ctl->dumb_tty && ctl->erase_line)
			putp(ctl->erase_line);
		else
			printf("%*s", ctl->prompt_len - col, "");
	}
	ctl->prompt_len = 0;
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

static int would_underline(char *s, int n)
{
	if (n < 2)
		return 0;
	if ((s[0] == '_' && s[1] == '\b') || (s[1] == '\b' && s[2] == '_'))
		return 1;
	return 0;
}

/* Print a buffer of n characters */
static void print_buf(struct more_control *ctl, char *s, int n)
{
	char c;	/* next output character */
	int state;	/* next output char's UL state */

	while (--n >= 0)
		if (!ctl->enable_underlining)
			putchar(*s++);
		else {
			if (*s == ' ' && ctl->underline_state == 0 && ctl->underline_glitch
			    && would_underline(s + 1, n - 1)) {
				s++;
				continue;
			}
			if ((state = would_underline(s, n)) != 0) {
				c = (*s == '_') ? s[2] : *s;
				n -= 2;
				s += 3;
			} else
				c = *s++;
			if (state != ctl->underline_state) {
				if (c == ' ' && state == 0 && ctl->underline_glitch
				    && would_underline(s, n - 1))
					state = 1;
				else
					putp(state ? ctl->enter_underline : ctl->exit_underline);
			}
			if (c != ' ' || ctl->underline_state == 0 || state != 0
			    || ctl->underline_glitch == 0)
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
			if (state && *ctl->underline_ch) {
				fputs(ctl->backspace_ch, stdout);
				putp(ctl->underline_ch);
			}
			ctl->underline_state = state;
		}
}

/* Erase the current line entirely */
static void kill_line(struct more_control *ctl)
{
	erase_prompt(ctl, 0);
	if (!ctl->erase_line || ctl->dumb_tty)
		putchar('\r');
}

static void output_prompt(struct more_control *ctl, char *filename)
{
	if (ctl->clear_line_ends)
		putp(ctl->erase_line);
	else if (ctl->prompt_len > 0)
		kill_line(ctl);
	if (!ctl->hard_tty) {
		ctl->prompt_len = 0;
		if (ctl->enter_std && ctl->exit_std) {
			putp(ctl->enter_std);
			ctl->prompt_len += (2 * ctl->stdout_glitch);
		}
		if (ctl->clear_line_ends)
			putp(ctl->erase_line);
		ctl->prompt_len += printf(_("--More--"));
		if (filename != NULL) {
			ctl->prompt_len += printf(_("(Next file: %s)"), filename);
		} else if (!ctl->no_tty_in) {
			ctl->prompt_len +=
			    printf("(%d%%)",
				   (int)((ctl->file_position * 100) / ctl->file_size));
		}
		if (ctl->suppress_bell) {
			ctl->prompt_len +=
			    printf(_("[Press space to continue, 'q' to quit.]"));
		}
		if (ctl->enter_std && ctl->exit_std)
			putp(ctl->exit_std);
		if (ctl->clear_line_ends)
			putp(ctl->clear_rest);
		fflush(stdout);
	} else
		fputc(RINGBELL, stderr);
	ctl->waiting_input++;
}

static void reset_tty(void)
{
	if (global_ctl->no_tty_out)
		return;
	if (global_ctl->underline_state) {
		putp(global_ctl->exit_underline);
		fflush(stdout);
		global_ctl->underline_state = 0;
	}
	global_ctl->output_tty.c_lflag |= ICANON | ECHO;
	global_ctl->output_tty.c_cc[VMIN] = global_ctl->original_tty.c_cc[VMIN];
	global_ctl->output_tty.c_cc[VTIME] = global_ctl->original_tty.c_cc[VTIME];
	tcsetattr(STDERR_FILENO, TCSANOW, &global_ctl->original_tty);
}

/* Clean up terminal state and exit. Also come here if interrupt signal received */
static void __attribute__((__noreturn__)) more_exit(int dummy __attribute__((__unused__)))
{
	/* May be executed as a signal handler as well as by main process.
	 *
	 * The _exit() may wait for pending I/O for really long time, be sure
	 * that signal handler is not executed in this time to avoid double
	 * de-initialization (free() calls, etc.).
	 */
	signal(SIGINT, SIG_IGN);

	reset_tty();
	if (global_ctl->clear_line_ends) {
		putchar('\r');
		putp(global_ctl->erase_line);
		fflush(stdout);
	} else if (!global_ctl->clear_line_ends && (global_ctl->prompt_len > 0)) {
		kill_line(global_ctl);
		fflush(stdout);
	} else
		fputc('\n', stderr);
	free(global_ctl->previous_search);
	free(global_ctl->line_buf);
	_exit(EXIT_SUCCESS);
}

static int read_user_input(struct more_control *ctl)
{
	unsigned char c;

	errno = 0;
	if (read(STDERR_FILENO, &c, 1) <= 0) {
		if (errno != EINTR)
			more_exit(0);
		else
			c = ctl->output_tty.c_cc[VKILL];
	}
	return c;
}

/* Read a decimal number from the terminal.  Set cmd to the non-digit
 * which terminates the number. */
static int read_number(struct more_control *ctl, char *cmd)
{
	int i;
	char ch;

	i = 0;
	ch = ctl->output_tty.c_cc[VKILL];
	for (;;) {
		ch = read_user_input(ctl);
		if (isdigit(ch))
			i = i * 10 + ch - '0';
		else if ((cc_t) ch == ctl->output_tty.c_cc[VKILL])
			i = 0;
		else {
			*cmd = ch;
			break;
		}
	}
	return i;
}

/* Change displayed file from command line list to next nskip, where nskip
 * is relative position in argv and can be negative, that is a previous
 * file.  */
static void change_file(struct more_control *ctl, int nskip)
{
	if (nskip == 0)
		return;
	if (nskip > 0) {
		if (ctl->argv_position + nskip > ctl->num_files - 1)
			nskip = ctl->num_files - ctl->argv_position - 1;
	} else if (ctl->within_file)
		ctl->argv_position++;
	ctl->argv_position += nskip;
	if (ctl->argv_position < 0)
		ctl->argv_position = 0;
	puts(_("\n...Skipping "));
	if (ctl->clear_line_ends)
		putp(ctl->erase_line);
	if (nskip > 0)
		fputs(_("...Skipping to file "), stdout);
	else
		fputs(_("...Skipping back to file "), stdout);
	puts(ctl->file_names[ctl->argv_position]);
	if (ctl->clear_line_ends)
		putp(ctl->erase_line);
	putchar('\n');
	ctl->argv_position--;
}

static void show(struct more_control *ctl, char c)
{
	if ((c < ' ' && c != '\n' && c != ESC) || c == RUBOUT) {
		c += (c == RUBOUT) ? -0100 : 0100;
		fputs(CARAT, stderr);
		ctl->prompt_len++;
	}
	fputc(c, stderr);
	ctl->prompt_len++;
}

static void more_error(struct more_control *ctl, char *mess)
{
	if (ctl->clear_line_ends)
		putp(ctl->erase_line);
	else
		kill_line(ctl);
	ctl->prompt_len += strlen(mess);
	if (ctl->enter_std && ctl->exit_std) {
		putp(ctl->enter_std);
		fputs(mess, stdout);
		putp(ctl->exit_std);
	} else
		fputs(mess, stdout);
	fflush(stdout);
	ctl->report_errors++;
	siglongjmp(ctl->destination, 1);
}

static void erase_one_column(struct more_control *ctl)
{
	if (ctl->erase_previous_ok)
		fputs(BSB, stderr);
	else
		fputs(BS, stderr);
}

static void ttyin(struct more_control *ctl, char buf[], int nmax, char pchar)
{
	char *sp;
	int c;
	int slash = 0;
	int maxlen;

	sp = buf;
	maxlen = 0;
	while (sp - buf < nmax) {
		if (ctl->prompt_len > maxlen)
			maxlen = ctl->prompt_len;
		c = read_user_input(ctl);
		if (c == '\\') {
			slash++;
		} else if (((cc_t) c == ctl->output_tty.c_cc[VERASE]) && !slash) {
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
						--ctl->prompt_len;
						--sp;
					}
				} else
#endif	/* HAVE_WIDECHAR */
				{
					--ctl->prompt_len;
					erase_one_column(ctl);
					--sp;
				}

				if ((*sp < ' ' && *sp != '\n') || *sp == RUBOUT) {
					--ctl->prompt_len;
					erase_one_column(ctl);
				}
				continue;
			} else {
				if (!ctl->erase_line)
					ctl->prompt_len = maxlen;
				siglongjmp(ctl->destination, 1);
			}
		} else if (((cc_t) c == ctl->output_tty.c_cc[VKILL]) && !slash) {
			if (ctl->hard_tty) {
				show(ctl, c);
				putchar('\n');
				putchar(pchar);
			} else {
				putchar('\r');
				putchar(pchar);
				if (ctl->erase_line)
					erase_prompt(ctl, 1);
				else if (ctl->erase_input_ok)
					while (ctl->prompt_len-- > 1)
						fputs(BSB, stderr);
				ctl->prompt_len = 1;
			}
			sp = buf;
			fflush(stdout);
			continue;
		}
		if (slash && ((cc_t) c == ctl->output_tty.c_cc[VKILL]
			      || (cc_t) c == ctl->output_tty.c_cc[VERASE])) {
			erase_one_column(ctl);
			--sp;
		}
		if (c != '\\')
			slash = 0;
		*sp++ = c;
		if ((c < ' ' && c != '\n' && c != ESC) || c == RUBOUT) {
			c += (c == RUBOUT) ? -0100 : 0100;
			fputs(CARAT, stderr);
			ctl->prompt_len++;
		}
		if (c != '\n' && c != ESC) {
			fputc(c, stderr);
			ctl->prompt_len++;
		} else
			break;
	}
	*--sp = '\0';
	if (!ctl->erase_line)
		ctl->prompt_len = maxlen;
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

	xtra = strlen(ctl->file_names[ctl->argv_position]) + strlen(ctl->shell_line) + 1;
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
			if (!ctl->no_tty_in) {
				strcpy(outstr, ctl->file_names[ctl->argv_position]);
				outstr += strlen(ctl->file_names[ctl->argv_position]);
				changed++;
			} else
				*outstr++ = c;
			break;
		case '!':
			if (!ctl->previous_shell)
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
	ctl->output_tty.c_lflag &= ~(ICANON | ECHO);
	ctl->output_tty.c_cc[VMIN] = 1;	/* read at least 1 char */
	ctl->output_tty.c_cc[VTIME] = 0;	/* no timeout */
	tcsetattr(STDERR_FILENO, TCSANOW, &ctl->output_tty);
}

/* Come here if a quit signal is received */
static void sigquit_handler(int dummy __attribute__((__unused__)))
{
	signal(SIGQUIT, SIG_IGN);
	if (!global_ctl->waiting_input) {
		putchar('\n');
		if (!global_ctl->starting_up) {
			signal(SIGQUIT, sigquit_handler);
			siglongjmp(global_ctl->destination, 1);
		} else
			global_ctl->is_paused = 1;
	} else if (!global_ctl->suppress_bell && global_ctl->no_quit_dialog) {
		global_ctl->prompt_len += fprintf(stderr, _("[Use q or Q to quit]"));
		global_ctl->no_quit_dialog = 0;
	}
	signal(SIGQUIT, sigquit_handler);
}

/* Come here when we get a suspend signal from the terminal */
static void sigtstp_handler(int dummy __attribute__((__unused__)))
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
	signal(SIGTSTP, sigtstp_handler);
	set_tty(global_ctl);
	if (global_ctl->waiting_input)
		siglongjmp(global_ctl->destination, 1);
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
		if (ctl->catch_suspend)
			signal(SIGTSTP, SIG_DFL);
		while (wait(NULL) > 0)
			/* nothing */ ;
		signal(SIGINT, more_exit);
		signal(SIGQUIT, sigquit_handler);
		if (ctl->catch_suspend)
			signal(SIGTSTP, sigtstp_handler);
	} else
		fputs(_("can't fork\n"), stderr);
	set_tty(ctl);
	puts("------------------------");
	output_prompt(ctl, filename);
}

static void run_shell(struct more_control *ctl, char *filename)
{
	char cmdbuf[COMMAND_BUF];
	int rc;
	char *expanded;

	kill_line(ctl);
	putchar('!');
	fflush(stdout);
	ctl->prompt_len = 1;
	if (ctl->run_previous_command)
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
			output_prompt(ctl, filename);
			return;
		} else if (rc > 0) {
			kill_line(ctl);
			ctl->prompt_len = printf("!%s", ctl->shell_line);
		}
	}
	fflush(stdout);
	fputc('\n', stderr);
	ctl->prompt_len = 0;
	ctl->previous_shell = 1;
	execute(ctl, filename, ctl->shell, ctl->shell, "-c", ctl->shell_line, 0);
}

/* Execute a colon-prefixed command.  Returns <0 if not a command that
 * should cause more of the file to be printed. */
static int colon_command(struct more_control *ctl, char *filename, int cmd, int nlines)
{
	char ch;

	if (cmd == 0)
		ch = read_user_input(ctl);
	else
		ch = cmd;
	ctl->last_colon_command = ch;
	switch (ch) {
	case 'f':
		kill_line(ctl);
		if (!ctl->no_tty_in)
			ctl->prompt_len =
			    printf(_("\"%s\" line %d"), ctl->file_names[ctl->argv_position], ctl->current_line);
		else
			ctl->prompt_len = printf(_("[Not a file] line %d"), ctl->current_line);
		fflush(stdout);
		return -1;
	case 'n':
		if (nlines == 0) {
			if (ctl->argv_position >= ctl->num_files - 1)
				more_exit(0);
			nlines++;
		}
		putchar('\r');
		erase_prompt(ctl, 0);
		change_file(ctl, nlines);
		return 0;
	case 'p':
		if (ctl->no_tty_in) {
			fputc(RINGBELL, stderr);
			return -1;
		}
		putchar('\r');
		erase_prompt(ctl, 0);
		if (nlines == 0)
			nlines++;
		change_file(ctl, -nlines);
		return 0;
	case '!':
		run_shell(ctl, filename);
		return -1;
	case 'q':
	case 'Q':
		more_exit(0);
	default:
		fputc(RINGBELL, stderr);
		return -1;
	}
}

/* Skip n lines in the file f */
static void skip_lines(struct more_control *ctl, int n, FILE *f)
{
	int c;

	while (n > 0) {
		while ((c = more_getc(ctl, f)) != '\n')
			if (c == EOF)
				return;
		n--;
		ctl->current_line++;
	}
}

/*  Clear the screen */
static void more_clear_screen(struct more_control *ctl)
{
	if (ctl->clear && !ctl->hard_tty) {
		putp(ctl->clear);
		/* Put out carriage return so that system doesn't get
		 * confused by escape sequences when expanding tabs */
		putchar('\r');
		ctl->prompt_len = 0;
	}
}

static void read_line(struct more_control *ctl, FILE *f)
{
	int c;
	char *p;

	prepare_line_buffer(ctl);

	p = ctl->line_buf;
	while ((c = more_getc(ctl, f)) != '\n' && c != EOF
	       && (size_t)(p - ctl->line_buf) < ctl->line_sz - 1)
		*p++ = c;
	if (c == '\n')
		ctl->current_line++;
	*p = '\0';
}

/* Search for nth occurrence of regular expression contained in buf in
 * the file */
static void search(struct more_control *ctl, char buf[], FILE *file, int n)
{
	long startline = ctl->file_position;
	long line1 = startline;
	long line2 = startline;
	long line3;
	int lncount;
	int saveln, rc;
	regex_t re;

	ctl->context.line_num = saveln = ctl->current_line;
	ctl->context.row_num = startline;
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
		line1 = ctl->file_position;
		read_line(ctl, file);
		lncount++;
		if (regexec(&re, ctl->line_buf, 0, NULL, 0) == 0) {
			if (--n == 0) {
				if (lncount > 3 || (lncount > 1 && ctl->no_tty_in)) {
					putchar('\n');
					if (ctl->clear_line_ends)
						putp(ctl->erase_line);
					fputs(_("...skipping\n"), stdout);
				}
				if (!ctl->no_tty_in) {
					ctl->current_line -=
					    (lncount >= 3 ? 3 : lncount);
					more_fseek(ctl, file, line3);
					if (ctl->no_scroll) {
						if (ctl->clear_line_ends) {
							putp(ctl->go_home);
							putp(ctl->erase_line);
						} else
							more_clear_screen(ctl);
					}
				} else {
					kill_line(ctl);
					if (ctl->no_scroll) {
						if (ctl->clear_line_ends) {
							putp(ctl->go_home);
							putp(ctl->erase_line);
						} else
							more_clear_screen(ctl);
					}
					puts(ctl->line_buf);
				}
				break;
			}
		}
	}
	regfree(&re);
	if (feof(file)) {
		if (!ctl->no_tty_in) {
			ctl->current_line = saveln;
			more_fseek(ctl, file, startline);
		} else {
			fputs(_("\nPattern not found\n"), stdout);
			more_exit(0);
		}
		free(ctl->previous_search);
		ctl->previous_search = NULL;
notfound:
		more_error(ctl, _("Pattern not found"));
	}
}

/* Read a command and do it.  A command consists of an optional integer
 * argument followed by the command character.  Return the number of
 * lines to display in the next screenful.  If there is nothing more to
 * display in the current file, zero is returned. */
static int more_key_command(struct more_control *ctl, char *filename, FILE *f)
{
	int nlines;
	int retval = 0;
	int c;
	char colonch;
	int done;
	char comchar, cmdbuf[INIT_BUF];

	done = 0;
	if (!ctl->report_errors)
		output_prompt(ctl, filename);
	else
		ctl->report_errors = 0;
	for (;;) {
		nlines = read_number(ctl, &comchar);
		ctl->run_previous_command = colonch = 0;
		if (comchar == '.') {	/* Repeat last command */
			ctl->run_previous_command++;
			comchar = ctl->last_key_command;
			nlines = ctl->last_key_arg;
			if (ctl->last_key_command == ':')
				colonch = ctl->last_colon_command;
		}
		ctl->last_key_command = comchar;
		ctl->last_key_arg = nlines;
		if ((cc_t) comchar == ctl->output_tty.c_cc[VERASE]) {
			kill_line(ctl);
			output_prompt(ctl, filename);
			continue;
		}
		switch (comchar) {
		case ':':
			retval = colon_command(ctl, filename, colonch, nlines);
			if (retval >= 0)
				done++;
			break;
		case 'b':
		case ctrl('B'):
			{
				int initline;

				if (ctl->no_tty_in) {
					fputc(RINGBELL, stderr);
					return -1;
				}

				if (nlines == 0)
					nlines++;

				putchar('\r');
				erase_prompt(ctl, 0);
				putchar('\n');
				if (ctl->clear_line_ends)
					putp(ctl->erase_line);
				printf(P_("...back %d page",
					"...back %d pages", nlines),
					nlines);
				if (ctl->clear_line_ends)
					putp(ctl->erase_line);
				putchar('\n');

				initline = ctl->current_line - ctl->lines_per_screen * (nlines + 1);
				if (!ctl->no_scroll)
					--initline;
				if (initline < 0)
					initline = 0;
				more_fseek(ctl, f, 0L);
				ctl->current_line = 0;	/* skip_lines() will make current_line correct */
				skip_lines(ctl, initline, f);
				if (!ctl->no_scroll) {
					retval = ctl->lines_per_screen + 1;
				} else {
					retval = ctl->lines_per_screen;
				}
				done = 1;
				break;
			}
		case ' ':
		case 'z':
			if (nlines == 0)
				nlines = ctl->lines_per_screen;
			else if (comchar == 'z')
				ctl->lines_per_screen = nlines;
			retval = nlines;
			done = 1;
			break;
		case 'd':
		case ctrl('D'):
			if (nlines != 0)
				ctl->d_scroll_len = nlines;
			retval = ctl->d_scroll_len;
			done = 1;
			break;
		case 'q':
		case 'Q':
			more_exit(0);
		case 's':
		case 'f':
		case ctrl('F'):
			if (nlines == 0)
				nlines++;
			if (comchar == 'f')
				nlines *= ctl->lines_per_screen;
			putchar('\r');
			erase_prompt(ctl, 0);
			putchar('\n');
			if (ctl->clear_line_ends)
				putp(ctl->erase_line);
			printf(P_("...skipping %d line",
				"...skipping %d lines", nlines),
				nlines);

			if (ctl->clear_line_ends)
				putp(ctl->erase_line);
			putchar('\n');

			while (nlines > 0) {
				while ((c = more_getc(ctl, f)) != '\n')
					if (c == EOF) {
						retval = 0;
						done++;
						goto endsw;
					}
				ctl->current_line++;
				nlines--;
			}
			retval = ctl->lines_per_screen;
			done = 1;
			break;
		case '\n':
			if (nlines != 0)
				ctl->lines_per_screen = nlines;
			else
				nlines = 1;
			retval = nlines;
			done = 1;
			break;
		case '\f':
			if (!ctl->no_tty_in) {
				more_clear_screen(ctl);
				more_fseek(ctl, f, ctl->screen_start.row_num);
				ctl->current_line = ctl->screen_start.line_num;
				retval = ctl->lines_per_screen;
				done = 1;
				break;
			} else {
				fputc(RINGBELL, stderr);
				break;
			}
		case '\'':
			if (!ctl->no_tty_in) {
				kill_line(ctl);
				fputs(_("\n***Back***\n\n"), stdout);
				more_fseek(ctl, f, ctl->context.row_num);
				ctl->current_line = ctl->context.line_num;
				retval = ctl->lines_per_screen;
				done = 1;
				break;
			} else {
				fputc(RINGBELL, stderr);
				break;
			}
		case '=':
			kill_line(ctl);
			ctl->prompt_len = printf("%d", ctl->current_line);
			fflush(stdout);
			break;
		case 'n':
			if (!ctl->previous_search) {
				more_error(ctl, _("No previous regular expression"));
				break;
			}
			ctl->run_previous_command++;
			/* fallthrough */
		case '/':
			if (nlines == 0)
				nlines++;
			kill_line(ctl);
			putchar('/');
			ctl->prompt_len = 1;
			fflush(stdout);
			if (ctl->run_previous_command) {
				fputc('\r', stderr);
				search(ctl, ctl->previous_search, f, nlines);
			} else {
				ttyin(ctl, cmdbuf, sizeof(cmdbuf) - 2, '/');
				fputc('\r', stderr);
				free(ctl->previous_search);
				ctl->previous_search = xstrdup(cmdbuf);
				search(ctl, cmdbuf, f, nlines);
			}
			retval = ctl->lines_per_screen - 1;
			done = 1;
			break;
		case '!':
			run_shell(ctl, filename);
			break;
		case '?':
		case 'h':
			if (ctl->no_scroll)
				more_clear_screen(ctl);
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
			output_prompt(ctl, filename);
			break;
		case 'v':	/* This case should go right before default */
			if (!ctl->no_tty_in) {
				/* Earlier: call vi +n file. This also
				 * works for emacs.  POSIX: call vi -c n
				 * file (when editor is vi or ex). */
				char *editor, *p;
				int n = (ctl->current_line - ctl->lines_per_screen <= 0 ? 1 :
					 ctl->current_line - (ctl->lines_per_screen + 1) / 2);
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
				       ctl->file_names[ctl->argv_position]);
				if (split) {
					cmdbuf[2] = 0;
					execute(ctl, filename, editor, editor,
						cmdbuf, cmdbuf + 3,
						ctl->file_names[ctl->argv_position], (char *)0);
				} else
					execute(ctl, filename, editor, editor,
						cmdbuf, ctl->file_names[ctl->argv_position],
						(char *)0);
				break;
			}
			/* fallthrough */
		default:
			if (ctl->suppress_bell) {
				kill_line(ctl);
				if (ctl->enter_std && ctl->exit_std) {
					putp(ctl->enter_std);
					ctl->prompt_len =
					    printf(_
						   ("[Press 'h' for instructions.]"))
					    + 2 * ctl->stdout_glitch;
					putp(ctl->exit_std);
				} else
					ctl->prompt_len =
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
	ctl->waiting_input = 0;
	ctl->no_quit_dialog = 1;
	return retval;
}

/* Print out the contents of the file f, one screenful at a time. */
static void screen(struct more_control *ctl, FILE *f, int num_lines)
{
	int c;
	int nchars;
	int length;			/* length of current line */
	static int prev_len = 1;	/* length of previous line */

	for (;;) {
		while (num_lines > 0 && !ctl->is_paused) {
			if ((nchars = get_line(ctl, f, &length)) == EOF) {
				if (ctl->clear_line_ends)
					putp(ctl->clear_rest);
				return;
			}
			if (ctl->squeeze_spaces && length == 0 && prev_len == 0)
				continue;
			prev_len = length;
			if (ctl->bad_stdout
			    || ((ctl->enter_std && *ctl->enter_std == ' ') && (ctl->prompt_len > 0)))
				erase_prompt(ctl, 0);
			/* must clear before drawing line since tabs on
			 * some terminals do not erase what they tab
			 * over. */
			if (ctl->clear_line_ends)
				putp(ctl->erase_line);
			print_buf(ctl, ctl->line_buf, length);
			if (nchars < ctl->prompt_len)
				erase_prompt(ctl, nchars);	/* erase_prompt () sets prompt_len to 0 */
			else
				ctl->prompt_len = 0;
			if (nchars < ctl->num_columns || !ctl->fold_long_lines)
				print_buf(ctl, "\n", 1);	/* will turn off UL if necessary */
			num_lines--;
		}
		if (ctl->underline_state) {
			putp(ctl->exit_underline);
			ctl->underline_state = 0;
		}
		fflush(stdout);
		if ((c = more_getc(ctl, f)) == EOF) {
			if (ctl->clear_line_ends)
				putp(ctl->clear_rest);
			return;
		}

		if (ctl->is_paused && ctl->clear_line_ends)
			putp(ctl->clear_rest);
		more_ungetc(ctl, c, f);
		sigsetjmp(ctl->destination, 1);
		ctl->is_paused = 0;
		ctl->starting_up = 0;
		if ((num_lines = more_key_command(ctl, NULL, f)) == 0)
			return;
		if (ctl->hard_tty && ctl->prompt_len > 0)
			erase_prompt(ctl, 0);
		if (ctl->no_scroll && num_lines >= ctl->lines_per_screen) {
			if (ctl->clear_line_ends)
				putp(ctl->go_home);
			else
				more_clear_screen(ctl);
		}
		ctl->screen_start.line_num = ctl->current_line;
		ctl->screen_start.row_num = ctl->file_position;
	}
}

/* Come here if a signal for a window size change is received */
static void sigwinch_handler(int dummy __attribute__((__unused__)))
{
	struct winsize win;

	signal(SIGWINCH, SIG_IGN);
	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &win) != -1) {
		if (win.ws_row != 0) {
			global_ctl->lines_per_page = win.ws_row;
			global_ctl->d_scroll_len = global_ctl->lines_per_page / 2 - 1;
			if (global_ctl->d_scroll_len <= 0)
				global_ctl->d_scroll_len = 1;
			global_ctl->lines_per_screen = global_ctl->lines_per_page - 1;
		}
		if (win.ws_col != 0)
			global_ctl->num_columns = win.ws_col;
	}
	signal(SIGWINCH, sigwinch_handler);
}

static void copy_file(FILE *f)
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
	ctl->no_tty_out = tcgetattr(STDOUT_FILENO, &ctl->output_tty);
#endif
	if (!ctl->no_tty_out) {
		ctl->erase_previous_ok = (ctl->output_tty.c_cc[VERASE] != 255);
		ctl->erase_input_ok = (ctl->output_tty.c_cc[VKILL] != 255);
		if ((term = getenv("TERM")) == NULL) {
			ctl->dumb_tty = 1;
			ctl->enable_underlining = 0;
		}
		setupterm(term, 1, &ret);
		if (ret <= 0) {
			ctl->dumb_tty = 1;
			ctl->enable_underlining = 0;
		} else {
			if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &win) < 0) {
				ctl->lines_per_page = tigetnum(TERM_LINES);
				ctl->num_columns = tigetnum(TERM_COLS);
			} else {
				if ((ctl->lines_per_page = win.ws_row) == 0)
					ctl->lines_per_page = tigetnum(TERM_LINES);
				if ((ctl->num_columns = win.ws_col) == 0)
					ctl->num_columns = tigetnum(TERM_COLS);
			}
			if ((ctl->lines_per_page <= 0) || tigetflag(TERM_HARD_COPY)) {
				ctl->hard_tty = 1;
				ctl->lines_per_page = LINES_PER_PAGE;
			}

			if (tigetflag(TERM_EAT_NEW_LINE))
				/* Eat newline at last column + 1; dec, concept */
				ctl->eat_newline++;
			if (ctl->num_columns <= 0)
				ctl->num_columns = NUM_COLUMNS;

			ctl->wrap_margin = tigetflag(TERM_AUTO_RIGHT_MARGIN);
			ctl->bad_stdout = tigetflag(TERM_CEOL);
			ctl->erase_line = tigetstr(TERM_CLEAR_TO_LINE_END);
			ctl->clear = tigetstr(TERM_CLEAR);
			ctl->enter_std = tigetstr(TERM_STANDARD_MODE);
			ctl->exit_std = tigetstr(TERM_EXIT_STANDARD_MODE);
			if (0 < tigetnum(TERM_STD_MODE_GLITCH))
				ctl->stdout_glitch = 1;

			/* Set up for underlining:  some terminals don't
			 * need it; others have start/stop sequences,
			 * still others have an underline char sequence
			 * which is assumed to move the cursor forward
			 * one character.  If underline sequence isn't
			 * available, settle for standout sequence. */
			if (tigetflag(TERM_UNDERLINE)
			    || tigetflag(TERM_OVER_STRIKE))
				ctl->enable_underlining = 0;
			if ((ctl->underline_ch = tigetstr(TERM_UNDERLINE_CHAR)) == NULL)
				ctl->underline_ch = "";
			if (((ctl->enter_underline =
			      tigetstr(TERM_ENTER_UNDERLINE)) == NULL
			     || (ctl->exit_underline =
				 tigetstr(TERM_EXIT_UNDERLINE)) == NULL)
			    && !*ctl->underline_ch) {
				if ((ctl->enter_underline = ctl->enter_std) == NULL
				    || (ctl->exit_underline = ctl->exit_std) == NULL) {
					ctl->enter_underline = "";
					ctl->exit_underline = "";
				} else
					ctl->underline_glitch = ctl->stdout_glitch;
			} else {
				ctl->underline_glitch = 0;
			}
			ctl->go_home = tigetstr(TERM_HOME);
			if (ctl->go_home == NULL || *ctl->go_home == '\0') {
				if ((ctl->cursor_addr =
				     tigetstr(TERM_CURSOR_ADDRESS)) != NULL) {
					const char *t =
					    (const char *)tparm(ctl->cursor_addr, 0,
								   0);
					xstrncpy(ctl->home_position, t,
						 sizeof(ctl->home_position));
					ctl->go_home = ctl->home_position;
				}
			}
			ctl->clear_rest = tigetstr(TERM_CLEAR_TO_SCREEN_END);
			if ((ctl->backspace_ch = tigetstr(TERM_LINE_DOWN)) == NULL)
				ctl->backspace_ch = BS;

		}
		if ((ctl->shell = getenv("SHELL")) == NULL)
			ctl->shell = "/bin/sh";
	}
	ctl->no_tty_in = tcgetattr(STDIN_FILENO, &ctl->output_tty);
	tcgetattr(STDERR_FILENO, &ctl->output_tty);
	ctl->original_tty = ctl->output_tty;
	ctl->hard_tabs = (ctl->output_tty.c_oflag & TABDLY) != TAB3;
	if (!ctl->no_tty_out) {
		ctl->output_tty.c_lflag &= ~(ICANON | ECHO);
		ctl->output_tty.c_cc[VMIN] = 1;
		ctl->output_tty.c_cc[VTIME] = 0;
	}
}

int main(int argc, char **argv)
{
	FILE *f;
	char *s;
	int chr;
	int left;
	int print_names = 0;
	int init = 0;
	int search_at_start = 0;
	int skip_file = 0;
	int start_at_line = 0;
	char *initbuf = NULL;
	struct more_control ctl = {
		.first_file = 1,
		.fold_long_lines = 1,
		.no_quit_dialog = 1,
		.starting_up = 1,
		.stop_after_formfeed = 1,
		.enable_underlining = 1,
		.wrap_margin = 1,
		.lines_per_page = LINES_PER_PAGE,
		.num_columns = NUM_COLUMNS,
		.d_scroll_len = SCROLL_LEN,
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

	ctl.num_files = argc;
	ctl.file_names = argv;
	setlocale(LC_ALL, "");
	initterm(&ctl);

	/* Auto set no scroll on when binary is called page */
	if (!(strcmp(program_invocation_short_name, "page")))
		ctl.no_scroll++;

	prepare_line_buffer(&ctl);

	ctl.d_scroll_len = ctl.lines_per_page / 2 - 1;
	if (ctl.d_scroll_len <= 0)
		ctl.d_scroll_len = 1;

	if ((s = getenv("MORE")) != NULL)
		arg_parser(&ctl, s);

	while (--ctl.num_files > 0) {
		if ((chr = (*++ctl.file_names)[0]) == '-') {
			arg_parser(&ctl, *ctl.file_names + 1);
		} else if (chr == '+') {
			s = *ctl.file_names;
			if (*++s == '/') {
				search_at_start++;
				initbuf = xstrdup(s + 1);
			} else {
				init++;
				for (start_at_line = 0; *s != '\0'; s++)
					if (isdigit(*s))
						start_at_line =
						    start_at_line * 10 + *s - '0';
				--start_at_line;
			}
		} else
			break;
	}
	/* allow clear_line_ends only if go_home and erase_line and clear_rest strings are
	 * defined, and in that case, make sure we are in no_scroll mode */
	if (ctl.clear_line_ends) {
		if ((ctl.go_home == NULL) || (*ctl.go_home == '\0') ||
		    (ctl.erase_line == NULL) || (*ctl.erase_line == '\0') ||
		    (ctl.clear_rest == NULL) || (*ctl.clear_rest == '\0'))
			ctl.clear_line_ends = 0;
		else
			ctl.no_scroll = 1;
	}
	if (ctl.lines_per_screen == 0)
		ctl.lines_per_screen = ctl.lines_per_page - 1;
	left = ctl.lines_per_screen;
	if (ctl.num_files > 1)
		print_names++;
	if (!ctl.no_tty_in && ctl.num_files == 0) {
		warnx(_("bad usage"));
		errtryhelp(EXIT_FAILURE);
	} else
		f = stdin;
	if (!ctl.no_tty_out) {
		signal(SIGQUIT, sigquit_handler);
		signal(SIGINT, more_exit);
		signal(SIGWINCH, sigwinch_handler);
		if (signal(SIGTSTP, SIG_IGN) == SIG_DFL) {
			signal(SIGTSTP, sigtstp_handler);
			ctl.catch_suspend++;
		}
		tcsetattr(STDERR_FILENO, TCSANOW, &ctl.output_tty);
	}
	if (ctl.no_tty_in) {
		if (ctl.no_tty_out)
			copy_file(stdin);
		else {
			if ((chr = getc(f)) == '\f')
				more_clear_screen(&ctl);
			else {
				ungetc(chr, f);
				if (ctl.no_scroll && (chr != EOF)) {
					if (ctl.clear_line_ends)
						putp(ctl.go_home);
					else
						more_clear_screen(&ctl);
				}
			}
			if (search_at_start) {
				free(ctl.previous_search);
				ctl.previous_search = xstrdup(initbuf);
				search(&ctl, initbuf, stdin, 1);
				if (ctl.no_scroll)
					left--;
			} else if (init)
				skip_lines(&ctl, start_at_line, stdin);
			screen(&ctl, stdin, left);
		}
		ctl.no_tty_in = 0;
		print_names++;
		ctl.first_file = 0;
	}

	while (ctl.argv_position < ctl.num_files) {
		if ((f = checkf(&ctl, ctl.file_names[ctl.argv_position], &skip_file)) != NULL) {
			ctl.context.line_num = ctl.context.row_num = 0;
			ctl.current_line = 0;
			if (ctl.first_file)
				sigsetjmp(ctl.destination, 1);
			if (ctl.first_file) {
				ctl.first_file = 0;
				if (search_at_start) {
					free(ctl.previous_search);
					ctl.previous_search = xstrdup(initbuf);
					search(&ctl, initbuf, f, 1);
					if (ctl.no_scroll)
						left--;
				} else if (init)
					skip_lines(&ctl, start_at_line, f);
			} else if (ctl.argv_position < ctl.num_files && !ctl.no_tty_out) {
				sigsetjmp(ctl.destination, 1);
				left = more_key_command(&ctl, ctl.file_names[ctl.argv_position], f);
			}
			if (left != 0) {
				if ((ctl.no_scroll || skip_file)
				    && (ctl.file_size != LONG_MAX)) {
					if (ctl.clear_line_ends)
						putp(ctl.go_home);
					else
						more_clear_screen(&ctl);
				}
				if (print_names) {
					if (ctl.bad_stdout)
						erase_prompt(&ctl, 0);
					if (ctl.clear_line_ends)
						putp(ctl.erase_line);
					fputs("::::::::::::::", stdout);
					if (ctl.prompt_len > 14)
						erase_prompt(&ctl, 14);
					putchar('\n');
					if (ctl.clear_line_ends)
						putp(ctl.erase_line);
					puts(ctl.file_names[ctl.argv_position]);
					if (ctl.clear_line_ends)
						putp(ctl.erase_line);
					fputs("::::::::::::::\n", stdout);
					if (left > ctl.lines_per_page - 4)
						left = ctl.lines_per_page - 4;
				}
				if (ctl.no_tty_out)
					copy_file(f);
				else {
					ctl.within_file = 1;
					screen(&ctl, f, left);
					ctl.within_file = 0;
				}
			}
			sigsetjmp(ctl.destination, 1);
			fflush(stdout);
			fclose(f);
			ctl.screen_start.line_num = ctl.screen_start.row_num = 0L;
			ctl.context.line_num = ctl.context.row_num = 0L;
		}
		ctl.argv_position++;
		ctl.first_file = 0;
	}
	free(ctl.previous_search);
	free(initbuf);
	free(ctl.line_buf);
	reset_tty();
	exit(EXIT_SUCCESS);
}
