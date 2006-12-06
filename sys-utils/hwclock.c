/**************************************************************************
                                hwclock
***************************************************************************

  This is a program for reading and setting the Hardware Clock on an ISA
  family computer.  This is the clock that is also known as the RTC,
  real time clock, or, unfortunately, the CMOS clock.

  See man page for details.

  By Bryan Henderson, 96.09.19.  bryanh@giraffe-data.com

  Based on work by others; see history at end of source code.

**************************************************************************/
/**************************************************************************
  Maintenance notes

  To compile this, you must use GNU compiler optimization (-O option)
  in order to make the "extern inline" functions from asm/io.h (inb(),
  etc.)  compile.  If you don't optimize, which means the compiler
  will generate no inline functions, the references to these functions
  in this program will be compiled as external references.  Since you
  probably won't be linking with any functions by these names, you will
  have unresolved external references when you link.
  
  The program is designed to run setuid superuser, since we need to be
  able to do direct I/O.  (More to the point: we need permission to 
  execute the iopl() system call).  (However, if you use one of the 
  methods other than direct ISA I/O to access the clock, no setuid is
  required).
 
  Here's some info on how we must deal with the time that elapses while
  this program runs: There are two major delays as we run:

    1) Waiting up to 1 second for a transition of the Hardware Clock so
       we are synchronized to the Hardware Clock.

    2) Running the "date" program to interpret the value of our --date
       option.

  Reading the /etc/adjtime file is the next biggest source of delay and
  uncertainty.

  The user wants to know what time it was at the moment he invoked us,
  not some arbitrary time later.  And in setting the clock, he is
  giving us the time at the moment we are invoked, so if we set the
  clock some time later, we have to add some time to that.

  So we check the system time as soon as we start up, then run "date"
  and do file I/O if necessary, then wait to synchronize with a
  Hardware Clock edge, then check the system time again to see how
  much time we spent.  We immediately read the clock then and (if 
  appropriate) report that time, and additionally, the delay we measured.

  If we're setting the clock to a time given by the user, we wait some
  more so that the total delay is an integral number of seconds, then
  set the Hardware Clock to the time the user requested plus that
  integral number of seconds.  N.B. The Hardware Clock can only be set
  in integral seconds.

  If we're setting the clock to the system clock value, we wait for
  the system clock to reach the top of a second, and then set the
  Hardware Clock to the system clock's value.

  Here's an interesting point about setting the Hardware Clock:  On my
  machine, when you set it, it sets to that precise time.  But one can
  imagine another clock whose update oscillator marches on a steady one
  second period, so updating the clock between any two oscillator ticks
  is the same as updating it right at the earlier tick.  To avoid any
  complications that might cause, we set the clock as soon as possible
  after an oscillator tick.

  
  About synchronizing to the Hardware Clock when reading the time: The
  precision of the Hardware Clock counters themselves is one second.
  You can't read the counters and find out that is 12:01:02.5.  But if
  you consider the location in time of the counter's ticks as part of
  its value, then its precision is as infinite as time is continuous!
  What I'm saying is this: To find out the _exact_ time in the
  hardware clock, we wait until the next clock tick (the next time the
  second counter changes) and measure how long we had to wait.  We
  then read the value of the clock counters and subtract the wait time
  and we know precisely what time it was when we set out to query the
  time.

  hwclock uses this method, and considers the Hardware Clock to have
  infinite precision.


  Enhancements needed:

   - When waiting for whole second boundary in set_hardware_clock_exact,
     fail if we miss the goal by more than .1 second, as could happen if
     we get pre-empted (by the kernel dispatcher).

****************************************************************************/ 

#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <sys/stat.h>
#ifdef __i386__
#include <asm/io.h>		/* for inb, outb */
#endif
#include <shhopt.h>
#include "../version.h"

#define MYNAME "hwclock"
#define VERSION "2.4"

#define FLOOR(arg) ((arg >= 0 ? (int) arg : ((int) arg) - 1));

/* Here the information for time adjustments is kept. */
#define ADJPATH "/etc/adjtime"

/* Note that we must define the boolean type as int because we use the
   shhopt option processing library which, unfortunately, returns flag
   options as integers.  It is customary to define bool as char, but 
   then we would have to do a lot of conversion in order to interface
   with shhopt.
*/
typedef int bool;
#define TRUE 1
#define FALSE 0

struct adjtime {
  /* This is information we keep in the adjtime file that tells us how
     to do drift corrections.  Elements are all straight from the
     adjtime file, so see documentation of that file for details.
     Exception is <dirty>, which is an indication that what's in this
     structure is not what's in the disk file (because it has been
     updated since read from the disk file).  
     */
  bool dirty;        
  float drift_factor;    
  time_t last_adj_time;
  float not_adjusted;
  time_t last_calib_time;
};


enum clock_access_method {ISA, RTC_IOCTL, KD, NOCLOCK};
  /* A method for accessing (reading, writing) the hardware clock:
     
     ISA: 
       via direct CPU I/O instructions that work on an ISA family
       machine (IBM PC compatible).

     RTC_IOCTL: 
       via the rtc device driver, using device special file /dev/rtc.

     KD:
       via the console driver, using device special file /dev/tty1.
       This is the m68k ioctl interface, known as KDGHWCLK.

     NO_CLOCK:
       Unable to determine a usable access method for the system clock.
   */
       
#ifdef __i386__
/* The following are just constants.  Oddly, this program will not
   compile if the inb() and outb() functions use something even
   slightly different from these variables.  This is probably at least
   partially related to the fact that __builtin_constant_p() doesn't
   work (is never true) in an inline function.  See comment to this 
   effect in asm/io.h. 
*/
static unsigned short clock_ctl_addr = 0x70;
static unsigned short clock_data_addr = 0x71;
#endif

bool debug;
  /* We are running in debug mode, wherein we put a lot of information about
     what we're doing to standard output.  Because of the pervasive and yet
     background nature of this value, this is a global variable.  */

bool interrupts_enabled;
  /* Interrupts are enabled as normal.  We, unfortunately, turn interrupts
     on the machine off in some places where we do the direct ISA accesses
     to the Hardware Clock.  It is in extremely poor form for a user space
     program to do this, but that's the price we have to pay to run on an
     ISA machine without the rtc driver in the kernel.

     Code which turns interrupts off uses this value to determine if they
     need to be turned back on.
     */

#include <linux/version.h>
/* Check if the /dev/rtc interface is available in this version of
   the system headers.  131072 is linux 2.0.0.  Might need to make
   it conditional on i386 or something too -janl */
#if LINUX_VERSION_CODE >= 131072
#include <linux/mc146818rtc.h>
#include <linux/kd.h>
static const bool got_rtc = TRUE;
#else
static const bool got_rtc = FALSE;
/* Dummy definitions to make it compile.  If any lines containing these
   macros ever execute, there is a bug in the code.
   */
#define RTC_SET_TIME -1
#define RTC_RD_TIME -1
#define RTC_UIE_ON -1
#define RTC_UIE_OFF -1
#endif

/* The RTC_EPOCH_READ and RTC_EPOCH_SET macros are supposed to be
   defined by linux/mc146818rtc.h, included above.  However, these are
   recent inventions and at the time of this writing, not in any
   official Linux.  Since these values aren't even necessary for most
   uses of hwclock, we don't want compilation to depend on the user 
   having some arcane version of this header file on his system.  Thus,
   we define the macros ourselves if the header file failed to do so.
   98.03.03.
*/
   
#ifndef RTC_EPOCH_READ
#define RTC_EPOCH_READ	_IOR('p', 0x0d, unsigned long)	 /* Read epoch      */
  /* Not all kernels have this ioctl */
#endif

#ifndef RTC_EPOCH_SET
#define RTC_EPOCH_SET	_IOW('p', 0x0e, unsigned long)	 /* Set epoch       */
  /* Not all kernels have this ioctl */
#endif


#if defined(KDGHWCLK)
static const bool got_kdghwclk = TRUE;
static const int kdghwclk_ioctl = KDGHWCLK;
static const int kdshwclk_ioctl = KDSHWCLK;
#else
static const bool got_kdghwclk = FALSE;
static const int kdghwclk_ioctl;  /* Never used; just to make compile work */
struct hwclk_time {int sec;};  
  /* Never used; just to make compile work */
#endif


/* We're going to assume that if the CPU is in the Intel x86 family, 
   this is an ISA family machine.  For all practical purposes, this is 
   the case at the time of this writing, especially after we assume a
   Linux kernel is running on it.
   */
const bool isa_machine = 
#ifdef __i386__
TRUE
#else
FALSE;
#endif
;

const bool alpha_machine = 
#ifdef __alpha__
TRUE
#else
FALSE;
#endif
;



