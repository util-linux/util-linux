/**************************************************************************
                                hwclock
***************************************************************************

  This is a program for reading and setting the Hardware Clock on an ISA
  family computer.  This is the clock that is also known as the RTC,
  real time clock, or, unfortunately, the CMOS clock.

  See man page for details.

  By Bryan Henderson, 96.09.19

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
  execute the iopl() system call.)  (However, if you use one of the 
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

  If we're setting the clock to the system clock value, we wait for it
  to reach the top of a second, and then set the Hardware Clock to the
  system clock's value.

  Here's an interesting point about setting the Hardware Clock:  On my
  machine, when you set it, it sets to that precise time.  But one can
  imagine another clock whose update oscillator marches on a steady one
  second period, so updating the clock between any two oscillator ticks
  is the same as updating it right at the earlier tick.  To avoid any
  complications that might cause, we set the clock as soon as possible
  after an oscillator tick.

  Enhancements needed:

   - When waiting for whole second boundary in set_hardware_clock_exact,
     fail if we miss the goal by more than .1 second, as could happen if
     we get pre-empted (by the kernel dispatcher).

****************************************************************************/ 

#define _GNU_SOURCE             /* for snprintf */

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
#include <asm/io.h>
#include <shhopt.h>
#include "../version.h"

#define MYNAME "hwclock"
#define VERSION "2.1"

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


enum clock_access_method {ISA, RTC_IOCTL, KD};
  /* A method for accessing (reading, writing) the hardware clock:
     
     ISA: 
       via direct CPU I/O instructions that work on an ISA family
       machine (IBM PC compatible).

     RTC_IOCTL: 
       via the rtc device driver, using device special file /dev/rtc.

     KD:
       via the console driver, using device special file /dev/console.
       This is the m64k ioctl interface.

     NO_CLOCK:
       Unable to determine a accessmethod for the system clock.
   */
       

/* The following are just constants.  Oddly, this program will not
   compile if the inb() and outb() functions use something even
   slightly different from these variables.  This is probably at least
   partially related to the fact that __builtin_constant_p() doesn't
   work (is never true) in an inline function.  See comment to this 
   effect in asm/io.h. 
*/
static unsigned short clock_ctl_addr = 0x70;
static unsigned short clock_data_addr = 0x71;

bool debug;
  /* We are running in debug mode, wherein we put a lot of information about
     what we're doing to standard error.  Because of the pervasive and yet
     background nature of this value, this is a global variable.  */


#include <linux/version.h>
/* Check if the /dev/rtc interface is available in this version of
   the system headers.  131072 is linux 2.0.0.  Might need to make
   it conditional on i386 or something too -janl */
#if LINUX_VERSION_CODE >= 131072
#include <linux/mc146818rtc.h>
#include <linux/kd.h>
static const bool got_rtc = TRUE;
#else
/* Dummy to make it compile */
#define RTC_SET_TIME 0
static const bool got_rtc = FALSE;
#endif



#if defined(KDGHWCLK)
static const bool got_kdghwclk = TRUE;
static const int kdghwclk_ioctl = KDGHWCLK;
static const int kdshwclk_ioctl = KDSHWCLK;
#else
static const bool got_kdghwclk = FALSE;
static const int kdghwclk_ioctl;  /* Never used; just to make compile work */
struct hwclk_time {char dummy;};  
  /* Never used; just to make compile work */
#endif



float 
time_diff(struct timeval subtrahend, struct timeval subtractor) {
/*---------------------------------------------------------------------------
  The difference in seconds between two times in "timeval" format.
----------------------------------------------------------------------------*/
  return( (subtrahend.tv_sec - subtractor.tv_sec)
           + (subtrahend.tv_usec - subtractor.tv_usec) / 1E6 );
}


struct timeval
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
---------------------------------------------------------------------------*/
#ifdef __i386__
  register unsigned char ret;
  __asm__ volatile ("cli");
  /* & 0x7f ensures that we are not disabling NMI while we read.
     Setting on Bit 7 here would disable NMI
     */
  outb(reg & 0x7f, clock_ctl_addr);
  ret = inb(clock_data_addr);
  __asm__ volatile ("sti");
  return ret;
#endif
}



