/*
 * Please, don't add this file to libcommon because timers requires
 * -lrt on systems with old libc (and probably also -lpthread for static
 *  build).
 */
#include <time.h>
#include <signal.h>
#include <sys/time.h>

#include "c.h"
#include "timer.h"

/*
 * Note the timeout is used for the first signal, then the signal is send
 * repeatedly in interval ~1% of the original timeout to avoid race in signal
 * handling -- for example you want to use timer to define timeout for a
 * syscall:
 *
 *	 setup_timer()
 *	 syscall()
 *	 cancel_timer()
 *
 * if the timeout is too short than it's possible that the signal is delivered
 * before application enter the syscall function. For this reason timer send
 * the signal repeatedly.
 *
 * The applications need to ensure that they can tolerate multiple signal
 * deliveries.
 */
int setup_timer(timer_t * t_id, struct itimerval *timeout,
		void (*timeout_handler)(int, siginfo_t *, void *))
{
	time_t sec = timeout->it_value.tv_sec;
	long usec = timeout->it_value.tv_usec;
	struct sigaction sig_a;
	static struct sigevent sig_e = {
		.sigev_notify = SIGEV_SIGNAL,
		.sigev_signo = SIGALRM
	};
	struct itimerspec val = {
		.it_value.tv_sec = sec,
		.it_value.tv_nsec = usec * 1000,
		.it_interval.tv_sec = sec / 100,
		.it_interval.tv_nsec = (sec ? sec % 100 : 1) * 10*1000*1000
	};

	if (sigemptyset(&sig_a.sa_mask))
		return 1;

	sig_a.sa_flags = SA_SIGINFO;
	sig_a.sa_sigaction = timeout_handler;

	if (sigaction(SIGALRM, &sig_a, NULL))
		return 1;
	if (timer_create(CLOCK_MONOTONIC, &sig_e, t_id))
		return 1;
	if (timer_settime(*t_id, 0, &val, NULL))
		return 1;
	return 0;
}

void cancel_timer(timer_t *t_id)
{
	timer_delete(*t_id);
}