static int
i386_iopl(const int level) {
/*----------------------------------------------------------------------------
   When compiled for an Intel target, this is just the iopl() kernel call.
   When compiled for any other target, this is a dummy function.

   We do it this way in order to keep the conditional compilation stuff
   out of the way so it doesn't mess up readability of the code.
-----------------------------------------------------------------------------*/
#ifdef __i386__
  extern int iopl(const int level);
  return iopl(level);
#else
  return -1;
#endif
}



static float 
time_diff(struct timeval subtrahend, struct timeval subtractor) {
/*---------------------------------------------------------------------------
  The difference in seconds between two times in "timeval" format.
----------------------------------------------------------------------------*/
  return( (subtrahend.tv_sec - subtractor.tv_sec)
           + (subtrahend.tv_usec - subtractor.tv_usec) / 1E6 );
}


static struct timeval
time_inc(struct timeval addend, float increment) {
/*----------------------------------------------------------------------------
  The time, in "timeval" format, which is <increment> seconds after
  the time <addend>.  Of course, <increment> may be negative.
-----------------------------------------------------------------------------*/
  struct timeval newtime;

  newtime.tv_sec = addend.tv_sec + (int) increment;
  newtime.tv_usec = addend.tv_usec + (increment - (int) increment) * 1E6;

  /* Now adjust it so that the microsecond value is between 0 and 1 million */
  if (newtime.tv_usec < 0) {
    newtime.tv_usec += 1E6;
    newtime.tv_sec -= 1;
  } else if (newtime.tv_usec >= 1E6) {
    newtime.tv_usec -= 1E6;
    newtime.tv_sec += 1;
  }
  return(newtime);
}



static inline unsigned char 
hclock_read(unsigned char reg) {
/*---------------------------------------------------------------------------
  Relative byte <reg> of the Hardware Clock value.

  On non-ISA machine, just return 0.
---------------------------------------------------------------------------*/
  register unsigned char ret;
#ifdef __i386__
  const bool interrupts_were_enabled = interrupts_enabled;

  __asm__ volatile ("cli");
  interrupts_enabled = FALSE;
  /* & 0x7f ensures that we are not disabling NMI while we read.
     Setting on Bit 7 here would disable NMI
     */
  outb(reg & 0x7f, clock_ctl_addr);
  ret = inb(clock_data_addr);
  if (interrupts_were_enabled) {
    __asm__ volatile ("sti");
    interrupts_enabled = TRUE;
  }
#else
  ret = 0;  
#endif
  return ret;
}



static inline void 
hclock_write(unsigned char reg, unsigned char val) {
/*----------------------------------------------------------------------------
  Set relative byte <reg> of the Hardware Clock value to <val>.

  On non-ISA machine, do nothing.
----------------------------------------------------------------------------*/
#ifdef __i386__
  /* & 0x7f ensures that we are not disabling NMI while we read.
     Setting on Bit 7 here would disable NMI
     */
  outb(reg & 0x7f, clock_ctl_addr);
  outb(val, clock_data_addr);
#endif
}



static inline int 
hclock_read_bcd (int addr) {
  int b;
  b = hclock_read(addr);
  return (b & 15) + (b >> 4) * 10;
}

static inline void 
hclock_write_bcd(int addr, int value) {
  hclock_write(addr, ((value / 10) << 4) + value % 10);
}


static void
read_adjtime(struct adjtime *adjtime_p, int *rc_p) {
/*----------------------------------------------------------------------------
  Read the adjustment parameters out of the /etc/adjtime file.

  Return them as the adjtime structure <*adjtime_p>.

  If there is no /etc/adjtime file, return defaults.
  If values are missing from the file, return defaults for them.
  
  return *rc_p = 0 if all OK, !=0 otherwise.

-----------------------------------------------------------------------------*/
  FILE *adjfile;
  int rc;  /* local return code */
  struct stat statbuf;  /* We don't even use the contents of this. */

  rc = stat(ADJPATH, &statbuf);
  if (rc < 0 && errno == ENOENT) {
    /* He doesn't have a adjtime file, so we'll use defaults. */
    adjtime_p->drift_factor = 0;
    adjtime_p->last_adj_time = 0;
    adjtime_p->not_adjusted = 0;
    adjtime_p->last_calib_time = 0;

    *rc_p = 0;
  } else { 
    adjfile = fopen(ADJPATH, "r");   /* open file for reading */
    if (adjfile == NULL) {
      const int fopen_errno = errno;
      fprintf(stderr, MYNAME " is unable to open file " ADJPATH ".  "
              "fopen() errno=%d:%s", fopen_errno, strerror(fopen_errno));
      *rc_p = 2;
    } else {
      char line1[81];           /* String: first line of adjtime file */
      char line2[81];           /* String: second line of adjtime file */
      
      line1[0] = '\0';          /* In case fgets fails */
      fgets(line1, sizeof(line1), adjfile);
      line2[0] = '\0';          /* In case fgets fails */
      fgets(line2, sizeof(line2), adjfile);
      
      fclose(adjfile);
      
      /* Set defaults in case values are missing from file */
      adjtime_p->drift_factor = 0;
      adjtime_p->last_adj_time = 0;
      adjtime_p->not_adjusted = 0;
      adjtime_p->last_calib_time = 0;
      
      sscanf(line1, "%f %d %f", 
             &adjtime_p->drift_factor,
             (int *) &adjtime_p->last_adj_time, 
             &adjtime_p->not_adjusted);
      
      sscanf(line2, "%d", (int *) &adjtime_p->last_calib_time);
      
      *rc_p = 0;
    }
    adjtime_p->dirty = FALSE;

    if (debug) {
      printf("Last drift adjustment done at %d seconds after 1969\n", 
             (int) adjtime_p->last_adj_time);
      printf("Last calibration done at %d seconds after 1969\n",
             (int) adjtime_p->last_calib_time);
    }
  }
}



static void
synchronize_to_clock_tick_ISA(int *retcode_p) {
/*----------------------------------------------------------------------------
  Same as synchronize_to_clock_tick(), but just for ISA.
-----------------------------------------------------------------------------*/
  int i;  /* local loop index */

  /* Wait for rise.  Should be within a second, but in case something
     weird happens, we have a limit on this loop to reduce the impact
     of this failure.
     */
  for (i = 0; !(hclock_read(10) & 0x80) && (i < 10000000); i++);
  if (i >= 10000000) *retcode_p = 1;
  else { 
    /* Wait for fall.  Should be within 2.228 ms. */
    for (i = 0; (hclock_read(10) & 0x80) && (i < 1000000); i++);
    if (i >= 10000000) *retcode_p = 1;
    else *retcode_p = 0;
  }
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

  rc = ioctl(rtc_fd, RTC_RD_TIME, &start_time);
  if (rc == -1) {
    fprintf(stderr, "ioctl() to /dev/rtc to read time failed, "
            "errno = %s (%d).\n", strerror(errno), errno);
    *retcode_p = 1;
  } else {
    /* Wait for change.  Should be within a second, but in case something
       weird happens, we have a limit on this loop to reduce the impact
       of this failure.
       */
    struct tm nowtime;
    int i;  /* local loop index */
    int rc;  /* Return code from ioctl */

    for (i = 0; 
         (rc = ioctl(rtc_fd, RTC_RD_TIME, &nowtime)) != -1
         && start_time.tm_sec == nowtime.tm_sec && i < 1000000; 
         i++);
    if (i >= 1000000) {
      fprintf(stderr, "Timed out waiting for time change.\n");
      *retcode_p = 2;
    } else if (rc == -1) {
      fprintf(stderr, "ioctl() to /dev/rtc to read time failed, "
              "errno = %s (%d).\n", strerror(errno), errno);
      *retcode_p = 3;
    } else *retcode_p = 0;
  }
}



static void
synchronize_to_clock_tick_RTC(int *retcode_p) {
/*----------------------------------------------------------------------------
  Same as synchronize_to_clock_tick(), but just for /dev/rtc.
-----------------------------------------------------------------------------*/
int rtc_fd;  /* File descriptor of /dev/rtc */

  rtc_fd = open("/dev/rtc",O_RDONLY);
  if (rtc_fd == -1) {
    fprintf(stderr, "open() of /dev/rtc failed, errno = %s (%d).\n",
            strerror(errno), errno);
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
      if (debug) printf("/dev/rtc does not have interrupt functions. ");
      busywait_for_rtc_clock_tick(rtc_fd, retcode_p);
    } else if (rc != -1) {
      int rc;  /* return code from ioctl */
      unsigned long dummy;

      /* this blocks until the next update interrupt */
      rc = read(rtc_fd, &dummy, sizeof(dummy));
      if (rc == -1) {
        fprintf(stderr, "read() to /dev/rtc to wait for clock tick failed, "
                "errno = %s (%d).\n", strerror(errno), errno);
        *retcode_p = 1;
      } else {
        *retcode_p = 0;
      }
      /* Turn off update interrupts */
      rc = ioctl(rtc_fd, RTC_UIE_OFF, 0);
      if (rc == -1) {
        fprintf(stderr, "ioctl() to /dev/rtc to turn off update interrupts "
                "failed, errno = %s (%d).\n", strerror(errno), errno);
      }
    } else {
      fprintf(stderr, "ioctl() to /dev/rtc to turn on update interrupts "
              "failed unexpectedly, errno = %s (%d).\n", 
              strerror(errno), errno);
      *retcode_p = 1;
    }
    close(rtc_fd);
  }
}



