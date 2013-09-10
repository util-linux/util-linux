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
#include <string.h>
#include <time.h>

#include "c.h"
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
