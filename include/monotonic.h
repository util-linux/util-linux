#ifndef UTIL_LINUX_BOOTTIME_H
#define UTIL_LINUX_BOOTTIME_H

/*
 * Uses clock_gettime() that requires $CLOCKGETTIME_LIBS
 */
extern int get_boot_time(struct timeval *boot_time);

extern int gettime_monotonic(struct timeval *tv);

#endif /* UTIL_LINUX_BOOTTIME_H */
