/*
 * Copyright (c) 1989, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Kim Letkeman.
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

/* 1999-02-01	Jean-Francois Bignolles: added option '-m' to display
 *		monday as the first day of the week.
 * 1999-02-22 Arkadiusz Miśkiewicz <misiek@pld.ORG.PL>
 * - added Native Language Support
 *
 * 2000-09-01  Michael Charles Pruznick <dummy@netwiz.net>
 *             Added "-3" option to print prev/next month with current.
 *             Added over-ridable default NUM_MONTHS and "-1" option to
 *             get traditional output when -3 is the default.  I hope that
 *             enough people will like -3 as the default that one day the
 *             product can be shipped that way.
 *
 * 2001-05-07  Pablo Saratxaga <pablo@mandrakesoft.com>
 *             Fixed the bugs with multi-byte charset (zg: cjk, utf-8)
 *             displaying. made the 'month year' ("%s %d") header translatable
 *             so it can be adapted to conventions used by different languages
 *             added support to read "first_weekday" locale information
 *             still to do: support for 'cal_direction' (will require a major
 *             rewrite of the displaying) and proper handling of RTL scripts
 */

#include <sys/types.h>

#include <ctype.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>

#include "c.h"
#include "closestream.h"
#include "colors.h"
#include "nls.h"
#include "mbsalign.h"
#include "strutils.h"

#if defined(HAVE_LIBNCURSES) || defined(HAVE_LIBNCURSESW)
# ifdef HAVE_NCURSES_H
#  include <ncurses.h>
# elif defined(HAVE_NCURSES_NCURSES_H)
#  include <ncurses/ncurses.h>
# endif
# include <term.h>

static void my_setupterm(const char *term, int fildes, int *errret)
{
	setupterm((char *)term, fildes, errret);
}

static void my_putstring(char *s)
{
	putp(s);
}

static const char *my_tgetstr(char *s __attribute__((__unused__)), char *ss)
{
	const char *ret = tigetstr(ss);
	if (!ret || ret == (char *)-1)
		return "";

	return ret;
}

#elif defined(HAVE_LIBTERMCAP)
# include <termcap.h>

static char termbuffer[4096];
static char tcbuffer[4096];
static char *strbuf = termbuffer;

static void my_setupterm(const char *term, int fildes __attribute__((__unused__)), int *errret)
{
	*errret = tgetent(tcbuffer, term);
}

static void my_putstring(char *s)
{
	tputs(s, 1, putchar);
}

static const char *my_tgetstr(char *s, char *ss __attribute__((__unused__)))
{
	const char *ret = tgetstr(s, &strbuf);
	if (!ret)
		return "";

	return ret;
}

#else	/* ! (HAVE_LIBTERMCAP || HAVE_LIBNCURSES || HAVE_LIBNCURSESW) */

static void my_putstring(char *s)
{
	fputs(s, stdout);
}

#endif	/* end of LIBTERMCAP / NCURSES */

#if defined(HAVE_LIBNCURSES) || defined(HAVE_LIBNCURSESW) || defined(HAVE_LIBTERMCAP)
static const char	*term="";
static int		Slen;		/* strlen of Senter+Sexit */
#endif

static const char	*Senter="", *Sexit="";/* enter and exit standout mode */
static char		*Hrow;		/* pointer to highlighted row in month */

#include "widechar.h"

/* allow compile-time define to over-ride default */
#ifndef NUM_MONTHS
# define NUM_MONTHS 1
#endif

#if ( NUM_MONTHS != 1 && NUM_MONTHS !=3 )
# error NUM_MONTHS must be 1 or 3
#endif

enum {
	SUNDAY = 0,
	MONDAY,
	TUESDAY,
	WEDNESDAY,
	THURSDAY,
	FRIDAY,
	SATURDAY,
	DAYS_IN_WEEK,
	NONEDAY
};

#define	FIRST_WEEKDAY		SATURDAY	/* Jan 1st, 1 was a Saturday */
#define REFORMATION_YEAR	1752		/* Signed-off-by: Lord Chesterfield */
#define REFORMATION_MONTH	9		/* September */
#define	FIRST_MISSING_DAY	639799		/* 3 Sep 1752 */
#define	NUMBER_MISSING_DAYS	11		/* 11 day correction */
#define YDAY_AFTER_MISSING	258             /* 14th in Sep 1752 */

#define DAYS_IN_YEAR		365		/* the common case, leap years are calculated */
#define MONTHS_IN_YEAR		12
#define DAYS_IN_MONTH		31
#define	MAXDAYS			42		/* slots in a month array */
#define	SPACE			-1		/* used in day array */

