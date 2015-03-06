#ifndef UTIL_LINUX_TIMER_H
#define UTIL_LINUX_TIMER_H

#include <signal.h>
#include <sys/time.h>

extern int setup_timer(timer_t * t_id, struct itimerval *timeout,
		       void (*timeout_handler)(int, siginfo_t *, void *));
extern void cancel_timer(timer_t * t_id);

#endif /* UTIL_LINUX_TIMER_H */
