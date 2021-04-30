/***
  First set of functions in this file are part of systemd, and were
  copied to util-linux at August 2013.

  Copyright 2010 Lennart Poettering

  systemd is free software; you can redistribute it and/or modify it
  under the terms of the GNU Lesser General Public License as published by
  the Free Software Foundation; either version 2.1 of the License, or
  (at your option) any later version.

  systemd is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with util-linux; If not, see <http://www.gnu.org/licenses/>.
***/

#include <assert.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <inttypes.h>

#include "c.h"
#include "nls.h"
#include "strutils.h"
#include "timeutils.h"

#define WHITESPACE " \t\n\r"

#define streq(a,b) (strcmp((a),(b)) == 0)

static int parse_sec(const char *t, usec_t *usec)
{
	static const struct {
		const char *suffix;
		usec_t usec;
	} table[] = {
		{ "seconds",	USEC_PER_SEC },
		{ "second",	USEC_PER_SEC },
		{ "sec",	USEC_PER_SEC },
		{ "s",		USEC_PER_SEC },
		{ "minutes",	USEC_PER_MINUTE },
		{ "minute",	USEC_PER_MINUTE },
		{ "min",	USEC_PER_MINUTE },
		{ "months",	USEC_PER_MONTH },
		{ "month",	USEC_PER_MONTH },
		{ "msec",	USEC_PER_MSEC },
		{ "ms",		USEC_PER_MSEC },
		{ "m",		USEC_PER_MINUTE },
		{ "hours",	USEC_PER_HOUR },
		{ "hour",	USEC_PER_HOUR },
		{ "hr",		USEC_PER_HOUR },
		{ "h",		USEC_PER_HOUR },
		{ "days",	USEC_PER_DAY },
		{ "day",	USEC_PER_DAY },
		{ "d",		USEC_PER_DAY },
		{ "weeks",	USEC_PER_WEEK },
		{ "week",	USEC_PER_WEEK },
		{ "w",		USEC_PER_WEEK },
		{ "years",	USEC_PER_YEAR },
		{ "year",	USEC_PER_YEAR },
		{ "y",		USEC_PER_YEAR },
		{ "usec",	1ULL },
		{ "us",		1ULL },
		{ "",		USEC_PER_SEC },	/* default is sec */
	};

	const char *p;
	usec_t r = 0;
	int something = FALSE;

	assert(t);
	assert(usec);

	p = t;
	for (;;) {
		long long l, z = 0;
		char *e;
		unsigned i, n = 0;

		p += strspn(p, WHITESPACE);

		if (*p == 0) {
			if (!something)
				return -EINVAL;

			break;
		}

		errno = 0;
		l = strtoll(p, &e, 10);

		if (errno > 0)
			return -errno;

		if (l < 0)
			return -ERANGE;

		if (*e == '.') {
			char *b = e + 1;

			errno = 0;
			z = strtoll(b, &e, 10);
			if (errno > 0)
				return -errno;

			if (z < 0)
				return -ERANGE;

			if (e == b)
				return -EINVAL;

			n = e - b;

		} else if (e == p)
			return -EINVAL;

		e += strspn(e, WHITESPACE);

		for (i = 0; i < ARRAY_SIZE(table); i++)
			if (startswith(e, table[i].suffix)) {
				usec_t k = (usec_t) z * table[i].usec;

				for (; n > 0; n--)
					k /= 10;

				r += (usec_t) l *table[i].usec + k;
				p = e + strlen(table[i].suffix);

				something = TRUE;
				break;
			}

		if (i >= ARRAY_SIZE(table))
			return -EINVAL;

	}

	*usec = r;

	return 0;
}

