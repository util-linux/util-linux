/**************************************************************************

  This is a component of the hwclock program.

  This file contains the code for accessing the hardware clock via
  the KDHWCLK facility of M68k machines.

****************************************************************************/

#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>

#include "hwclock.h"



#if defined(KDGHWCLK)
const bool got_kdghwclk = TRUE;
static const int kdghwclk_ioctl = KDGHWCLK;
static const int kdshwclk_ioctl = KDSHWCLK;
#else
const bool got_kdghwclk = FALSE;
static const int kdghwclk_ioctl;  /* Never used; just to make compile work */
struct hwclk_time {int sec;};  
  /* Never used; just to make compile work */
#endif


void
synchronize_to_clock_tick_KD(int *retcode_p) {
/*----------------------------------------------------------------------------
   Wait for the top of a clock tick by calling KDGHWCLK in a busy loop until
   we see it.  
-----------------------------------------------------------------------------*/
  int con_fd;

  if (debug)
    printf("Waiting in loop for time from KDGHWCLK to change\n");

  con_fd = open("/dev/tty1", O_RDONLY);
  if (con_fd < 0) {
    fprintf(stderr, "%s: open() failed to open /dev/tty1, errno = %s (%d).\n",
            MYNAME, strerror(errno), errno);
    *retcode_p = 1;
  } else {
    int rc;  /* return code from ioctl() */
    int i;  /* local loop index */
    /* The time when we were called (and started waiting) */
	struct hwclk_time start_time, nowtime;

	rc = ioctl(con_fd, kdghwclk_ioctl, &start_time);
	if (rc == -1) {
      fprintf(stderr, "%s: KDGHWCLK to read time failed, "
              "errno = %s (%d).\n", MYNAME, strerror(errno), errno);
      *retcode_p = 3;
    }
	
    for (i = 0; 
         (rc = ioctl(con_fd, kdghwclk_ioctl, &nowtime)) != -1
         && start_time.sec == nowtime.sec && i < 1000000; 
         i++);
    if (i >= 1000000) {
      fprintf(stderr, "%s: Timed out waiting for time change.\n", MYNAME);
      *retcode_p = 2;
    } else if (rc == -1) {
      fprintf(stderr, "%s: KDGHWCLK to read time failed, "
              "errno = %s (%d).\n", MYNAME, strerror(errno), errno);
      *retcode_p = 3;
    } else *retcode_p = 0;
    close(con_fd);
  }
}



void
read_hardware_clock_kd(struct tm *tm) {
/*----------------------------------------------------------------------------
  Read the hardware clock and return the current time via <tm>
  argument.  Use ioctls to /dev/tty1 on what we assume is an m68k
  machine.
  
  Note that we don't use /dev/console here.  That might be a serial
  console.
-----------------------------------------------------------------------------*/
#ifdef KDGHWCLK
  int con_fd;
  struct hwclk_time t;

  con_fd = open("/dev/tty1", O_RDONLY);
  if (con_fd < 0) {
    fprintf(stderr, "%s: open() failed to open /dev/tty1, errno = %s (%d).\n",
            MYNAME, strerror(errno), errno);
    exit(5);
  } else {
    int rc;  /* return code from ioctl() */

    rc = ioctl(con_fd, kdghwclk_ioctl, &t);
    if (rc == -1) {
      fprintf(stderr, "%s: ioctl() failed to read time from  /dev/tty1, "
              "errno = %s (%d).\n",
              MYNAME, strerror(errno), errno);
      exit(5);
    }
    close(con_fd);
  }

  tm->tm_sec  = t.sec;
  tm->tm_min  = t.min;
  tm->tm_hour = t.hour;
  tm->tm_mday = t.day;
  tm->tm_mon  = t.mon;
  tm->tm_year = t.year;
  tm->tm_wday = t.wday;
  tm->tm_isdst = -1;     /* Don't know if it's Daylight Savings Time */
#else
  /* This routine should never be invoked.  It is here just to make the
     program compile.
     */
#endif
}



void
set_hardware_clock_kd(const struct tm new_broken_time, 
                      const bool testing) {
/*----------------------------------------------------------------------------
  Set the Hardware Clock to the time <new_broken_time>.  Use ioctls to
  /dev/tty1 on what we assume is an m68k machine.

  Note that we don't use /dev/console here.  That might be a serial console.
----------------------------------------------------------------------------*/
#ifdef KDGHWCLK
  int con_fd;  /* File descriptor of /dev/tty1 */
  struct hwclk_time t;

  con_fd = open("/dev/tty1", O_RDONLY);
  if (con_fd < 0) {
    fprintf(stderr, "%s: Error opening /dev/tty1.  Errno: %s (%d)\n",
            MYNAME, strerror(errno), errno);
    exit(1);
  } else {
    int rc;  /* locally used return code */

    t.sec  = new_broken_time.tm_sec;
    t.min  = new_broken_time.tm_min;
    t.hour = new_broken_time.tm_hour;
    t.day  = new_broken_time.tm_mday;
    t.mon  = new_broken_time.tm_mon;
    t.year = new_broken_time.tm_year;
    t.wday = new_broken_time.tm_wday;

    if (testing) 
      printf("Not setting Hardware Clock because running in test mode.\n");
    else {
      rc = ioctl(con_fd, kdshwclk_ioctl, &t );
      if (rc < 0) {
        fprintf(stderr, "%s: ioctl() to open /dev/tty1 failed.  "
                "Errno: %s (%d)\n",
                MYNAME, strerror(errno), errno);
        exit(1);
      }
    }
    close(con_fd);
  }
#else
  /* This function should never be invoked.  It is here just to make the
     program compile.
     */
#endif
}



void
see_if_kdghwclk_works(bool * const kdghwclk_works_p) {
/*----------------------------------------------------------------------------
   Find out if we are capable of accessing the Hardware Clock via the
   KDHWCLK facility (ioctl to /dev/tty1).
-----------------------------------------------------------------------------*/
  if (got_kdghwclk) {
    int con_fd;
    struct hwclk_time t;

    con_fd = open("/dev/tty1", O_RDONLY);
    if (con_fd >= 0) {
      if (ioctl( con_fd, kdghwclk_ioctl, &t ) >= 0) 
        *kdghwclk_works_p = TRUE;
      else {
        if (errno == EINVAL) {
          /* KDGHWCLK not implemented in this kernel... */
          *kdghwclk_works_p = FALSE;
          if (debug) 
            printf(MYNAME "was built with KDGHWCLK capability, but the "
                   "ioctl does not exist in the kernel.  The ioctl (to "
                   "/dev/tty1) failed with errno EINVAL.\n");
        } else {
          *kdghwclk_works_p = FALSE;
          fprintf(stderr,
                  "%s: KDGHWCLK ioctl failed, errno = %s (%d).\n",
                  MYNAME, strerror(errno), errno);
        }
      }
    } else {
      *kdghwclk_works_p = FALSE;
      fprintf(stderr, 
              "%s: Can't open /dev/tty1.  open() errno = %s (%d).\n",
              MYNAME, strerror(errno), errno);
    }
    close(con_fd);
  } else *kdghwclk_works_p = FALSE;
}



