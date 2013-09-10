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
  along with systemd; If not, see <http://www.gnu.org/licenses/>.
***/
#ifndef UTIL_LINUX_TIME_UTIL_H
#define UTIL_LINUX_TIME_UTIL_H

#include <stdio.h>
#include <inttypes.h>

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

#endif /* UTIL_LINUX_TIME_UTIL_H */