int parse_timestamp(const char *t, usec_t *usec)
{
	static const struct {
		const char *name;
		const int nr;
	} day_nr[] = {
		{ "Sunday",	0 },
		{ "Sun",	0 },
		{ "Monday",	1 },
		{ "Mon",	1 },
		{ "Tuesday",	2 },
		{ "Tue",	2 },
		{ "Wednesday",	3 },
		{ "Wed",	3 },
		{ "Thursday",	4 },
		{ "Thu",	4 },
		{ "Friday",	5 },
		{ "Fri",	5 },
		{ "Saturday",	6 },
		{ "Sat",	6 },
	};

	const char *k;
	struct tm tm, copy;
	time_t x;
	usec_t plus = 0, minus = 0, ret;
	int r, weekday = -1;
	unsigned i;

	/*
	 * Allowed syntaxes:
	 *
	 *   2012-09-22 16:34:22
	 *   2012-09-22T16:34:22
	 *   2012-09-22 16:34	  (seconds will be set to 0)
	 *   2012-09-22		  (time will be set to 00:00:00)
	 *   16:34:22		  (date will be set to today)
	 *   16:34		  (date will be set to today, seconds to 0)
	 *   now
	 *   yesterday		  (time is set to 00:00:00)
	 *   today		  (time is set to 00:00:00)
	 *   tomorrow		  (time is set to 00:00:00)
	 *   +5min
	 *   -5days
	 *
	 */

	assert(t);
	assert(usec);

	x = time(NULL);
	localtime_r(&x, &tm);
	tm.tm_isdst = -1;

	if (streq(t, "now"))
		goto finish;

	else if (streq(t, "today")) {
		tm.tm_sec = tm.tm_min = tm.tm_hour = 0;
		goto finish;

	} else if (streq(t, "yesterday")) {
		tm.tm_mday--;
		tm.tm_sec = tm.tm_min = tm.tm_hour = 0;
		goto finish;

	} else if (streq(t, "tomorrow")) {
		tm.tm_mday++;
		tm.tm_sec = tm.tm_min = tm.tm_hour = 0;
		goto finish;

	} else if (t[0] == '+') {

		r = parse_sec(t + 1, &plus);
		if (r < 0)
			return r;

		goto finish;
	} else if (t[0] == '-') {

		r = parse_sec(t + 1, &minus);
		if (r < 0)
			return r;

		goto finish;

	} else if (endswith(t, " ago")) {
		char *z;

		z = strndup(t, strlen(t) - 4);
		if (!z)
			return -ENOMEM;

		r = parse_sec(z, &minus);
		free(z);
		if (r < 0)
			return r;

		goto finish;
	}

	for (i = 0; i < ARRAY_SIZE(day_nr); i++) {
		size_t skip;

		if (!startswith_no_case(t, day_nr[i].name))
			continue;

		skip = strlen(day_nr[i].name);
		if (t[skip] != ' ')
			continue;

		weekday = day_nr[i].nr;
		t += skip + 1;
		break;
	}

	copy = tm;
	k = strptime(t, "%y-%m-%d %H:%M:%S", &tm);
	if (k && *k == 0)
		goto finish;

	tm = copy;
	k = strptime(t, "%Y-%m-%d %H:%M:%S", &tm);
	if (k && *k == 0)
		goto finish;

	tm = copy;
	k = strptime(t, "%Y-%m-%dT%H:%M:%S", &tm);
	if (k && *k == 0)
		goto finish;

	tm = copy;
	k = strptime(t, "%y-%m-%d %H:%M", &tm);
	if (k && *k == 0) {
		tm.tm_sec = 0;
		goto finish;
	}

	tm = copy;
	k = strptime(t, "%Y-%m-%d %H:%M", &tm);
	if (k && *k == 0) {
		tm.tm_sec = 0;
		goto finish;
	}

	tm = copy;
	k = strptime(t, "%y-%m-%d", &tm);
	if (k && *k == 0) {
		tm.tm_sec = tm.tm_min = tm.tm_hour = 0;
		goto finish;
	}

	tm = copy;
	k = strptime(t, "%Y-%m-%d", &tm);
	if (k && *k == 0) {
		tm.tm_sec = tm.tm_min = tm.tm_hour = 0;
		goto finish;
	}

	tm = copy;
	k = strptime(t, "%H:%M:%S", &tm);
	if (k && *k == 0)
		goto finish;

	tm = copy;
	k = strptime(t, "%H:%M", &tm);
	if (k && *k == 0) {
		tm.tm_sec = 0;
		goto finish;
	}

	tm = copy;
	k = strptime(t, "%Y%m%d%H%M%S", &tm);
	if (k && *k == 0) {
		tm.tm_sec = 0;
		goto finish;
	}

	return -EINVAL;

 finish:
	x = mktime(&tm);
	if (x == (time_t)-1)
		return -EINVAL;

	if (weekday >= 0 && tm.tm_wday != weekday)
		return -EINVAL;

	ret = (usec_t) x *USEC_PER_SEC;

	ret += plus;
	if (ret > minus)
		ret -= minus;
	else
		ret = 0;

	*usec = ret;

	return 0;
}

/* Returns the difference in seconds between its argument and GMT. If if TP is
 * invalid or no DST information is available default to UTC, that is, zero.
 * tzset is called so, for example, 'TZ="UTC" hwclock' will work as expected.
 * Derived from glibc/time/strftime_l.c
 */
