/* rtc.c - Use /dev/rtc for clock access */
#include <unistd.h>		/* for close() */
#include <fcntl.h>		/* for O_RDONLY */
#include <sys/ioctl.h>

#include "clock.h"
#include "nls.h"

/*
 * Get defines for rtc stuff.
 *
 * Getting the rtc defines is nontrivial.
 * The obvious way is by including <linux/mc146818rtc.h>
 * but that again includes <asm/io.h> which again includes ...
 * and on sparc and alpha this gives compilation errors for
 * many kernel versions. So, we give the defines ourselves here.
 * Moreover, some Sparc person decided to be incompatible, and
 * used a struct rtc_time different from that used in mc146818rtc.h.
 */

/* ia64 uses /dev/efirtc (char 10,136) */
#if __ia64__
#define RTC_DEV "/dev/efirtc"
#else
#define RTC_DEV "/dev/rtc"
#endif

/* On Sparcs, there is a <asm/rtc.h> that defines different ioctls
   (that are required on my machine). However, this include file
   does not exist on other architectures. */
/* One might do:
#ifdef __sparc__
#include <asm/rtc.h>
#endif
 */
/* The following is roughly equivalent */
struct sparc_rtc_time
{
        int     sec;    /* Seconds (0-59) */
        int     min;    /* Minutes (0-59) */
        int     hour;   /* Hour (0-23) */
        int     dow;    /* Day of the week (1-7) */
        int     dom;    /* Day of the month (1-31) */
        int     month;  /* Month of year (1-12) */
        int     year;   /* Year (0-99) */
};

#define RTCGET _IOR('p', 20, struct sparc_rtc_time)
#define RTCSET _IOW('p', 21, struct sparc_rtc_time)


/* non-sparc stuff */
#if 0
#include <linux/version.h>
/* Check if the /dev/rtc interface is available in this version of
   the system headers.  131072 is linux 2.0.0. */
#if LINUX_VERSION_CODE >= 131072
#include <linux/mc146818rtc.h>
#endif
#endif

/* struct rtc_time is present since 1.3.99 */
/* Earlier (since 1.3.89), a struct tm was used. */
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
#define RTC_RD_TIME       _IOR('p', 0x09, struct linux_rtc_time)
#define RTC_SET_TIME      _IOW('p', 0x0a, struct linux_rtc_time)
#define RTC_UIE_ON        _IO('p', 0x03)	/* Update int. enable on */
#define RTC_UIE_OFF       _IO('p', 0x04)	/* Update int. enable off */
#endif
/* RTC_EPOCH_READ and RTC_EPOCH_SET are present since 2.0.34 and 2.1.89 */
#ifndef RTC_EPOCH_READ
#define RTC_EPOCH_READ	_IOR('p', 0x0d, unsigned long)	 /* Read epoch */
#define RTC_EPOCH_SET	_IOW('p', 0x0e, unsigned long)	 /* Set epoch */
#endif


static int
do_rtc_read_ioctl(int rtc_fd, struct tm *tm) {
  int rc;
  char *ioctlname;
#ifdef __sparc__
  struct sparc_rtc_time stm;

  ioctlname = "RTCGET";
  rc = ioctl(rtc_fd, RTCGET, &stm);
#else
  ioctlname = "RTC_RD_TIME";
  rc = ioctl(rtc_fd, RTC_RD_TIME, tm);
#endif
  if (rc == -1) {
    perror(ioctlname);
    fprintf(stderr, _("ioctl() to %s to read the time failed.\n"),RTC_DEV);
    exit(5);
  }
#ifdef __sparc__
  tm->tm_sec = stm.sec;
  tm->tm_min = stm.min;
  tm->tm_hour = stm.hour;
  tm->tm_mday = stm.dom;
  tm->tm_mon = stm.month - 1;
  tm->tm_year = stm.year - 1900;
  tm->tm_wday = stm.dow - 1;
  tm->tm_yday = -1;		/* day in the year */
#endif
  tm->tm_isdst = -1;          /* don't know whether it's daylight */
  return 0;
}  


