/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * rtc.c - Use /dev/rtc for clock access
 */
#include <asm/ioctl.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "monotonic.h"
#include "nls.h"

#include "hwclock.h"

/*
 * Get defines for rtc stuff.
 *
 * Getting the rtc defines is nontrivial. The obvious way is by including
 * <linux/mc146818rtc.h> but that again includes <asm/io.h> which again
 * includes ... and on sparc and alpha this gives compilation errors for
 * many kernel versions. So, we give the defines ourselves here. Moreover,
 * some Sparc person decided to be incompatible, and used a struct rtc_time
 * different from that used in mc146818rtc.h.
 */

/*
 * On Sparcs, there is a <asm/rtc.h> that defines different ioctls (that are
 * required on my machine). However, this include file does not exist on
 * other architectures.
 */
/* One might do:
#ifdef __sparc__
# include <asm/rtc.h>
#endif
 */
#ifdef __sparc__
/* The following is roughly equivalent */
struct sparc_rtc_time
{
	int sec;	/* Seconds		0-59 */
	int min;	/* Minutes		0-59 */
	int hour;	/* Hour			0-23 */
	int dow;	/* Day of the week	1-7  */
	int dom;	/* Day of the month	1-31 */
	int month;	/* Month of year	1-12 */
	int year;	/* Year			0-99 */
};
#define RTCGET _IOR('p', 20, struct sparc_rtc_time)
#define RTCSET _IOW('p', 21, struct sparc_rtc_time)
#endif

/*
 * struct rtc_time is present since 1.3.99.
 * Earlier (since 1.3.89), a struct tm was used.
 */
struct linux_rtc_time {
	int tm_sec;
	int tm_min;
	int tm_hour;
	int tm_mday;
	int tm_mon;
	int tm_year;
	int tm_wday;
	int tm_yday;
	int tm_isdst;
};

/* RTC_RD_TIME etc have this definition since 1.99.9 (pre2.0-9) */
#ifndef RTC_RD_TIME
# define RTC_RD_TIME	_IOR('p', 0x09, struct linux_rtc_time)
# define RTC_SET_TIME	_IOW('p', 0x0a, struct linux_rtc_time)
# define RTC_UIE_ON	_IO('p', 0x03)	/* Update int. enable on */
# define RTC_UIE_OFF	_IO('p', 0x04)	/* Update int. enable off */
#endif

/* RTC_EPOCH_READ and RTC_EPOCH_SET are present since 2.0.34 and 2.1.89 */
#ifndef RTC_EPOCH_READ
# define RTC_EPOCH_READ	_IOR('p', 0x0d, unsigned long)	/* Read epoch */
# define RTC_EPOCH_SET	_IOW('p', 0x0e, unsigned long)	/* Set epoch */
#endif

/*
 * /dev/rtc is conventionally chardev 10/135
 * ia64 uses /dev/efirtc, chardev 10/136
 * devfs (obsolete) used /dev/misc/... for miscdev
 * new RTC framework + udev uses dynamic major and /dev/rtc0.../dev/rtcN
 * ... so we need an overridable default
 */

/* default or user defined dev (by hwclock --rtc=<path>) */
static const char *rtc_dev_name;
static int rtc_dev_fd = -1;

static void close_rtc(void)
{
	if (rtc_dev_fd != -1)
		close(rtc_dev_fd);
	rtc_dev_fd = -1;
}

static int open_rtc(const struct hwclock_control *ctl)
{
	static const char *fls[] = {
#ifdef __ia64__
		"/dev/efirtc",
		"/dev/misc/efirtc",
#endif
		"/dev/rtc0",
		"/dev/rtc",
		"/dev/misc/rtc"
	};
	size_t i;

	if (rtc_dev_fd != -1)
		return rtc_dev_fd;

	/* --rtc option has been given */
	if (ctl->rtc_dev_name) {
		rtc_dev_name = ctl->rtc_dev_name;
		rtc_dev_fd = open(rtc_dev_name, O_RDONLY);
	} else {
		for (i = 0; i < ARRAY_SIZE(fls); i++) {
			if (ctl->verbose)
				printf(_("Trying to open: %s\n"), fls[i]);
			rtc_dev_fd = open(fls[i], O_RDONLY);

			if (rtc_dev_fd < 0) {
				if (errno == ENOENT || errno == ENODEV)
					continue;
				if (ctl->verbose)
					warn(_("cannot open %s"), fls[i]);
			}
			rtc_dev_name = fls[i];
			break;
		}
		if (rtc_dev_fd < 0)
			rtc_dev_name = *fls;	/* default for error messages */
	}
	if (rtc_dev_fd != -1)
		atexit(close_rtc);
	return rtc_dev_fd;
}