static void
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
    fprintf(stderr, "open() failed to open /dev/tty1, errno = %s (%d).\n",
            strerror(errno), errno);
    *retcode_p = 1;
  } else {
    int rc;  /* return code from ioctl() */
    int i;  /* local loop index */
    /* The time when we were called (and started waiting) */
	struct hwclk_time start_time, nowtime;

	rc = ioctl(con_fd, kdghwclk_ioctl, &start_time);
	if (rc == -1) {
      fprintf(stderr, "KDGHWCLK to read time failed, "
              "errno = %s (%d).\n", strerror(errno), errno);
      *retcode_p = 3;
    }
	
    for (i = 0; 
         (rc = ioctl(con_fd, kdghwclk_ioctl, &nowtime)) != -1
         && start_time.sec == nowtime.sec && i < 1000000; 
         i++);
    if (i >= 1000000) {
      fprintf(stderr, "Timed out waiting for time change.\n");
      *retcode_p = 2;
    } else if (rc == -1) {
      fprintf(stderr, "KDGHWCLK to read time failed, "
              "errno = %s (%d).\n", strerror(errno), errno);
      *retcode_p = 3;
    } else *retcode_p = 0;
    close(con_fd);
  }
}



static void
synchronize_to_clock_tick(enum clock_access_method clock_access,
                          int *retcode_p) {
/*-----------------------------------------------------------------------------
  Wait until the falling edge of the Hardware Clock's update flag so
  that any time that is read from the clock immediately after we
  return will be exact.

  The clock only has 1 second precision, so it gives the exact time only
  once per second, right on the falling edge of the update flag.

  We wait (up to one second) either blocked waiting for an rtc device
  or in a CPU spin loop.  The former is probably not very accurate.  

  For the KD clock access method, we have no way to synchronize, so we
  just return immediately.  This will mess some things up, but it's the
  best we can do.

  Return *retcode_p == 0 if it worked, nonzero if it didn't.

-----------------------------------------------------------------------------*/
  if (debug) printf("Waiting for clock tick...\n");

  switch (clock_access) {
  case ISA: synchronize_to_clock_tick_ISA(retcode_p); break;
  case RTC_IOCTL: synchronize_to_clock_tick_RTC(retcode_p); break;
  case KD: synchronize_to_clock_tick_KD(retcode_p); break;
  default:
    fprintf(stderr, "Internal error in synchronize_to_clock_tick.  Invalid "
            "value for clock_access argument.\n");
    *retcode_p = 1;
  }
  if (debug) printf("...got clock tick\n");
  return;
}



static void
mktime_tz(struct tm tm, const bool universal, 
          bool *valid_p, time_t *systime_p) {
/*-----------------------------------------------------------------------------
  Convert a time in broken down format (hours, minutes, etc.) into standard
  unix time (seconds into epoch).  Return it as *systime_p.

  The broken down time is argument <tm>.  This broken down time is either in
  local time zone or UTC, depending on value of logical argument "universal".
  True means it is in UTC.

  If the argument contains values that do not constitute a valid time,
  and mktime() recognizes this, return *valid_p == false and
  *systime_p undefined.  However, mktime() sometimes goes ahead and
  computes a fictional time "as if" the input values were valid,
  e.g. if they indicate the 31st day of April, mktime() may compute
  the time of May 1.  In such a case, we return the same fictional
  value mktime() does as *systime_p and return *valid_p == true.

-----------------------------------------------------------------------------*/
  time_t mktime_result;  /* The value returned by our mktime() call */
  char *zone;       /* Local time zone name */

  /* We use the C library function mktime(), but since it only works on 
     local time zone input, we may have to fake it out by temporarily 
     changing the local time zone to UTC.
     */
  zone = (char *) getenv("TZ");	/* remember original time zone */

  if (universal) {
    /* Set timezone to UTC */
    setenv("TZ", "", TRUE);
    /* Note: tzset() gets called implicitly by the time code, but only the
       first time.  When changing the environment variable, better call
       tzset() explicitly.
       */
    tzset();
  }
  mktime_result = mktime(&tm);
  if (mktime_result == -1) {
    /* This apparently (not specified in mktime() documentation) means
       the 'tm' structure does not contain valid values (however, not
       containing valid values does _not_ imply mktime() returns -1).
       */
    *valid_p = FALSE;
    *systime_p = 0;
    if (debug)
      printf("Invalid values in hardware clock: "
             "%2d/%.2d/%.2d %.2d:%.2d:%.2d\n",
             tm.tm_year, tm.tm_mon+1, tm.tm_mday,
             tm.tm_hour, tm.tm_min, tm.tm_sec
             );
  } else {
    *valid_p = TRUE;
    *systime_p = mktime_result;
    if (debug) 
      printf("Hw clock time : %.2d:%.2d:%.2d = %d seconds since 1969\n", 
             tm.tm_hour, tm.tm_min, tm.tm_sec, (int) *systime_p);
  }
  /* now put back the original zone.  */
  if (zone) setenv("TZ", zone, TRUE);
  else unsetenv("TZ");
  tzset();
}



