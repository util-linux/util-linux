/***
  First set of functions in this file are part of systemd, and were
  copied to util-linux at August 2013.

  Copyright 2010 Lennart Poettering
  Copyright (C) 2014 Karel Zak <kzak@redhat.com>

  systemd is free software; you can redistribute it and/or modify it
  under the terms of the GNU Lesser General Public License as published by
  the Free Software Foundation; either version 2.1 of the License, or
  (at your option) any later version.

  systemd is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with systemd; If not, see <http://www.gnu.org/licenses/>.
***/
#ifndef UTIL_LINUX_TIME_UTIL_H
#define UTIL_LINUX_TIME_UTIL_H

#include <stdio.h>
#include <inttypes.h>
#include <sys/time.h>

typedef uint64_t usec_t;
typedef uint64_t nsec_t;

#define MSEC_PER_SEC  1000ULL
#define USEC_PER_SEC  1000000ULL
#define USEC_PER_MSEC 1000ULL
#define NSEC_PER_SEC  1000000000ULL
#define NSEC_PER_MSEC 1000000ULL
#define NSEC_PER_USEC 1000ULL

#define USEC_PER_MINUTE	(60ULL*USEC_PER_SEC)
#define NSEC_PER_MINUTE	(60ULL*NSEC_PER_SEC)
#define USEC_PER_HOUR	(60ULL*USEC_PER_MINUTE)
#define NSEC_PER_HOUR	(60ULL*NSEC_PER_MINUTE)
#define USEC_PER_DAY	(24ULL*USEC_PER_HOUR)
#define NSEC_PER_DAY	(24ULL*NSEC_PER_HOUR)
#define USEC_PER_WEEK	(7ULL*USEC_PER_DAY)
#define NSEC_PER_WEEK	(7ULL*NSEC_PER_DAY)
#define USEC_PER_MONTH	(2629800ULL*USEC_PER_SEC)
#define NSEC_PER_MONTH	(2629800ULL*NSEC_PER_SEC)
#define USEC_PER_YEAR	(31557600ULL*USEC_PER_SEC)
#define NSEC_PER_YEAR	(31557600ULL*NSEC_PER_SEC)

#define FORMAT_TIMESTAMP_MAX ((4*4+1)+11+9+4+1)	/* weekdays can be unicode */
#define FORMAT_TIMESTAMP_RELATIVE_MAX 256
#define FORMAT_TIMESPAN_MAX 64

int parse_timestamp(const char *t, usec_t *usec);

/* flags for strxxx_iso() functions */
enum {
	ISO_8601_DATE		= (1 << 1),
	ISO_8601_TIME		= (1 << 2),
	ISO_8601_DOTUSEC	= (1 << 3),
	ISO_8601_COMMAUSEC	= (1 << 4),
	ISO_8601_TIMEZONE	= (1 << 5),
	ISO_8601_SPACE		= (1 << 6),
	ISO_8601_GMTIME		= (1 << 7)
};

#define ISO_8601_BUFSIZ	32

int strtimeval_iso(struct timeval *tv, int flags, char *buf, size_t bufsz);
int strtm_iso(struct tm *tm, int flags, char *buf, size_t bufsz);
int strtime_iso(const time_t *t, int flags, char *buf, size_t bufsz);

#define UL_SHORTTIME_THISYEAR_HHMM (1 << 1)

int time_is_today(const time_t *t, struct timeval *now);
int time_is_thisyear(const time_t *t, struct timeval *now);

int strtime_short(const time_t *t, struct timeval *now, int flags, char *buf, size_t bufsz);

#endif /* UTIL_LINUX_TIME_UTIL_H */
