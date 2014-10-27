#ifndef UTIL_LINUX_BOOTTIME_H
#define UTIL_LINUX_BOOTTIME_H

/*
 * Uses clock_gettime() that requires $CLOCKGETTIME_LIBS
 */
extern int get_boot_time(struct timeval *boot_time);

#endif /* UTIL_LINUX_BOOTTIME_H */