static void
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
    fprintf(stderr, "open() failed to open /dev/tty1, errno = %s (%d).\n",
            strerror(errno), errno);
    exit(5);
  } else {
    int rc;  /* return code from ioctl() */

    rc = ioctl(con_fd, kdghwclk_ioctl, &t);
    if (rc == -1) {
      fprintf(stderr, "ioctl() failed to read time from  /dev/tty1, "
              "errno = %s (%d).\n",
              strerror(errno), errno);
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



static void
read_hardware_clock_rtc_ioctl(struct tm *tm) {
/*----------------------------------------------------------------------------
  Read the hardware clock and return the current time via <tm>
  argument.  Use ioctls to "rtc" device /dev/rtc.
-----------------------------------------------------------------------------*/
#if defined(_MC146818RTC_H)
  int rc;   /* Local return code */
  int rtc_fd;  /* File descriptor of /dev/rtc */

  rtc_fd = open("/dev/rtc",O_RDONLY);
  if (rtc_fd == -1) {
    fprintf(stderr, "open() of /dev/rtc failed, errno = %s (%d).\n",
            strerror(errno), errno);
    exit(5);
  } else {
    /* Read the RTC time/date */
    rc = ioctl(rtc_fd, RTC_RD_TIME, tm);
    if (rc == -1) {
      fprintf(stderr, "ioctl() to /dev/rtc to read the time failed, "
              "errno = %s (%d).\n", strerror(errno), errno);
      exit(5);
    }
    close(rtc_fd);
  }
  tm->tm_isdst = -1;          /* don't know whether it's daylight */
#else
  /* This function should never be called.  It exists just to make the 
     program compile.
     */
#endif
}



static void
read_hardware_clock_isa(struct tm *tm) {
/*----------------------------------------------------------------------------
  Read the hardware clock and return the current time via <tm> argument.
  Assume we have an ISA machine and read the clock directly with CPU I/O
  instructions.

  This function is not totally reliable.  It takes a finite and
  unpredictable amount of time to execute the code below.  During that
  time, the clock may change and we may even read an invalid value in
  the middle of an update.  We do a few checks to minimize this
  possibility, but only the kernel can actually read the clock
  properly, since it can execute code in a short and predictable
  amount of time (by turning of interrupts).

  In practice, the chance of this function returning the wrong time is
  extremely remote.

-----------------------------------------------------------------------------*/
  bool got_time;
    /* We've successfully read a time from the Hardware Clock */

  got_time = FALSE;
  while (!got_time) {
    /* Bit 7 of Byte 10 of the Hardware Clock value is the Update In Progress
       (UIP) bit, which is on while and 244 uS before the Hardware Clock 
       updates itself.  It updates the counters individually, so reading 
       them during an update would produce garbage.  The update takes 2mS,
       so we could be spinning here that long waiting for this bit to turn
       off.

       Furthermore, it is pathologically possible for us to be in this
       code so long that even if the UIP bit is not on at first, the
       clock has changed while we were running.  We check for that too,
       and if it happens, we start over.
       */

    if ((hclock_read(10) & 0x80) == 0) {
      /* No clock update in progress, go ahead and read */
      tm->tm_sec = hclock_read_bcd(0);
      tm->tm_min = hclock_read_bcd(2);
      tm->tm_hour = hclock_read_bcd(4);
      tm->tm_wday = hclock_read_bcd(6) - 3;
      tm->tm_mday = hclock_read_bcd(7);
      tm->tm_mon = hclock_read_bcd(8) - 1;
      tm->tm_year = hclock_read_bcd(9);
      /* We don't use the century byte (Byte 50) of the Hardware Clock.
         Here's why:  On older machines, it isn't defined.  In at least
         one reported case, a machine puts some arbitrary value in that
         byte.  Furthermore, the Linux standard time data structure doesn't
         allow for times beyond about 2037 and no Linux systems were 
         running before 1937.  Therefore, all the century byte could tell
         us is that the clock is wrong or this whole program is obsolete!
         
         So we just say if the year of century is less than 37, it's the
         21st century, otherwise it's the 20th.
         */

        if (hclock_read_bcd(9) >= 37) tm->tm_year = hclock_read_bcd(9);
        else tm->tm_year = hclock_read_bcd(9) + 100;
    }
    /* Unless the clock changed while we were reading, consider this 
       a good clock read .
       */
    if (tm->tm_sec == hclock_read_bcd (0)) got_time = TRUE;
    /* Yes, in theory we could have been running for 60 seconds and
       the above test wouldn't work!
       */
  }
  tm->tm_isdst = -1;        /* don't know whether it's daylight */
}



static void
read_hardware_clock(const enum clock_access_method method, 
                    const bool universal, bool *valid_p, time_t *systime_p){
/*----------------------------------------------------------------------------
  Read the hardware clock and return the current time via <tm> argument.

  Use the method indicated by <method> argument to access the hardware clock.
-----------------------------------------------------------------------------*/
  struct tm tm;


  switch (method) {
  case ISA:
    read_hardware_clock_isa(&tm);
    break;
  case RTC_IOCTL:
    read_hardware_clock_rtc_ioctl(&tm);
    break;
  case KD:
    read_hardware_clock_kd(&tm);
    break;
  default:
    fprintf(stderr, 
            "Internal error: invalid value for clock access method.\n");
    exit(5);
  }
  if (debug)
    printf ("Time read from Hardware Clock: %02d:%02d:%02d\n",
            tm.tm_hour, tm.tm_min, tm.tm_sec);
  mktime_tz(tm, universal, valid_p, systime_p);
}



static void
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
    fprintf(stderr, "Error opening /dev/tty1.  Errno: %s (%d)\n",
            strerror(errno), errno);
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
        fprintf(stderr, "ioctl() to open /dev/tty1 failed.  "
                "Errno: %s (%d)\n",
                strerror(errno), errno);
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



static void
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
    fprintf(stderr, "Unable to open /dev/rtc, open() errno = %s (%d)\n",
            strerror(errno), errno);
    exit(5);
  } else {
    if (testing) 
      printf("Not setting Hardware Clock because running in test mode.\n");
    else {
      rc = ioctl(rtc_fd, RTC_SET_TIME, &new_broken_time);
      if (rc == -1) {
        fprintf(stderr, 
                "ioctl() (RTC_SET_TIME) to /dev/rtc to set time failed, "
                "errno = %s (%d).\n", strerror(errno), errno);
        exit(5);
      } else {
        if (debug)
          printf("ioctl(RTC_SET_TIME) was successful.\n");
      }
    }
    close(rtc_fd);
  }
}



static void
set_hardware_clock_isa(const struct tm new_broken_time, 
                       const bool testing) {
/*----------------------------------------------------------------------------
  Set the Hardware Clock to the time (in broken down format)
  new_broken_time.  Use direct I/O instructions to what we assume is
  an ISA Hardware Clock.
----------------------------------------------------------------------------*/
  unsigned char save_control, save_freq_select;

  if (testing) 
    printf("Not setting Hardware Clock because running in test mode.\n");
  else {
#ifdef __i386__
    const bool interrupts_were_enabled = interrupts_enabled;

    __asm__ volatile ("cli");
    interrupts_enabled = FALSE;
#endif
    save_control = hclock_read(11);   /* tell the clock it's being set */
    hclock_write(11, (save_control | 0x80));
    save_freq_select = hclock_read(10);       /* stop and reset prescaler */
    hclock_write (10, (save_freq_select | 0x70));

    hclock_write_bcd(0, new_broken_time.tm_sec);
    hclock_write_bcd(2, new_broken_time.tm_min);
    hclock_write_bcd(4, new_broken_time.tm_hour);
    hclock_write_bcd(6, new_broken_time.tm_wday + 3);
    hclock_write_bcd(7, new_broken_time.tm_mday);
    hclock_write_bcd(8, new_broken_time.tm_mon + 1);
    hclock_write_bcd(9, new_broken_time.tm_year%100);
    hclock_write_bcd(50, (1900+new_broken_time.tm_year)/100);
    
    /* The kernel sources, linux/arch/i386/kernel/time.c, have the
       following comment:
    
       The following flags have to be released exactly in this order,
       otherwise the DS12887 (popular MC146818A clone with integrated
       battery and quartz) will not reset the oscillator and will not
       update precisely 500 ms later.  You won't find this mentioned
       in the Dallas Semiconductor data sheets, but who believes data
       sheets anyway ...  -- Markus Kuhn
    
    Hence, they will also be done in this order here.
    faith@cs.unc.edu, Thu Nov  9 08:26:37 1995 
    */

    hclock_write (11, save_control);
    hclock_write (10, save_freq_select);
#ifdef __i386__
    if (interrupts_were_enabled) {
      __asm__ volatile ("sti");
      interrupts_enabled = TRUE;
    }
#endif
  }
}


static void
set_hardware_clock(const enum clock_access_method method,
                   const time_t newtime, 
                   const bool universal, 
                   const bool testing) {
/*----------------------------------------------------------------------------
  Set the Hardware Clock to the time <newtime>, in local time zone or UTC,
  according to <universal>.

  Use the method indicated by the <method> argument.
----------------------------------------------------------------------------*/

  struct tm new_broken_time;  
    /* Time to which we will set Hardware Clock, in broken down format, in
       the time zone of caller's choice
       */

  if (universal) new_broken_time = *gmtime(&newtime);
  else new_broken_time = *localtime(&newtime);

  if (debug) 
    printf("Setting Hardware Clock to %.2d:%.2d:%.2d "
           "= %d seconds since 1969\n", 
           new_broken_time.tm_hour, new_broken_time.tm_min, 
           new_broken_time.tm_sec, (int) newtime);

  switch (method) {
  case ISA:
    set_hardware_clock_isa(new_broken_time, testing);
    break;
  case RTC_IOCTL:
    set_hardware_clock_rtc_ioctl(new_broken_time, testing);
    break;
  case KD:
    set_hardware_clock_kd(new_broken_time, testing);
    break;
  default:
    fprintf(stderr, 
            "Internal error: invalid value for clock access method.\n");
    exit(5);
  }
}



static void
set_hardware_clock_exact(const time_t settime, 
                         const struct timeval ref_time,
                         const enum clock_access_method clock_access,
                         const bool universal, 
                         const bool testing) {
/*----------------------------------------------------------------------------
  Set the Hardware Clock to the time "settime", in local time zone or UTC,
  according to "universal".

  But correct "settime" and wait for a fraction of a second so that
  "settime" is the value of the Hardware Clock as of system time
  "ref_time", which is in the past.  For example, if "settime" is
  14:03:05 and "ref_time" is 12:10:04.5 and the current system
  time is 12:10:06.0: Wait .5 seconds (to make exactly 2 seconds since
  "ref_time") and then set the Hardware Clock to 14:03:07, thus
  getting a precise and retroactive setting of the clock.

  (Don't be confused by the fact that the system clock and the Hardware
  Clock differ by two hours in the above example.  That's just to remind 
  you that there are two independent time scales here).

  This function ought to be able to accept set times as fractional times.
  Idea for future enhancement.

-----------------------------------------------------------------------------*/
  time_t newtime;  /* Time to which we will set Hardware Clock */
  struct timeval now_time;  /* locally used time */

  gettimeofday(&now_time, NULL);
  newtime = settime + (int) time_diff(now_time, ref_time) + 1;
  if (debug) 
    printf("Time elapsed since reference time has been %.6f seconds.\n"
           "Delaying further to reach the next full second.\n",
           time_diff(now_time, ref_time));
  
  /* Now delay some more until Hardware Clock time newtime arrives */
  do gettimeofday(&now_time, NULL);
  while (time_diff(now_time, ref_time) < newtime - settime);
  
  set_hardware_clock(clock_access, newtime, universal, testing);
}



