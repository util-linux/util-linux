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
 *             Added over-ridable default MONTHS_IN_ROW and "-1" option to
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
#include <stdint.h>
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
#include "optutils.h"

static int has_term = 0;
static const char *Senter = "", *Sexit = "";	/* enter and exit standout mode */

#if defined(HAVE_LIBNCURSES) || defined(HAVE_LIBNCURSESW)
# ifdef HAVE_NCURSES_H
#  include <ncurses.h>
# elif defined(HAVE_NCURSES_NCURSES_H)
#  include <ncurses/ncurses.h>
# endif
# include <term.h>

static int setup_terminal(char *term)
{
	int ret;

	if (setupterm(term, STDOUT_FILENO, &ret) != OK || ret != 1)
		return -1;
	return 0;
}

static void my_putstring(char *s)
{
	if (has_term)
		putp(s);
	else
		fputs(s, stdout);
}

static const char *my_tgetstr(char *s __attribute__((__unused__)), char *ss)
{
	const char *ret = NULL;

	if (has_term)
		ret = tigetstr(ss);
	if (!ret || ret == (char *)-1)
		return "";
	return ret;
}

#elif defined(HAVE_LIBTERMCAP)
# include <termcap.h>

static char termbuffer[4096];
static char tcbuffer[4096];
static char *strbuf = termbuffer;

static int setup_terminal(char *term)
{
	if (tgetent(tcbuffer, term) < 0)
		return -1;
	return 0;
}

static void my_putstring(char *s)
{
	if (has_term)
		tputs(s, 1, putchar);
	else
		fputs(s, stdout);
}

