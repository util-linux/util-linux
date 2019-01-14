#ifndef UTIL_LINUX_MONOTONIC_H
#define UTIL_LINUX_MONOTONIC_H

# ifdef CLOCK_MONOTONIC_RAW
#  define UL_CLOCK_MONOTONIC	CLOCK_MONOTONIC_RAW
# else
#  define UL_CLOCK_MONOTONIC	CLOCK_MONOTONIC
# endif

#include <sys/time.h>

extern int get_boot_time(struct timeval *boot_time);

extern int gettime_monotonic(struct timeval *tv);

#endif /* UTIL_LINUX_MONOTONIC_H */
