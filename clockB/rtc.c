/**************************************************************************

  This is a component of the hwclock program.

  This file contains the code for accessing the hardware clock via
  the rtc device driver (usually hooked up to the /dev/rtc device
  special file).

****************************************************************************/

#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include "hwclock.h"

/*
  Get defines for rtc stuff.
 
  Getting the rtc defines is nontrivial.  The obvious way is by
  including <linux/mc146818rtc.h> but that again includes <asm/io.h>
  which again includes ...  and on sparc and alpha this gives
  compilation errors for many kernel versions. So, we give the defines
  ourselves here.  Moreover, some Sparc person decided to be
  incompatible, and used a struct rtc_time different from that used in
  mc146818rtc.h.  
*/

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


#include <linux/version.h>
/* Check if the /dev/rtc interface is available in this version of
   the system headers.  131072 is linux 2.0.0.  Might need to make
   it conditional on i386 or something too -janl */
#if LINUX_VERSION_CODE >= 131072
#include <linux/kd.h>
static const bool got_rtc = TRUE;
#else
static const bool got_rtc = FALSE;
#endif

/* struct linux_rtc_time is present since Linux 1.3.99 */
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

/* RTC_RD_TIME etc have this definition since Linux 1.99.9 (pre2.0-9) */
#ifndef RTC_RD_TIME
#define RTC_RD_TIME       _IOR('p', 0x09, struct linux_rtc_time)
#define RTC_SET_TIME      _IOW('p', 0x0a, struct linux_rtc_time)
#define RTC_UIE_ON        _IO('p', 0x03)	/* Update int. enable on */
#define RTC_UIE_OFF       _IO('p', 0x04)	/* Update int. enable off */
#endif
/* RTC_EPOCH_READ and RTC_EPOCH_SET ioctls are in kernels since 
   Linux 2.0.34 and 2.1.89 
   */
#ifndef RTC_EPOCH_READ
#define RTC_EPOCH_READ	_IOR('p', 0x0d, unsigned long)	 /* Read epoch */
#define RTC_EPOCH_SET	_IOW('p', 0x0e, unsigned long)	 /* Set epoch */
#endif



static void
do_rtc_read_ioctl(int rtc_fd, struct tm *tm, int *retcode_p) {
/*----------------------------------------------------------------------------
   Do the ioctl to read the time.  This is, unfortunately, a slightly
   different function for Sparc than for everything else.  But we
   return the standard 'tm' structure result in spite of the fact that
   the Sparc ioctl returns something else.
   
   If the ioctl fails, issue message to stderr and return rc = 1;
   else, no message and rc = 0.
-----------------------------------------------------------------------------*/
  int rc;

#ifdef __sparc__
  struct sparc_rtc_time stm;

  rc = ioctl(rtc_fd, RTCGET, &stm);
  tm->tm_sec = stm.sec;
  tm->tm_min = stm.min;
  tm->tm_hour = stm.hour;
  tm->tm_mday = stm.dom;
  tm->tm_mon = stm.month - 1;
  tm->tm_year = stm.year - 1900;
  tm->tm_wday = stm.dow - 1;
  tm->tm_yday = -1;		/* day in the year */
#else
  rc = ioctl(rtc_fd, RTC_RD_TIME, tm);
#endif
  if (rc == -1) {
    fprintf(stderr, "%s: ioctl() to /dev/rtc to read the time failed.  "
            "errno = %s (%d)\n", MYNAME, strerror(errno), errno);
    *retcode_p = 1;
  } else *retcode_p = 0;
  tm->tm_isdst = -1;          /* don't know whether it's daylight */
  return;
}  



void
get_epoch(unsigned long *epoch_p, char **reason_p){
/*----------------------------------------------------------------------------
  Get the Hardware Clock epoch setting from the kernel.
  
  If we succeed, return the setting (number of year A.D.) as 
  *epoch_p and *reason_p == NULL.

  If we fail, return and English description of what went wrong as a
  null-terminated string in newly malloc'ed storage and the pointer to
  it as *reason_p.
----------------------------------------------------------------------------*/
  int rtc_fd;

  rtc_fd = open("/dev/rtc", O_RDONLY);
  if (rtc_fd < 0) {
    if (errno == ENOENT) 
      *reason_p = 
        strdup("To manipulate the epoch value in the kernel, we must "
               "access the Linux 'rtc' device driver via the device special "
               "file /dev/rtc.  This file does not exist on this system.\n");
    else {
      *reason_p = malloc(200);
      sprintf(*reason_p, "Unable to open /dev/rtc, open() errno = %s (%d)\n",
              strerror(errno), errno);
    }
  } else {
    int rc;  /* return code from ioctl */
    rc = ioctl(rtc_fd, RTC_EPOCH_READ, epoch_p);
    if (rc == -1) {
      *reason_p = malloc(200);
      sprintf(*reason_p, "ioctl(RTC_EPOCH_READ) to /dev/rtc failed, "
              "errno = %s (%d).\n", strerror(errno), errno);
    } else {
      *reason_p = NULL;
      if (debug) printf("we have read epoch %ld from /dev/rtc "
                        "with RTC_EPOCH_READ ioctl.\n", *epoch_p);
    }
    close(rtc_fd);
  }
  return;
}