#define SMALLEST_YEAR		1

#define	DAY_LEN			3		/* 3 spaces per day */
#define	WEEK_LEN		(DAYS_IN_WEEK * DAY_LEN)
#define	HEAD_SEP		2
#define MONTH_COLS		3		/* month columns in year view */
#define WNUM_LEN                3

#define TODAY_FLAG		0x400		/* flag day for highlighting */

#define FMT_ST_LINES 9
#define FMT_ST_CHARS 300	/* 90 suffices in most locales */
struct fmt_st
{
	char s[FMT_ST_LINES][FMT_ST_CHARS];
};

static const int days_in_month[2][13] = {
	{0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31},
	{0, 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31},
};

/* September 1752 is special, and has static assignments for both date
 * and Julian representations.  */
static const int d_sep1752[MAXDAYS / 2] = {
	SPACE,	SPACE,	1,	2,	14,	15,	16,
	17,	18,	19,	20,	21,	22,	23,
	24,	25,	26,	27,	28,	29,	30
}, j_sep1752[MAXDAYS / 2] = {
	SPACE,	SPACE,	245,	246,	258,	259,	260,
	261,	262,	263,	264,	265,	266,	267,
	268,	269,	270,	271,	272,	273,	274
}, empty[MAXDAYS] = {
	SPACE,	SPACE,	SPACE,	SPACE,	SPACE,	SPACE,	SPACE,
	SPACE,	SPACE,	SPACE,	SPACE,	SPACE,	SPACE,	SPACE,
	SPACE,	SPACE,	SPACE,	SPACE,	SPACE,	SPACE,	SPACE,
	SPACE,	SPACE,	SPACE,	SPACE,	SPACE,	SPACE,	SPACE,
	SPACE,	SPACE,	SPACE,	SPACE,	SPACE,	SPACE,	SPACE,
	SPACE,	SPACE,	SPACE,	SPACE,	SPACE,	SPACE,	SPACE
};

enum {
	WEEK_NUM_DISABLED = 0,
	WEEK_NUM_MASK=0xff,
	WEEK_NUM_ISO=0x100,
	WEEK_NUM_US=0x200,
};

/* utf-8 can have up to 6 bytes per char; and an extra byte for ending \0 */
static char day_headings[(WEEK_LEN + 1) * 6 + 1];

struct cal_control {
	const char *full_month[MONTHS_IN_YEAR];	/* month names */
	int colormode;			/* day and week number highlight */
	int num_months;			/* number of months horizontally in print out */
	int weekstart;			/* day the week starts, often Sun or Mon */
	int weektype;			/* WEEK_TYPE_{NONE,ISO,US} */
	int weeknum;			/* requested --week=<number> */
	size_t day_width;		/* day width in characters in printout */
	size_t week_width;		/* 7 * day_width + possible week num */
	unsigned int	julian:1,	/* julian output */
			yflag:1;	/* print whole year */
};

/* function prototypes */
static int leap_year(long year);
static void headers_init(struct cal_control *ctl);
static int do_monthly(int day, int month, long year, struct fmt_st *out, int header_hint,
		      const struct cal_control *ctl);
static void monthly(int day, int month, long year, const struct cal_control *ctl);
static int two_header_lines(int month, long year, const struct cal_control *ctl);
static void monthly3(int day, int month, long year, const struct cal_control *ctl);
static char *append_weeknum(char *p, int *dp, int month, long year, int cal, int row,
			    const struct cal_control *ctl);
static void yearly(int day, long year, const struct cal_control *ctl);
static void day_array(int day, int month, long year, int *days, const struct cal_control *ctl);
static int day_in_year(int day, int month, long year);
static int day_in_week(int day, int month, long year);
static int week_number(int day, int month, long year, const struct cal_control *ctl);
static int week_to_day(long year, const struct cal_control *ctl);
static char *ascii_day(char *p, int day, const struct cal_control *ctl);
static char *ascii_weeknum(char *p, int weeknum, const struct cal_control *ctl);
static int center_str(const char *src, char *dest, size_t dest_size, size_t width);
static void center(const char *str, size_t len, int separate);
static void __attribute__((__noreturn__)) usage(FILE *out);

