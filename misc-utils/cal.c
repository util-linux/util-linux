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
 * 1999-02-22 Arkadiusz Mi¶kiewicz <misiek@pld.ORG.PL>
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <locale.h>
#include "errs.h"
#include "nls.h"
#include "../defines.h"

#if defined(HAVE_ncurses)

#if NCH
#include <ncurses.h>
#else
#include <curses.h>
#endif
#include <term.h>                       /* include after <curses.h> */

static void
my_setupterm(const char *term, int fildes, int *errret) {
    setupterm((char*)term, fildes, errret);
}

static void
my_putstring(char *s) {
     putp(s);
}

static char *
my_tgetstr(char *s, char *ss) {
     return tigetstr(ss);
}

#elif defined(HAVE_termcap)

#include <termcap.h>

char termbuffer[4096];
char tcbuffer[4096];
char *strbuf = termbuffer;

static void
my_setupterm(const char *term, int fildes, int *errret) {
    *errret = tgetent(tcbuffer, term);
}

static void
my_putstring(char *s) {
     tputs (s, 1, putchar);
}

static char *
my_tgetstr(char *s, char *ss) {
     return tgetstr(s, &strbuf);
}

#endif
const char	*term="";
const char	*Senter="", *Sexit="";/* enter and exit standout mode */

#ifdef HAVE_langinfo_h
# include <langinfo.h>
#else
# include <localeinfo.h>	/* libc4 only */
#endif

#include "widechar.h"

#define SIZE(a)	(sizeof(a)/sizeof((a)[0]))

/* allow compile-time define to over-ride default */
#ifndef NUM_MONTHS
#define NUM_MONTHS 1
#endif

#if ( NUM_MONTHS != 1 && NUM_MONTHS !=3 )
#error NUM_MONTHS must be 1 or 3
#endif

#define	THURSDAY		4		/* for reformation */
#define	SATURDAY		6		/* 1 Jan 1 was a Saturday */

#define	FIRST_MISSING_DAY	639799		/* 3 Sep 1752 */
#define	NUMBER_MISSING_DAYS	11		/* 11 day correction */

#define	MAXDAYS			43		/* max slots in a month array */
#define	SPACE			-1		/* used in day array */

static int days_in_month[2][13] = {
	{0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31},
	{0, 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31},
};

int sep1752[MAXDAYS] = {
	SPACE,	SPACE,	1,	2,	14,	15,	16,
	17,	18,	19,	20,	21,	22,	23,
	24,	25,	26,	27,	28,	29,	30,
	SPACE,	SPACE,	SPACE,	SPACE,	SPACE,	SPACE,	SPACE,
	SPACE,	SPACE,	SPACE,	SPACE,	SPACE,	SPACE,	SPACE,
	SPACE,	SPACE,	SPACE,	SPACE,	SPACE,	SPACE,	SPACE,
	SPACE
}, j_sep1752[MAXDAYS] = {
	SPACE,	SPACE,	245,	246,	258,	259,	260,
	261,	262,	263,	264,	265,	266,	267,
	268,	269,	270,	271,	272,	273,	274,
	SPACE,	SPACE,	SPACE,	SPACE,	SPACE,	SPACE,	SPACE,
	SPACE,	SPACE,	SPACE,	SPACE,	SPACE,	SPACE,	SPACE,
	SPACE,	SPACE,	SPACE,	SPACE,	SPACE,	SPACE,	SPACE,
	SPACE
}, empty[MAXDAYS] = {
	SPACE,	SPACE,	SPACE,	SPACE,	SPACE,	SPACE,	SPACE,
	SPACE,	SPACE,	SPACE,	SPACE,	SPACE,	SPACE,	SPACE,
	SPACE,	SPACE,	SPACE,	SPACE,	SPACE,	SPACE,	SPACE,
	SPACE,	SPACE,	SPACE,	SPACE,	SPACE,	SPACE,	SPACE,
	SPACE,	SPACE,	SPACE,	SPACE,	SPACE,	SPACE,	SPACE,
	SPACE,	SPACE,	SPACE,	SPACE,	SPACE,	SPACE,	SPACE,
	SPACE
};

