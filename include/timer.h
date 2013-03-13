#ifndef UTIL_LINUX_TIMER_H
#define UTIL_LINUX_TIMER_H

#include <signal.h>
#include <sys/time.h>

static inline int setup_timer(
			struct itimerval *timer,
			struct itimerval *old_timer,
			struct sigaction *old_sa,
			void (*timeout_handler)(int))
{
	struct sigaction sa;

	memset(&sa, 0, sizeof sa);
	sa.sa_handler = timeout_handler;
	sa.sa_flags = SA_RESETHAND;
	sigaction(SIGALRM, &sa, old_sa);

	return setitimer(ITIMER_REAL, timer, old_timer);
}

static inline void cancel_timer(
			struct itimerval *old_timer,
			struct sigaction *old_sa)
{
	setitimer(ITIMER_REAL, old_timer, NULL);
	sigaction(SIGALRM, old_sa, NULL);
}

#endif