static void
get_epoch(unsigned long *epoch_p, int *retcode_p) {
/*----------------------------------------------------------------------------
  Get the Hardware Clock epoch setting from the kernel.
----------------------------------------------------------------------------*/
  int rtc_fd;

  rtc_fd = open("/dev/rtc", O_RDONLY);
  if (rtc_fd < 0) {
    if (errno == ENOENT) 
      fprintf(stderr, "To manipulate the epoch value in the kernel, we must "
              "access the Linux 'rtc' device driver via the device special "
              "file /dev/rtc.  This file does not exist on this system.\n");
    else 
      fprintf(stderr, "Unable to open /dev/rtc, open() errno = %s (%d)\n",
              strerror(errno), errno);
    *retcode_p = 1;
  } else {
    int rc;  /* return code from ioctl */
    rc = ioctl(rtc_fd, RTC_EPOCH_READ, epoch_p);
    if (rc == -1) {
      fprintf(stderr, "ioctl(RTC_EPOCH_READ) to /dev/rtc failed, "
              "errno = %s (%d).\n", strerror(errno), errno);
      *retcode_p = 1;
    } else {
      *retcode_p = 0;
      if (debug) printf("we have read epoch %ld from /dev/rtc "
                        "with RTC_EPOCH_READ ioctl.\n", *epoch_p);
    }
    close(rtc_fd);
  }
  return;
}



static void
set_epoch(unsigned long epoch, const bool testing, int *retcode_p) {
/*----------------------------------------------------------------------------
  Set the Hardware Clock epoch in the kernel.
----------------------------------------------------------------------------*/
  if (epoch < 1900)
    /* kernel would not accept this epoch value */
    fprintf(stderr, "The epoch value may not be less than 1900.  "
            "You requested %ld\n", epoch);
  else {
    int rtc_fd;
    
    rtc_fd = open("/dev/rtc", O_RDONLY);
    if (rtc_fd < 0) {
      if (errno == ENOENT) 
        fprintf(stderr, "To manipulate the epoch value in the kernel, we must "
                "access the Linux 'rtc' device driver via the device special "
                "file /dev/rtc.  This file does not exist on this system.\n");
      fprintf(stderr, "Unable to open /dev/rtc, open() errno = %s (%d)\n",
              strerror(errno), errno);
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
            fprintf(stderr, "The kernel (specifically, the device driver "
                    "for /dev/rtc) does not have the RTC_EPOCH_SET ioctl.  "
                    "Get a newer driver.\n");
          else 
            fprintf(stderr, "ioctl(RTC_EPOCH_SET) to /dev/rtc failed, "
                    "errno = %s (%d).\n", strerror(errno), errno);
          *retcode_p = 1;
        } else *retcode_p = 0;
      }
      close(rtc_fd);
    }
  }
}



static void
display_time(const bool hclock_valid, const time_t systime, 
             const float sync_duration) {
/*----------------------------------------------------------------------------
  Put the time "systime" on standard output in display format.
  Except if hclock_valid == false, just tell standard output that we don't
  know what time it is.

  Include in the output the adjustment "sync_duration".
-----------------------------------------------------------------------------*/
  if (!hclock_valid)
    fprintf(stderr, "The Hardware Clock registers contain values that are "
            "either invalid (e.g. 50th day of month) or beyond the range "
            "we can handle (e.g. Year 2095).\n");
  else {
    char *ctime_now;  /* Address of static storage containing time string */

    /* For some strange reason, ctime() is designed to include a newline
       character at the end.  We have to remove that.
       */
    ctime_now = ctime(&systime);    /* Compute display value for time */
    *(ctime_now+strlen(ctime_now)-1) = '\0';  /* Cut off trailing newline */
    
    printf("%s  %.6f seconds\n", ctime_now, -(sync_duration));
  }
}



static int
interpret_date_string(const char *date_opt, time_t * const time_p) {
/*----------------------------------------------------------------------------
  Interpret the value of the --date option, which is something like
  "13:05:01".  In fact, it can be any of the myriad ASCII strings that specify
  a time which the "date" program can understand.  The date option value in
  question is our "dateopt" argument.  

  The specified time is in the local time zone.

  Our output, "*time_p", is a seconds-into-epoch time.

  We use the "date" program to interpret the date string.  "date" must be
  runnable by issuing the command "date" to the /bin/sh shell.  That means
  in must be in the current PATH.

  If anything goes wrong (and many things can), we return return code
  10 and arbitrary *time_p.  Otherwise, return code is 0 and *time_p
  is valid.
----------------------------------------------------------------------------*/
  FILE *date_child_fp;
  char date_resp[100];
  const char magic[]="seconds-into-epoch=";
  char date_command[100];  
  int retcode;  /* our eventual return code */
  int rc;  /* local return code */

  if (date_opt == NULL) {
    fprintf(stderr, "No --date option specified.\n");
    retcode = 14;
  } else if (strchr(date_opt, '"') != NULL) {
    /* Quotation marks in date_opt would ruin the date command we construct.
       */
    fprintf(stderr, "The value of the --date option is not a valid date.\n"
            "In particular, it contains quotation marks.\n");
    retcode = 12;
  } else {
    sprintf(date_command, "date --date=\"%s\" +seconds-into-epoch=%%s", 
            date_opt);
    if (debug) printf("Issuing date command: %s\n", date_command);

    date_child_fp = popen(date_command, "r");
    if (date_child_fp == NULL) {
      fprintf(stderr, "Unable to run 'date' program in /bin/sh shell.  "
              "popen() failed with errno=%d:%s\n", errno, strerror(errno));
      retcode = 10;
    } else {
      date_resp[0] = '\0';  /* in case fgets fails */
      fgets(date_resp, sizeof(date_resp), date_child_fp);
      if (debug) printf("response from date command = %s\n", date_resp);
      if (strncmp(date_resp, magic, sizeof(magic)-1) != 0) {
        fprintf(stderr, "The date command issued by " MYNAME " returned "
                "unexpected results.\n"
                "The command was:\n  %s\nThe response was:\n  %s\n", 
                date_command, date_resp);
        retcode = 8;
      } else {
        int seconds_since_epoch;
        rc = sscanf(date_resp + sizeof(magic)-1, "%d", &seconds_since_epoch);
        if (rc < 1) {
          fprintf(stderr, "The date command issued by " MYNAME " returned"
                  "something other than an integer where the converted"
                  "time value was expected.\n"
                  "The command was:\n  %s\nThe response was:\n %s\n",
                  date_command, date_resp);
          retcode = 6;
        } else {
          retcode = 0;
          *time_p = seconds_since_epoch;
          if (debug) 
            printf("date string %s equates to %d seconds since 1969.\n",
                   date_opt, (int) *time_p);
        }
      }
      fclose(date_child_fp);
    }
  }
  return(retcode);
}

 

static int 
set_system_clock(const bool hclock_valid, const time_t newtime, 
                 const bool testing) {
/*----------------------------------------------------------------------------
   Set the System Clock to time 'newtime'.

   Also set the kernel time zone value to the value indicated by the 
   TZ environment variable and/or /usr/lib/zoneinfo/, interpreted as
   tzset() would interpret them.  Except: do not consider Daylight
   Savings Time to be a separate component of the time zone.  Include
   any effect of DST in the basic timezone value and set the kernel
   DST value to 0.

   EXCEPT: if hclock_valid is false, just issue an error message
   saying there is no valid time in the Hardware Clock to which to set
   the system time.

   If 'testing' is true, don't actually update anything -- just say we 
   would have.
-----------------------------------------------------------------------------*/
  int retcode;  /* our eventual return code */

  if (!hclock_valid) {
    fprintf(stderr,"The Hardware Clock does not contain a valid time, so "
            "we cannot set the System Time from it.\n");
    retcode = 1;
  } else {
    struct timeval tv;
    int rc;  /* local return code */
    
    tv.tv_sec = newtime;
    tv.tv_usec = 0;
    
    tzset(); /* init timezone, daylight from TZ or ...zoneinfo/localtime */
    /* An undocumented function of tzset() is to set global variabales
       'timezone' and 'daylight'
       */
    
    if (debug) {
      printf( "Calling settimeofday:\n" );
      printf( "\ttv.tv_sec = %ld, tv.tv_usec = %ld\n",
             (long) tv.tv_sec, (long) tv.tv_usec );
    }
    if (testing) {
      printf("Not setting system clock because running in test mode.\n");
      retcode = 0;
    } else {
      /* For documentation of settimeofday(), in addition to its man page,
         see kernel/time.c in the Linux source code.  
         */
      const struct timezone tz = { timezone/60 - 60*daylight, 0 };
      /* put daylight in minuteswest rather than dsttime,
         since the latter is mostly ignored ... */
      rc = settimeofday(&tv, &tz);
      if (rc != 0) {
        if (errno == EPERM)
          fprintf(stderr, "Must be superuser to set system clock.\n");
        else
          fprintf(stderr,
                  "settimeofday() failed, errno=%d:%s\n", 
                  errno, strerror(errno));
        retcode = 1;
      } else retcode = 0;
    }
  }
  return(retcode);
}


