#ifndef HWCLOCK_CLOCK_H
#define HWCLOCK_CLOCK_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "c.h"
#include "debug.h"
#include "nls.h"

#define HWCLOCK_DEBUG_INIT		(1 << 0)
#define HWCLOCK_DEBUG_RANDOM_SLEEP	(1 << 1)
#define HWCLOCK_DEBUG_DELTA_VS_TARGET	(1 << 2)
#define HWCLOCK_DEBUG_ALL		0xFFFF

UL_DEBUG_DECLARE_MASK(hwclock);
#define DBG(m, x)	__UL_DBG(hwclock, HWCLOCK_DEBUG_, m, x)
#define ON_DBG(m, x)	__UL_DBG_CALL(hwclock, HWCLOCK_DEBUG_, m, x)

struct hwclock_control {
	char *date_opt;
	char *adj_file_name;
	double rtc_delay;	/* --delay <seconds> */
#if defined(__linux__) && defined(__alpha__)
	char *epoch_option;
#endif
#ifdef __linux__
	char *rtc_dev_name;
#endif
	char *param_get_option;
	char *param_set_option;
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
		universal:1,	/* will store hw_clock_is_utc() return value */
		verbose:1;
};

struct clock_ops {
	char *interface_name;
	int (*get_permissions) (void);
	int (*read_hardware_clock) (const struct hwclock_control *ctl, struct tm * tm);
	int (*set_hardware_clock) (const struct hwclock_control *ctl, const struct tm * tm);
	int (*synchronize_to_clock_tick) (const struct hwclock_control *ctl);
	const char *(*get_device_path) (void);
};

extern struct clock_ops *probe_for_cmos_clock(void);
extern struct clock_ops *probe_for_rtc_clock(const struct hwclock_control *ctl);

/* hwclock.c */
extern double time_diff(struct timeval subtrahend, struct timeval subtractor);

/* rtc.c */
#if defined(__linux__) && defined(__alpha__)
extern int get_epoch_rtc(const struct hwclock_control *ctl, unsigned long *epoch);
extern int set_epoch_rtc(const struct hwclock_control *ctl);
#endif

struct rtc_param {
	uint64_t param;
	union {
		uint64_t uvalue;
		int64_t svalue;
		uint64_t ptr;
	};
	uint32_t index;
	uint32_t __pad;
};

#define RTC_PARAM_GET	_IOW('p', 0x13, struct rtc_param)
#define RTC_PARAM_SET	_IOW('p', 0x14, struct rtc_param)

#define RTC_PARAM_FEATURES		0
#define RTC_PARAM_CORRECTION		1
#define RTC_PARAM_BACKUP_SWITCH_MODE	2

struct hwclock_param {
	int id;
	const char *name;
	const char *help;
};

static struct hwclock_param hwclock_params[] = {
	{ RTC_PARAM_FEATURES,  "features", N_("supported features") },
	{ RTC_PARAM_CORRECTION, "correction", N_("time correction") },
	{ RTC_PARAM_BACKUP_SWITCH_MODE, "bsm", N_("backup switch mode") },
	{ }
};

extern int get_param_rtc(const struct hwclock_control *ctl, struct rtc_param *param);
extern int set_param_rtc(const struct hwclock_control *ctl);

extern void __attribute__((__noreturn__))
hwclock_exit(const struct hwclock_control *ctl, int status);

extern int parse_date(struct timespec *, char const *, struct timespec const *);

#endif				/* HWCLOCK_CLOCK_H */