int main(int argc, char **argv)
{
	struct tm *local_time;
	time_t now;
	int ch, day = 0, month = 0;
	long year;
	static struct cal_control ctl = {
		.weekstart = SUNDAY,
		.num_months = NUM_MONTHS,
		.colormode = UL_COLORMODE_AUTO,
		.weektype = WEEK_NUM_DISABLED,
		.day_width = DAY_LEN
	};

	enum {
		OPT_COLOR = CHAR_MAX + 1
	};

	static const struct option longopts[] = {
		{"one", no_argument, NULL, '1'},
		{"three", no_argument, NULL, '3'},
		{"sunday", no_argument, NULL, 's'},
		{"monday", no_argument, NULL, 'm'},
		{"julian", no_argument, NULL, 'j'},
		{"year", no_argument, NULL, 'y'},
		{"week", optional_argument, NULL, 'w'},
		{"color", optional_argument, NULL, OPT_COLOR},
		{"version", no_argument, NULL, 'V'},
		{"help", no_argument, NULL, 'h'},
		{NULL, 0, NULL, 0}
	};

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

#if defined(HAVE_LIBNCURSES) || defined(HAVE_LIBNCURSESW) || defined(HAVE_LIBTERMCAP)
	if ((term = getenv("TERM"))) {
		int ret;
		my_setupterm(term, STDOUT_FILENO, &ret);
		if (ret > 0) {
			Senter = my_tgetstr("so","smso");
			Sexit = my_tgetstr("se","rmso");
			Slen = strlen(Senter) + strlen(Sexit);
		}
	}
#endif

/*
 * The traditional Unix cal utility starts the week at Sunday,
 * while ISO 8601 starts at Monday. We read the start day from
 * the locale database, which can be overridden with the
 * -s (Sunday) or -m (Monday) options.
 */
#if HAVE_DECL__NL_TIME_WEEK_1STDAY
	/*
	 * You need to use 2 locale variables to get the first day of the week.
	 * This is needed to support first_weekday=2 and first_workday=1 for
	 * the rare case where working days span across 2 weeks.
	 * This shell script shows the combinations and calculations involved:
	 *
	 * for LANG in en_US ru_RU fr_FR csb_PL POSIX; do
	 *   printf "%s:\t%s + %s -1 = " $LANG $(locale week-1stday first_weekday)
	 *   date -d"$(locale week-1stday) +$(($(locale first_weekday)-1))day" +%w
	 * done
	 *
	 * en_US:  19971130 + 1 -1 = 0  #0 = sunday
	 * ru_RU:  19971130 + 2 -1 = 1
	 * fr_FR:  19971201 + 1 -1 = 1
	 * csb_PL: 19971201 + 2 -1 = 2
	 * POSIX:  19971201 + 7 -1 = 0
	 */
	{
		int wfd;
		union { unsigned int word; char *string; } val;
		val.string = nl_langinfo(_NL_TIME_WEEK_1STDAY);

		wfd = val.word;
		wfd = day_in_week(wfd % 100, (wfd / 100) % 100, wfd / (100 * 100));
		ctl.weekstart = (wfd + *nl_langinfo(_NL_TIME_FIRST_WEEKDAY) - 1) % DAYS_IN_WEEK;
	}
#endif

	while ((ch = getopt_long(argc, argv, "13mjsywVh", longopts, NULL)) != -1)
		switch(ch) {
		case '1':
			ctl.num_months = 1;		/* default */
			break;
		case '3':
			ctl.num_months = 3;
			break;
		case 's':
			ctl.weekstart = SUNDAY;		/* default */
			break;
		case 'm':
			ctl.weekstart = MONDAY;
			break;
		case 'j':
			ctl.julian = 1;
			ctl.day_width = DAY_LEN + 1;
			break;
		case 'y':
			ctl.yflag = 1;
			break;
		case 'w':
			if (optarg) {
				ctl.weeknum = strtos32_or_err(optarg,
						_("invalid week argument"));
				if (ctl.weeknum < 1 || 53 < ctl.weeknum)
					errx(EXIT_FAILURE,_("illegal week value: use 1-53"));
			}
			ctl.weektype = WEEK_NUM_US;	/* default per weekstart */
			break;
		case OPT_COLOR:
			if (optarg)
				ctl.colormode = colormode_or_err(optarg,
						_("unsupported color mode"));
			break;
		case 'V':
			printf(UTIL_LINUX_VERSION);
			return EXIT_SUCCESS;
		case 'h':
			usage(stdout);
		case '?':
		default:
			usage(stderr);
		}
	argc -= optind;
	argv += optind;

	if (ctl.weektype) {
		ctl.weektype = ctl.weeknum & WEEK_NUM_MASK;
		ctl.weektype |= (ctl.weekstart == MONDAY ? WEEK_NUM_ISO : WEEK_NUM_US);
		ctl.week_width = (ctl.day_width * DAYS_IN_WEEK) + WNUM_LEN;
	} else
		ctl.week_width = ctl.day_width * DAYS_IN_WEEK;

	time(&now);
	local_time = localtime(&now);

	switch(argc) {
	case 3:
		day = strtos32_or_err(*argv++, _("illegal day value"));
		if (day < 1 || DAYS_IN_MONTH < day)
			errx(EXIT_FAILURE, _("illegal day value: use 1-%d"), DAYS_IN_MONTH);
		/* FALLTHROUGH */
	case 2:
		month = strtos32_or_err(*argv++, _("illegal month value: use 1-12"));
		if (month < 1 || MONTHS_IN_YEAR < month)
			errx(EXIT_FAILURE, _("illegal month value: use 1-12"));
		/* FALLTHROUGH */
	case 1:
		year = strtol_or_err(*argv++, _("illegal year value"));
		if (year < SMALLEST_YEAR)
			errx(EXIT_FAILURE, _("illegal year value: use positive integer"));
		if (day) {
			int dm = days_in_month[leap_year(year)][month];
			if (day > dm)
				errx(EXIT_FAILURE, _("illegal day value: use 1-%d"), dm);
			day = day_in_year(day, month, year);
		} else if ((long) (local_time->tm_year + 1900) == year) {
			day = local_time->tm_yday + 1;
		}
		if (!month && !ctl.weeknum)
			ctl.yflag = 1;
		break;
	case 0:
		day = local_time->tm_yday + 1;
		year = local_time->tm_year + 1900;
		month = local_time->tm_mon + 1;
		break;
	default:
		usage(stderr);
	}

	if (0 < ctl.weeknum) {
		int yday = week_to_day(year, &ctl);
		int leap = leap_year(year);

		if (yday < 1)
			errx(EXIT_FAILURE, _("illegal week value: year %ld "
					     "doesn't have week %d"),
					year, ctl.weeknum);
		month = 1;
		while (month <= 12 && yday > days_in_month[leap][month])
			yday -= days_in_month[leap][month++];
		if (month > 12) {
			/* In some years (e.g. 2010 in ISO mode) it's possible
			 * to have a remnant of week 53 starting the year yet
			 * the year in question ends during 52, in this case
			 * we're assuming that early remnant is being referred
			 * to if 53 is given as argument. */
			if (ctl.weeknum == week_number(31, 12, year - 1, &ctl))
				month = 1;
			else
				errx(EXIT_FAILURE,
					_("illegal week value: year %ld "
					  "doesn't have week %d"),
					year, ctl.weeknum);
		}
	}

	headers_init(&ctl);

	if (!colors_init(ctl.colormode)) {
		day = 0;
		ctl.weektype &= ~WEEK_NUM_MASK;
	}

	if (ctl.yflag) {
		if (ctl.julian)
			ctl.num_months = MONTH_COLS - 1;
		else
			ctl.num_months = MONTH_COLS;
		yearly(day, year, &ctl);
	} else if (ctl.num_months == 1)
		monthly(day, month, year, &ctl);
	else if (ctl.num_months == 3)
		monthly3(day, month, year, &ctl);

	return EXIT_SUCCESS;
}