static void
adjust_drift_factor(struct adjtime *adjtime_p,
                    const time_t nowtime, 
                    const bool hclock_valid, const time_t hclocktime   ) {
/*---------------------------------------------------------------------------
  Update the drift factor in <*adjtime_p> to reflect the fact that the
  Hardware Clock was calibrated to <nowtime> and before that was set
  to <hclocktime>.

  We assume that the user has been doing regular drift adjustments
  using the drift factor in the adjtime file, so if <nowtime> and
  <clocktime> are different, that means the adjustment factor isn't
  quite right.

  We record in the adjtime file the time at which we last calibrated
  the clock so we can compute the drift rate each time we calibrate.

  EXCEPT: if <hclock_valid> is false, assume Hardware Clock was not set
  before to anything meaningful and regular adjustments have not been
  done, so don't adjust the drift factor.

----------------------------------------------------------------------------*/
  if (!hclock_valid) {
    if (debug)
      printf("Not adjusting drift factor because the Hardware Clock "
             "previously contained garbage.\n");
  } else if ((hclocktime - adjtime_p->last_calib_time) < 23 * 60 * 60) {
    if (debug) 
      printf("Not adjusting drift factor because it has been less than a "
             "day since the last calibration.\n");
  } else {
    const float factor_adjust = 
      ((float) (nowtime - hclocktime) 
       / (hclocktime - adjtime_p->last_calib_time))
        * 24 * 60 * 60;

    if (debug)
      printf("Clock drifted %d seconds in the past %d seconds "
             "in spite of a drift factor of %f seconds/day.\n"
             "Adjusting drift factor by %f seconds/day\n",
             (int) (nowtime - hclocktime),
             (int) (hclocktime - adjtime_p->last_calib_time),
             adjtime_p->drift_factor,
             factor_adjust  );
      
    adjtime_p->drift_factor += factor_adjust;
  }
  adjtime_p->last_calib_time = nowtime;
  
  adjtime_p->last_adj_time = nowtime;
  
  adjtime_p->not_adjusted = 0;
    
  adjtime_p->dirty = TRUE;
}



static void
calculate_adjustment(
                     const float factor,
                     const time_t last_time, 
                     const float not_adjusted,
                     const time_t systime,
                     int *adjustment_p, 
                     float *retro_p,
                     const int debug ) {
/*----------------------------------------------------------------------------
  Do the drift adjustment calculation.

  The way we have to set the clock, we need the adjustment in two parts:

    1) an integer number of seconds (return as *adjustment_p)
       
    2) a positive fraction of a second (less than 1) (return as *retro_p)

  The sum of these two values is the adjustment needed.  Positive means to
  advance the clock or insert seconds.  Negative means to retard the clock
  or remove seconds.
----------------------------------------------------------------------------*/
  float exact_adjustment;

  exact_adjustment = ((float) (systime - last_time)) * factor / (24 * 60 * 60)
                     + not_adjusted;
  *adjustment_p = FLOOR(exact_adjustment);
  
  *retro_p = exact_adjustment - (float) *adjustment_p;
  if (debug) {
    printf ("Time since last adjustment is %d seconds\n",
            (int) (systime - last_time));
    printf ("Need to insert %d seconds and refer time back "
            "%.6f seconds ago\n",
            *adjustment_p, *retro_p);
  }
}



static void
save_adjtime(const struct adjtime adjtime, const bool testing) {
/*-----------------------------------------------------------------------------
  Write the contents of the <adjtime> structure to its disk file.

  But if the contents are clean (unchanged since read from disk), don't
  bother.
-----------------------------------------------------------------------------*/
  FILE *adjfile;
  char newfile[405];   /* Stuff to write to disk file */

  int rc;   /* locally used: return code from a function */

  if (adjtime.dirty) {
    /* snprintf is not always available, but this is safe
       as long as libc does not use more than 100 positions for %ld or %f */
    sprintf(newfile, "%f %ld %f\n%ld\n",
             adjtime.drift_factor,
             (long) adjtime.last_adj_time,
             adjtime.not_adjusted,
             (long) adjtime.last_calib_time  );

    if (testing) {
      printf("Not updating adjtime file because of testing mode.\n");
      printf("Would have written the following to %s:\n%s", 
             ADJPATH, newfile);
    } else {
      adjfile = fopen(ADJPATH, "w");
      if (adjfile == NULL) {
        const int fopen_errno = errno;
        printf("Could not open file with the clock adjustment parameters "
               "in it (%s) for output.\n"
               "fopen() returned errno %d: %s.\n"
               "Drift adjustment parameters not updated.\n", 
               ADJPATH, fopen_errno, strerror(errno));
      } else {
        rc = fprintf(adjfile, newfile);
        if (rc < 0) {
          const int fprintf_errno = errno;
          printf("Could not update file with the clock adjustment parameters "
                 "(%s) in it.\n"
                 "fprintf() returned errno %d: %s.\n"
                 "Drift adjustment parameters not updated.\n",
                 ADJPATH, fprintf_errno, strerror(errno));
        }
        rc = fclose(adjfile);
        if (rc < 0) {
          const int fclose_errno = errno;
          printf("Could not update file with the clock adjustment parameters "
                 "(%s) in it.\n"
                 "fclose() returned errno %d: %s.\n"
                 "Drift adjustment parameters not updated.\n",
                 ADJPATH, fclose_errno, strerror(errno));
        }
      }
    }
  }
}



static void
do_adjustment(struct adjtime *adjtime_p,
              const bool hclock_valid, const time_t hclocktime, 
              const struct timeval read_time,
              const enum clock_access_method clock_access,
              const bool universal, const bool testing) {
/*---------------------------------------------------------------------------
  Do the adjustment requested, by 1) setting the Hardware Clock (if 
  necessary), and 2) updating the last-adjusted time in the adjtime
  structure.

  Do not update anything if the Hardware Clock does not currently present
  a valid time.

  arguments <factor> and <last_time> are current values from the adjtime
  file.

  <hclock_valid> means the Hardware Clock contains a valid time, and that
  time is <hclocktime>.

  <read_time> is the current system time (to be precise, it is the system 
  time at the time <hclocktime> was read, which due to computational delay
  could be a short time ago).

  <universal>: the Hardware Clock is kept in UTC.

  <testing>:  We are running in test mode (no updating of clock).

  We do not bother to update the clock if the adjustment would be less than
  one second.  This is to avoid cumulative error and needless CPU hogging
  (remember we use an infinite loop for some timing) if the user runs us
  frequently.

----------------------------------------------------------------------------*/
  if (!hclock_valid) {
    fprintf(stderr, "The Hardware Clock does not contain a valid time, "
            "so we cannot adjust it.\n");
  } else {
    int adjustment;
    /* Number of seconds we must insert in the Hardware Clock */
    float retro;   
    /* Fraction of second we have to remove from clock after inserting
       <adjustment> whole seconds.
       */
    calculate_adjustment(adjtime_p->drift_factor,
                         adjtime_p->last_adj_time,
                         adjtime_p->not_adjusted,
                         hclocktime,
                         &adjustment, &retro,
                         debug );
    if (adjustment > 0 || adjustment < -1) {
      set_hardware_clock_exact(hclocktime + adjustment, 
                               time_inc(read_time, -retro),
                               clock_access, universal, testing);
      adjtime_p->last_adj_time = hclocktime + adjustment;
      adjtime_p->not_adjusted = 0;
      adjtime_p->dirty = TRUE;
    } else 
      if (debug) 
        printf("Needed adjustment is less than one second, "
               "so not setting clock.\n");
  }
}



