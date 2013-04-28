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
	else
		return ret;
}

#elif defined(HAVE_LIBTERMCAP)
# include <termcap.h>

char termbuffer[4096];
char tcbuffer[4096];
char *strbuf = termbuffer;

static void my_setupterm(const char *term, int fildes, int *errret)
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
	else
		return ret;
}

#else	/* ! (HAVE_LIBTERMCAP || HAVE_LIBNCURSES || HAVE_LIBNCURSESW) */

static void my_putstring(char *s)
{
	fputs(s, stdout);
}

#endif	/* end of LIBTERMCAP / NCURSES */


const char	*term="";
const char	*Senter="", *Sexit="";/* enter and exit standout mode */
int		Slen;		/* strlen of Senter+Sexit */
char		*Hrow;		/* pointer to highlighted row in month */

#include "widechar.h"

/* allow compile-time define to over-ride default */
#ifndef NUM_MONTHS
#define NUM_MONTHS 1
#endif

#if ( NUM_MONTHS != 1 && NUM_MONTHS !=3 )
#error NUM_MONTHS must be 1 or 3
#endif

enum {
	SUNDAY = 0,
	MONDAY,
	TUESDAY,
	WEDNESDAY,
	THURSDAY,
	FRIDAY,
	SATURDAY,
	DAYS_IN_WEEK
};

#define	FIRST_WEEKDAY		SATURDAY	/* Jan 1st, 1 was a Saturday */
#define	REFORMATION_WEEKDAY	THURSDAY	/* after reformation it was Thursday */
#define REFORMATION_YEAR	1752		/* Signed-off-by: Lord Chesterfield */
#define REFORMATION_MONTH	9		/* September */
#define	FIRST_MISSING_DAY	639799		/* 3 Sep 1752 */
#define	NUMBER_MISSING_DAYS	11		/* 11 day correction */

#define DAYS_IN_YEAR		365		/* the common case, leap years are calculated */
#define MONTHS_IN_YEAR		12
#define DAYS_IN_MONTH		31
#define	MAXDAYS			42		/* slots in a month array */
#define	SPACE			-1		/* used in day array */

#define SMALLEST_YEAR		1
#define GREATEST_YEAR		9999

#define	DAY_LEN			3		/* 3 spaces per day */
#define	WEEK_LEN		(DAYS_IN_WEEK * DAY_LEN)
#define	HEAD_SEP		2
#define MONTH_COLS		3		/* month columns in year view */

#define	J_DAY_LEN		4		/* 4 spaces per day */
#define	J_WEEK_LEN		(DAYS_IN_WEEK * J_DAY_LEN)
#define	J_HEAD_SEP		2
#define J_MONTH_COLS		2

#define TODAY_FLAG		0x400		/* flag day for highlighting */

#define FMT_ST_LINES 8
#define FMT_ST_CHARS 300	/* 90 suffices in most locales */
struct fmt_st
{
  char s[FMT_ST_LINES][FMT_ST_CHARS];
};

static int days_in_month[2][13] = {
	{0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31},
	{0, 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31},
};