int get_gmtoff(const struct tm *tp)
{
	if (tp->tm_isdst < 0)
		return 0;

#if HAVE_TM_GMTOFF
	return tp->tm_gmtoff;
#else
	struct tm tm;
	struct tm gtm;
	struct tm ltm = *tp;
	time_t lt;

	tzset();
	lt = mktime(&ltm);
	/* Check if mktime returning -1 is an error or a valid time_t */
	if (lt == (time_t) -1) {
		if (! localtime_r(&lt, &tm)
			|| ((ltm.tm_sec ^ tm.tm_sec)
			    | (ltm.tm_min ^ tm.tm_min)
			    | (ltm.tm_hour ^ tm.tm_hour)
			    | (ltm.tm_mday ^ tm.tm_mday)
			    | (ltm.tm_mon ^ tm.tm_mon)
			    | (ltm.tm_year ^ tm.tm_year)))
			return 0;
	}

	if (! gmtime_r(&lt, &gtm))
		return 0;

	/* Calculate the GMT offset, that is, the difference between the
	 * TP argument (ltm) and GMT (gtm).
	 *
	 * Compute intervening leap days correctly even if year is negative.
	 * Take care to avoid int overflow in leap day calculations, but it's OK
	 * to assume that A and B are close to each other.
	 */
	int a4 = (ltm.tm_year >> 2) + (1900 >> 2) - ! (ltm.tm_year & 3);
	int b4 = (gtm.tm_year >> 2) + (1900 >> 2) - ! (gtm.tm_year & 3);
	int a100 = a4 / 25 - (a4 % 25 < 0);
	int b100 = b4 / 25 - (b4 % 25 < 0);
	int a400 = a100 >> 2;
	int b400 = b100 >> 2;
	int intervening_leap_days = (a4 - b4) - (a100 - b100) + (a400 - b400);

	int years = ltm.tm_year - gtm.tm_year;
	int days = (365 * years + intervening_leap_days
		    + (ltm.tm_yday - gtm.tm_yday));

	return (60 * (60 * (24 * days + (ltm.tm_hour - gtm.tm_hour))
		+ (ltm.tm_min - gtm.tm_min)) + (ltm.tm_sec - gtm.tm_sec));
#endif
}

static int format_iso_time(struct tm *tm, suseconds_t usec, int flags, char *buf, size_t bufsz)
{
	char *p = buf;
	int len;

	if (flags & ISO_DATE) {
		len = snprintf(p, bufsz, "%4ld-%.2d-%.2d",
			       tm->tm_year + (long) 1900,
			       tm->tm_mon + 1, tm->tm_mday);
		if (len < 0 || (size_t) len > bufsz)
			goto err;
		bufsz -= len;
		p += len;
	}

	if ((flags & ISO_DATE) && (flags & ISO_TIME)) {
		if (bufsz < 1)
			goto err;
		*p++ = (flags & ISO_T) ? 'T' : ' ';
		bufsz--;
	}

	if (flags & ISO_TIME) {
		len = snprintf(p, bufsz, "%02d:%02d:%02d", tm->tm_hour,
			       tm->tm_min, tm->tm_sec);
		if (len < 0 || (size_t) len > bufsz)
			goto err;
		bufsz -= len;
		p += len;
	}

	if (flags & ISO_DOTUSEC) {
		len = snprintf(p, bufsz, ".%06"PRId64, (int64_t) usec);
		if (len < 0 || (size_t) len > bufsz)
			goto err;
		bufsz -= len;
		p += len;

	} else if (flags & ISO_COMMAUSEC) {
		len = snprintf(p, bufsz, ",%06"PRId64, (int64_t) usec);
		if (len < 0 || (size_t) len > bufsz)
			goto err;
		bufsz -= len;
		p += len;
	}

	if (flags & ISO_TIMEZONE) {
		int tmin  = get_gmtoff(tm) / 60;
		int zhour = tmin / 60;
		int zmin  = abs(tmin % 60);
		len = snprintf(p, bufsz, "%+03d:%02d", zhour,zmin);
		if (len < 0 || (size_t) len > bufsz)
			goto err;
	}
	return 0;
 err:
	warnx(_("format_iso_time: buffer overflow."));
	return -1;
}