#define	DAY_LEN		3		/* 3 spaces per day */
#define	J_DAY_LEN	4		/* 4 spaces per day */
#define	WEEK_LEN	21		/* 7 days * 3 characters */
#define	J_WEEK_LEN	28		/* 7 days * 4 characters */
#define	HEAD_SEP	2		/* spaces between day headings */
#define	J_HEAD_SEP	2

/* utf-8 can have up to 6 bytes per char; and an extra byte for ending \0 */
char day_headings[WEEK_LEN*6+1];
/* week1stday = 1  =>   " M Tu  W Th  F  S  S " */
char j_day_headings[J_WEEK_LEN*6+1];
/* week1stday = 1  =>   "  M  Tu   W  Th   F   S   S " */
const char *full_month[12];

/* leap year -- account for gregorian reformation in 1752 */
#define	leap_year(yr) \
	((yr) <= 1752 ? !((yr) % 4) : \
	(!((yr) % 4) && ((yr) % 100)) || !((yr) % 400))

/* number of centuries since 1700, not inclusive */
#define	centuries_since_1700(yr) \
	((yr) > 1700 ? (yr) / 100 - 17 : 0)

/* number of centuries since 1700 whose modulo of 400 is 0 */
#define	quad_centuries_since_1700(yr) \
	((yr) > 1600 ? ((yr) - 1600) / 400 : 0)

/* number of leap years between year 1 and this year, not inclusive */
#define	leap_years_since_year_1(yr) \
	((yr) / 4 - centuries_since_1700(yr) + quad_centuries_since_1700(yr))

/* 0 => sunday (default), 1 => monday */
int week1stday=0;
int julian;

#define TODAY_FLAG		0x400		/* flag day for highlighting */

#define FMT_ST_LINES 8
#define FMT_ST_CHARS 300	/* 90 suffices in most locales */
struct fmt_st
{
  char s[FMT_ST_LINES][FMT_ST_CHARS];
};

char * ascii_day(char *, int);
void center_str(const char* src, char* dest, size_t dest_size, int width);
void center(const char *, int, int);
void day_array(int, int, int, int *);
int day_in_week(int, int, int);
int day_in_year(int, int, int);
void yearly(int, int);
void j_yearly(int, int);
void do_monthly(int, int, int, struct fmt_st*);
void monthly(int, int, int);
void monthly3(int, int, int);
void trim_trailing_spaces(char *);
void usage(void);
void headers_init(void);
extern char *__progname;

