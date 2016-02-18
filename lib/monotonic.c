/*
 * Please, don't add this file to libcommon because clock_gettime() requires
 * -lrt on systems with old libc.
 */
#include <time.h>
#include <signal.h>
#ifdef HAVE_SYSINFO
#include <sys/sysinfo.h>
#endif
#include <sys/time.h>

#include "c.h"
#include "monotonic.h"

int get_boot_time(struct timeval *boot_time)
{
#ifdef CLOCK_BOOTTIME
	struct timespec hires_uptime;
	struct timeval lores_uptime;
#endif
	struct timeval now;
#ifdef HAVE_SYSINFO
	struct sysinfo info;
#endif

	if (gettimeofday(&now, NULL) != 0)
		return -errno;
#ifdef CLOCK_BOOTTIME
	if (clock_gettime(CLOCK_BOOTTIME, &hires_uptime) == 0) {
		TIMESPEC_TO_TIMEVAL(&lores_uptime, &hires_uptime);
		timersub(&now, &lores_uptime, boot_time);
		return 0;
	}
#endif
#ifdef HAVE_SYSINFO
	/* fallback */
	if (sysinfo(&info) != 0)
		return -errno;

	boot_time->tv_sec = now.tv_sec - info.uptime;
	boot_time->tv_usec = 0;
	return 0;
#else
	return -ENOSYS;
#endif
}

int gettime_monotonic(struct timeval *tv)
{
#ifdef CLOCK_MONOTONIC
	/* Can slew only by ntp and adjtime */
	int ret;
	struct timespec ts;

# ifdef CLOCK_MONOTONIC_RAW
	/* Linux specific, can't slew */
	if (!(ret = clock_gettime(CLOCK_MONOTONIC_RAW, &ts))) {
# else
	if (!(ret = clock_gettime(CLOCK_MONOTONIC, &ts))) {
# endif
		tv->tv_sec = ts.tv_sec;
		tv->tv_usec = ts.tv_nsec / 1000;
	}
	return ret;
#else
	return gettimeofday(tv, NULL);
#endif
}


