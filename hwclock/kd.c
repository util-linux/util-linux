/* kd.c - KDGHWCLK stuff, possibly m68k only - deprecated */

#include "clock.h"

#ifndef __m68k__

struct clock_ops *
probe_for_kd_clock() {
	return NULL;
}

#else /* __m68k__ */

#include <unistd.h>		/* for close() */
#include <fcntl.h>		/* for O_RDONLY */
#include <sysexits.h>
#include <sys/ioctl.h>

#include "nls.h"
#include "usleep.h"

static int con_fd = -1;		/* opened by probe_for_kd_clock() */
				/* never closed */
static char *con_fd_filename;	/* usually "/dev/tty1" */

/* Get defines for KDGHWCLK and KDSHWCLK (m68k) */
#include <linux/kd.h>
#ifndef KDGHWCLK
#define KDGHWCLK        0x4B50     /* get hardware clock */
#define KDSHWCLK        0x4B51     /* set hardware clock */
struct hwclk_time {
        unsigned        sec;    /* 0..59 */
        unsigned        min;    /* 0..59 */
        unsigned        hour;   /* 0..23 */
        unsigned        day;    /* 1..31 */
        unsigned        mon;    /* 0..11 */
        unsigned        year;   /* 70... */
        int             wday;   /* 0..6, 0 is Sunday, -1 means unknown/don't set */
};
#endif

static int
synchronize_to_clock_tick_kd(void) {
/*----------------------------------------------------------------------------
   Wait for the top of a clock tick by calling KDGHWCLK in a busy loop until
   we see it.
-----------------------------------------------------------------------------*/

  /* The time when we were called (and started waiting) */
  struct hwclk_time start_time, nowtime;
  struct timeval begin, now;

  if (debug)
    printf(_("Waiting in loop for time from KDGHWCLK to change\n"));

  if (ioctl(con_fd, KDGHWCLK, &start_time) == -1) {
    outsyserr(_("KDGHWCLK ioctl to read time failed"));
    return 3;
  }

  /* Wait for change.  Should be within a second, but in case something
   * weird happens, we have a time limit (1.5s) on this loop to reduce the
   * impact of this failure.
   */
  gettimeofday(&begin, NULL);
  do {
    /* Added by Roman Hodek <Roman.Hodek@informatik.uni-erlangen.de>
     * "The culprit is the fast loop with KDGHWCLK ioctls. It seems
     *  the kernel gets confused by those on Amigas with A2000 RTCs
     *  and simply hangs after some time. Inserting a sleep helps."
     */
    usleep(1);

    if (ioctl(con_fd, KDGHWCLK, &nowtime) == -1) {
      outsyserr(_("KDGHWCLK ioctl to read time failed in loop"));
      return 3;
    }
    if (start_time.tm_sec != nowtime.tm_sec)
      break;
    gettimeofday(&now, NULL);
    if (time_diff(now, begin) > 1.5) {
      fprintf(stderr, _("Timed out waiting for time change.\n"));
      return 2;
    }
  } while(1);

  return 0;
}


static int
read_hardware_clock_kd(struct tm *tm) {
/*----------------------------------------------------------------------------
  Read the hardware clock and return the current time via <tm>
  argument.  Use ioctls to /dev/tty1 on what we assume is an m68k
  machine.

  Note that we don't use /dev/console here.  That might be a serial
  console.
-----------------------------------------------------------------------------*/
  struct hwclk_time t;

  if (ioctl(con_fd, KDGHWCLK, &t) == -1) {
    outsyserr(_("ioctl() failed to read time from %s"), con_fd_filename);
    hwclock_exit(EX_IOERR);
  }

  tm->tm_sec  = t.sec;
  tm->tm_min  = t.min;
  tm->tm_hour = t.hour;
  tm->tm_mday = t.day;
  tm->tm_mon  = t.mon;
  tm->tm_year = t.year;
  tm->tm_wday = t.wday;
  tm->tm_isdst = -1;     /* Don't know if it's Daylight Savings Time */

  return 0;
}


static int
set_hardware_clock_kd(const struct tm *new_broken_time) {
/*----------------------------------------------------------------------------
  Set the Hardware Clock to the time <new_broken_time>.  Use ioctls to
  /dev/tty1 on what we assume is an m68k machine.

  Note that we don't use /dev/console here.  That might be a serial console.
----------------------------------------------------------------------------*/
  struct hwclk_time t;

  t.sec  = new_broken_time->tm_sec;
  t.min  = new_broken_time->tm_min;
  t.hour = new_broken_time->tm_hour;
  t.day  = new_broken_time->tm_mday;
  t.mon  = new_broken_time->tm_mon;
  t.year = new_broken_time->tm_year;
  t.wday = new_broken_time->tm_wday;

  if (ioctl(con_fd, KDSHWCLK, &t ) == -1) {
    outsyserr(_("ioctl KDSHWCLK failed"));
    hwclock_exit(1);
  }
  return 0;
}

static int
get_permissions_kd(void) {
  return 0;
}

static struct clock_ops kd = {
	"KDGHWCLK interface to m68k clock",
	get_permissions_kd,
	read_hardware_clock_kd,
	set_hardware_clock_kd,
	synchronize_to_clock_tick_kd,
};

/* return &kd if KDGHWCLK works, NULL otherwise */
struct clock_ops *
probe_for_kd_clock() {
	struct clock_ops *ret = NULL;
	struct hwclk_time t;

	if (con_fd < 0) {	/* first time here */
		con_fd_filename = "/dev/tty1";
		con_fd = open(con_fd_filename, O_RDONLY);
	}
	if (con_fd < 0) {
		/* perhaps they are using devfs? */
		con_fd_filename = "/dev/vc/1";
		con_fd = open(con_fd_filename, O_RDONLY);
	}
	if (con_fd < 0) {
		/* probably KDGHWCLK exists on m68k only */
		outsyserr(_("Can't open /dev/tty1 or /dev/vc/1"));
	} else {
		if (ioctl(con_fd, KDGHWCLK, &t) == -1) {
			if (errno != EINVAL)
				outsyserr(_("KDGHWCLK ioctl failed"));
		} else
			ret = &kd;
	}
	return ret;
}
#endif /* __m68k__ */