int
main(int argc, char **argv) {
	struct tm *local_time;
	time_t now;
	int ch, day, month, year, yflag;
	char *progname, *p;
	int num_months = NUM_MONTHS;

	progname = argv[0];
	if ((p = strrchr(progname, '/')) != NULL)
		progname = p+1;
	__progname = progname;

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

#if defined(HAVE_ncurses) || defined(HAVE_termcap)
	if ((term = getenv("TERM"))) {
		int ret;
		my_setupterm(term, 1, &ret);
		if (ret > 0) {
			Senter = my_tgetstr("so","smso");
			Sexit = my_tgetstr("se","rmso");
		}
	}
#endif

#if 0				/* setting week1stday is against man page */
/*
 * What *is* the first day of the week? Note that glibc does not
 * provide any information today, it (almost) always answers Monday.
 * Sunday is the Jewish and Christian tradition.
 * Sometimes an answer is built into the language:
 * German calls Wednesday "Mittwoch", so starts at Sunday.
 * Portuguese calls Monday "segunda-feira", so starts at Sunday.
 * Russian calls Friday "pyatnitsa", so starts at Monday.
 * ISO 8601 decided to start at Monday.
 *
 * The traditional Unix cal utility starts at Sunday.
 * We start at Sunday and have an option -m for starting at Monday.
 *
 * At some future time this may become -s for Sunday, -m for Monday,
 * no option for glibc-determined locale-dependent version.
 */
#ifdef HAVE_langinfo_h
	week1stday = (int)(nl_langinfo(_NL_TIME_FIRST_WEEKDAY))[0];
#endif
#endif

	yflag = 0;
	while ((ch = getopt(argc, argv, "13mjsyV")) != -1)
		switch(ch) {
		case '1':
			num_months = 1;		/* default */
			break;
		case '3':
			num_months = 3;
			break;
		case 's':
			week1stday = 0;		/* default */
			break;
		case 'm':
			week1stday = 1;
			break;
		case 'j':
			julian = 1;
			break;
		case 'y':
			yflag = 1;
			break;
		case 'V':
			printf(_("%s from %s\n"),
			       progname, util_linux_version);
			return 0;
		case '?':
		default:
			usage();
		}
	argc -= optind;
	argv += optind;

	day = month = year = 0;
	switch(argc) {
	case 2:
		if ((month = atoi(*argv++)) < 1 || month > 12)
			errx(1, _("illegal month value: use 1-12"));
		/* FALLTHROUGH */
	case 1:
		if ((year = atoi(*argv)) < 1 || year > 9999)
			errx(1, _("illegal year value: use 1-9999"));
		break;
	case 0:
		time(&now);
		local_time = localtime(&now);
		if (isatty(1))
			day = local_time->tm_yday + 1;
		year = local_time->tm_year + 1900;
		if (!yflag)
			month = local_time->tm_mon + 1;
		break;
	default:
		usage();
	}
	headers_init();

	if (month && num_months == 1)
		monthly(day, month, year);
	else if (month && num_months == 3)
		monthly3(day, month, year);
	else if (julian)
		j_yearly(day, year);
	else
		yearly(day, year);
	exit(0);
}

#ifndef ENABLE_WIDECHAR
static char *eos(char *s) {
	while (s && *s)
		s++;
	return s;
}
#endif

void headers_init(void)
{
  int i, wd;
#ifdef ENABLE_WIDECHAR
  wchar_t day_headings_wc[22],j_day_headings_wc[29];
  wchar_t wd_wc[10];
#endif

  strcpy(day_headings,"");
  strcpy(j_day_headings,"");
#ifdef ENABLE_WIDECHAR
  wcscpy(day_headings_wc,L"");
  wcscpy(j_day_headings_wc,L"");
#endif

#ifdef HAVE_langinfo_h
# define weekday(wd)	nl_langinfo(ABDAY_1+wd)
#else
# define weekday(wd)	_time_info->abbrev_wkday[wd]
#endif

  for(i = 0 ; i < 7 ; i++ ) {
     wd = (i + week1stday) % 7;
#ifdef ENABLE_WIDECHAR
     mbstowcs(wd_wc,weekday(wd),10);
     if (wcswidth(wd_wc,10) < 3)
	     wcscat(j_day_headings_wc,L" ");
     if (wcswidth(wd_wc,10) < 2) {
	     wcscat(day_headings_wc, L" ");
	     wcscat(j_day_headings_wc, L" ");
     }
     wcsncat(day_headings_wc,wd_wc,2);
     wcsncat(j_day_headings_wc,wd_wc,3);
     wcscat(day_headings_wc, L" ");
     wcscat(j_day_headings_wc, L" ");
#else
     sprintf(eos(day_headings), "%2.2s ", weekday(wd));
     sprintf(eos(j_day_headings), "%3.3s ", weekday(wd));
#endif
  }

#ifdef ENABLE_WIDECHAR
  wcstombs(day_headings,day_headings_wc,sizeof(day_headings));
  wcstombs(j_day_headings,j_day_headings_wc,sizeof(j_day_headings));
#endif

  trim_trailing_spaces(day_headings);
  trim_trailing_spaces(j_day_headings);
#undef weekday

  for (i = 0; i < 12; i++) {
#ifdef HAVE_langinfo_h
     full_month[i] = nl_langinfo(MON_1+i);
#else
     full_month[i] = _time_info->full_month[i];
#endif
  }
}