static int open_rtc_or_exit(const struct hwclock_control *ctl)
{
	int rtc_fd = open_rtc(ctl);

	if (rtc_fd < 0) {
		warn(_("cannot open rtc device"));
		hwclock_exit(ctl, EXIT_FAILURE);
	}
	return rtc_fd;
}

static int do_rtc_read_ioctl(int rtc_fd, struct tm *tm)
{
	int rc = -1;
	char *ioctlname;
#ifdef __sparc__
	/* some but not all sparcs use a different ioctl and struct */
	struct sparc_rtc_time stm;
#endif

	ioctlname = "RTC_RD_TIME";
	rc = ioctl(rtc_fd, RTC_RD_TIME, tm);

#ifdef __sparc__
	if (rc == -1) {		/* sparc sbus */
		ioctlname = "RTCGET";
		rc = ioctl(rtc_fd, RTCGET, &stm);
		if (rc == 0) {
			tm->tm_sec = stm.sec;
			tm->tm_min = stm.min;
			tm->tm_hour = stm.hour;
			tm->tm_mday = stm.dom;
			tm->tm_mon = stm.month - 1;
			tm->tm_year = stm.year - 1900;
			tm->tm_wday = stm.dow - 1;
			tm->tm_yday = -1;	/* day in the year */
		}
	}
#endif

	if (rc == -1) {
		warn(_("ioctl(%s) to %s to read the time failed"),
			ioctlname, rtc_dev_name);
		return -1;
	}

	tm->tm_isdst = -1;	/* don't know whether it's dst */
	return 0;
}

/*
 * Wait for the top of a clock tick by reading /dev/rtc in a busy loop
 * until we see it. This function is used for rtc drivers without ioctl
 * interrupts. This is typical on an Alpha, where the Hardware Clock
 * interrupts are used by the kernel for the system clock, so aren't at
 * the user's disposal.
 */
static int busywait_for_rtc_clock_tick(const struct hwclock_control *ctl,
				       const int rtc_fd)
{
	struct tm start_time;
	/* The time when we were called (and started waiting) */
	struct tm nowtime;
	int rc;
	struct timeval begin, now;

	if (ctl->verbose) {
		printf("ioctl(%d, RTC_UIE_ON, 0): %s\n",
		       rtc_fd, strerror(errno));
		printf(_("Waiting in loop for time from %s to change\n"),
		       rtc_dev_name);
	}

	if (do_rtc_read_ioctl(rtc_fd, &start_time))
		return 1;

	/*
	 * Wait for change.  Should be within a second, but in case
	 * something weird happens, we have a time limit (1.5s) on this loop
	 * to reduce the impact of this failure.
	 */
	gettime_monotonic(&begin);
	do {
		rc = do_rtc_read_ioctl(rtc_fd, &nowtime);
		if (rc || start_time.tm_sec != nowtime.tm_sec)
			break;
		gettime_monotonic(&now);
		if (time_diff(now, begin) > 1.5) {
			warnx(_("Timed out waiting for time change."));
			return 1;
		}
	} while (1);

	if (rc)
		return 1;
	return 0;
}

/*
 * Same as synchronize_to_clock_tick(), but just for /dev/rtc.
 */
static int synchronize_to_clock_tick_rtc(const struct hwclock_control *ctl)
{
	int rtc_fd;		/* File descriptor of /dev/rtc */
	int ret = 1;

	rtc_fd = open_rtc(ctl);
	if (rtc_fd == -1) {
		warn(_("cannot open rtc device"));
		return ret;
	}

	/* Turn on update interrupts (one per second) */
	int rc = ioctl(rtc_fd, RTC_UIE_ON, 0);

	if (rc != -1) {
		/*
		 * Just reading rtc_fd fails on broken hardware: no
		 * update interrupt comes and a bootscript with a
		 * hwclock call hangs
		 */
		fd_set rfds;
		struct timeval tv;

		/*
		 * Wait up to ten seconds for the next update
		 * interrupt
		 */
		FD_ZERO(&rfds);
		FD_SET(rtc_fd, &rfds);
		tv.tv_sec = 10;
		tv.tv_usec = 0;
		rc = select(rtc_fd + 1, &rfds, NULL, NULL, &tv);
		if (0 < rc)
			ret = 0;
		else if (rc == 0) {
			warnx(_("select() to %s to wait for clock tick timed out"),
			      rtc_dev_name);
		} else
			warn(_("select() to %s to wait for clock tick failed"),
			     rtc_dev_name);
		/* Turn off update interrupts */
		rc = ioctl(rtc_fd, RTC_UIE_OFF, 0);
		if (rc == -1)
			warn(_("ioctl() to %s to turn off update interrupts failed"),
			     rtc_dev_name);
		} else if (errno == ENOTTY || errno == EINVAL) {
			/* rtc ioctl interrupts are unimplemented */
			ret = busywait_for_rtc_clock_tick(ctl, rtc_fd);
		} else
			warn(_("ioctl(%d, RTC_UIE_ON, 0) to %s failed"),
			     rtc_fd, rtc_dev_name);
	return ret;
}