static void
determine_clock_access_method(const bool user_requests_ISA,
                              enum clock_access_method *clock_access_p) {
/*----------------------------------------------------------------------------
  Figure out how we're going to access the hardware clock, by seeing
  what facilities are available, looking at invocation options, and 
  using compile-time constants.

  <user_requests_ISA> means the user explicitly asked for the ISA method.
  Even if he did, we will not select the ISA method if this is not an 
  ISA machine.
-----------------------------------------------------------------------------*/
  bool rtc_works;
    /* The /dev/rtc method is available and seems to work on this machine */

  if (got_rtc) {
    int rtc_fd = open("/dev/rtc", O_RDONLY);
    if (rtc_fd > 0) {
      rtc_works = TRUE;
      close(rtc_fd);
    } else {
      rtc_works = FALSE;
      if (debug)
        printf("Open of /dev/rtc failed, errno = %s (%d).  "
               "falling back to more primitive clock access method.\n",
               strerror(errno), errno);
    }
  } else {
    if (debug)
      printf("The Linux kernel for which this copy of hwclock() was built "
             "is too old to have /dev/rtc\n");
    rtc_works = FALSE;
  }

  if (user_requests_ISA && isa_machine) *clock_access_p = ISA;
  else if (rtc_works) *clock_access_p = RTC_IOCTL;
  else if (got_kdghwclk) {
    int con_fd;
    struct hwclk_time t;

    con_fd = open("/dev/tty1", O_RDONLY);
    if (con_fd >= 0) {
      if (ioctl( con_fd, kdghwclk_ioctl, &t ) >= 0) 
        *clock_access_p = KD;
      else {
        if (errno == EINVAL) {
          /* KDGHWCLK not implemented in this kernel... */
          *clock_access_p = ISA;
        } else {
          *clock_access_p = KD;
          fprintf(stderr,
                  "KDGHWCLK ioctl failed, errno = %s (%d).\n",
                  strerror(errno), errno);
        }
      }
    } else {
      *clock_access_p = KD;
      fprintf(stderr, 
              "Can't open /dev/tty1.  open() errno = %s (%d).\n",
              strerror(errno), errno);
    }
    close(con_fd);
  } else if (isa_machine) {
    *clock_access_p = ISA;
  } else
    *clock_access_p = NOCLOCK;
  if (debug) {
    switch (*clock_access_p) {
    case ISA: printf("Using direct I/O instructions to ISA clock.\n"); break;
    case KD: printf("Using KDGHWCLK interface to m68k clock.\n"); break;
    case RTC_IOCTL: printf("Using /dev/rtc interface to clock.\n"); break;
    default:  
      printf("determine_clock_access_method() returned invalid value: %d.\n",
             *clock_access_p);
    }
  }
}



static void
manipulate_clock(const bool show, const bool adjust, 
                 const bool set, const time_t set_time,
                 const bool hctosys, const bool systohc, 
                 const struct timeval startup_time, 
                 const enum clock_access_method clock_access,
                 const bool universal, const bool testing,
                 int *retcode_p
                 ) {
/*---------------------------------------------------------------------------
  Do all the normal work of hwclock - read, set clock, etc.

  Issue output to stdout and error message to stderr where appropriate.

  Return rc == 0 if everything went OK, rc != 0 if not.
----------------------------------------------------------------------------*/
  struct adjtime adjtime;
    /* Contents of the adjtime file, or what they should be. */
  int rc;  /* local return code */
  bool no_auth;  /* User lacks necessary authorization to access the clock */

  if (clock_access == ISA) {
    rc = i386_iopl(3);
    if (rc != 0) {
      fprintf(stderr, MYNAME " is unable to get I/O port access.  "
              "I.e. iopl(3) returned nonzero return code %d.\n"
              "This is often because the program isn't running "
              "with superuser privilege, which it needs.\n", 
              rc);
      no_auth = TRUE;
    } else no_auth = FALSE;
  } else no_auth = FALSE;

  if (no_auth) *retcode_p = 1;
  else {
    if (adjust || set || systohc) 
      read_adjtime(&adjtime, &rc);
    else {
      /* A little trick to avoid reading the file if we don't have to */
      adjtime.dirty = FALSE; 
      rc = 0;
    }
    if (rc != 0) *retcode_p = 2;
    else {
      synchronize_to_clock_tick(clock_access, retcode_p);  
        /* this takes up to 1 second */
      if (*retcode_p == 0) {
        struct timeval read_time; 
          /* The time at which we read the Hardware Clock */

        bool hclock_valid;
          /* The Hardware Clock gives us a valid time, or at least something
             close enough to fool mktime().
             */

        time_t hclocktime;
          /* The time the hardware clock had just after we
             synchronized to its next clock tick when we started up.
             Defined only if hclock_valid is true.
             */
        
        gettimeofday(&read_time, NULL);
        read_hardware_clock(clock_access, universal, &hclock_valid, 
                            &hclocktime); 
        
        if (show) {
          display_time(hclock_valid, hclocktime, 
                       time_diff(read_time, startup_time));
          *retcode_p = 0;
        } else if (set) {
          set_hardware_clock_exact(set_time, startup_time, 
                                   clock_access, universal, testing);
          adjust_drift_factor(&adjtime, set_time, hclock_valid, hclocktime);
          *retcode_p = 0;
        } else if (adjust) {
          do_adjustment(&adjtime, hclock_valid, hclocktime, 
                        read_time, clock_access,
                        universal, testing);
          *retcode_p = 0;
        } else if (systohc) {
          struct timeval nowtime, reftime;
          /* We can only set_hardware_clock_exact to a whole seconds
             time, so we set it with reference to the most recent
             whole seconds time.  
             */
          gettimeofday(&nowtime, NULL);
          reftime.tv_sec = nowtime.tv_sec;
          reftime.tv_usec = 0;
          
          set_hardware_clock_exact((time_t) reftime.tv_sec, reftime, 
                                   clock_access, universal, testing);
          *retcode_p = 0;
          adjust_drift_factor(&adjtime, (time_t) reftime.tv_sec, hclock_valid, 
                              hclocktime);
        } else if (hctosys) {
          rc = set_system_clock(hclock_valid, hclocktime, testing);
          if (rc != 0) {
            printf("Unable to set system clock.\n");
            *retcode_p = 1;
          } else *retcode_p = 0;
        }
        save_adjtime(adjtime, testing);
      }
    }
  }
}



static void
manipulate_epoch(const bool getepoch, const bool setepoch, 
                 const int epoch_opt, const bool testing) {
/*----------------------------------------------------------------------------
   Get or set the Hardware Clock epoch value in the kernel, as appropriate.
   <getepoch>, <setepoch>, and <epoch> are hwclock invocation options.

   <epoch> == -1 if the user did not specify an "epoch" option.

-----------------------------------------------------------------------------*/
  /*
   Maintenance note:  This should work on non-Alpha machines, but the 
   evidence today (98.03.04) indicates that the kernel only keeps the
   epoch value on Alphas.  If that is ever fixed, this function should be
   changed.
   */

  if (!alpha_machine)
    fprintf(stderr, "The kernel keeps an epoch value for the Hardware Clock "
            "only on an Alpha machine.\nThis copy of hwclock was built for "
            "a machine other than Alpha\n(and thus is presumably not running "
            "on an Alpha now).  No action taken.\n");
  else {
    if (getepoch) {
      unsigned long epoch;
      int retcode;

      get_epoch(&epoch, &retcode);
      if (retcode != 0)
        printf("Unable to get the epoch value from the kernel.\n");
      else 
        printf("Kernel is assuming an epoch value of %lu\n", epoch);
    } else if (setepoch) {
      if (epoch_opt == -1)
        fprintf(stderr, "To set the epoch value, you must use the 'epoch' "
                "option to tell to what value to set it.\n");
      else {
        int rc;
        set_epoch(epoch_opt, testing, &rc);
        if (rc != 0)
          printf("Unable to set the epoch value in the kernel.\n");
      }
    }
  }
}