static void
busywait_for_rtc_clock_tick(const int rtc_fd, int *retcode_p) {
/*----------------------------------------------------------------------------
   Wait for the top of a clock tick by reading /dev/rtc in a busy loop until
   we see it.  
-----------------------------------------------------------------------------*/
  struct tm start_time;
    /* The time when we were called (and started waiting) */
  int rc;

  if (debug)
    printf("Waiting in loop for time from /dev/rtc to change\n");

  do_rtc_read_ioctl(rtc_fd, &start_time, &rc);
  if (rc != 0) *retcode_p = 1;
  else {
    /* Wait for change.  Should be within a second, but in case something
       weird happens, we have a limit on this loop to reduce the impact
       of this failure.
       */
    struct tm nowtime;
    int iterations;  /* how many time's we've spun through the loop */
    int rc;  /* Return code from do_rtc_read_ioctl */

    iterations = 0;
    do {
      do_rtc_read_ioctl(rtc_fd, &nowtime, &rc);
    } while (rc == 0 && start_time.tm_sec == nowtime.tm_sec 
             && iterations++ < 1000000);

    if (iterations >= 1000000) {
      fprintf(stderr, "%s: Timed out waiting for time change.\n", MYNAME);
      *retcode_p = 2;
    } else if (rc != 0) *retcode_p = 3;
    else *retcode_p = 0;
  }
}



void
synchronize_to_clock_tick_RTC(int *retcode_p) {
/*----------------------------------------------------------------------------
  Same as synchronize_to_clock_tick(), but just for /dev/rtc.
-----------------------------------------------------------------------------*/
  int rtc_fd;  /* File descriptor of /dev/rtc */

  rtc_fd = open("/dev/rtc", O_RDONLY);

  if (rtc_fd == -1) {
    fprintf(stderr, "%s: open() of /dev/rtc failed, errno = %s (%d).\n",
            MYNAME, strerror(errno), errno);
    *retcode_p = 1;
  } else {
    int rc;  /* Return code from ioctl */
    /* Turn on update interrupts (one per second) */
    rc = ioctl(rtc_fd, RTC_UIE_ON, 0);
    if (rc == -1 && errno == EINVAL) {
      /* This rtc device doesn't have interrupt functions.  This is typical
         on an Alpha, where the Hardware Clock interrupts are used by the
         kernel for the system clock, so aren't at the user's disposal.
         */
      if (debug) printf("/dev/rtc does not have interrupt functions. \n");
      busywait_for_rtc_clock_tick(rtc_fd, retcode_p);
    } else if (rc != -1) {
      int rc;  /* return code from ioctl */
      unsigned long dummy;

      /* this blocks until the next update interrupt */
      rc = read(rtc_fd, &dummy, sizeof(dummy));
      if (rc == -1) {
        fprintf(stderr, "%s: read() to /dev/rtc to wait for clock tick "
                "failed, errno = %s (%d).\n", MYNAME, strerror(errno), errno);
        *retcode_p = 1;
      } else {
        *retcode_p = 0;
      }
      /* Turn off update interrupts */
      rc = ioctl(rtc_fd, RTC_UIE_OFF, 0);
      if (rc == -1) {
        fprintf(stderr, 
                "%s: ioctl() to /dev/rtc to turn off update interrupts "
                "failed, errno = %s (%d).\n", MYNAME, strerror(errno), errno);
      }
    } else {
      fprintf(stderr, "%s: ioctl() to /dev/rtc to turn on update interrupts "
              "failed unexpectedly, errno = %s (%d).\n", 
              MYNAME, strerror(errno), errno);
      *retcode_p = 1;
    } 
    close(rtc_fd);
  }
}