/* leap year -- account for gregorian reformation in 1752 */
static int leap_year(long year)
{
	if (year <= REFORMATION_YEAR)
		return !(year % 4);
	else
		return ( !(year % 4) && (year % 100) ) || !(year % 400);
}

static void headers_init(struct cal_control *ctl)
{
	size_t i, wd;
	char *cur_dh = day_headings;

	for (i = 0; i < DAYS_IN_WEEK; i++) {
		size_t space_left;
		wd = (i + ctl->weekstart) % DAYS_IN_WEEK;

		if (i)
			strcat(cur_dh++, " ");
		space_left = sizeof(day_headings) - (cur_dh - day_headings);

		if (space_left <= (ctl->day_width - 1))
			break;
		cur_dh += center_str(nl_langinfo(ABDAY_1 + wd), cur_dh,
				     space_left, ctl->day_width - 1);
	}

	for (i = 0; i < MONTHS_IN_YEAR; i++)
		ctl->full_month[i] = nl_langinfo(MON_1 + i);
}

static int do_monthly(int day, int month, long year, struct fmt_st *out,
		      int header_hint, const struct cal_control *ctl)
{
	int col, row, days[MAXDAYS];
	char *p, lineout[FMT_ST_CHARS];
	int pos = 0;

	day_array(day, month, year, days, ctl);

	if (header_hint < 0)
		header_hint = two_header_lines(month, year, ctl);
	if (header_hint) {
		snprintf(lineout, sizeof(lineout), _("%s"), ctl->full_month[month - 1]);
		center_str(lineout, out->s[pos], ARRAY_SIZE(out->s[pos]), ctl->week_width - 1);
		pos++;
		snprintf(lineout, sizeof(lineout), _("%ld"), year);
		center_str(lineout, out->s[pos], ARRAY_SIZE(out->s[pos]), ctl->week_width - 1);
		pos++;
	} else {
		/* TRANSLATORS: %s is the month name, %ld the year number.
		 * You can change the order and/or add something here;
		 * e.g. for Basque the translation should be "%2$ldko %1$s".
		 */
		snprintf(lineout, sizeof(lineout), _("%s %ld"),
			ctl->full_month[month - 1], year);
		center_str(lineout, out->s[pos], ARRAY_SIZE(out->s[pos]), ctl->week_width - 1);
		pos++;
	}

	snprintf(out->s[pos++], FMT_ST_CHARS, "%s%s",
				(ctl->weektype ? "   " : ""),
				day_headings);

	for (row = 0; row < DAYS_IN_WEEK - 1; row++) {
		int has_hl = 0;
		p = lineout;
		if (ctl->weektype)
			for (col = 0; col < DAYS_IN_WEEK; col++) {
				int xd = days[row * DAYS_IN_WEEK + col];
				if (xd != SPACE) {
					int wn = week_number(xd & ~TODAY_FLAG,
							month, year, ctl);
					p = ascii_weeknum(p, wn, ctl);
					break;
				} else if (col+1 == DAYS_IN_WEEK)
					p += sprintf(p,"   ");
			}
		for (col = 0; col < DAYS_IN_WEEK; col++) {
			int xd = days[row * DAYS_IN_WEEK + col];
			if (xd != SPACE && (xd & TODAY_FLAG))
				has_hl = 1;
			p = ascii_day(p, xd, ctl);
		}
		*p = '\0';
		snprintf(out->s[row+pos], FMT_ST_CHARS, "%s", lineout);
		if (has_hl)
			Hrow = out->s[row+pos];
	}
	pos += row;
	return pos;
}