static inline void 
hclock_write(unsigned char reg, unsigned char val) {
/*----------------------------------------------------------------------------
  Set relative byte <reg> of the Hardware Clock value to <val>.
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


void
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



void
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



void
synchronize_to_clock_tick_RTC(int *retcode_p) {
/*----------------------------------------------------------------------------
  Same as synchronize_to_clock_tick(), but just for /dev/rtc.
-----------------------------------------------------------------------------*/
#if defined(_MC146818RTC_H)
  int rc;  /* local return code */
  int rtc_fd;  /* File descriptor of /dev/rtc */

  rtc_fd = open("/dev/rtc",O_RDONLY);
  if (rtc_fd == -1) {
    fprintf(stderr, "open() of /dev/rtc failed, errno = %s (%d).\n",
            strerror(errno), errno);
    *retcode_p = 1;
  } else {
    /* Turn on update interrupts (one per second) */
    rc = ioctl(rtc_fd, RTC_UIE_ON, 0);
    if (rc == -1) {
      fprintf(stderr, "ioctl() to /dev/rtc to turn on update interrupts "
              "failed, errno = %s (%d).\n", strerror(errno), errno);
      *retcode_p = 1;
    } else {
      unsigned long dummy;

      /* this blocks */
      rc = read(rtc_fd, &dummy, sizeof(unsigned long));
      if (rc == -1) {
        fprintf(stderr, "read() to /dev/rtc to wait for clock tick failed, "
                "errno = %s (%d).\n", strerror(errno), errno);
        *retcode_p = 1;
      } else {
        *retcode_p = 0;
        
        /* Turn off update interrupts */
        rc = ioctl(rtc_fd, RTC_UIE_OFF, 0);
        if (rc == -1) {
          fprintf(stderr, "ioctl() to /dev/rtc to turn off update interrupts "
                  "failed, errno = %s (%d).\n", strerror(errno), errno);
        }
      }
    }
    close(rtc_fd);
  }
#else
/* This function should never be called.  It is here just to make the 
   program compile.
*/
#endif
}

  

int
synchronize_to_clock_tick(enum clock_access_method clock_access) {
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

  Return 1 if something weird goes wrong (nothing can normally go wrong),
  0 if everything OK.

-----------------------------------------------------------------------------*/
  int retcode;  /* our eventual return code */

  if (debug) printf("Waiting for clock tick...\n");

  switch (clock_access) {
  case ISA: synchronize_to_clock_tick_ISA(&retcode); break;
  case RTC_IOCTL: synchronize_to_clock_tick_RTC(&retcode); break;
  case KD:
    if (debug) 
      printf("Can't wait for clock tick because we're using the Alpha "
             "/dev/console clock!  Assuming a clock tick.\n");
    retcode = 1;
    break;
  default:
    fprintf(stderr, "Internal error in synchronize_to_clock_tick.  Invalid "
            "value for clock_access argument.\n");
    retcode = 1;
  }
  if (debug) printf("...got clock tick\n");
  return(retcode);
}



time_t
mktime_tz(struct tm tm, const bool universal) {
/*-----------------------------------------------------------------------------
  Convert a time in broken down format (hours, minutes, etc.) into standard
  unix time (seconds into epoch).

  The broken down time is argument <tm>.  This broken down time is either in
  local time zone or UTC, depending on value of logical argument "universal".
  True means it is in UTC.
-----------------------------------------------------------------------------*/
  time_t systime;   /* our eventual return value */
  char *zone;       /* Local time zone name */

  /* We use the C library function mktime(), but since it only works on 
     local time zone input, we may have to fake it out by temporarily 
     changing the local time zone to UTC.
     */
  zone = (char *) getenv("TZ");	/* remember original time zone */

  if (universal) {
    /* Set timezone to UTC */
    (void) putenv("TZ=");
    /* Note: tzset() gets called implicitly by the time code, but only the
       first time.  When changing the environment variable, better call
       tzset() explicitly.
       */
    tzset();
  }
  systime = mktime(&tm);
  if (systime == -1) {
    /* We don't expect this to happen.  Consider this a crash */
    fprintf(stderr, "mktime() failed unexpectedly (rc -1).  Aborting.\n");
    exit(2);
  }
  
  /* now put back the original zone.  */
  if (zone)
    setenv ("TZ", zone, 1);
  else
    putenv ("TZ");
  tzset();

  if (debug) 
    printf("Hw clock time : %.2d:%.2d:%.2d = %d seconds since 1969\n", 
           tm.tm_hour, tm.tm_min, tm.tm_sec, (int) systime);

  return(systime);
}