void
read_hardware_clock_rtc_ioctl(struct tm *tm) {
/*----------------------------------------------------------------------------
  Read the hardware clock and return the current time via <tm>
  argument.  Use ioctls to "rtc" device /dev/rtc.
-----------------------------------------------------------------------------*/
  int rc;   /* Local return code */
  int rtc_fd;  /* File descriptor of /dev/rtc */
  
  rtc_fd = open("/dev/rtc",O_RDONLY);
if (rtc_fd == -1) {
fprintf(stderr, "%s: open() of /dev/rtc failed, errno = %s (%d).\n",
            MYNAME, strerror(errno), errno);
exit(5);
  } else {
    /* Read the RTC time/date */

    rc = ioctl(rtc_fd, RTC_RD_TIME, tm);
    if (rc == -1) {
      fprintf(stderr, "%s: ioctl() to /dev/rtc to read the time failed, "
              "errno = %s (%d).\n", MYNAME, strerror(errno), errno);
      exit(5);
    }
    close(rtc_fd);
  }
  tm->tm_isdst = -1;          /* don't know whether it's daylight */
}



void
set_hardware_clock_rtc_ioctl(const struct tm new_broken_time, 
                             const bool testing) {
/*----------------------------------------------------------------------------
  Set the Hardware Clock to the broken down time <new_broken_time>.
  Use ioctls to "rtc" device /dev/rtc.
----------------------------------------------------------------------------*/
  int rc;
  int rtc_fd;

  rtc_fd = open("/dev/rtc", O_RDONLY);
  if (rtc_fd < 0) {
    fprintf(stderr, "%s: Unable to open /dev/rtc, open() errno = %s (%d)\n",
            MYNAME, strerror(errno), errno);
    exit(5);
  } else {
    if (testing) 
      printf("Not setting Hardware Clock because running in test mode.\n");
    else {
      rc = ioctl(rtc_fd, RTC_SET_TIME, &new_broken_time);
      if (rc == -1) {
        fprintf(stderr, 
                "%s: ioctl() (RTC_SET_TIME) to /dev/rtc to set time failed, "
                "errno = %s (%d).\n", MYNAME, strerror(errno), errno);
        exit(5);
      } else {
        if (debug)
          printf("ioctl(RTC_SET_TIME) was successful.\n");
      }
    }
    close(rtc_fd);
  }
}



void
set_epoch(unsigned long epoch, const bool testing, int *retcode_p) {
/*----------------------------------------------------------------------------
  Set the Hardware Clock epoch in the kernel.
----------------------------------------------------------------------------*/
  if (epoch < 1900)
    /* kernel would not accept this epoch value */
    fprintf(stderr, "%s: The epoch value may not be less than 1900.  "
            "You requested %ld\n", MYNAME, epoch);
  else {
    int rtc_fd;
    
    rtc_fd = open("/dev/rtc", O_RDONLY);
    if (rtc_fd < 0) {
      if (errno == ENOENT) 
        fprintf(stderr, 
                "%s: To manipulate the epoch value in the kernel, we must "
                "access the Linux 'rtc' device driver via the device special "
                "file /dev/rtc.  This file does not exist on this system.\n",
                MYNAME);
      fprintf(stderr, "%s: Unable to open /dev/rtc, open() errno = %s (%d)\n",
              MYNAME, strerror(errno), errno);
      *retcode_p = 1;
    } else {
      if (debug) printf("setting epoch to %ld "
                        "with RTC_EPOCH_SET ioctl to /dev/rtc.\n", epoch);
      if (testing) {
        printf("Not setting epoch because running in test mode.\n");
        *retcode_p = 0;
      } else {
        int rc;                 /* return code from ioctl */
        rc = ioctl(rtc_fd, RTC_EPOCH_SET, epoch);
        if (rc == -1) {
          if (errno == EINVAL)
            fprintf(stderr, "%s: The kernel (specifically, the device driver "
                    "for /dev/rtc) does not have the RTC_EPOCH_SET ioctl.  "
                    "Get a newer driver.\n", MYNAME);
          else 
            fprintf(stderr, "%s: ioctl(RTC_EPOCH_SET) to /dev/rtc failed, "
                    "errno = %s (%d).\n", MYNAME, strerror(errno), errno);
          *retcode_p = 1;
        } else *retcode_p = 0;
      }
      close(rtc_fd);
    }
  }
}



void
see_if_rtc_works(bool * const rtc_works_p) {
/*----------------------------------------------------------------------------
   Find out if we are capable of accessing the Hardware Clock via the rtc
   driver (via device file /dev/rtc).
-----------------------------------------------------------------------------*/
  if (got_rtc) {
    int rtc_fd = open("/dev/rtc", O_RDONLY);
    if (rtc_fd > 0) {
      *rtc_works_p = TRUE;
      close(rtc_fd);
    } else {
      *rtc_works_p = FALSE;
      if (debug)
        printf("Open of /dev/rtc failed, errno = %s (%d).  "
               "falling back to more primitive clock access method.\n",
               strerror(errno), errno);
    }
  } else {
    if (debug)
      printf("The Linux kernel for which this copy of hwclock() was built "
             "is too old to have /dev/rtc\n");
    *rtc_works_p = FALSE;
  }
}