static int
busywait_for_rtc_clock_tick(const int rtc_fd) {
/*----------------------------------------------------------------------------
   Wait for the top of a clock tick by reading /dev/rtc in a busy loop until
   we see it.  
-----------------------------------------------------------------------------*/
  struct tm start_time;
    /* The time when we were called (and started waiting) */
  struct tm nowtime;
  int i;  /* local loop index */
  int rc;

  if (debug)
    printf(_("Waiting in loop for time from %s to change\n"),RTC_DEV);

  rc = do_rtc_read_ioctl(rtc_fd, &start_time);
  if (rc)
    return 1;

  /* Wait for change.  Should be within a second, but in case something
     weird happens, we have a limit on this loop to reduce the impact
     of this failure.
     */
  for (i = 0;
       (rc = do_rtc_read_ioctl(rtc_fd, &nowtime)) == 0
        && start_time.tm_sec == nowtime.tm_sec;
       i++)
    if (i >= 1000000) {
      fprintf(stderr, _("Timed out waiting for time change.\n"));
      return 2;
    }

  if (rc)
    return 3;
  return 0;
}



static int
synchronize_to_clock_tick_rtc(void) {
/*----------------------------------------------------------------------------
  Same as synchronize_to_clock_tick(), but just for /dev/rtc.
-----------------------------------------------------------------------------*/
int rtc_fd;  /* File descriptor of /dev/rtc */
int ret;

  rtc_fd = open(RTC_DEV,O_RDONLY);
  if (rtc_fd == -1) {
    outsyserr(_("open() of %s failed"),RTC_DEV);
    ret = 1;
  } else {
    int rc;  /* Return code from ioctl */
    /* Turn on update interrupts (one per second) */
#if defined(__alpha__) || defined(__sparc__)
    /* Not all alpha kernels reject RTC_UIE_ON, but probably they should. */
    rc = -1;
    errno = EINVAL;
#else
    rc = ioctl(rtc_fd, RTC_UIE_ON, 0);
#endif
    if (rc == -1 && errno == EINVAL) {
      /* This rtc device doesn't have interrupt functions.  This is typical
         on an Alpha, where the Hardware Clock interrupts are used by the
         kernel for the system clock, so aren't at the user's disposal.
         */
      if (debug)
	      printf(_("%s does not have interrupt functions. "),RTC_DEV);
      ret = busywait_for_rtc_clock_tick(rtc_fd);
    } else if (rc == 0) {
      unsigned long dummy;

      /* this blocks until the next update interrupt */
      rc = read(rtc_fd, &dummy, sizeof(dummy));
      if (rc == -1) {
        outsyserr(_("read() to %s to wait for clock tick failed"),RTC_DEV);
        ret = 1;
      } else {
        ret = 0;
      }
      /* Turn off update interrupts */
      rc = ioctl(rtc_fd, RTC_UIE_OFF, 0);
      if (rc == -1)
        outsyserr(_("ioctl() to %s to turn off update interrupts "
		    "failed"),RTC_DEV);
    } else {
      outsyserr(_("ioctl() to %s to turn on update interrupts "
		"failed unexpectedly"),RTC_DEV);
      ret = 1;
    }
    close(rtc_fd);
  }
  return ret;
}


static int
read_hardware_clock_rtc(struct tm *tm) {
/*----------------------------------------------------------------------------
  Read the hardware clock and return the current time via <tm>
  argument.  Use ioctls to "rtc" device /dev/rtc.
-----------------------------------------------------------------------------*/
  int rtc_fd;  /* File descriptor of /dev/rtc */

  rtc_fd = open(RTC_DEV,O_RDONLY);
  if (rtc_fd == -1) {
    outsyserr(_("open() of %s failed"),RTC_DEV);
    exit(5);
  }

  /* Read the RTC time/date */
  do_rtc_read_ioctl(rtc_fd, tm);

  close(rtc_fd);
  return 0;
}