static void monthly(int day, int month, long year, const struct cal_control *ctl)
{
	int i, rows;
	struct fmt_st out;

	rows = do_monthly(day, month, year, &out, -1, ctl);
	for (i = 0; i < rows; i++) {
		my_putstring(out.s[i]);
		my_putstring("\n");
	}
}

static int two_header_lines(int month, long year, const struct cal_control *ctl)
{
	char lineout[FMT_ST_CHARS];
	size_t len;
	snprintf(lineout, sizeof(lineout), "%ld", year);
	len = strlen(lineout);
	len += strlen(ctl->full_month[month - 1]) + 1;
	if (ctl->week_width - 1 < len)
		return 1;
	return 0;
}

static void monthly3(int day, int month, long year, const struct cal_control *ctl)
{
	char lineout[FMT_ST_CHARS];
	int i;
	int rows, two_lines;
	struct fmt_st out_prev;
	struct fmt_st out_curm;
	struct fmt_st out_next;
	int prev_month, next_month;
	long prev_year, next_year;

	memset(&out_prev, 0, sizeof(struct fmt_st));
	memset(&out_curm, 0, sizeof(struct fmt_st));
	memset(&out_next, 0, sizeof(struct fmt_st));
	if (month == 1) {
		prev_month = MONTHS_IN_YEAR;
		prev_year  = year - 1;
	} else {
		prev_month = month - 1;
		prev_year  = year;
	}
	if (month == MONTHS_IN_YEAR) {
		next_month = 1;
		next_year  = year + 1;
	} else {
		next_month = month + 1;
		next_year  = year;
	}
	two_lines = two_header_lines(prev_month, prev_year, ctl);
	two_lines += two_header_lines(month, year, ctl);
	two_lines += two_header_lines(next_month, next_year, ctl);
	if (0 < two_lines)
		rows = FMT_ST_LINES;
	else
		rows = FMT_ST_LINES - 1;
	do_monthly(day, prev_month, prev_year, &out_prev, two_lines, ctl);
	do_monthly(day, month,      year,      &out_curm, two_lines, ctl);
	do_monthly(day, next_month, next_year, &out_next, two_lines, ctl);

	for (i = 0; i < (two_lines ? 3 : 2); i++) {
		snprintf(lineout, sizeof(lineout),
			"%s  %s  %s\n", out_prev.s[i], out_curm.s[i], out_next.s[i]);
		my_putstring(lineout);
	}
	for (i = two_lines ? 3 : 2; i < rows; i++) {
		int w1, w2, w3;
		w1 = w2 = w3 = ctl->week_width;

#if defined(HAVE_LIBNCURSES) || defined(HAVE_LIBNCURSESW) || defined(HAVE_LIBTERMCAP)
		/* adjust width to allow for non printable characters */
		w1 += (out_prev.s[i] == Hrow ? Slen : 0);
		w2 += (out_curm.s[i] == Hrow ? Slen : 0);
		w3 += (out_next.s[i] == Hrow ? Slen : 0);
#endif
		snprintf(lineout, sizeof(lineout), "%-*s %-*s %-*s\n",
		       w1, out_prev.s[i],
		       w2, out_curm.s[i],
		       w3, out_next.s[i]);

		my_putstring(lineout);
	}
}

