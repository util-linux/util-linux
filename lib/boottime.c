
#include <time.h>
#include <sys/sysinfo.h>
#include <sys/time.h>

#include "c.h"
#include "nls.h"
#include "boottime.h"

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

	if (gettimeofday(&now, NULL) != 0) {
		warn(_("gettimeofday failed"));
		return -errno;
	}
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
		warn(_("sysinfo failed"));

	boot_time->tv_sec = now.tv_sec - info.uptime;
	boot_time->tv_usec = 0;
	return 0;
#else
	return -ENOSYS;
#endif
}
