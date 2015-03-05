#ifndef UTIL_LINUX_BOOTTIME_H
#define UTIL_LINUX_BOOTTIME_H

/*
 * Uses clock_gettime() that requires $CLOCKGETTIME_LIBS
 */
extern int get_boot_time(struct timeval *boot_time);

extern int gettime_monotonic(struct timeval *tv);

extern int setup_timer(timer_t * t_id, struct itimerval *timeout,
		       void (*timeout_handler)(int, siginfo_t *, void *));
extern void cancel_timer(timer_t * t_id);

#endif /* UTIL_LINUX_BOOTTIME_H */