static char *append_weeknum(char *p, int *dp,
			 int month, long year, int cal,
			 int row, const struct cal_control *ctl)
{
	int col;

	for (col = 0; col < DAYS_IN_WEEK; col++) {
		int xd = dp[row * DAYS_IN_WEEK + col];

		if (xd != SPACE) {
			int weeknum = week_number(xd & ~TODAY_FLAG,
					       month + cal + 1, year, ctl);
			p = ascii_weeknum(p, weeknum, ctl);
			break;
		} else if (col+1 == DAYS_IN_WEEK)
			p += sprintf(p,"   ");
	}
	return p;
}

static void yearly(int day, long year, const struct cal_control *ctl)
{
	int col, i, month, row, which_cal;
	int days[MONTHS_IN_YEAR][MAXDAYS];
	char *p;
	/* three weeks + separators + \0 */
	int weeknumlen = (ctl->weektype ? WNUM_LEN : 0);
	char lineout[ weeknumlen + sizeof(day_headings) + 2 +
		      weeknumlen + sizeof(day_headings) + 2 +
		      weeknumlen + sizeof(day_headings) + 1 ];

	snprintf(lineout, sizeof(lineout), "%ld", year);

	/* 2013-04-28: The -1 near HEAD_SEP makes year header to be aligned
	 * exactly how it has been aligned for long time, but it is
	 * unexplainable.  */
	center(lineout, (ctl->week_width + HEAD_SEP) * ctl->num_months - HEAD_SEP - 1, 0);
	my_putstring("\n\n");

	for (i = 0; i < MONTHS_IN_YEAR; i++)
		day_array(day, i + 1, year, days[i], ctl);

	for (month = 0; month < MONTHS_IN_YEAR; month += ctl->num_months) {
		center(ctl->full_month[month], ctl->week_width - 1, HEAD_SEP + 1);
		if (ctl->julian) {
			center(ctl->full_month[month + 1], ctl->week_width - 1, 0);
		} else {
			center(ctl->full_month[month + 1], ctl->week_width - 1, HEAD_SEP + 1);
			center(ctl->full_month[month + 2], ctl->week_width - 1, 0);
		}
		if (ctl->julian)
			snprintf(lineout, sizeof(lineout),
				 "\n%*s%s%*s %*s%s\n",
				 weeknumlen,"", day_headings, HEAD_SEP, "",
				 weeknumlen,"", day_headings);
		else
			snprintf(lineout, sizeof(lineout),
				 "\n%*s%s%*s %*s%s%*s %*s%s\n",
				 weeknumlen,"", day_headings, HEAD_SEP, "",
				 weeknumlen,"", day_headings, HEAD_SEP, "",
				 weeknumlen,"", day_headings);

		my_putstring(lineout);
		for (row = 0; row < DAYS_IN_WEEK - 1; row++) {
			p = lineout;
			for (which_cal = 0; which_cal < ctl->num_months; which_cal++) {
				int *dp = &days[month + which_cal][row * DAYS_IN_WEEK];

				if (ctl->weektype)
					p = append_weeknum(p, days[month + which_cal],
							month, year, which_cal,
							row, ctl);

				for (col = 0; col < DAYS_IN_WEEK; col++)
					p = ascii_day(p, *dp++, ctl);
				p += sprintf(p, "  ");
			}
			*p = '\0';
			my_putstring(lineout);
			my_putstring("\n");
		}
	}
	my_putstring("\n");
}

/*
 * day_array --
 *	Fill in an array of 42 integers with a calendar.  Assume for a moment
 *	that you took the (maximum) 6 rows in a calendar and stretched them
 *	out end to end.  You would have 42 numbers or spaces.  This routine
 *	builds that array for any month from Jan. 1 through Dec. 9999.
 */