/* September 1752 is special, and has static assignments for both date
 * and Julian representations.  */
 int d_sep1752[MAXDAYS / 2] = {
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


/* utf-8 can have up to 6 bytes per char; and an extra byte for ending \0 */
char day_headings[J_WEEK_LEN * 6 + 1];
/* weekstart = 1  =>   "  M  Tu   W  Th   F   S   S " */
const char *full_month[MONTHS_IN_YEAR];

/* 0 => sunday, 1 => monday */
int weekstart = SUNDAY;
int julian;

/* function prototypes */
static int leap_year(int year);
static int centuries_since_1700(int year, int centuries);
static int leap_years_since_year_1(int year);
char * ascii_day(char *, int);
int center_str(const char* src, char* dest, size_t dest_size, size_t width);
void center(const char *, size_t, int);
void day_array(int, int, int, int *);
int day_in_week(int, int, int);
int day_in_year(int, int, int);
void yearly(int, int, int);
void do_monthly(int, int, int, struct fmt_st*);
void monthly(int, int, int);
void monthly3(int, int, int);
void trim_trailing_spaces(char *);
static void __attribute__ ((__noreturn__)) usage(FILE * out);
void headers_init(int);

int
main(int argc, char **argv) {
	struct tm *local_time;
	time_t now;
	int ch, day = 0, month = 0, year = 0, yflag = 0;
	int num_months = NUM_MONTHS;

	static const struct option longopts[] = {
		{"one", no_argument, NULL, '1'},
		{"three", no_argument, NULL, '3'},
		{"sunday", no_argument, NULL, 's'},
		{"monday", no_argument, NULL, 'm'},
		{"julian", no_argument, NULL, 'j'},
		{"year", no_argument, NULL, 'y'},
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
		weekstart = (wfd + *nl_langinfo(_NL_TIME_FIRST_WEEKDAY) - 1) % DAYS_IN_WEEK;
	}
#endif

	while ((ch = getopt_long(argc, argv, "13mjsyVh", longopts, NULL)) != -1)
		switch(ch) {
		case '1':
			num_months = 1;		/* default */
			break;
		case '3':
			num_months = 3;
			break;
		case 's':
			weekstart = SUNDAY;	/* default */
			break;
		case 'm':
			weekstart = MONDAY;
			break;
		case 'j':
			julian = 1;
			break;
		case 'y':
			yflag = 1;
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
		year = strtos32_or_err(*argv++, _("illegal year value: use 1-9999"));
		if (year < SMALLEST_YEAR || GREATEST_YEAR < year)
			errx(EXIT_FAILURE, _("illegal year value: use 1-9999"));
		if (day) {
			int dm = days_in_month[leap_year(year)][month];
			if (day > dm)
				errx(EXIT_FAILURE, _("illegal day value: use 1-%d"), dm);
			day = day_in_year(day, month, year);
		} else if ((local_time->tm_year + 1900) == year) {
			day = local_time->tm_yday + 1;
		}
		if (!month)
			yflag=1;
		break;
	case 0:
		day = local_time->tm_yday + 1;
		year = local_time->tm_year + 1900;
		month = local_time->tm_mon + 1;
		break;
	default:
		usage(stderr);
	}
	headers_init(julian);

	if (!isatty(STDOUT_FILENO))
		day = 0; /* don't highlight */

	if (yflag)
		yearly(day, year, julian);
	else if (num_months == 1)
		monthly(day, month, year);
	else if (num_months == 3)
		monthly3(day, month, year);

	return EXIT_SUCCESS;
}

/* leap year -- account for gregorian reformation in 1752 */
static int leap_year(int year)
{
	if (year <= REFORMATION_YEAR)
		return !(year % 4);
	else
		return ( !(year % 4) && (year % 100) ) || !(year % 400);
}

/* number of centuries since 1700 */
static int centuries_since_1700(int year, int n)
{
	if (year < REFORMATION_YEAR)
		return 0;
	else
		return ((year / (100 * n)) - ((REFORMATION_YEAR / 100) / n));
}

/* number of leap years between year 1 and this year, not inclusive */
static int leap_years_since_year_1(int year)
{
	return (year / 4 - centuries_since_1700(year, 1) +
		centuries_since_1700(year, 4));
}

void headers_init(int julian)
{
	int i, wd, spaces = julian ? J_DAY_LEN - 1 : DAY_LEN - 1;
	char *cur_dh = day_headings;

	for (i = 0; i < DAYS_IN_WEEK; i++) {
		ssize_t space_left;
		wd = (i + weekstart) % DAYS_IN_WEEK;

		if (i)
			strcat(cur_dh++, " ");
		space_left =
		    sizeof(day_headings) - (cur_dh - day_headings);
		if (space_left <= spaces)
			break;
		cur_dh +=
		    center_str(nl_langinfo(ABDAY_1 + wd), cur_dh,
			       space_left, spaces);
	}

	for (i = 0; i < MONTHS_IN_YEAR; i++)
		full_month[i] = nl_langinfo(MON_1 + i);
}

void
do_monthly(int day, int month, int year, struct fmt_st *out) {
	int col, row, days[MAXDAYS];
	char *p, lineout[FMT_ST_CHARS];
	int width = (julian ? J_WEEK_LEN : WEEK_LEN) - 1;

	day_array(day, month, year, days);

	/*
	 * %s is the month name, %d the year number.
	 * you can change the order and/or add something here; eg for
	 * Basque the translation should be: "%2$dko %1$s", and
	 * the Vietnamese should be "%s na(m %d", etc.
	 */
	snprintf(lineout, sizeof(lineout), _("%s %d"),
			full_month[month - 1], year);
	center_str(lineout, out->s[0], ARRAY_SIZE(out->s[0]), width);

	snprintf(out->s[1], FMT_ST_CHARS, "%s", day_headings);
	for (row = 0; row < DAYS_IN_WEEK - 1; row++) {
		int has_hl = 0;
		for (col = 0, p = lineout; col < DAYS_IN_WEEK; col++) {
			int xd = days[row * DAYS_IN_WEEK + col];
			if (xd != SPACE && (xd & TODAY_FLAG))
				has_hl = 1;
			p = ascii_day(p, xd);
		}
		*p = '\0';
		trim_trailing_spaces(lineout);
		snprintf(out->s[row+2], FMT_ST_CHARS, "%s", lineout);
		if (has_hl)
			Hrow = out->s[row+2];
	}
}

void
monthly(int day, int month, int year) {
	int i;
	struct fmt_st out;

	do_monthly(day, month, year, &out);
	for (i = 0; i < FMT_ST_LINES; i++) {
		my_putstring(out.s[i]);
		my_putstring("\n");
	}
}

void
monthly3(int day, int month, int year) {
	char lineout[FMT_ST_CHARS];
	int i;
	int width;
	struct fmt_st out_prev;
	struct fmt_st out_curm;
	struct fmt_st out_next;
	int prev_month, prev_year;
	int next_month, next_year;

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

	do_monthly(day, prev_month, prev_year, &out_prev);
	do_monthly(day, month,      year,      &out_curm);
	do_monthly(day, next_month, next_year, &out_next);

	width = (julian ? J_WEEK_LEN : WEEK_LEN) -1;
	for (i = 0; i < 2; i++) {
		snprintf(lineout, sizeof(lineout),
			"%s  %s  %s\n", out_prev.s[i], out_curm.s[i], out_next.s[i]);
		my_putstring(lineout);
	}
	for (i = 2; i < FMT_ST_LINES; i++) {
		int w1, w2, w3;
		w1 = w2 = w3 = width;

#if defined(HAVE_LIBNCURSES) || defined(HAVE_LIBNCURSESW) || defined(HAVE_LIBTERMCAP)
		/* adjust width to allow for non printable characters */
		w1 += (out_prev.s[i] == Hrow ? Slen : 0);
		w2 += (out_curm.s[i] == Hrow ? Slen : 0);
		w3 += (out_next.s[i] == Hrow ? Slen : 0);
#endif
		snprintf(lineout, sizeof(lineout), "%-*s  %-*s  %-*s\n",
		       w1, out_prev.s[i],
		       w2, out_curm.s[i],
		       w3, out_next.s[i]);

		my_putstring(lineout);
	}
}

void
yearly(int day, int year, int julian) {
	int col, *dp, i, month, row, which_cal;
	int maxrow, sep_len, week_len;
	int days[MONTHS_IN_YEAR][MAXDAYS];
	char *p, lineout[100];

	if (julian) {
		maxrow = J_MONTH_COLS;
		sep_len = J_HEAD_SEP;
		week_len = J_WEEK_LEN;
	} else {
		maxrow = MONTH_COLS;
		sep_len = HEAD_SEP;
		week_len = WEEK_LEN;
	}
	snprintf(lineout, sizeof(lineout), "%d", year);
	/* 2013-04-28: The -1 near sep_len makes year header to be
	 * aligned exactly how it has been aligned for long time, but it
	 * is unexplainable.  */
	center(lineout, (week_len + sep_len) * maxrow - sep_len - 1, 0);
	my_putstring("\n\n");

	for (i = 0; i < MONTHS_IN_YEAR; i++)
		day_array(day, i + 1, year, days[i]);
	for (month = 0; month < MONTHS_IN_YEAR; month += maxrow) {
		center(full_month[month], week_len - 1, sep_len + 1);
		if (julian) {
			center(full_month[month + 1], week_len - 1, 0);
		} else {
			center(full_month[month + 1], week_len - 1, sep_len + 1);
			center(full_month[month + 2], week_len - 1, 0);
		}
		if (julian)
			snprintf(lineout, sizeof(lineout),
				 "\n%s%*s %s\n", day_headings, sep_len, "", day_headings);
		else
			snprintf(lineout, sizeof(lineout),
				 "\n%s%*s %s%*s %s\n", day_headings, sep_len,
				 "", day_headings, sep_len, "", day_headings);
		my_putstring(lineout);
		for (row = 0; row < DAYS_IN_WEEK - 1; row++) {
			p = lineout;
			for (which_cal = 0; which_cal < maxrow; which_cal++) {
				dp = &days[month + which_cal][row * DAYS_IN_WEEK];
				for (col = 0; col < DAYS_IN_WEEK; col++)
					p = ascii_day(p, *dp++);
				p += sprintf(p, "  ");
			}
			*p = '\0';
			trim_trailing_spaces(lineout);
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
void
day_array(int day, int month, int year, int *days) {
	int julday, daynum, dw, dm;
	int *sep1752;

	memcpy(days, empty, MAXDAYS * sizeof(int));
	if (year == REFORMATION_YEAR && month == REFORMATION_MONTH) {
		sep1752 = julian ? j_sep1752 : d_sep1752;
		memcpy(days, sep1752 + weekstart,
		       ((MAXDAYS / 2) - weekstart) * sizeof(int));
		for (dm = 0; dm < MAXDAYS / 2; dm++)
			if (j_sep1752[dm] == day)
				days[dm] |= TODAY_FLAG;
		return;
	}
	dm = days_in_month[leap_year(year)][month];
	dw = (day_in_week(1, month, year) - weekstart + DAYS_IN_WEEK) % DAYS_IN_WEEK;
	julday = day_in_year(1, month, year);
	daynum = julian ? julday : 1;
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
int
day_in_year(int day, int month, int year) {
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
 *	3 Sep. 1752 through 13 Sep. 1752.  Returns Thursday for all
 *	missing days.
 */
int
day_in_week(int day, int month, int year) {
	long temp;

	temp =
	    (long)(year - SMALLEST_YEAR) * DAYS_IN_YEAR +
	    leap_years_since_year_1(year - SMALLEST_YEAR)
	    + day_in_year(day, month, year);
	if (temp < FIRST_MISSING_DAY)
		return ((temp - 1 + FIRST_WEEKDAY) % DAYS_IN_WEEK);
	if (temp >= (FIRST_MISSING_DAY + NUMBER_MISSING_DAYS))
		return (((temp - 1 + FIRST_WEEKDAY) - NUMBER_MISSING_DAYS) % DAYS_IN_WEEK);
	return(REFORMATION_WEEKDAY);
}

char *
ascii_day(char *p, int day) {
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
		int len = julian ? J_DAY_LEN : DAY_LEN;
		memset(p, ' ', len);
		return p+len;
	}
	if (day & TODAY_FLAG) {
		day &= ~TODAY_FLAG;
		p += sprintf(p, "%s", Senter);
		highlight = 1;
	}
	if (julian) {
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

void
trim_trailing_spaces(char *s)
{
	char *p;

	for (p = s; *p; ++p)
		continue;
	while (p > s && isspace(*--p))
		continue;
	if (p > s)
		++p;
	*p = '\0';
}

/*
 * Center string, handling multibyte characters appropriately.
 * In addition if the string is too large for the width it's truncated.
 * The number of trailing spaces may be 1 less than the number of leading spaces.
 */
int
center_str(const char* src, char* dest, size_t dest_size, size_t width)
{
	return mbsalign(src, dest, dest_size, &width,
			MBS_ALIGN_CENTER, MBA_UNIBYTE_FALLBACK);
}

void
center(const char *str, size_t len, int separate)
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
	fputs(_("\nUsage:\n"), out);
	fprintf(out,
	      _(" %s [options] [[[day] month] year]\n"),
			program_invocation_short_name);

	fputs(_("\nOptions:\n"), out);
	fputs(_(" -1, --one        show only current month (default)\n"
	        " -3, --three      show previous, current and next month\n"
		" -s, --sunday     Sunday as first day of week\n"
		" -m, --monday     Monday as first day of week\n"
		" -j, --julian     output Julian dates\n"
		" -y, --year       show whole current year\n"
		" -V, --version    display version information and exit\n"
		" -h, --help       display this help text and exit\n\n"), out);

	exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}