int 
main(int argc, char **argv, char **envp) {
/*----------------------------------------------------------------------------
                                   MAIN
-----------------------------------------------------------------------------*/
  struct timeval startup_time;
    /* The time we started up, in seconds into the epoch, including fractions.
       */
  time_t set_time;  /* Time to which user said to set Hardware Clock */

  enum clock_access_method clock_access;
    /* The method that we determine is best for accessing Hardware Clock 
       on this system. 
       */

  bool permitted;  /* User is permitted to do the function */
  int retcode;   /* Our eventual return code */

  int rc;  /* local return code */

  /* option_def is the control table for the option parser.  These other
     variables are the results of parsing the options and their meanings
     are given by the option_def.  The only exception is <show>, which
     may be modified after parsing is complete to effect an implied option.
     */
  bool show, set, systohc, hctosys, adjust, getepoch, setepoch, version;
  bool universal, testing, directisa;
  char *date_opt;
  int epoch_opt;

  const optStruct option_def[] = {
    { 'r', (char *) "show",      OPT_FLAG,   &show,      0 },
    { 0,   (char *) "set",       OPT_FLAG,   &set,       0 },
    { 'w', (char *) "systohc",   OPT_FLAG,   &systohc,   0 },
    { 's', (char *) "hctosys",   OPT_FLAG,   &hctosys,   0 },
    { 0,   (char *) "getepoch",  OPT_FLAG,   &getepoch,  0 },
    { 0,   (char *) "setepoch",  OPT_FLAG,   &setepoch,  0 },
    { 'a', (char *) "adjust",    OPT_FLAG,   &adjust,    0 },
    { 'v', (char *) "version",   OPT_FLAG,   &version,   0 },
    { 0,   (char *) "date",      OPT_STRING, &date_opt,  0 },
    { 0,   (char *) "epoch",     OPT_UINT,   &epoch_opt, 0 },
    { 'u', (char *) "utc",       OPT_FLAG,   &universal, 0 },
    { 0,   (char *) "directisa", OPT_FLAG,   &directisa, 0 },
    { 0,   (char *) "test",      OPT_FLAG,   &testing,   0 },
    { 'D', (char *) "debug",     OPT_FLAG,   &debug,     0 },
    { 0,   (char *) NULL,        OPT_END,    NULL,       0 }
  };
  int argc_parse;       /* argc, except we modify it as we parse */
  char **argv_parse;    /* argv, except we modify it as we parse */

  interrupts_enabled = TRUE;  /* Since we haven't messed with them yet */

  gettimeofday(&startup_time, NULL);  /* Remember what time we were invoked */

  /* set option defaults */
  show = set = systohc = hctosys = adjust = getepoch = setepoch = 
    version = universal = 
    directisa = testing = debug = FALSE;
  date_opt = NULL;
  epoch_opt = -1; 

  argc_parse = argc; argv_parse = argv;
  optParseOptions(&argc_parse, argv_parse, option_def, 0);
    /* Uses and sets argc_parse, argv_parse. 
       Sets show, systohc, hctosys, adjust, universal, version, testing, 
       debug, set, date_opt, getepoch, setepoch, epoch_opt
       */
  
  if (argc_parse - 1 > 0) {
    fprintf(stderr, MYNAME " takes no non-option arguments.  "
            "You supplied %d.\n",
            argc_parse - 1);
    exit(100);
  }

  if (show + set + systohc + hctosys + adjust + 
      getepoch + setepoch + version > 1) {
    fprintf(stderr, "You have specified multiple function options.\n"
            "You can only perform one function at a time.\n");
    exit(100);
  }

  if (set) {
    rc = interpret_date_string(date_opt, &set_time);  /* (time-consuming) */
    if (rc != 0) {
      fprintf(stderr, "No usable set-to time.  Cannot set clock.\n");
      exit(100);
    }
  }

  if (directisa && !isa_machine) {
    fprintf(stderr, "You have requested direct access to the ISA Hardware "
            "Clock using machine instructions from the user process.  "
            "But this method only works on an ISA machine with an x86 "
            "CPU, and this is not one!\n");
    exit(100);
  }

  if (!(show | set | systohc | hctosys | adjust | getepoch | setepoch |
        version)) 
    show = 1; /* default to show */

  
  if (getuid() == 0) permitted = TRUE;
  else {
    /* program is designed to run setuid (in some situations) -- be secure! */
    if (set || hctosys || systohc || adjust) {
      fprintf(stderr, 
              "Sorry, only the superuser can change the Hardware Clock.\n");
      permitted = FALSE;
    } else if (setepoch) {
      fprintf(stderr, 
              "Sorry, only the superuser can change "
              "the Hardware Clock epoch in the kernel.\n");
      permitted = FALSE;
    } else permitted = TRUE;
  }

  if (!permitted) retcode = 2;
  else {
    retcode = 0;
    if (version) {
      printf(MYNAME " " VERSION "/%s\n",util_linux_version);
    } else if (getepoch || setepoch) {
      manipulate_epoch(getepoch, setepoch, epoch_opt, testing);
    } else {
      determine_clock_access_method(directisa, &clock_access);
      if (clock_access == NOCLOCK)
        fprintf(stderr, "Cannot access the Hardware Clock via any known "
                "method.  Use --debug option to see the details of our "
                "search for an access method.\n");
      else
        manipulate_clock(show, adjust, set, set_time, hctosys, systohc, 
                         startup_time, clock_access, universal, testing, &rc);
    }
  }
  exit(retcode);
}


/****************************************************************************

  History of this program:

  98.08.12 BJH   Version 2.4 

  Don't use century byte from Hardware Clock.  Add comments telling why.


  98.06.20 BJH   Version 2.3.

  Make --hctosys set the kernel timezone from TZ environment variable
  and/or /usr/lib/zoneinfo.  From Klaus Ripke (klaus@ripke.com).

  98.03.05 BJH.  Version 2.2.  

  Add --getepoch and --setepoch.  

  Fix some word length things so it works on Alpha.

  Make it work when /dev/rtc doesn't have the interrupt functions.
  In this case, busywait for the top of a second instead of blocking and
  waiting for the update complete interrupt.

  Fix a bunch of bugs too numerous to mention.

  97.06.01: BJH.  Version 2.1.  Read and write the century byte (Byte
  50) of the ISA Hardware Clock when using direct ISA I/O.  Problem
  discovered by job (jei@iclnl.icl.nl).
  
  Use the rtc clock access method in preference to the KDGHWCLK method.
  Problem discovered by Andreas Schwab <schwab@LS5.informatik.uni-dortmund.de>.

  November 1996: Version 2.0.1.  Modifications by Nicolai Langfeldt
  (janl@math.uio.no) to make it compile on linux 1.2 machines as well
  as more recent versions of the kernel. Introduced the NO_CLOCK
  access method and wrote feature test code to detect absense of rtc
  headers.


  Bryan Henderson based hwclock on the program "clock", in September
  1996.  While remaining mostly backward compatible with clock,
  hwclock added the following:

    - You can set the hardware clock without also modifying the Linux
      system clock.

    - You can read and set the clock with finer than 1 second precision.

    - When you set the clock, hwclock automatically refigures the drift
      rate, based on how far off the clock was before you set it.  (This
      is the drift rate that is used with the --adjust function to 
      automatically adjust the clock periodically to compensate for drift).

    - More mnemonic GNU-style command line options.

    - Comments describing how the clock and program work to improve 
      maintainability.

    - Removed the old dead I/O code that worked without the inb/outb
      instructions and without the asm/io.h definitions.

  The first version of hwclock was Version 2.

  Here is the history section from the "clock" program at the time it was
  used as a basis for hwclock:

  V1.0

  
  V1.0 by Charles Hedrick, hedrick@cs.rutgers.edu, April 1992.
 
 ********************
  V1.1
  Modified for clock adjustments - Rob Hooft, hooft@chem.ruu.nl, Nov 1992
  Also moved error messages to stderr. The program now uses getopt.
  Changed some exit codes. Made 'gcc 2.3 -Wall' happy.
 
 *****
  V1.2
 
  Applied patches by Harald Koenig (koenig@nova.tat.physik.uni-tuebingen.de)
  Patched and indented by Rob Hooft (hooft@EMBL-Heidelberg.DE)
  
  A free quote from a MAIL-message (with spelling corrections):
 
  "I found the explanation and solution for the CMOS reading 0xff problem
   in the 0.99pl13c (ALPHA) kernel: the RTC goes offline for a small amount
   of time for updating. Solution is included in the kernel source 
   (linux/kernel/time.c)."
 
  "I modified clock.c to fix this problem and added an option (now default,
   look for USE_INLINE_ASM_IO) that I/O instructions are used as inline
   code and not via /dev/port (still possible via #undef ...)."
 
  With the new code, which is partially taken from the kernel sources, 
  the CMOS clock handling looks much more "official".
  Thanks Harald (and Torsten for the kernel code)!
 
 *****
  V1.3
  Canges from alan@spri.levels.unisa.edu.au (Alan Modra):
  a) Fix a few typos in comments and remove reference to making
     clock -u a cron job.  The kernel adjusts cmos time every 11
     minutes - see kernel/sched.c and kernel/time.c set_rtc_mmss().
     This means we should really have a cron job updating
     /etc/adjtime every 11 mins (set last_time to the current time
     and not_adjusted to ???).
  b) Swapped arguments of outb() to agree with asm/io.h macro of the
     same name.  Use outb() from asm/io.h as it's slightly better.
  c) Changed CMOS_READ and CMOS_WRITE to inline functions.  Inserted
     cli()..sti() pairs in appropriate places to prevent possible
     errors, and changed ioperm() call to iopl() to allow cli.
  d) Moved some variables around to localise them a bit.
  e) Fixed bug with clock -ua or clock -us that cleared environment
     variable TZ.  This fix also cured the annoying display of bogus
     day of week on a number of machines. (Use mktime(), ctime()
     rather than asctime() )
  f) Use settimeofday() rather than stime().  This one is important
     as it sets the kernel's timezone offset, which is returned by
     gettimeofday(), and used for display of MSDOS and OS2 file
     times.
  g) faith@cs.unc.edu added -D flag for debugging
 
  V1.4: alan@SPRI.Levels.UniSA.Edu.Au (Alan Modra)
        Wed Feb  8 12:29:08 1995, fix for years > 2000.
        faith@cs.unc.edu added -v option to print version.  */