static void day_array(int day, int month, long year, int *days, const struct cal_control *ctl)
{
	int julday, daynum, dw, dm;
	const int *sep1752;

	memcpy(days, empty, MAXDAYS * sizeof(int));
	if (year == REFORMATION_YEAR && month == REFORMATION_MONTH) {
		sep1752 = ctl->julian ? j_sep1752 : d_sep1752;
		memcpy(days, sep1752 + ctl->weekstart,
		       ((MAXDAYS / 2) - ctl->weekstart) * sizeof(int));
		for (dm = 0; dm < MAXDAYS / 2; dm++)
			if (j_sep1752[dm] == day)
				days[dm] |= TODAY_FLAG;
		return;
	}
	dm = days_in_month[leap_year(year)][month];
	dw = (day_in_week(1, month, year) - ctl->weekstart + DAYS_IN_WEEK) % DAYS_IN_WEEK;
	julday = day_in_year(1, month, year);
	daynum = ctl->julian ? julday : 1;

	while (dm--) {
		days[dw] = daynum++;
		if (julday++ == day)
			days[dw] |= TODAY_FLAG;
		dw++;
	}
}

/*
 * day_in_year --
 *	return the 1 based day number within the year
 */
static int day_in_year(int day, int month, long year)
{
	int i, leap;

	leap = leap_year(year);
	for (i = 1; i < month; i++)
		day += days_in_month[leap][i];
	return day;
}

/*
 * day_in_week
 *	return the 0 based day number for any date from 1 Jan. 1 to
 *	31 Dec. 9999.  Assumes the Gregorian reformation eliminates
 *	3 Sep. 1752 through 13 Sep. 1752, and returns invalid weekday
 *	during the period of 11 days.
 */
static int day_in_week(int day, int month, long year)
{
	static const int reform[] = {
		SUNDAY, WEDNESDAY, TUESDAY, FRIDAY, SUNDAY, WEDNESDAY,
		FRIDAY, MONDAY, THURSDAY, SATURDAY, TUESDAY, THURSDAY
	};
	static const int old[] = {
		FRIDAY, MONDAY, SUNDAY, WEDNESDAY, FRIDAY, MONDAY,
		WEDNESDAY, SATURDAY, TUESDAY, THURSDAY, SUNDAY, TUESDAY
	};
	if (year != 1753)
		year -= month < 3;
	else
		year -= (month < 3) + 14;
	if (REFORMATION_YEAR < year
	    || (year == REFORMATION_YEAR && 9 < month)
	    || (year == REFORMATION_YEAR && month == 9 && 13 < day))
		return (year + (year / 4) - (year / 100) + (year / 400) + reform[month - 1] +
			day) % 7;
	if (year < REFORMATION_YEAR
	    || (year == REFORMATION_YEAR && month < 9)
	    || (year == REFORMATION_YEAR && month == 9 && day < 3))
		return (year + year / 4 + old[month - 1] + day) % 7;
	return NONEDAY;
}

/*
 * week_number
 *      return the week number of a given date, 1..53.
 *      Supports ISO-8601 and North American modes.
 *      Day may be given as Julian day of the year mode, in which
 *      case the month is disregarded entirely.
 */
static int week_number(int day, int month, long year, const struct cal_control *ctl)
{
	int fday = 0, yday;
	int wday = day_in_week(1, 1, year);

	if (ctl->weektype & WEEK_NUM_ISO)
		fday = wday + (wday >= FRIDAY ? -2 : 5);
	else
		/* WEEK_NUM_US
		 * - according to gcal, the first Sun is in the first week
		 * - according to wikipedia, the first Sat is in the first week
		 */
		fday = wday + (wday == SUNDAY ? 6 : -1);

	/* For julian dates the month can be set to 1, the global julian
	 * variable cannot be relied upon here, because we may recurse
	 * internally for 31.12. which would not work. */
	if (day > 31)
		month = 1;

	yday = day_in_year(day,month,year);
	if (year == REFORMATION_YEAR) {
		if (yday >= YDAY_AFTER_MISSING)
			fday -= NUMBER_MISSING_DAYS;
	}

	/* Last year is last year */
	if (yday + fday < 7)
		return week_number(31, 12, year - 1, ctl);

	/* Or it could be part of the next year.  The reformation year had less
	 * days than 365 making this check invalid, but reformation year ended
	 * on Sunday and in week 51, so it's ok here. */
	if (ctl->weektype == WEEK_NUM_ISO && yday >= 363
	    && day_in_week(day, month, year) >= MONDAY
	    && day_in_week(day, month, year) <= WEDNESDAY
	    && day_in_week(31, 12, year) >= MONDAY
	    && day_in_week(31, 12, year) <= WEDNESDAY)
		return  week_number(1, 1, year + 1, ctl);

	return (yday + fday) / 7;
}