static int
set_hardware_clock_rtc(const struct tm *new_broken_time) {
/*----------------------------------------------------------------------------
  Set the Hardware Clock to the broken down time <new_broken_time>.
  Use ioctls to "rtc" device /dev/rtc.
----------------------------------------------------------------------------*/
  int rc;
  int rtc_fd;

  rtc_fd = open(RTC_DEV, O_RDONLY);
  if (rtc_fd < 0) {
    outsyserr(_("Unable to open %s"),RTC_DEV);
    exit(5);
  } else {
    char *ioctlname;
#ifdef __sparc__
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
#else
    ioctlname = "RTC_SET_TIME";
    rc = ioctl(rtc_fd, RTC_SET_TIME, new_broken_time);
#endif
    if (rc == -1) {
      perror(ioctlname);
      fprintf(stderr, _("ioctl() to %s to set the time failed.\n"),RTC_DEV);
      exit(5);
    } else {
      if (debug)
        printf(_("ioctl(%s) was successful.\n"), ioctlname);
    }
    close(rtc_fd);
  }
  return 0;
}


static int
get_permissions_rtc(void) {
	return 0;
}

static struct clock_ops rtc = {
	RTC_DEV " interface to clock",
	get_permissions_rtc,
	read_hardware_clock_rtc,
	set_hardware_clock_rtc,
	synchronize_to_clock_tick_rtc,
};

/* return &rtc if /dev/rtc can be opened, NULL otherwise */
struct clock_ops *
probe_for_rtc_clock(){
    int rtc_fd = open(RTC_DEV, O_RDONLY);
    if (rtc_fd > 0) {
      close(rtc_fd);
      return &rtc;
    }
    if (debug)
      outsyserr(_("Open of %s failed"),RTC_DEV);
    return NULL;
}



int
get_epoch_rtc(unsigned long *epoch_p, int silent) {
/*----------------------------------------------------------------------------
  Get the Hardware Clock epoch setting from the kernel.
----------------------------------------------------------------------------*/
  int rtc_fd;

  rtc_fd = open(RTC_DEV, O_RDONLY);
  if (rtc_fd < 0) {
    if (!silent) {
      if (errno == ENOENT) 
        fprintf(stderr, _(
		"To manipulate the epoch value in the kernel, we must "
                "access the Linux 'rtc' device driver via the device special "
                "file %s.  This file does not exist on this system.\n"),RTC_DEV);
      else 
        outsyserr(_("Unable to open %s"),RTC_DEV);
    }
    return 1;
  }

  if (ioctl(rtc_fd, RTC_EPOCH_READ, epoch_p) == -1) {
    if (!silent)
      outsyserr(_("ioctl(RTC_EPOCH_READ) to %s failed"),RTC_DEV);
    close(rtc_fd);
    return 1;
  }

  if (debug)
	  printf(_("we have read epoch %ld from %s "
		 "with RTC_EPOCH_READ ioctl.\n"), *epoch_p,RTC_DEV);

  close(rtc_fd);
  return 0;
}



int
set_epoch_rtc(unsigned long epoch) {
/*----------------------------------------------------------------------------
  Set the Hardware Clock epoch in the kernel.
----------------------------------------------------------------------------*/
  int rtc_fd;

  if (epoch < 1900) {
    /* kernel would not accept this epoch value */
    /* Hmm - bad habit, deciding not to do what the user asks
       just because one believes that the kernel might not like it. */
    fprintf(stderr, _("The epoch value may not be less than 1900.  "
            "You requested %ld\n"), epoch);
    return 1;
  }

  rtc_fd = open(RTC_DEV, O_RDONLY);
  if (rtc_fd < 0) {
    if (errno == ENOENT) 
      fprintf(stderr, _("To manipulate the epoch value in the kernel, we must "
              "access the Linux 'rtc' device driver via the device special "
              "file %s.  This file does not exist on this system.\n"),RTC_DEV);
    else
      outsyserr(_("Unable to open %s"),RTC_DEV);
    return 1;
  }

  if (debug)
    printf(_("setting epoch to %ld "
	   "with RTC_EPOCH_SET ioctl to %s.\n"), epoch, RTC_DEV);

  if (ioctl(rtc_fd, RTC_EPOCH_SET, epoch) == -1) {
    if (errno == EINVAL)
      fprintf(stderr, _("The kernel device driver for %s "
	      "does not have the RTC_EPOCH_SET ioctl.\n"),RTC_DEV);
    else 
      outsyserr(_("ioctl(RTC_EPOCH_SET) to %s failed"),RTC_DEV);
    close(rtc_fd);
    return 1;
  }

  close(rtc_fd);
  return 0;
}
