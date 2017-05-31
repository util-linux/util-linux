#ifndef HWCLOCK_CLOCK_H
#define HWCLOCK_CLOCK_H

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "c.h"

enum {
	RTC_BUSYWAIT_OK = 0,
	RTC_BUSYWAIT_FAILED,
	RTC_BUSYWAIT_TIMEOUT
};

struct hwclock_control {
	char *date_opt;
	char *adj_file_name;
#if defined(__linux__) && defined(__alpha__)
	unsigned long epoch_option;
#endif
#ifdef __linux__
	char *rtc_dev_name;
#endif
	unsigned int debug;
	unsigned int
		hwaudit_on:1,
		adjust:1,
		show:1,
		hctosys:1,
		utc:1,
		systohc:1,
#if defined(__linux__) && defined(__alpha__)
		getepoch:1,
		setepoch:1,
#endif
		noadjfile:1,
		local_opt:1,
		directisa:1,
		testing:1,
		systz:1,
		predict:1,
		get:1,
		set:1,
		update:1,
		universal:1;	/* will store hw_clock_is_utc() return value */
};

struct clock_ops {
	char *interface_name;
	int (*get_permissions) (void);
	int (*read_hardware_clock) (const struct hwclock_control *ctl, struct tm * tm);
	int (*set_hardware_clock) (const struct hwclock_control *ctl, const struct tm * tm);
	int (*synchronize_to_clock_tick) (const struct hwclock_control *ctl);
};

extern struct clock_ops *probe_for_cmos_clock(void);
extern struct clock_ops *probe_for_rtc_clock(const struct hwclock_control *ctl);

typedef int bool;

/* hwclock.c */
extern int debug;
extern unsigned long epoch_option;
extern double time_diff(struct timeval subtrahend, struct timeval subtractor);

/* rtc.c */
#if defined(__linux__) && defined(__alpha__)
extern int get_epoch_rtc(const struct hwclock_control *ctl, unsigned long *epoch);
extern int set_epoch_rtc(const struct hwclock_control *ctl);
#endif

extern void hwclock_exit(const struct hwclock_control *ctl, int status);

#endif				/* HWCLOCK_CLOCK_H */