void
do_monthly(int day, int month, int year, struct fmt_st *out) {
	int col, row, len, days[MAXDAYS];
	char *p, lineout[FMT_ST_CHARS];
	int width = (julian ? J_WEEK_LEN : WEEK_LEN) - 1;

	day_array(day, month, year, days);

	/*
	 * %s is the month name, %d the year number.
	 * you can change the order and/or add something here; eg for
	 * Basque the translation should be: "%2$dko %1$s", and
	 * the Vietnamese should be "%s na(m %d", etc.
	 */
	len = sprintf(lineout, _("%s %d"), full_month[month - 1], year);
	center_str(lineout, out->s[0], SIZE(out->s[0]), width);

	sprintf(out->s[1],"%s",
		julian ? j_day_headings : day_headings);
	for (row = 0; row < 6; row++) {
		for (col = 0, p = lineout; col < 7; col++)
			p = ascii_day(p, days[row * 7 + col]);
		*p = '\0';
		trim_trailing_spaces(lineout);
		sprintf(out->s[row+2], "%s", lineout);
	}
}

void
monthly(int day, int month, int year) {
	int i;
	struct fmt_st out;

	do_monthly(day, month, year, &out);
	for (i = 0; i < FMT_ST_LINES; i++) {
#if defined(HAVE_ncurses) || defined(HAVE_termcap)
		my_putstring(out.s[i]);putchar('\n');
#else
		puts(out.s[i]);
#endif
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
		prev_month = 12;
		prev_year  = year - 1;
	} else {
		prev_month = month - 1;
		prev_year  = year;
	}
	if (month == 12) {
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
	for (i = 0; i < 2; i++)
		printf("%s  %s  %s\n", out_prev.s[i], out_curm.s[i], out_next.s[i]);
	for (i = 2; i < FMT_ST_LINES; i++) {
		snprintf(lineout, SIZE(lineout), "%-*s  %-*s  %-*s\n",
		       width, out_prev.s[i],
		       width, out_curm.s[i],
		       width, out_next.s[i]);
#if defined(HAVE_ncurses) || defined(HAVE_termcap)
		my_putstring(lineout);
#else
		fputs(lineout,stdout);
#endif
	}
}

void
j_yearly(int day, int year) {
	int col, *dp, i, month, row, which_cal;
	int days[12][MAXDAYS];
	char *p, lineout[80];

	sprintf(lineout, "%d", year);
	center(lineout, J_WEEK_LEN * 2 + J_HEAD_SEP, 0);
	printf("\n\n");

	for (i = 0; i < 12; i++)
		day_array(day, i + 1, year, days[i]);
	memset(lineout, ' ', sizeof(lineout) - 1);
	lineout[sizeof(lineout) - 1] = '\0';
	for (month = 0; month < 12; month += 2) {
		center(full_month[month], J_WEEK_LEN, J_HEAD_SEP);
		center(full_month[month + 1], J_WEEK_LEN, 0);
		printf("\n%s%*s %s\n", j_day_headings, J_HEAD_SEP, "",
		    j_day_headings);
		for (row = 0; row < 6; row++) {
			p = lineout;
			for (which_cal = 0; which_cal < 2; which_cal++) {
				dp = &days[month + which_cal][row * 7];
				for (col = 0; col < 7; col++)
					p = ascii_day(p, *dp++);
				p += sprintf(p, "  ");
			}
			*p = '\0';
			trim_trailing_spaces(lineout);
#if defined(HAVE_ncurses) || defined(HAVE_termcap)
			my_putstring(lineout);putchar('\n');
#else
			puts(lineout);
#endif
		}
	}
	printf("\n");
}

void
yearly(int day, int year) {
	int col, *dp, i, month, row, which_cal;
	int days[12][MAXDAYS];
	char *p, lineout[100];

	sprintf(lineout, "%d", year);
	center(lineout, WEEK_LEN * 3 + HEAD_SEP * 2, 0);
	printf("\n\n");

	for (i = 0; i < 12; i++)
		day_array(day, i + 1, year, days[i]);
	memset(lineout, ' ', sizeof(lineout) - 1);
	lineout[sizeof(lineout) - 1] = '\0';
	for (month = 0; month < 12; month += 3) {
		center(full_month[month], WEEK_LEN, HEAD_SEP);
		center(full_month[month + 1], WEEK_LEN, HEAD_SEP);
		center(full_month[month + 2], WEEK_LEN, 0);
		printf("\n%s%*s %s%*s %s\n", day_headings, HEAD_SEP,
		    "", day_headings, HEAD_SEP, "", day_headings);
		for (row = 0; row < 6; row++) {
			p = lineout;
			for (which_cal = 0; which_cal < 3; which_cal++) {
				dp = &days[month + which_cal][row * 7];
				for (col = 0; col < 7; col++)
					p = ascii_day(p, *dp++);
				p += sprintf(p, "  ");
			}
			*p = '\0';
			trim_trailing_spaces(lineout);
#if defined(HAVE_ncurses) || defined(HAVE_termcap)
                        my_putstring(lineout);putchar('\n');
#else
                        puts(lineout);
#endif
		}
	}
	printf("\n");
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
	int *d_sep1752;

	if (month == 9 && year == 1752) {
		d_sep1752 = julian ? j_sep1752 : sep1752;
		memcpy(days, d_sep1752 + week1stday, MAXDAYS * sizeof(int));
		return;
	}
	memcpy(days, empty, MAXDAYS * sizeof(int));
	dm = days_in_month[leap_year(year)][month];
	dw = (day_in_week(1, month, year) - week1stday + 7) % 7;
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

	temp = (long)(year - 1) * 365 + leap_years_since_year_1(year - 1)
	    + day_in_year(day, month, year);
	if (temp < FIRST_MISSING_DAY)
		return ((temp - 1 + SATURDAY) % 7);
	if (temp >= (FIRST_MISSING_DAY + NUMBER_MISSING_DAYS))
		return (((temp - 1 + SATURDAY) - NUMBER_MISSING_DAYS) % 7);
	return (THURSDAY);
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
		p += sprintf(p, Senter);
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
		p += sprintf(p, Sexit);
	*p++ = ' ';
	return p;
}

void
trim_trailing_spaces(s)
	char *s;
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
 */
void
center_str(const char* src, char* dest, size_t dest_size, int width)
{
#ifdef ENABLE_WIDECHAR
	wchar_t str_wc[FMT_ST_CHARS];
#endif
	char str[FMT_ST_CHARS];
	const char* str_to_print=src;
	int len, spaces, wide_char_enabled=0;

	len = strlen(src);

#ifdef ENABLE_WIDECHAR
	if (mbstowcs(str_wc, src, FMT_ST_CHARS) > 0) {
		wide_char_enabled = 1;
		len = wcswidth(str_wc, SIZE(str_wc));
	}
#endif
	if (len > width) {
		str_to_print=str;
		if (wide_char_enabled) {
#ifdef ENABLE_WIDECHAR
			str_wc[width]=L'\0';
			wcstombs(str, str_wc, SIZE(str));
#endif
		} else {
			strncpy(str, src, SIZE(str));
			str[width]='\0';
		}
	}

	spaces = width - len;
	spaces = ( spaces < 0 ? 0 : spaces );

	snprintf(dest, dest_size, "%*s%s%*s",
		spaces / 2, "",
                str_to_print,
		spaces / 2 + spaces % 2, "" );
}

void
center(str, len, separate)
	const char *str;
	int len;
	int separate;
{
	char lineout[FMT_ST_CHARS];
	center_str(str, lineout, SIZE(lineout), len);
	fputs(lineout, stdout);
	if (separate)
		(void)printf("%*s", separate, "");
}

void
usage()
{

	(void)fprintf(stderr, _("usage: cal [-13smjyV] [[month] year]\n"));
	exit(1);
}