static const char *my_tgetstr(char *s, char *ss __attribute__((__unused__)))
{
	const char *ret = NULL;

	if (has_term)
		ret = tgetstr(s, &strbuf);
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


#include "widechar.h"

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

enum {
	JANUARY = 1,
	FEBRUARY,
	MARCH,
	APRIL,
	MAY,
	JUNE,
	JULY,
	AUGUST,
	SEPTEMBER,
	OCTOBER,
	NOVEMBER,
	DECEMBER
};

#define REFORMATION_YEAR	1752		/* Signed-off-by: Lord Chesterfield */
#define REFORMATION_MONTH	SEPTEMBER
#define	NUMBER_MISSING_DAYS	11		/* 11 day correction */
#define YDAY_AFTER_MISSING	258             /* 14th in Sep 1752 */

#define MONTHS_IN_YEAR		DECEMBER
#define DAYS_IN_MONTH		31
#define	MAXDAYS			42		/* slots in a month array */
#define	SPACE			-1		/* used in day array */

#define SMALLEST_YEAR		1

#define	DAY_LEN			3		/* 3 spaces per day */
#define	WEEK_LEN		(DAYS_IN_WEEK * DAY_LEN)
#define MONTHS_IN_YEAR_ROW	3		/* month columns in year view */
#define WNUM_LEN                3

#define FMT_ST_CHARS 300	/* 90 suffices in most locales */

static const int days_in_month[2][13] = {
	{0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31},
	{0, 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31},
};

enum {
	WEEK_NUM_DISABLED = 0,
	WEEK_NUM_MASK=0xff,
	WEEK_NUM_ISO=0x100,
	WEEK_NUM_US=0x200,
};

/* utf-8 can have up to 6 bytes per char; and an extra byte for ending \0 */
static char day_headings[(WEEK_LEN + 1) * 6 + 1];

struct cal_request {
	int day;
	int month;
	int32_t year;
	int week;
	int start_month;
};

struct cal_control {
	const char *full_month[MONTHS_IN_YEAR];	/* month names */
	int colormode;			/* day and week number highlight */
	int num_months;			/* number of requested mounths */
	int months_in_row;		/* number of months horizontally in print out */
	int weekstart;			/* day the week starts, often Sun or Mon */
	int weektype;			/* WEEK_TYPE_{NONE,ISO,US} */
	size_t day_width;		/* day width in characters in printout */
	size_t week_width;		/* 7 * day_width + possible week num */
	int gutter_width;		/* spaces in between horizontal month outputs */
	struct cal_request req;		/* the times user is interested */
	unsigned int	julian:1,	/* julian output */
			header_year:1,	/* print year number */
			header_hint:1;	/* does month name + year need two lines to fit */
};

struct cal_month {
	int days[MAXDAYS];		/* the day numbers, or SPACE */
	int weeks[MAXDAYS / DAYS_IN_WEEK];
	int month;
	int32_t year;
	struct cal_month *next;
};

/* function prototypes */
static int leap_year(int32_t year);
static void headers_init(struct cal_control *ctl);
static void cal_fill_month(struct cal_month *month, const struct cal_control *ctl);
static void cal_output_header(struct cal_month *month, const struct cal_control *ctl);
static void cal_output_months(struct cal_month *month, const struct cal_control *ctl);
static void monthly(const struct cal_control *ctl);
static void yearly(const struct cal_control *ctl);
static int day_in_year(int day, int month, int32_t year);
static int day_in_week(int day, int month, int32_t year);
static int week_number(int day, int month, int32_t year, const struct cal_control *ctl);
static int week_to_day(const struct cal_control *ctl);
static int center_str(const char *src, char *dest, size_t dest_size, size_t width);
static void center(const char *str, size_t len, int separate);
static void __attribute__((__noreturn__)) usage(FILE *out);

int main(int argc, char **argv)
{
	struct tm *local_time;
	time_t now;
	int ch = 0, yflag = 0, Yflag = 0;
	static struct cal_control ctl = {
		.weekstart = SUNDAY,
		.num_months = 1,		/* default is "cal -1" */
		.colormode = UL_COLORMODE_UNDEF,
		.weektype = WEEK_NUM_DISABLED,
		.day_width = DAY_LEN,
		.gutter_width = 2,
		.req.day = 0,
		.req.month = 0
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
		{"months", required_argument, NULL, 'n'},
		{"year", no_argument, NULL, 'y'},
		{"week", optional_argument, NULL, 'w'},
		{"color", optional_argument, NULL, OPT_COLOR},
		{"version", no_argument, NULL, 'V'},
		{"twelve", no_argument, NULL, 'Y'},
		{"help", no_argument, NULL, 'h'},
		{NULL, 0, NULL, 0}
	};

	static const ul_excl_t excl[] = {       /* rows and cols in in ASCII order */
		{ 'Y','n','y' },
		{ 0 }
	};
	int excl_st[ARRAY_SIZE(excl)] = UL_EXCL_STATUS_INIT;

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

#if defined(HAVE_LIBNCURSES) || defined(HAVE_LIBNCURSESW) || defined(HAVE_LIBTERMCAP)
	{
		char *term = getenv("TERM");

		if (term) {
			has_term = setup_terminal(term) == 0;
			if (has_term) {
				Senter = my_tgetstr("so","smso");
				Sexit = my_tgetstr("se","rmso");
			}
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
	while ((ch = getopt_long(argc, argv, "13mjn:sywYVh", longopts, NULL)) != -1) {

		err_exclusive_options(ch, longopts, excl, excl_st);

		switch(ch) {
		case '1':
			/* default */
			break;
		case '3':
			ctl.num_months = 3;
			ctl.months_in_row = 3;
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
			yflag = 1;
			break;
		case 'Y':
			Yflag = 1;
			break;
		case 'n':
			ctl.num_months = strtou32_or_err(optarg,
						_("invalid month argument"));
			break;
		case 'w':
			if (optarg) {
				ctl.req.week = strtos32_or_err(optarg,
						_("invalid week argument"));
				if (ctl.req.week < 1 || 54 < ctl.req.week)
					errx(EXIT_FAILURE,_("illegal week value: use 1-54"));
			}
			ctl.weektype = WEEK_NUM_US;	/* default per weekstart */
			break;
		case OPT_COLOR:
			ctl.colormode = UL_COLORMODE_AUTO;
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
	}

	argc -= optind;
	argv += optind;

	if (ctl.weektype) {
		ctl.weektype = ctl.req.week & WEEK_NUM_MASK;
		ctl.weektype |= (ctl.weekstart == MONDAY ? WEEK_NUM_ISO : WEEK_NUM_US);
		ctl.week_width = (ctl.day_width * DAYS_IN_WEEK) + WNUM_LEN;
	} else
		ctl.week_width = ctl.day_width * DAYS_IN_WEEK;

	time(&now);
	local_time = localtime(&now);

	switch(argc) {
	case 3:
		ctl.req.day = strtos32_or_err(*argv++, _("illegal day value"));
		if (ctl.req.day < 1 || DAYS_IN_MONTH < ctl.req.day)
			errx(EXIT_FAILURE, _("illegal day value: use 1-%d"), DAYS_IN_MONTH);
		/* FALLTHROUGH */
	case 2:
		ctl.req.month = strtos32_or_err(*argv++, _("illegal month value: use 1-12"));
		if (ctl.req.month < 1 || MONTHS_IN_YEAR < ctl.req.month)
			errx(EXIT_FAILURE, _("illegal month value: use 1-12"));
		/* FALLTHROUGH */
	case 1:
		ctl.req.year = strtos32_or_err(*argv++, _("illegal year value"));
		if (ctl.req.year < SMALLEST_YEAR)
			errx(EXIT_FAILURE, _("illegal year value: use positive integer"));
		if (ctl.req.year == INT32_MAX)
			errx(EXIT_FAILURE, _("illegal year value"));
		if (ctl.req.day) {
			int dm = days_in_month[leap_year(ctl.req.year)][ctl.req.month];
			if (ctl.req.day > dm)
				errx(EXIT_FAILURE, _("illegal day value: use 1-%d"), dm);
			ctl.req.day = day_in_year(ctl.req.day, ctl.req.month, ctl.req.year);
		} else if ((int32_t) (local_time->tm_year + 1900) == ctl.req.year) {
			ctl.req.day = local_time->tm_yday + 1;
		}
		if (!ctl.req.month && !ctl.req.week) {
			ctl.req.month = local_time->tm_mon + 1;
			yflag = 1;
		}
		break;
	case 0:
		ctl.req.day = local_time->tm_yday + 1;
		ctl.req.year = local_time->tm_year + 1900;
		ctl.req.month = local_time->tm_mon + 1;
		break;
	default:
		usage(stderr);
	}

	if (0 < ctl.req.week) {
		int yday = week_to_day(&ctl);
		int leap = leap_year(ctl.req.year);
		int m = 1;

		if (yday < 1)
			errx(EXIT_FAILURE, _("illegal week value: year %d "
					     "doesn't have week %d"),
					ctl.req.year, ctl.req.week);
		while (m <= DECEMBER && yday > days_in_month[leap][m])
			yday -= days_in_month[leap][m++];
		if (DECEMBER < m && ctl.weektype & WEEK_NUM_ISO) {
			/* In some years (e.g. 2010 in ISO mode) it's possible
			 * to have a remnant of week 53 starting the year yet
			 * the year in question ends during 52, in this case
			 * we're assuming that early remnant is being referred
			 * to if 53 is given as argument. */
			if (ctl.req.week != week_number(31, DECEMBER, ctl.req.year - 1, &ctl))
				errx(EXIT_FAILURE,
					_("illegal week value: year %d "
					  "doesn't have week %d"),
					ctl.req.year, ctl.req.week);
		}
		if (!ctl.req.month)
			ctl.req.month = MONTHS_IN_YEAR < m ? 1 : m;
	}

	headers_init(&ctl);

	if (!colors_init(ctl.colormode, "cal")) {
		ctl.req.day = 0;
		ctl.weektype &= ~WEEK_NUM_MASK;
	}

	if (yflag || Yflag) {
		ctl.gutter_width = 3;
		ctl.num_months = MONTHS_IN_YEAR;
		if (yflag) {
			ctl.req.start_month = 1;	/* start from Jan */
			ctl.header_year = 1;		/* print year number */
		}
	}

	if (ctl.num_months > 1 && ctl.months_in_row == 0)
		ctl.months_in_row = ctl.julian ? MONTHS_IN_YEAR_ROW - 1 :
						 MONTHS_IN_YEAR_ROW;
	else if (!ctl.months_in_row)
		ctl.months_in_row = 1;

	if (yflag || Yflag)
		yearly(&ctl);
	else
		monthly(&ctl);

	return EXIT_SUCCESS;
}

/* leap year -- account for gregorian reformation in 1752 */
static int leap_year(int32_t year)
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
	char tmp[FMT_ST_CHARS];
	size_t year_len;

	year_len = snprintf(tmp, sizeof(tmp), "%d", ctl->req.year);

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

	for (i = 0; i < MONTHS_IN_YEAR; i++) {
		ctl->full_month[i] = nl_langinfo(MON_1 + i);
		/* The +1 after year_len is space in between month and year. */
		if (ctl->week_width < strlen(ctl->full_month[i]) + year_len + 1)
			ctl->header_hint = 1;
	}
}

static void cal_fill_month(struct cal_month *month, const struct cal_control *ctl)
{
	int first_week_day = day_in_week(1, month->month, month->year);
	int month_days;
	int i, j, weeklines = 0;

	if (ctl->julian)
		j = day_in_year(1, month->month, month->year);
	else
		j = 1;
	month_days = j + days_in_month[leap_year(month->year)][month->month];

	/* True when Sunday is not first day in the output week. */
	if (ctl->weekstart) {
		first_week_day -= ctl->weekstart;
		if (first_week_day < 0)
			first_week_day = DAYS_IN_WEEK - ctl->weekstart;
		month_days += ctl->weekstart - 1;
	}

	/* Fill day array. */
	for (i = 0; i < MAXDAYS; i++) {
		if (0 < first_week_day) {
			month->days[i] = SPACE;
			first_week_day--;
			continue;
		}
		if (j < month_days) {
			if (month->year == REFORMATION_YEAR && month->month == REFORMATION_MONTH && (j == 3 || j == 247))
				j += NUMBER_MISSING_DAYS;
			month->days[i] = j;
			j++;
			continue;
		}
		month->days[i] = SPACE;
		weeklines++;
	}

	/* Add week numbers */
	if (ctl->weektype) {
		int weeknum = week_number(1, month->month, month->year, ctl);
		weeklines = MAXDAYS / DAYS_IN_WEEK - weeklines / DAYS_IN_WEEK;
		for (i = 0; i < MAXDAYS / DAYS_IN_WEEK; i++) {
			if (0 < weeklines) {
				if (52 < weeknum)
					weeknum = week_number(month->days[i * DAYS_IN_WEEK], month->month, month->year, ctl);
				month->weeks[i] = weeknum++;
			} else
				month->weeks[i] = SPACE;
			weeklines--;
		}
	}
}

static void cal_output_header(struct cal_month *month, const struct cal_control *ctl)
{
	char out[FMT_ST_CHARS];
	struct cal_month *i;

	if (ctl->header_hint || ctl->header_year) {
		for (i = month; i; i = i->next) {
			sprintf(out, _("%s"), ctl->full_month[i->month - 1]);
			center(out, ctl->week_width - 1, i->next == NULL ? 0 : ctl->gutter_width);
		}
		if (!ctl->header_year) {
			my_putstring("\n");
			for (i = month; i; i = i->next) {
				sprintf(out, _("%d"), i->year);
				center(out, ctl->week_width - 1, i->next == NULL ? 0 : ctl->gutter_width);
			}
		}
	} else {
		for (i = month; i; i = i->next) {
			sprintf(out, _("%s %d"), ctl->full_month[i->month - 1], i->year);
			center(out, ctl->week_width - 1, i->next == NULL ? 0 : ctl->gutter_width);
		}
	}
	my_putstring("\n");
	for (i = month; i; i = i->next) {
		if (ctl->weektype) {
			if (ctl->julian)
				sprintf(out, "%*s%s", (int)ctl->day_width - 1, "", day_headings);
			else
				sprintf(out, "%*s%s", (int)ctl->day_width, "", day_headings);
			my_putstring(out);
		} else
			my_putstring(day_headings);
		if (i->next != NULL) {
			sprintf(out, "%*s", ctl->gutter_width, "");
			my_putstring(out);
		}
	}
	my_putstring("\n");
}

static void cal_output_months(struct cal_month *month, const struct cal_control *ctl)
{
	char out[FMT_ST_CHARS];
	int reqday, week_line, d;
	int skip;
	struct cal_month *i;

	for (week_line = 0; week_line < MAXDAYS / DAYS_IN_WEEK; week_line++) {
		for (i = month; i; i = i->next) {
			/* Determine the day that should be highlighted. */
			reqday = 0;
			if (i->month == ctl->req.month && i->year == ctl->req.year) {
				if (ctl->julian)
					reqday = ctl->req.day;
				else
					reqday =
					    ctl->req.day + 1 - day_in_year(1, i->month,
									   i->year);
			}

			if (ctl->weektype) {
				if (0 < i->weeks[week_line]) {
					if ((ctl->weektype & WEEK_NUM_MASK) ==
					    i->weeks[week_line])
						sprintf(out, "%s%2d%s", Senter, i->weeks[week_line],
						       Sexit);
					else
						sprintf(out, "%2d", i->weeks[week_line]);
				} else
					sprintf(out, "%2s", "");
				my_putstring(out);
				skip = ctl->day_width;
			} else
				/* First day of the week is one char narrower than the other days,
				 * unless week number is printed.  */
				skip = ctl->day_width - 1;

			for (d = DAYS_IN_WEEK * week_line;
			     d < DAYS_IN_WEEK * week_line + DAYS_IN_WEEK; d++) {
				if (0 < i->days[d]) {
					if (reqday == i->days[d])
						sprintf(out, "%*s%s%*d%s", skip - (ctl->julian ? 3 : 2),
						       "", Senter, (ctl->julian ? 3 : 2),
						       i->days[d], Sexit);
					else
						sprintf(out, "%*d", skip, i->days[d]);
				} else
					sprintf(out, "%*s", skip, "");
				my_putstring(out);
				if (skip < (int)ctl->day_width)
					skip++;
			}
			if (i->next != NULL) {
				sprintf(out, "%*s", ctl->gutter_width, "");
				my_putstring(out);
			}
		}
		if (i == NULL) {
			int extra = ctl->num_months > 3 ? 0 : 1;
			sprintf(out, "%*s\n", ctl->gutter_width - extra, "");
			my_putstring(out);
		}
	}
}

static void monthly(const struct cal_control *ctl)
{
	struct cal_month m1,m2,m3, *m;
	int i, rows, month = ctl->req.start_month ? ctl->req.start_month : ctl->req.month;
	int32_t year = ctl->req.year;

	/* cal -3 */
	if (ctl->num_months == 3 && ctl->months_in_row == 3) {
		if (month == 1){
			month = MONTHS_IN_YEAR;
			year--;
		} else
			month--;
	}

	m1.next = (ctl->months_in_row > 1) ? &m2 : NULL;
	m2.next = (ctl->months_in_row > 2) ? &m3 : NULL;
	m3.next = NULL;

	rows = (ctl->num_months - 1) / ctl->months_in_row;
	for (i = 0; i < rows + 1 ; i++){
		if (i == rows){
			switch (ctl->num_months % ctl->months_in_row){
				case 1:
					m1.next = NULL;
					/* fallthrough */
				case 2:
					m2.next = NULL;
					/* fallthrough */
			}
		}
		for (m = &m1; m; m = m->next){
			m->month = month++;
			m->year = year;
			if (MONTHS_IN_YEAR < month) {
				year++;
				month = 1;
			}
			cal_fill_month(m, ctl);
		}
		cal_output_header(&m1, ctl);
		cal_output_months(&m1, ctl);
	}
}

static void yearly(const struct cal_control *ctl)
{
	char out[FMT_ST_CHARS];
	int year_width = 0;

	year_width += (ctl->week_width + 1) * (ctl->julian ? 2 : 3);
	if (ctl->julian)
		year_width--;

	if (ctl->header_year) {
		sprintf(out, "%d", ctl->req.year);
		center(out, year_width, 0);
		my_putstring("\n\n");
	}
	monthly(ctl);

	/* Is empty line at the end year output really needed? */
	my_putstring("\n");
}

/*
 * day_in_year --
 *	return the 1 based day number within the year
 */
static int day_in_year(int day, int month, int32_t year)
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
static int day_in_week(int day, int month, int32_t year)
{
	static const int reform[] = {
		SUNDAY, WEDNESDAY, TUESDAY, FRIDAY, SUNDAY, WEDNESDAY,
		FRIDAY, MONDAY, THURSDAY, SATURDAY, TUESDAY, THURSDAY
	};
	static const int old[] = {
		FRIDAY, MONDAY, SUNDAY, WEDNESDAY, FRIDAY, MONDAY,
		WEDNESDAY, SATURDAY, TUESDAY, THURSDAY, SUNDAY, TUESDAY
	};
	if (year != REFORMATION_YEAR + 1)
		year -= month < MARCH;
	else
		year -= (month < MARCH) + 14;
	if (REFORMATION_YEAR < year
	    || (year == REFORMATION_YEAR && REFORMATION_MONTH < month)
	    || (year == REFORMATION_YEAR && month == REFORMATION_MONTH && 13 < day)) {
		int64_t long_year = year;
		return (long_year + (year / 4) - (year / 100) + (year / 400) + reform[month - 1] +
			day) % DAYS_IN_WEEK;
	}
	if (year < REFORMATION_YEAR
	    || (year == REFORMATION_YEAR && month < REFORMATION_MONTH)
	    || (year == REFORMATION_YEAR && month == REFORMATION_MONTH && day < 3))
		return (year + year / 4 + old[month - 1] + day) % DAYS_IN_WEEK;
	return NONEDAY;
}

/*
 * week_number
 *      return the week number of a given date, 1..54.
 *      Supports ISO-8601 and North American modes.
 *      Day may be given as Julian day of the year mode, in which
 *      case the month is disregarded entirely.
 */
static int week_number(int day, int month, int32_t year, const struct cal_control *ctl)
{
	int fday = 0, yday;
	const int wday = day_in_week(1, JANUARY, year);

	if (ctl->weektype & WEEK_NUM_ISO)
		fday = wday + (wday >= FRIDAY ? -2 : 5);
	else {
		/* WEEK_NUM_US: Jan 1 is always First week, that may
		 * begin previous year.  That means there is very seldom
		 * more than 52 weeks, */
		fday = wday + 6;
	}
	/* For julian dates the month can be set to 1, the global julian
	 * variable cannot be relied upon here, because we may recurse
	 * internally for 31.12. which would not work. */
	if (day > DAYS_IN_MONTH)
		month = JANUARY;

	yday = day_in_year(day,month,year);
	if (year == REFORMATION_YEAR) {
		if (yday >= YDAY_AFTER_MISSING)
			fday -= NUMBER_MISSING_DAYS;
	}

	/* Last year is last year */
	if (yday + fday < DAYS_IN_WEEK)
		return week_number(31, DECEMBER, year - 1, ctl);

	/* Or it could be part of the next year.  The reformation year had less
	 * days than 365 making this check invalid, but reformation year ended
	 * on Sunday and in week 51, so it's ok here. */
	if (ctl->weektype == WEEK_NUM_ISO && yday >= 363
	    && day_in_week(day, month, year) >= MONDAY
	    && day_in_week(day, month, year) <= WEDNESDAY
	    && day_in_week(31, DECEMBER, year) >= MONDAY
	    && day_in_week(31, DECEMBER, year) <= WEDNESDAY)
		return week_number(1, JANUARY, year + 1, ctl);

	return (yday + fday) / DAYS_IN_WEEK;
}

/*
 * week_to_day
 *      return the yday of the first day in a given week inside
 *      the given year. This may be something other than Monday
 *      for ISO-8601 modes. For North American numbering this
 *      always returns a Sunday.
 */
static int week_to_day(const struct cal_control *ctl)
{
	int yday, wday;

	wday = day_in_week(1, JANUARY, ctl->req.year);
	yday = ctl->req.week * DAYS_IN_WEEK - wday;

	if (ctl->weektype & WEEK_NUM_ISO)
		yday -= (wday >= FRIDAY ? -2 : 5);
	else
		yday -= (wday == SUNDAY ? 6 : -1);	/* WEEK_NUM_US */
	if (yday <= 0)
		return 1;

	return yday;
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
	fputs(_(" -n, --months <num>    show num months starting with date's month\n"), out);
	fputs(_(" -s, --sunday          Sunday as first day of week\n"), out);
	fputs(_(" -m, --monday          Monday as first day of week\n"), out);
	fputs(_(" -j, --julian          output Julian dates\n"), out);
	fputs(_(" -y, --year            show the whole year\n"), out);
	fputs(_(" -Y, --twelve          show the next twelve months\n"), out);
	fputs(_(" -w, --week[=<num>]    show US or ISO-8601 week numbers\n"), out);
	fputs(_("     --color[=<when>]  colorize messages (auto, always or never)\n"), out);
	fprintf(out,
	        "                         %s\n", USAGE_COLORS_DEFAULT);

	fputs(USAGE_SEPARATOR, out);
	fputs(USAGE_HELP, out);
	fputs(USAGE_VERSION, out);
	fprintf(out, USAGE_MAN_TAIL("cal(1)"));

	exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}