static int read_hardware_clock_rtc(const struct hwclock_control *ctl,
				   struct tm *tm)
{
	int rtc_fd, rc;

	rtc_fd = open_rtc_or_exit(ctl);

	/* Read the RTC time/date, return answer via tm */
	rc = do_rtc_read_ioctl(rtc_fd, tm);

	return rc;
}

/*
 * Set the Hardware Clock to the broken down time <new_broken_time>. Use
 * ioctls to "rtc" device /dev/rtc.
 */
static int set_hardware_clock_rtc(const struct hwclock_control *ctl,
				  const struct tm *new_broken_time)
{
	int rc = -1;
	int rtc_fd;
	char *ioctlname;

	rtc_fd = open_rtc_or_exit(ctl);

	ioctlname = "RTC_SET_TIME";
	rc = ioctl(rtc_fd, RTC_SET_TIME, new_broken_time);

#ifdef __sparc__
	if (rc == -1) {		/* sparc sbus */
		struct sparc_rtc_time stm;

		stm.sec = new_broken_time->tm_sec;
		stm.min = new_broken_time->tm_min;
		stm.hour = new_broken_time->tm_hour;
		stm.dom = new_broken_time->tm_mday;
		stm.month = new_broken_time->tm_mon + 1;
		stm.year = new_broken_time->tm_year + 1900;
		stm.dow = new_broken_time->tm_wday + 1;

		ioctlname = "RTCSET";
		rc = ioctl(rtc_fd, RTCSET, &stm);
	}
#endif

	if (rc == -1) {
		warn(_("ioctl(%s) to %s to set the time failed"),
			ioctlname, rtc_dev_name);
		hwclock_exit(ctl, EXIT_FAILURE);
	}

	if (ctl->verbose)
		printf(_("ioctl(%s) was successful.\n"), ioctlname);

	return 0;
}

static int get_permissions_rtc(void)
{
	return 0;
}

static const char *get_device_path(void)
{
	return rtc_dev_name;
}

static struct clock_ops rtc_interface = {
	N_("Using the rtc interface to the clock."),
	get_permissions_rtc,
	read_hardware_clock_rtc,
	set_hardware_clock_rtc,
	synchronize_to_clock_tick_rtc,
	get_device_path,
};

/* return &rtc if /dev/rtc can be opened, NULL otherwise */
struct clock_ops *probe_for_rtc_clock(const struct hwclock_control *ctl)
{
	const int rtc_fd = open_rtc(ctl);

	if (rtc_fd < 0)
		return NULL;
	return &rtc_interface;
}

#ifdef __alpha__
/*
 * Get the Hardware Clock epoch setting from the kernel.
 */
int get_epoch_rtc(const struct hwclock_control *ctl, unsigned long *epoch_p)
{
	int rtc_fd;

	rtc_fd = open_rtc(ctl);
	if (rtc_fd < 0) {
		warn(_("cannot open %s"), rtc_dev_name);
		return 1;
	}

	if (ioctl(rtc_fd, RTC_EPOCH_READ, epoch_p) == -1) {
		warn(_("ioctl(%d, RTC_EPOCH_READ, epoch_p) to %s failed"),
		     rtc_fd, rtc_dev_name);
		return 1;
	}

	if (ctl->verbose)
		printf(_("ioctl(%d, RTC_EPOCH_READ, epoch_p) to %s succeeded.\n"),
		       rtc_fd, rtc_dev_name);

	return 0;
}

/*
 * Set the Hardware Clock epoch in the kernel.
 */
int set_epoch_rtc(const struct hwclock_control *ctl)
{
	int rtc_fd;
	unsigned long epoch;

	epoch = strtoul(ctl->epoch_option, NULL, 10);

	/* There were no RTC clocks before 1900. */
	if (epoch < 1900 || epoch == ULONG_MAX) {
		warnx(_("invalid epoch '%s'."), ctl->epoch_option);
		return 1;
	}

	rtc_fd = open_rtc(ctl);
	if (rtc_fd < 0) {
		warn(_("cannot open %s"), rtc_dev_name);
		return 1;
	}

	if (ioctl(rtc_fd, RTC_EPOCH_SET, epoch) == -1) {
		warn(_("ioctl(%d, RTC_EPOCH_SET, %lu) to %s failed"),
		     rtc_fd, epoch, rtc_dev_name);
		return 1;
	}

	if (ctl->verbose)
		printf(_("ioctl(%d, RTC_EPOCH_SET, %lu) to %s succeeded.\n"),
		       rtc_fd, epoch, rtc_dev_name);

	return 0;
}
#endif	/* __alpha__ */