void
read_hardware_clock_kd(struct tm *tm) {
/*----------------------------------------------------------------------------
  Read the hardware clock and return the current time via <tm>
  argument.  Use ioctls to /dev/console on what we assume is an Alpha
  machine.
-----------------------------------------------------------------------------*/
#ifdef KDGHWCLK
  int con_fd;
  struct hwclk_time t;

  con_fd = open("/dev/console", O_RDONLY);
  if (con_fd < 0) {
    fprintf(stderr, "open() failed to open /dev/console, errno = %s (%d).\n",
            strerror(errno), errno);
    exit(5);
  } else {
    int rc;  /* return code from ioctl() */

    rc = ioctl(con_fd, kdghwclk_ioctl, &t);
    if (rc == -1) {
      fprintf(stderr, "ioctl() failed to read time from  /dev/console, "
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



void
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



void
read_hardware_clock_isa(struct tm *tm) {
/*----------------------------------------------------------------------------
  Read the hardware clock and return the current time via <tm> argument.
  Assume we have an ISA machine and read the clock directly with CPU I/O
  instructions.
-----------------------------------------------------------------------------*/
  /* The loop here is just for integrity.  In theory it should never run 
     more than once 
     */
  do { 
    tm->tm_sec = hclock_read_bcd(0);
    tm->tm_min = hclock_read_bcd(2);
    tm->tm_hour = hclock_read_bcd(4);
    tm->tm_wday = hclock_read_bcd(6);
    tm->tm_mday = hclock_read_bcd(7);
    tm->tm_mon = hclock_read_bcd(8);
    tm->tm_year = hclock_read_bcd(9);
    if (hclock_read_bcd(50) == 0) {
      /* I suppose Linux could run on an old machine that doesn't implement
         the Byte 50 century value, and that if it does, that machine puts
         zero in Byte 50.  If so, this could could be useful, in that it
         makes values 70-99 -> 1970-1999 and 00-69 -> 2000-2069.
         */
      if (hclock_read_bcd(9) >= 70) tm->tm_year = hclock_read_bcd(9);
      else tm->tm_year = hclock_read_bcd(9) + 100;
    } else {
      tm->tm_year = hclock_read_bcd(50) * 100 + hclock_read_bcd(9) - 1900;
      /* Note: Byte 50 contains centuries since A.D.  Byte 9 contains
         years since beginning of century.  tm_year contains years
         since 1900.  At least we _assume_ that's what tm_year
         contains.  It is documented only as "year", and it could
         conceivably be years since the beginning of the current
         century.  If so, this code won't work after 1999.  
         */
    }
  } while (tm->tm_sec != hclock_read_bcd (0));

  tm->tm_mon--;	           /* DOS uses 1 base */
  tm->tm_wday -= 3;         /* DOS uses 3 - 9 for week days */
  tm->tm_isdst = -1;        /* don't know whether it's daylight */
}



void
read_hardware_clock(const enum clock_access_method method, struct tm *tm){
/*----------------------------------------------------------------------------
  Read the hardware clock and return the current time via <tm> argument.

  Use the method indicated by <method> argument to access the hardware clock.
-----------------------------------------------------------------------------*/
  switch (method) {
  case ISA:
    read_hardware_clock_isa(tm);
    break;
  case RTC_IOCTL:
    read_hardware_clock_rtc_ioctl(tm);
    break;
  case KD:
    read_hardware_clock_kd(tm);
    break;
  default:
    fprintf(stderr, 
            "Internal error: invalid value for clock access method.\n");
    exit(5);
  }
  if (debug)
    printf ("Time read from Hardware Clock: %02d:%02d:%02d\n",
            tm->tm_hour, tm->tm_min, tm->tm_sec);
}



void
set_hardware_clock_kd(const struct tm new_broken_time, 
                      const bool testing) {
/*----------------------------------------------------------------------------
  Set the Hardware Clock to the time <new_broken_time>.  Use ioctls to
  /dev/console on what we assume is an Alpha machine.
----------------------------------------------------------------------------*/
#ifdef KDGHWCLK
  int con_fd;  /* File descriptor of /dev/console */
  struct hwclk_time t;

  con_fd = open("/dev/console", O_RDONLY);
  if (con_fd < 0) {
    fprintf(stderr, "Error opening /dev/console.  Errno: %s (%d)\n",
            strerror(errno), errno);
    exit(1);
  } else {
    int rc;  /* locally used return code */

    t.sec  = new_broken_time->tm_sec;
    t.min  = new_broken_time->tm_min;
    t.hour = new_broken_time->tm_hour;
    t.day  = new_broken_time->tm_mday;
    t.mon  = new_broken_time->tm_mon;
    t.year = new_broken_time->tm_year;
    t.wday = new_broken_time->tm_wday;

    if (testing) 
      printf("Not setting Hardware Clock because running in test mode.\n");
    else {
      rc = ioctl(con_fd, kdshwclk_ioctl, &t );
      if (rc < 0) {
        fprintf(stderr, "ioctl() to open /dev/console failed.  "
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
    fprintf(stderr, "Unable to open /dev/rtc, open() errno = %s (%d)\n",
            strerror(errno), errno);
    exit(5);
  } else {
    rc = ioctl(rtc_fd, RTC_SET_TIME, &new_broken_time);
    if (rc == -1) {
      fprintf(stderr, "ioctl() (RTC_SET_TIME) to /dev/rtc to set time failed, "
              "errno = %s (%d).\n", strerror(errno), errno);
      exit(5);
    } else {
      if (debug)
        fprintf(stderr, "ioctl(RTC_SET_TIME) was successful.\n");
    }
    close(rtc_fd);
  }
}



void
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
    __asm__ volatile ("cli");
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
    __asm__ volatile ("sti");
#endif
  }
}


void
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



void
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



void
display_time(const time_t systime, const float sync_duration) {
/*----------------------------------------------------------------------------
  Put the time "systime" on standard output in display format.

  Include in the output the adjustment "sync_duration".
-----------------------------------------------------------------------------*/
  char *ctime_now;  /* Address of static storage containing time string */

  /* For some strange reason, ctime() is designed to include a newline
     character at the end.  We have to remove that.
     */
  ctime_now = ctime(&systime);    /* Compute display value for time */
  *(ctime_now+strlen(ctime_now)-1) = '\0';  /* Cut off trailing newline */

  printf("%s  %.6f seconds\n", ctime_now, -(sync_duration));
}



int
interpret_date_string(const char *date_opt, const time_t *time_p) {
/*----------------------------------------------------------------------------
  Interpret the value of the --date option, which is something like
  "13:05:01".  In fact, it can be any of the myriad ASCII strings that specify
  a time which the "date" program can understand.  The date option value in
  question is our "dateopt" argument.  

  The specified time is in the local time zone.

  Our output, "*newtime", is a seconds-into-epoch time.

  We use the "date" program to interpret the date string.  "date" must be
  runnable by issuing the command "date" to the /bin/sh shell.  That means
  in must be in the current PATH.

  If anything goes wrong (and many things can), we return return code 10.
  Otherwise, return code is 0 and *newtime is valid.
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
        rc = sscanf(date_resp + sizeof(magic)-1, "%d", (int *) time_p);
        if (rc < 1) {
          fprintf(stderr, "The date command issued by " MYNAME " returned"
                  "something other than an integer where the converted"
                  "time value was expected.\n"
                  "The command was:\n  %s\nThe response was:\n %s\n",
                  date_command, date_resp);
          retcode = 6;
        } else {
          retcode = 0;
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

 

int 
set_system_clock(const time_t newtime, const int testing) {

  struct timeval tv;
  int retcode;  /* our eventual return code */
  int rc;  /* local return code */

  tv.tv_sec = newtime;
  tv.tv_usec = 0;
  
  if (debug) {
    printf( "Calling settimeofday:\n" );
    /* Note: In Linux 1.2, tv_sec and tv_usec were long int */
    printf( "\ttv.tv_sec = %d, tv.tv_usec = %d\n",
           tv.tv_sec, tv.tv_usec );
  }
  if (testing) {
    printf("Not setting system clock because running in test mode.\n");
    retcode = 0;
  } else {
    rc = settimeofday(&tv, NULL);
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
  return(retcode);
}


void
adjust_drift_factor(struct adjtime *adjtime_p,
                    const time_t nowtime, 
                    const time_t hclocktime   ) {
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

----------------------------------------------------------------------------*/
  if ((hclocktime - adjtime_p->last_calib_time) >= 24 * 60 * 60) {
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
  } else if (debug) 
    printf("Not adjusting drift factor because it has been less than a "
           "day since the last calibration.\n");

  adjtime_p->last_calib_time = nowtime;
  
  adjtime_p->last_adj_time = nowtime;

  adjtime_p->not_adjusted = 0;

  adjtime_p->dirty = TRUE;
}



void
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



void
save_adjtime(const struct adjtime adjtime, const bool testing) {
/*-----------------------------------------------------------------------------
  Write the contents of the <adjtime> structure to its disk file.

  But if the contents are clean (unchanged since read from disk), don't
  bother.
-----------------------------------------------------------------------------*/
  FILE *adjfile;
  char newfile[162];   /* Stuff to write to disk file */

  int rc;   /* locally used: return code from a function */

  if (adjtime.dirty) {
    snprintf(newfile, sizeof(newfile), "%f %d %f\n%d\n",
             adjtime.drift_factor,
             adjtime.last_adj_time,
             adjtime.not_adjusted,
             adjtime.last_calib_time  );

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



void
do_adjustment(struct adjtime *adjtime_p,
              const time_t hclocktime, const struct timeval read_time,
              const enum clock_access_method clock_access,
              const bool universal, const bool testing) {
/*---------------------------------------------------------------------------
  Do the adjustment requested, by 1) setting the Hardware Clock (if 
  necessary), and 2) updating the last-adjusted time in the adjtime
  structure.

  arguments <factor> and <last_time> are current values from the adjtime
  file.

  <hclocktime> is the current time set in the Hardware Clock.

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



void
determine_clock_access_method(const bool user_requests_ISA,
                              enum clock_access_method *clock_access_p) {
/*----------------------------------------------------------------------------
  Figure out how we're going to access the hardware clock, by seeing
  what facilities are available, looking at invocation options, and 
  using compile-time constants.

  <user_requests_ISA> means the user explicitly asked for the ISA method.
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
  } else rtc_works = TRUE;

  if (user_requests_ISA) *clock_access_p = ISA;
  else if (rtc_works) *clock_access_p = RTC_IOCTL;
  else if (got_kdghwclk) {
    int con_fd;
    struct hwclk_time t;

    con_fd = open("/dev/console", O_RDONLY);
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
              "Can't open /dev/console.  open() errno = %s (%d).\n",
              strerror(errno), errno);
    }
    close(con_fd);
  } else {
    *clock_access_p = ISA;
  }
  if (debug) {
    switch (*clock_access_p) {
    case ISA: printf("Using direct I/O instructions to ISA clock.\n"); break;
    case KD: printf("Using /dev/console interface to Alpha clock.\n"); break;
    case RTC_IOCTL: printf("Using /dev/rtc interface to clock.\n"); break;
    default:  
      printf("determine_clock_access_method() returned invalid value: %d.\n",
             *clock_access_p);
    }
  }
}



void
manipulate_clock(const bool show, const bool adjust, 
                 const bool set, const time_t set_time,
                 const bool hctosys, const bool systohc, 
                 const struct timeval startup_time, 
                 const enum clock_access_method clock_access,
                 const bool universal, const bool testing,
                 int *retcode
                 ) {
/*---------------------------------------------------------------------------
  Do all the normal work of hwclock - read, set clock, etc.

  Issue output to stdout and error message to stderr where appropriate.

  Return rc == 0 if everything went OK, rc != 0 if not.
----------------------------------------------------------------------------*/
  struct adjtime adjtime;
    /* Contents of the adjtime file, or what they should be. */
  struct tm tm;
  time_t hclocktime;
    /* The time the hardware clock had just after we synchronized to its
       next clock tick when we started up.
       */
  struct timeval read_time; 
    /* The time at which we read the Hardware Clock */

  int rc;  /* local return code */
  bool no_auth;  /* User lacks necessary authorization to access the clock */

  if (clock_access == ISA) {
    rc = iopl(3);
    if (rc != 0) {
      fprintf(stderr, MYNAME " is unable to get I/O port access.  "
              "I.e. iopl(3) returned nonzero return code %d.\n"
              "This is often because the program isn't running "
              "with superuser privilege, which it needs.\n", 
              rc);
      no_auth = TRUE;
    } else no_auth = FALSE;
  } else no_auth = FALSE;

  if (no_auth) *retcode = 1;
  else {
    if (adjust || set) 
      read_adjtime(&adjtime, &rc);
    else {
      /* A little trick to avoid reading the file if we don't have to */
      adjtime.dirty = FALSE; 
      rc = 0;
    }
    if (rc != 0) *retcode = 2;
    else {
      synchronize_to_clock_tick(clock_access);  /* this takes up to 1 second */
      
      /* Get current time from Hardware Clock, in case we need it */
      gettimeofday(&read_time, NULL);
      read_hardware_clock(clock_access, &tm); 
      hclocktime = mktime_tz(tm, universal);
      
      if (show) {
        display_time(hclocktime, time_diff(read_time, startup_time));
        *retcode = 0;
      } else if (set) {
        set_hardware_clock_exact(set_time, startup_time, 
                                 clock_access, universal, testing);
        adjust_drift_factor(&adjtime, set_time, hclocktime);
        *retcode = 0;
      } else if (adjust) {
        do_adjustment(&adjtime, hclocktime, read_time, clock_access,
                      universal, testing);
        *retcode = 0;
      } else if (systohc) {
        struct timeval nowtime, reftime;
        /* We can only set_hardware_clock_exact to a whole seconds time, so we
           set it with reference to the most recent whole seconds time.
           */
        gettimeofday(&nowtime, NULL);
        reftime.tv_sec = nowtime.tv_sec;
        reftime.tv_usec = 0;
    
        set_hardware_clock_exact((time_t) reftime.tv_sec, reftime, 
                                 clock_access, universal, testing);
        *retcode = 0;
      } else if (hctosys) {
        rc = set_system_clock(hclocktime, testing);
        if (rc != 0) {
          printf("Unable to set system clock.\n");
          *retcode = 1;
        } else *retcode = 0;
      }
      save_adjtime(adjtime, testing);
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
  bool show, set, systohc, hctosys, adjust, version;
  bool universal, testing, directisa;
  char *date_opt;

  const optStruct option_def[] = {
    { 'r', (char *) "show",      OPT_FLAG,   &show,      0 },
    { 0,   (char *) "set",       OPT_FLAG,   &set,       0 },
    { 'w', (char *) "systohc",   OPT_FLAG,   &systohc,   0 },
    { 's', (char *) "hctosys",   OPT_FLAG,   &hctosys,   0 },
    { 'a', (char *) "adjust",    OPT_FLAG,   &adjust,    0 },
    { 'v', (char *) "version",   OPT_FLAG,   &version,   0 },
    { 0,   (char *) "date",      OPT_STRING, &date_opt,  0 },
    { 'u', (char *) "utc",       OPT_FLAG,   &universal, 0 },
    { 0,   (char *) "directisa", OPT_FLAG,   &directisa, 0 },
    { 0,   (char *) "test",      OPT_FLAG,   &testing,   0 },
    { 'D', (char *) "debug",     OPT_FLAG,   &debug,     0 }
  };
  int argc_parse;       /* argc, except we modify it as we parse */
  char **argv_parse;    /* argv, except we modify it as we parse */

  gettimeofday(&startup_time, NULL);  /* Remember what time we were invoked */

  /* set option defaults */
  show = set = systohc = hctosys = adjust = version = universal = 
    directisa = testing = debug = FALSE;
  date_opt = NULL;

  argc_parse = argc; argv_parse = argv;
  optParseOptions(&argc_parse, argv_parse, option_def, 0);
    /* Uses and sets argc_parse, argv_parse. 
       Sets show, systohc, hctosys, adjust, universal, version, testing, 
       debug, set, date_opt
       */
  
  if (argc_parse - 1 > 0) {
    fprintf(stderr, MYNAME " takes no non-option arguments.  "
            "You supplied %d.\n",
            argc_parse - 1);
    exit(100);
  }

  if (show + set + systohc + hctosys + adjust + version > 1) {
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

  if (!(show | set | systohc | hctosys | adjust | version)) 
    show = 1; /* default to show */

  if (set || hctosys || systohc || adjust) {
    /* program is designed to run setuid, be secure! */

    if (getuid() != 0) {			
      fprintf(stderr, 
              "Sorry, only superuser can change the Hardware Clock.\n");
      permitted = FALSE;
    } else permitted = TRUE;
  } else permitted = TRUE;

  if (!permitted) retcode = 2;
  else {
    retcode = 0;
    if (version) {
      printf(MYNAME " " VERSION "/%s\n",util_linux_version);
    } else {
      determine_clock_access_method(directisa, &clock_access);

      manipulate_clock(show, adjust, set, set_time, hctosys, systohc, 
                       startup_time, clock_access, universal, testing, &rc);
    }
  }
  exit(retcode);
}


/****************************************************************************

  History of this program:

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