/*
 * week_to_day
 *      return the yday of the first day in a given week inside
 *      the given year. This may be something other than Monday
 *      for ISO-8601 modes. For North American numbering this
 *      always returns a Sunday.
 */
static int week_to_day(long year, const struct cal_control *ctl)
{
	int yday, wday;

	wday = day_in_week(1, 1, year);
	yday = ctl->weeknum * 7 - wday;

	if (ctl->weektype & WEEK_NUM_ISO)
		yday -= (wday >= FRIDAY ? -2 : 5);
	else
		yday -= (wday == SUNDAY ? 6 : -1);	/* WEEK_NUM_US */
	if (yday <= 0)
		return 1;

	return yday;
}

static char *ascii_day(char *p, int day, const struct cal_control *ctl)
{
	int display, val;
	int highlight = 0;
	static char *aday[] = {
		"",
		" 1", " 2", " 3", " 4", " 5", " 6", " 7",
		" 8", " 9", "10", "11", "12", "13", "14",
		"15", "16", "17", "18", "19", "20", "21",
		"22", "23", "24", "25", "26", "27", "28",
		"29", "30", "31",
	};

	if (day == SPACE) {
		memset(p, ' ', ctl->day_width);
		return p + ctl->day_width;
	}
	if (day & TODAY_FLAG) {
		day &= ~TODAY_FLAG;
		p += sprintf(p, "%s", Senter);
		highlight = 1;
	}
	if (ctl->julian) {
		if ((val = day / 100)) {
			day %= 100;
			*p++ = val + '0';
			display = 1;
		} else {
			*p++ = ' ';
			display = 0;
		}
		val = day / 10;
		if (val || display)
			*p++ = val + '0';
		else
			*p++ = ' ';
		*p++ = day % 10 + '0';
	} else {
		*p++ = aday[day][0];
		*p++ = aday[day][1];
	}
	if (highlight)
		p += sprintf(p, "%s", Sexit);
	*p++ = ' ';
	return p;
}

static char *ascii_weeknum(char *p, int weeknum, const struct cal_control *ctl)
{
	if ((ctl->weektype & WEEK_NUM_MASK) == weeknum)
		p += sprintf(p, "%s%2d%s ", Senter, weeknum, Sexit);
	else
		p += sprintf(p, "%2d ", weeknum);
	return p;
}

/*
 * Center string, handling multibyte characters appropriately.
 * In addition if the string is too large for the width it's truncated.
 * The number of trailing spaces may be 1 less than the number of leading spaces.
 */
static int center_str(const char* src, char* dest,
		      size_t dest_size, size_t width)
{
	return mbsalign(src, dest, dest_size, &width,
			MBS_ALIGN_CENTER, MBA_UNIBYTE_FALLBACK);
}

static void center(const char *str, size_t len, int separate)
{
	char lineout[FMT_ST_CHARS];

	center_str(str, lineout, ARRAY_SIZE(lineout), len);
	my_putstring(lineout);

	if (separate) {
		snprintf(lineout, sizeof(lineout), "%*s", separate, "");
		my_putstring(lineout);
	}
}

static void __attribute__ ((__noreturn__)) usage(FILE * out)
{
	fputs(USAGE_HEADER, out);
	fprintf(out, _(" %s [options] [[[day] month] year]\n"), program_invocation_short_name);

	fputs(USAGE_SEPARATOR, out);
	fputs(_("Display a calendar, or some part of it.\n"), out);
	fputs(_("Without any arguments, display the current month.\n"), out);

	fputs(USAGE_OPTIONS, out);
	fputs(_(" -1, --one             show only a single month (default)\n"), out);
	fputs(_(" -3, --three           show three months spanning the date\n"), out);
	fputs(_(" -s, --sunday          Sunday as first day of week\n"), out);
	fputs(_(" -m, --monday          Monday as first day of week\n"), out);
	fputs(_(" -j, --julian          output Julian dates\n"), out);
	fputs(_(" -y, --year            show the whole year\n"), out);
	fputs(_(" -w, --week[=<num>]    show US or ISO-8601 week numbers\n"), out);
	fputs(_("     --color[=<when>]  colorize messages (auto, always or never)\n"), out);

	fputs(USAGE_SEPARATOR, out);
	fputs(USAGE_HELP, out);
	fputs(USAGE_VERSION, out);
	fprintf(out, USAGE_MAN_TAIL("cal(1)"));

	exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}