/* timeval to ISO 8601 */
int strtimeval_iso(struct timeval *tv, int flags, char *buf, size_t bufsz)
{
	struct tm tm;
	struct tm *rc;

	if (flags & ISO_GMTIME)
		rc = gmtime_r(&tv->tv_sec, &tm);
	else
		rc = localtime_r(&tv->tv_sec, &tm);

	if (rc)
		return format_iso_time(&tm, tv->tv_usec, flags, buf, bufsz);

	warnx(_("time %"PRId64" is out of range."), (int64_t)(tv->tv_sec));
	return -1;
}

/* struct tm to ISO 8601 */
int strtm_iso(struct tm *tm, int flags, char *buf, size_t bufsz)
{
	return format_iso_time(tm, 0, flags, buf, bufsz);
}

/* time_t to ISO 8601 */
int strtime_iso(const time_t *t, int flags, char *buf, size_t bufsz)
{
	struct tm tm;
	struct tm *rc;

	if (flags & ISO_GMTIME)
		rc = gmtime_r(t, &tm);
	else
		rc = localtime_r(t, &tm);

	if (rc)
		return format_iso_time(&tm, 0, flags, buf, bufsz);

	warnx(_("time %"PRId64" is out of range."), (int64_t)*t);
	return -1;
}

/* relative time functions */
static inline int time_is_thisyear(struct tm const *const tm,
				   struct tm const *const tmnow)
{
	return tm->tm_year == tmnow->tm_year;
}

static inline int time_is_today(struct tm const *const tm,
				struct tm const *const tmnow)
{
	return (tm->tm_yday == tmnow->tm_yday &&
		time_is_thisyear(tm, tmnow));
}

int strtime_short(const time_t *t, struct timeval *now, int flags, char *buf, size_t bufsz)
{
	struct tm tm, tmnow;
	int rc = 0;

	if (now->tv_sec == 0)
		gettimeofday(now, NULL);

	localtime_r(t, &tm);
	localtime_r(&now->tv_sec, &tmnow);

	if (time_is_today(&tm, &tmnow)) {
		rc = snprintf(buf, bufsz, "%02d:%02d", tm.tm_hour, tm.tm_min);
		if (rc < 0 || (size_t) rc > bufsz)
			return -1;
		rc = 1;

	} else if (time_is_thisyear(&tm, &tmnow)) {
		if (flags & UL_SHORTTIME_THISYEAR_HHMM)
			rc = strftime(buf, bufsz, "%b%d/%H:%M", &tm);
		else
			rc = strftime(buf, bufsz, "%b%d", &tm);
	} else
		rc = strftime(buf, bufsz, "%Y-%b%d", &tm);

	return rc <= 0 ? -1 : 0;
}

#ifndef HAVE_TIMEGM
time_t timegm(struct tm *tm)
{
	const char *zone = getenv("TZ");
	time_t ret;

	setenv("TZ", "", 1);
	tzset();
	ret = mktime(tm);
	if (zone)
		setenv("TZ", zone, 1);
	else
		unsetenv("TZ");
	tzset();
	return ret;
}
#endif /* HAVE_TIMEGM */

#ifdef TEST_PROGRAM_TIMEUTILS

int main(int argc, char *argv[])
{
	struct timeval tv = { 0 };
	char buf[ISO_BUFSIZ];

	if (argc < 2) {
		fprintf(stderr, "usage: %s [<time> [<usec>]] | [--timestamp <str>]\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	if (strcmp(argv[1], "--timestamp") == 0) {
		usec_t usec = 0;

		parse_timestamp(argv[2], &usec);
		tv.tv_sec = (time_t) (usec / 1000000);
		tv.tv_usec = usec % 1000000;
	} else {
		tv.tv_sec = strtos64_or_err(argv[1], "failed to parse <time>");
		if (argc == 3)
			tv.tv_usec = strtos64_or_err(argv[2], "failed to parse <usec>");
	}

	strtimeval_iso(&tv, ISO_DATE, buf, sizeof(buf));
	printf("Date: '%s'\n", buf);

	strtimeval_iso(&tv, ISO_TIME, buf, sizeof(buf));
	printf("Time: '%s'\n", buf);

	strtimeval_iso(&tv, ISO_DATE | ISO_TIME | ISO_COMMAUSEC | ISO_T,
		       buf, sizeof(buf));
	printf("Full: '%s'\n", buf);

	strtimeval_iso(&tv, ISO_TIMESTAMP_DOT, buf, sizeof(buf));
	printf("Zone: '%s'\n", buf);

	return EXIT_SUCCESS;
}

#endif /* TEST_PROGRAM_TIMEUTILS */
