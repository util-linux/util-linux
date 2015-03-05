/*
 * Please, don't add this file to libcommon because clock_gettime() requires
 * -lrt on systems with old libc.
 */
#include <time.h>
#include <signal.h>
#include <sys/sysinfo.h>
#include <sys/time.h>

#include "c.h"
#include "nls.h"
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

int gettime_monotonic(struct timeval *tv)
{
#ifdef CLOCK_MONOTONIC
	/* Can slew only by ntp and adjtime */
	int ret;
	struct timespec ts;

# ifdef CLOCK_MONOTONIC_RAW
	/* Linux specific, cant slew */
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

int setup_timer(timer_t * t_id, struct itimerval *timeout,
		void (*timeout_handler)(int, siginfo_t *, void *))
{
	struct sigaction sig_a;
	static struct sigevent sig_e = {
		.sigev_notify = SIGEV_SIGNAL,
		.sigev_signo = SIGALRM
	};
	struct itimerspec val = {
		.it_value.tv_sec = timeout->it_value.tv_sec,
		.it_value.tv_nsec = timeout->it_value.tv_usec * 1000,
		.it_interval.tv_sec = 0,
		.it_interval.tv_nsec = 0
	};

	if (sigemptyset(&sig_a.sa_mask))
		return 1;

	sig_a.sa_flags = SA_SIGINFO;
	sig_a.sa_sigaction = timeout_handler;

	if (sigaction(SIGALRM, &sig_a, 0))
		return 1;
	if (timer_create(CLOCK_MONOTONIC, &sig_e, t_id))
		return 1;
	if (timer_settime(*t_id, SA_SIGINFO, &val, NULL))
		return 1;
	return 0;
}

void cancel_timer(timer_t *t_id)
{
	timer_delete(*t_id);
}
