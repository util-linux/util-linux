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

  Definition of century:  In this program, a century is a 100 year 
  period in which all the years' numbers in the Gregorian calendar
  differ only in their last two decimal digits.  E.g. 1900-1999 is
  a century.  The 20th Century (1901-2000), however, is not.


  About the unusual situation of the Jensen variety of Alpha:

  Martin Ostermann writes: 

  The problem with the Jensen is twofold: First, it has the clock at a
  different address. Secondly, it has a distinction beween "local" and
  normal bus addresses. The local ones pertain to the hardware integrated
  into the chipset, like serial/parallel ports and of course, the RTC.
  Those need to be addressed differently. This is handled fine in the kernel,
  and it's not a problem, since this usually gets totally optimized by the
  compile. But the i/o routines of (g)libc lack this support so far.
  The result of this is, that the old clock program worked only on the
  Jensen when USE_DEV_PORT was defined, but not with the normal inb/outb
  functions.
  


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
#include <sys/time.h>
#include <sys/stat.h>
#include "shhopt.h"
#include "../version.h"  /* Defines UTIL_LINUX, among other things */
#include "hwclock.h"

#define FLOOR(arg) ((arg >= 0 ? (int) arg : ((int) arg) - 1));

/* Here the information for time adjustments is kept. */
#define ADJPATH "/etc/adjtime"

/* Note that we must define the boolean type as int because we use the
   shhopt option processing library which, unfortunately, returns flag
   options as integers.  It is customary to define bool as char, but 
   then we would have to do a lot of conversion in order to interface
   with shhopt.
*/

/* The following are times, in unix standard format (seconds since 1969) */
#define START_OF_1994 757411200
#define END_OF_1995 820396800

struct adjtime {
  /* This is information we keep in the adjtime file that tells us how
     to do drift corrections, among other things.  Elements are all
     straight from the adjtime file, so see documentation of that file
     for details.  Exception is <dirty>, which is an indication that
     what's in this structure is not what's in the disk file (because
     it has been updated since read from the disk file).  
     */

  bool dirty;        
  float drift_factor;    
  time_t last_adj_time;
  float not_adjusted;
  time_t last_calib_time;
    /* The most recent time that we set the clock from an external 
       authority (as opposed to just doing a drift adjustment)
       */
  enum a_local_utc {LOCAL, UTC} local_utc;
    /* To which time zone, local or UTC, we most recently set the
       hardware clock.
       */
};





bool debug;
  /* We are running in debug mode, wherein we put a lot of information about
     what we're doing to standard output.  Because of the pervasive and yet
     background nature of this value, this is a global variable.  */



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



static bool
hw_clock_is_utc(const bool utc, const bool local_opt, 
                const struct adjtime adjtime) {
/*----------------------------------------------------------------------------
   Return true iff the hardware clock keeps Coordinated Universal Time
   rather than local time.

   'utc' means the user told us in the invocation options that the
   hardware clock is kept in UTC.
-----------------------------------------------------------------------------*/
  
  bool retval;  /* our return value */

  if (utc) retval = TRUE;
  else if (local_opt) retval = FALSE;
  else retval = (adjtime.local_utc == UTC);
  if (debug) printf("Assuming hardware clock is kept in %s time.\n",
                    retval ? "UTC" : "LOCAL");
  return retval;
}



static void
read_adjtime(struct adjtime *adjtime_p, int *rc_p) {
/*----------------------------------------------------------------------------
  Read the adjustment parameters and other persistent variables out of
  the /etc/adjtime file.

  Return them as the adjtime structure <*adjtime_p>.

  If there is no /etc/adjtime file, return defaults.
  If values are missing from the file, return defaults for them.
  
  return *rc_p = 0 if all OK, !=0 otherwise.

  Note: The default is LOCAL rather than UTC for historical reasons.

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
    adjtime_p->local_utc = LOCAL;

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
      char line3[81];           /* String: third line of adjtime file */
      
      line1[0] = '\0';          /* In case fgets fails */
      fgets(line1, sizeof(line1), adjfile);
      line2[0] = '\0';          /* In case fgets fails */
      fgets(line2, sizeof(line2), adjfile);
      line3[0] = '\0';          /* In case fgets fails */
      fgets(line3, sizeof(line3), adjfile);
      
      fclose(adjfile);
      
      /* Set defaults in case values are missing from file */
      adjtime_p->drift_factor = 0;
      adjtime_p->last_adj_time = 0;
      adjtime_p->not_adjusted = 0;
      adjtime_p->last_calib_time = 0;
      adjtime_p->local_utc = LOCAL;
      
      sscanf(line1, "%f %d %f", 
             &adjtime_p->drift_factor,
             (int *) &adjtime_p->last_adj_time, 
             &adjtime_p->not_adjusted);
      
      sscanf(line2, "%d", (int *) &adjtime_p->last_calib_time);

      {
        char local_utc_string[sizeof(line3)];
        
        local_utc_string[0] = '\0';  /* In case nothing in line3 */
        sscanf(line3, "%s", local_utc_string);

        *rc_p = 0;  /* Initial assumption - local/utc token is valid */
        if (strlen(local_utc_string) == 0) 
          adjtime_p->local_utc = LOCAL;
        else if (strcmp(local_utc_string, "UTC") == 0) 
          adjtime_p->local_utc = UTC;
        else if (strcmp(local_utc_string, "LOCAL") == 0) 
          adjtime_p->local_utc = LOCAL;
        else {
          fprintf(stderr, "%s: The first token of the third line of the file "
                  ADJPATH " is invalid.  It must be LOCAL or UTC, indicating "
                  "to which time zone the hardware clock is set.  Its "
                  "present value is '%s'.\n", MYNAME, local_utc_string);
          *rc_p = 5;
        }
      }
    }
    adjtime_p->dirty = FALSE;

    if (debug) {
      printf("Last drift adjustment done %s (Time %d)\n", 
             ctime2(adjtime_p->last_adj_time),
             (int) adjtime_p->last_adj_time);
      printf("Last calibration done %s (Time %d)\n",
             ctime2(adjtime_p->last_calib_time), 
             (int) adjtime_p->last_calib_time);
    }
  }
}



static void
synchronize_to_clock_tick(enum clock_access_method clock_access, 
                          const int dev_port, const bool use_uf_bit,
                          int *retcode_p) {
/*-----------------------------------------------------------------------------
  Wait until the moment the Hardware Clock updates to the next second,
  so we know the exact time.

  The clock only has 1 second precision, so it gives the exact time only
  once per second.

  Return *retcode_p == 0 if it worked, nonzero if it didn't.
-----------------------------------------------------------------------------*/
  if (debug) printf("Waiting for clock tick...\n");

  switch (clock_access) {
  case ISA: synchronize_to_clock_tick_ISA(retcode_p, -1, use_uf_bit); break;
  case DEV_PORT: synchronize_to_clock_tick_ISA(retcode_p, dev_port,
                                               use_uf_bit); break;
  case RTC_IOCTL: synchronize_to_clock_tick_RTC(retcode_p); break;
  case KD: synchronize_to_clock_tick_KD(retcode_p); break;
  default:
    fprintf(stderr, "%s: Internal error in synchronize_to_clock_tick.  "
            "Invalid value for clock_access argument: %d.\n",
            MYNAME, clock_access);
    *retcode_p = 1;
  }
  if (debug) printf("...got clock tick\n");
  return;
}


static struct tm
make_within_one_year(const struct tm base_tm, const time_t last_known_time) {
/*----------------------------------------------------------------------------
  Compute a time that is the same as the input base_tm, except for a
  different year.  The year shall be whatever year it takes to make the
  output time within one year after last_known_time.

  The timezone for both the input and output values is the value of
  the TZ environment variable.
-----------------------------------------------------------------------------*/
  struct tm broken_last_known_time;
    /* The input time last_known_time, in broken down format */
  struct tm test_time;

  if (debug)
    printf("Ignoring clock year and assuming "
           "it's within 1 year after %s\n",
           ctime2(last_known_time));

  broken_last_known_time = *localtime(&last_known_time);
  
  test_time = base_tm;
  test_time.tm_year = broken_last_known_time.tm_year;

  if (mktime(&test_time) < last_known_time)
    test_time.tm_year += 1;

  return(test_time);
}



static void
mktime_tz(struct tm hw_tm, const bool universal, const bool badyear,
          const time_t last_known_time,
          bool *valid_p, time_t *systime_p) {
/*-----------------------------------------------------------------------------
  Convert a time in broken down format (hours, minutes, etc.)  as read
  from the Hardware Clock into standard unix time (seconds into
  epoch).  Return it as *systime_p.

  The broken down time is argument <tm>.  This broken down time is
  either in local time zone or UTC, depending on value of logical
  argument 'universal'.  True means it is in UTC.

  Argument 'badyear' true means the input time is from one of those
  machines with the Award BIOS that is incapable of storing a year
  value less than 94 or 95, which means we can't use the year value
  from the clock (see documentation of hwclock's --badyear option).
  In this case, we instead determine the year by assuming that it's
  less than a year since the time <last_known_time>.


  If the argument contains values that do not constitute a valid time,
  and mktime() recognizes this, return *valid_p == false and
  *systime_p undefined.  However, mktime() sometimes goes ahead and
  computes a fictional time "as if" the input values were valid,
  e.g. if they indicate the 31st day of April, mktime() may compute
  the time of May 1.  In such a case, we return the same fictional
  value mktime() does as *systime_p and return *valid_p == true.

-----------------------------------------------------------------------------*/
  time_t mktime_result;  /* The value returned by our mktime() call */
  struct tm adjusted_tm;
    /* The same as the value from our argument, except if we determine
       the year in the argument is garbage, this value contains the year 
       computed from the ADJTIME file instead.
       */
  char *zone;       /* Local time zone name */

  /* We use the C library function mktime(), but since it only works on 
     local time zone input, we may have to fake it out by temporarily 
     changing the local time zone to UTC.
     */
  zone = (char *) getenv("TZ");	/* remember original time zone */

  if (universal) {
    /* Set timezone to UTC */
    setenv("TZ", "UTC 0", TRUE);
    /* Note: tzset() gets called implicitly by the time code, but only the
       first time.  When changing the environment variable, better call
       tzset() explicitly.

       Also: documentation for tzset() says if TZ = "", that means UTC.
       But practice shows that that only works if tzset() hasn't already
       been called before.  So we explicitly say "UTC 0".
       */
    tzset();
  }

  if (badyear) 
    adjusted_tm = make_within_one_year(hw_tm, last_known_time);
  else adjusted_tm = hw_tm; 

  mktime_result = mktime(&adjusted_tm);
  if (mktime_result == -1) {
    /* This apparently (not specified in mktime() documentation) means
       the 'adjusted_tm' structure does not contain valid values (however, not
       containing valid values does _not_ imply mktime() returns -1).
       */
    /* Note that we are assuming here that the invalidity came from the
       hardware values and was not introduced by our adjustments!
       */
    *valid_p = FALSE;
    *systime_p = 0;
    if (debug)
      printf("Invalid values in hardware clock: "
             "%2d/%.2d/%.2d %.2d:%.2d:%.2d\n",
             hw_tm.tm_year, hw_tm.tm_mon+1, hw_tm.tm_mday,
             hw_tm.tm_hour, hw_tm.tm_min, hw_tm.tm_sec
             );
  } else {
    *valid_p = TRUE;
    *systime_p = mktime_result;
    if (debug) 
      printf("Hw clock time : %s = %d seconds since 1969\n", 
             ctime2(*systime_p), (int) *systime_p);
  }
  /* now put back the original zone.  */
  if (zone) setenv("TZ", zone, TRUE);
  else unsetenv("TZ");
  tzset();
}



static void
read_hardware_clock(const enum clock_access_method method, 
                    const int dev_port,
                    const bool universal, const int hc_zero_year,
                    const bool badyear, 
                    const time_t last_known_time,
                    bool *valid_p, time_t *systime_p) {
/*----------------------------------------------------------------------------
  Read the hardware clock and return the current time via *systime_p
  argument.  

  If the hardware clock fails to tell us a time, return *valid_p == false
  and undefined value as *systime_p.  Otherwise *valid_p == true.

  Consider the hardware clock to be set in Coordinated Universal Time
  (UTC) iff 'universal' == true.

  Consider the year value of the clock to be useless iff 'badyear' == true.

  Recognize that the present time is is after 'last_known_time', which
  information may be necessary to interpret the value of some hardware
  clocks.

  Use the method indicated by 'method' argument to access the hardware clock.
-----------------------------------------------------------------------------*/
  struct tm tm;

  switch (method) {
  case RTC_IOCTL:
    read_hardware_clock_rtc_ioctl(&tm);
    break;
  case ISA:
    read_hardware_clock_isa(&tm, -1, hc_zero_year);
    break;
  case DEV_PORT:
    read_hardware_clock_isa(&tm, dev_port, hc_zero_year);
    break;
  case KD:
    read_hardware_clock_kd(&tm);
    break;
  default:
    fprintf(stderr, 
            "%s: Internal error: invalid value for clock access method.\n",
            MYNAME);
    exit(5);
  }
  if (debug)
    printf ("Time read from Hardware Clock: Y=%d M=%d D=%d %02d:%02d:%02d\n",
            tm.tm_year, tm.tm_mon+1, tm.tm_mday,
            tm.tm_hour, tm.tm_min, tm.tm_sec);
  mktime_tz(tm, universal, badyear, last_known_time, valid_p, systime_p);
}



static void
set_hardware_clock(const enum clock_access_method method,
                   const int dev_port,
                   const time_t newtime, 
                   const bool universal, 
                   const int hc_zero_year, const bool badyear, 
                   const bool testing) {
/*----------------------------------------------------------------------------
  Set the Hardware Clock to the time 'newtime', in local time zone or UTC,
  according to 'universal'.

  'badyear' true means the clock is incapable of storing the proper
  year value, so we instead store 95, 96, 97, or 98 so that it is at
  least in the right place in the leap year cycle (and will remain so
  for at least the next year).

  Use the method indicated by the 'method' argument.
----------------------------------------------------------------------------*/
  struct tm new_broken_time;  
    /* Time to which we will set Hardware Clock, in broken down format, in
       the time zone of caller's choice
       */

  if (universal) new_broken_time = *gmtime(&newtime);
  else new_broken_time = *localtime(&newtime);

  /* If the clock is incapable of storing the true year value, change
     the year to a fictional stand-in year as described in the prolog.
     */
  if (badyear)
    new_broken_time.tm_year = 95 + ((new_broken_time.tm_year + 1) % 4);

  if (debug) 
    printf("Setting Hardware Clock to %.2d:%.2d:%.2d "
           "= %d seconds since 1969\n", 
           new_broken_time.tm_hour, new_broken_time.tm_min, 
           new_broken_time.tm_sec, (int) newtime);

  switch (method) {
  case RTC_IOCTL:
    set_hardware_clock_rtc_ioctl(new_broken_time, testing);
    break;
  case ISA:
    set_hardware_clock_isa(new_broken_time, hc_zero_year, -1, testing);
    break;
  case DEV_PORT:
    set_hardware_clock_isa(new_broken_time, hc_zero_year, dev_port, testing);
    break;
  case KD:
    set_hardware_clock_kd(new_broken_time, testing);
    break;
  default:
    fprintf(stderr, 
            "%s: Internal error: invalid value for clock access method.\n",
            MYNAME);
    exit(5);
  }
}



static void
set_hardware_clock_exact(const time_t settime, 
                         const struct timeval ref_time,
                         const enum clock_access_method clock_access,
                         const int dev_port, 
                         const bool universal, 
                         const int hc_zero_year,
                         const bool badyear,
                         const bool testing) {
/*----------------------------------------------------------------------------
  Set the Hardware Clock to the time 'settime', in local time zone or UTC,
  according to 'universal'.

  But iff 'badyear', use a fictional year as appropriate for the --badyear
  option.

  But correct 'settime' and wait for a fraction of a second so that
  'settime' is the value of the Hardware Clock as of system time
  'ref_time', which is in the past.  For example, if 'settime' is
  14:03:05 and 'ref_time' is 12:10:04.5 and the current system
  time is 12:10:06.0: Wait .5 seconds (to make exactly 2 seconds since
  'ref_time') and then set the Hardware Clock to 14:03:07, thus
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
  
  /* Now delay some more until Hardware Clock time 'newtime' arrives */
  do gettimeofday(&now_time, NULL);
  while (time_diff(now_time, ref_time) < newtime - settime);
  
  set_hardware_clock(clock_access, dev_port, newtime, 
                     universal, hc_zero_year, badyear, testing);
}



static void
display_time(const bool hclock_valid, const time_t systime, 
             const float sync_duration, const bool badyear_warn) {
/*----------------------------------------------------------------------------
  Put the time 'systime' on standard output in display format.
  Except if hclock_valid == false, just tell standard output that we don't
  know what time it is.

  Include in the output the adjustment 'sync_duration'.

  If the year is 1994 or 1995 and 'badyear_warn' is true, warn the
  user that he has a brain-damaged clock and needs to use --badyear.
  Since we didn't exist in 1994 and 1995, we know the clock isn't
  correct.

-----------------------------------------------------------------------------*/
  if (!hclock_valid)
    fprintf(stderr, "%s: The Hardware Clock registers contain values that are "
            "either invalid (e.g. 50th day of month) or beyond the range "
            "we can handle (e.g. Year 2095).\n", MYNAME);
  else {
    if (badyear_warn && (systime > START_OF_1994 && systime < END_OF_1995)) {
      printf("WARNING:  The Hardware Clock shows a time in 1994 "
             "or 1995.  This probably means you have a Hardware Clock "
             "that is incapable of tracking years after 1999, and you "
             "must use the --badyear option to make hwclock work for "
             "you.  See hwclock documentation for details.\n");
    }

    printf("%s  %.6f seconds\n", ctime2(systime), -(sync_duration));
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
    fprintf(stderr, "%s: No --date option specified.\n", MYNAME);
    retcode = 14;
  } else if (strchr(date_opt, '"') != NULL) {
    /* Quotation marks in date_opt would ruin the date command we construct.
       */
    fprintf(stderr, "%s: The value of the --date option is not a valid date.\n"
            "In particular, it contains quotation marks.\n", MYNAME);
    retcode = 12;
  } else {
    sprintf(date_command, "date --date=\"%s\" +seconds-into-epoch=%%s", 
            date_opt);
    if (debug) printf("Issuing date command: %s\n", date_command);

    date_child_fp = popen(date_command, "r");
    if (date_child_fp == NULL) {
      fprintf(stderr, "%s: Unable to run 'date' program in /bin/sh shell.  "
              "popen() failed with errno=%s (%d)\n", 
              MYNAME, strerror(errno), errno);
      retcode = 10;
    } else {
      date_resp[0] = '\0';  /* in case fgets fails */
      fgets(date_resp, sizeof(date_resp), date_child_fp);
      if (debug) printf("response from date command = %s\n", date_resp);
      if (strncmp(date_resp, magic, sizeof(magic)-1) != 0) {
        fprintf(stderr, "%s: The date command issued by " MYNAME " returned "
                "unexpected results.\n"
                "The command was:\n  %s\nThe response was:\n  %s\n", 
                MYNAME, date_command, date_resp);
        retcode = 8;
      } else {
        int seconds_since_epoch;
        rc = sscanf(date_resp + sizeof(magic)-1, "%d", &seconds_since_epoch);
        if (rc < 1) {
          fprintf(stderr, "%s: The date command issued by " MYNAME " returned"
                  "something other than an integer where the converted"
                  "time value was expected.\n"
                  "The command was:\n  %s\nThe response was:\n %s\n",
                  MYNAME, date_command, date_resp);
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
    fprintf(stderr, "%s: The Hardware Clock does not contain a valid time, so "
            "we cannot set the System Time from it.\n", MYNAME);
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
      /* Note: tv_sec and tv_usec are declared variously on different
         systems: int, long, time_t.  Casting to long below makes it 
         compile everywhere.
         */
      printf( "\ttv.tv_sec = %ld, tv.tv_usec = %ld\n",
             (long) tv.tv_sec, (long) tv.tv_usec );
    }
    if (testing) {
      printf("Not setting system clock because running in test mode.\n");
      retcode = 0;
    } else {
      /* For documentation of settimeofday() see, in addition to its man page,
         kernel/time.c in the Linux source code.  
         */
      const struct timezone tz = { timezone/60 - 60*daylight, 0 };
      /* put daylight in minuteswest rather than dsttime,
         since the latter is mostly ignored ... */
      rc = settimeofday(&tv, &tz);
      if (rc != 0) {
        if (errno == EPERM)
          fprintf(stderr, "%s: Must be superuser to set system clock.\n",
                  MYNAME);
        else
          fprintf(stderr,
                  "%s: settimeofday() failed, errno=%s (%d)\n", 
                  MYNAME, strerror(errno), errno);
        retcode = 1;
      } else retcode = 0;
    }
  }
  return(retcode);
}


static void
adjust_drift_factor(struct adjtime *adjtime_p,
                    const time_t actual_time, 
                    const bool hclock_valid, 
                    const struct timeval hclocktime   ) {
/*---------------------------------------------------------------------------
  Update the drift factor and calibration parameters in '*adjtime_p'
  to reflect the fact that at some recent instant when the actual time
  was 'actual_time', the Hardware Clock said the time was
  'hclocktime', and that we have corrected the Hardware Clock
  accordingly.  Note that 'hclocktime' is a fractional time, taking
  into consideration the Hardware Clock register contents and how long
  those contents had been that.

  We assume that the only cause of error in the Hardware Clock is
  systematic drift and that the user has been doing regular drift
  adjustments using the drift factor in the adjtime file.  Therefore,
  if 'actual_time' and 'hclocktime' are different, that means the drift
  factor isn't quite right. 

  EXCEPT: if 'hclock_valid' is false, assume Hardware Clock was not set
  before to anything meaningful and regular adjustments have not been
  done, so don't adjust the drift factor.

  Also, don't adjust if the error is more than 30 minutes, because that
  kind of error probably isn't drift.

----------------------------------------------------------------------------*/
  if (!hclock_valid) {
    if (debug)
      printf("Not adjusting drift factor because the Hardware Clock "
             "previously contained garbage.\n");
  } else if (adjtime_p->last_calib_time == 0) {
    if (debug)
      printf("Not adjusting drift factor because there is no \n"
             "previous calibration information (i.e. adjtime file is \n"
             "nonexistent or has 0 in last calibration time field).\n");
  } else if (time_diff(hclocktime, t2tv(adjtime_p->last_calib_time)) 
                      < 23.0 * 60.0 * 60.0) {
    if (debug) 
      printf("Not adjusting drift factor because it has been less than a "
             "day since the last calibration.\n");
  } else {
    const float sec_per_day = 24.0 * 60.0 * 60.0;
    float atime_per_htime;  /* adjusted time units per hardware time unit */
    float adj_days;   /* days since last adjustment (in hardware clock time) */
    float cal_days;   /* days since last calibration (in hardware clock time) */
    float exp_drift;  /* expected drift (sec) since last adjustment */
    float unc_drift;  /* uncorrected drift (sec) since last calibration */
    float factor_adjust;  /* amount to add to previous drift factor */
    atime_per_htime = 1.0 + adjtime_p->drift_factor / sec_per_day;
    adj_days = time_diff(hclocktime, t2tv(adjtime_p->last_adj_time)) 
      / sec_per_day;
    exp_drift = adj_days * adjtime_p->drift_factor + adjtime_p->not_adjusted;
    unc_drift = time_diff(t2tv(actual_time), hclocktime) - exp_drift;
    cal_days = ((float)(adjtime_p->last_adj_time - adjtime_p->last_calib_time)
                + adjtime_p->not_adjusted) / (sec_per_day * atime_per_htime)
                  + adj_days;
    factor_adjust = unc_drift / cal_days;
  
    if (unc_drift > 30*60.0) {
      if (debug)
        printf("Not adjusting drift factor because we calculated the \n"
               "uncorrected drift as %.0f seconds, which is so large that \n"
               "it probably is not drift at all, but rather some \n"
               "clock setting anomaly.\n\n", unc_drift);
    } else {
      if (debug)
        printf("Clock drifted %.1f seconds in the past %d seconds "
               "in spite of a drift factor of %f seconds/day.\n"
               "Adjusting drift factor by %f seconds/day\n",
               unc_drift,
               (int) (actual_time - adjtime_p->last_calib_time),
               adjtime_p->drift_factor,
               factor_adjust  );
      
      adjtime_p->drift_factor += factor_adjust;
    }
  }
  adjtime_p->last_calib_time = actual_time;
  
  adjtime_p->last_adj_time = actual_time;
  
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
  char newfile[506];   /* Stuff to write to disk file */
    /* snprintf is not always available, but this is safe
       as long as libc does not use more than 100 positions for %ld or %f 
       */

  int rc;   /* locally used: return code from a function */

  if (adjtime.dirty) {
    /* We'd use snprintf here, but apparently, it isn't always available. */
    sprintf(newfile, "%f %ld %f\n%ld\n%s\n",
            adjtime.drift_factor,
            (long) adjtime.last_adj_time,
            adjtime.not_adjusted,
            (long) adjtime.last_calib_time,
            (adjtime.local_utc == UTC) ? "UTC" : "LOCAL"
            );

    if (testing) {
      printf("Not updating adjtime file because of testing mode.\n");
      printf("Would have written the following to %s:\n%s", 
             ADJPATH, newfile);
    } else {
      FILE *adjfile;

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
          printf("Could not update file (%s) "
                 "with the clock adjustment parameters in it.\n"
                 "fprintf() returned errno %d: %s.\n"
                 "Drift adjustment parameters not updated.\n",
                 ADJPATH, fprintf_errno, strerror(errno));
        }
        rc = fclose(adjfile);
        if (rc < 0) {
          const int fclose_errno = errno;
          printf("Could not update file (%s) "
                 "with the clock adjustment parameters in it.\n"
                 "fclose() returned errno %d: %s.\n"
                 "Drift adjustment parameters not updated.\n",
                 ADJPATH, fclose_errno, strerror(errno));
        }
      }
    }
  } else if (debug)
    printf("Skipping update of adjtime file because nothing has changed.\n");
}



static void
do_adjustment(struct adjtime *adjtime_p,
              const bool hclock_valid, const time_t hclocktime, 
              const struct timeval read_time,
              const enum clock_access_method clock_access,
              const int dev_port, const bool universal, 
              const int hc_zero_year, 
              const bool badyear, const bool testing) {
/*---------------------------------------------------------------------------
  Do the adjustment requested, by 1) setting the Hardware Clock (if 
  necessary), and 2) updating the last-adjusted time in the adjtime
  structure.

  Do not update anything if the Hardware Clock does not currently present
  a valid time.

  arguments 'factor' and 'last_time' are current values from the adjtime
  file.

  'hclock_valid' means the Hardware Clock contains a valid time, and that
  time is 'hclocktime'.

  'read_time' is the current system time (to be precise, it is the system 
  time at the time 'hclocktime' was read, which due to computational delay
  could be a short time ago).

  'universal': the Hardware Clock is kept in UTC.

  'badyear': the Hardware Clock is incapable of storing years outside
  the range 1994-1999.

  'testing':  We are running in test mode (no updating of clock).

  We do not bother to update the clock if the adjustment would be less than
  one second.  This is to avoid cumulative error and needless CPU hogging
  (remember we use an infinite loop for some timing) if the user runs us
  frequently.

----------------------------------------------------------------------------*/
  if (!hclock_valid) {
    fprintf(stderr, "%s: The Hardware Clock does not contain a valid time, "
            "so we cannot adjust it.\n", MYNAME);
    /* Any previous calibration had to be before the clock got hosed, so
       wipe out the record of it so it won't be used in the future.
       */
    adjtime_p->last_calib_time = 0;  
    adjtime_p->last_adj_time = 0;
    adjtime_p->not_adjusted = 0;
    adjtime_p->dirty = TRUE;
  } else if (adjtime_p->last_adj_time == 0) {
    if (debug)
      printf("Not adjusting clock because we have no information about \n"
             "the previous calibration (i.e. the adjtime file is \n"
             "nonexistent or contains zero in the last calibrated time \n"
             "field).\n");
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
                               clock_access, dev_port, universal, 
                               hc_zero_year, badyear, testing);
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
                              const bool user_says_jensen,
                              enum clock_access_method *clock_access_p) {
/*----------------------------------------------------------------------------
  Figure out how we're going to access the hardware clock, by seeing
  what facilities are available, looking at invocation options, and 
  using compile-time constants.

  'user_requests_ISA' means the user explicitly asked for the ISA method,
  so we'll use that (even if we know it will fail because the machine
  is incapable!).
-----------------------------------------------------------------------------*/
  const bool jensen = 
    user_says_jensen || 
      (alpha_machine && is_in_cpuinfo("system type", "Jensen"));
    /* See comments at top of program for how Jensen is a special case. */
  bool rtc_works;
    /* The /dev/rtc method is available and seems to work on this machine */
  bool kdghwclk_works;
    /* The KDHWCLK method is available and seems to work on this machine. */

  see_if_rtc_works(&rtc_works);  /* May issue error messages */
  see_if_kdghwclk_works(&kdghwclk_works);  /* May issue error messages */
  
  if (user_requests_ISA) *clock_access_p = ISA;
  else if (rtc_works) *clock_access_p = RTC_IOCTL;
  else if (kdghwclk_works) *clock_access_p = KD;
  else if (got_kdghwclk) *clock_access_p = ISA;
    /* I don't know on what machine the above line makes any sense, but the
       code has always been this way.  -BJH 99.03.31
       */
  else if (isa_machine) *clock_access_p = ISA;
  else if (jensen) *clock_access_p = DEV_PORT;
  else if (alpha_machine) *clock_access_p = ISA;
  else *clock_access_p = NOCLOCK;
  if (debug) {
    switch (*clock_access_p) {
    case ISA: printf("Using direct I/O instructions to ISA clock.\n"); break;
    case KD: printf("Using KDGHWCLK interface to m68k clock.\n"); break;
    case RTC_IOCTL: printf("Using /dev/rtc interface to clock.\n"); break;
    case DEV_PORT: printf("Using /dev/port interface to clock.\n"); break;
    case NOCLOCK: printf("Unable to find a usable clock access method.\n"); 
      break;
    default:  
      printf("determine_clock_access_method() returned invalid value: %d.\n",
             *clock_access_p);
    }
  }
}



static void
do_systohc(const enum clock_access_method clock_access,
           const int dev_port, 
           const time_t hclocktime, const bool hclock_valid,
           const struct timeval read_time, 
           const bool universal, const int hc_zero_year, 
           const bool badyear, const bool testing,
           struct adjtime * const adjtime_p) {
/*----------------------------------------------------------------------------
   Perform the specifics of the hwclock --systohc function.
-----------------------------------------------------------------------------*/
  struct timeval nowtime, reftime;
  /* We can only set_hardware_clock_exact to a whole seconds
     time, so we set it with reference to the most recent
     whole seconds time.  
     */
  gettimeofday(&nowtime, NULL);
  reftime.tv_sec = nowtime.tv_sec;
  reftime.tv_usec = 0;
          
  set_hardware_clock_exact((time_t) reftime.tv_sec, reftime, 
                           clock_access, dev_port, universal, 
                           hc_zero_year, badyear, testing);
  adjust_drift_factor(adjtime_p, (time_t) reftime.tv_sec, hclock_valid, 
                      time_inc(t2tv(hclocktime),
                               - time_diff(read_time, reftime)
                               )
                      );
}


static void
manipulate_clock(const bool show, const bool adjust, 
                 const bool set, const time_t set_time,
                 const bool hctosys, const bool systohc, 
                 const struct timeval startup_time, 
                 const enum clock_access_method clock_access,
                 const bool utc, const bool local_opt, 
                 const bool badyear, const bool arc_opt, const bool srm_opt,
                 const bool user_wants_uf,
                 const bool testing,
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
  int dev_port;
    /* File descriptor for /dev/port, if we're using it.  -1 if we
       couldn't open it.  0 if we aren't using it.
       */
  get_inb_outb_privilege(clock_access, &no_auth);

  if (no_auth) *retcode_p = 1;
  else {
    get_dev_port_access(clock_access, &dev_port);
    
    if (dev_port < 0) *retcode_p = 3;
    else {
      read_adjtime(&adjtime, &rc);
      if (rc != 0) *retcode_p = 2;
      else {
        const bool use_uf_bit = uf_bit_needed(user_wants_uf);
        const int hc_zero_year = zero_year(arc_opt, srm_opt);
        /* year of century to which a value of zero corresponds in the
           Hardware Clock's year register. 
           */
        const bool universal = hw_clock_is_utc(utc, local_opt, adjtime);
        /* The hardware clock is kept in Coordinated Universal Time. */

        if ((set || systohc || adjust) &&
            (adjtime.local_utc == UTC) != universal) {
          adjtime.local_utc = universal ? UTC : LOCAL;
          adjtime.dirty = TRUE;
        }

        synchronize_to_clock_tick(clock_access, dev_port, use_uf_bit,
                                  retcode_p);  
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
          read_hardware_clock(clock_access, dev_port, universal, 
                              hc_zero_year, badyear, 
                              adjtime.last_calib_time,
                              &hclock_valid, &hclocktime); 
        
          if (show) {
            display_time(hclock_valid, hclocktime, 
                         time_diff(read_time, startup_time), !badyear);
            *retcode_p = 0;
          } else if (set) {
            set_hardware_clock_exact(set_time, startup_time, 
                                     clock_access, dev_port, universal, 
                                     hc_zero_year, 
                                     badyear, testing);
            adjust_drift_factor(&adjtime, set_time, hclock_valid, 
                                time_inc(t2tv(hclocktime), 
                                         - time_diff(read_time, startup_time)  
                                         )
                                );
            *retcode_p = 0;
          } else if (adjust) {
            do_adjustment(&adjtime, hclock_valid, hclocktime, 
                          read_time, clock_access, dev_port, 
                          universal, hc_zero_year, 
                          badyear, testing);
            *retcode_p = 0;
          } else if (systohc) {
            do_systohc(clock_access, dev_port, 
                       hclocktime, hclock_valid, read_time, 
                       universal, hc_zero_year, badyear, testing,
                       &adjtime);
            *retcode_p = 0;
          }  else if (hctosys) {
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
    if (clock_access == DEV_PORT && dev_port >= 0) close(dev_port);
  }
}



static void
report_version(void) {

  char *additional_version;   /* malloc'ed */
    /* Stuff to add on to the version report, after the basic version. 
       If this is hwclock packaged with util-linux, this is the 
       util-linux version.  Otherwise, it's nothing.
       */

#ifdef UTIL_LINUX
  additional_version = malloc(strlen(util_linux_version) + 5);
  sprintf(additional_version, "/%s", util_linux_version);
#else
  additional_version = strdup("");
#endif
  printf(MYNAME " " VERSION "%s\n", additional_version);
  free(additional_version);
}



static void
manipulate_epoch(const bool getepoch, const bool setepoch, 
                 const int epoch_opt, const bool testing) {
/*----------------------------------------------------------------------------
   Get or set the Hardware Clock epoch value in the kernel, as appropriate.
   'getepoch', 'setepoch', and 'epoch' are hwclock invocation options.

   'epoch' == -1 if the user did not specify an "epoch" option.

-----------------------------------------------------------------------------*/
  /*
   Maintenance note:  This should work on non-Alpha machines, but the 
   evidence today (98.03.04) indicates that the kernel only keeps the
   epoch value on Alphas.  If that is ever fixed, this function should be
   changed.
   */

  if (!alpha_machine)
    fprintf(stderr, 
            "%s: The kernel keeps an epoch value for the Hardware Clock "
            "only on an Alpha machine.\nThis copy of hwclock was built for "
            "a machine other than Alpha\n(and thus is presumably not running "
            "on an Alpha now).  No action taken.\n", MYNAME);
  else {
    if (getepoch) {
      unsigned long epoch;
      char *reason;  /* malloc'ed */

      get_epoch(&epoch, &reason);
      if (reason != NULL) {
        printf("Unable to get the epoch value from the kernel.  %s\n",
               reason);
        free(reason);
      } else 
        printf("Kernel is assuming an epoch value of %lu\n", epoch);
    } else if (setepoch) {
      if (epoch_opt == -1)
        fprintf(stderr, "%s: To set the epoch value, you must use the 'epoch' "
                "option to tell to what value to set it.\n", MYNAME);
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
  bool utc, local_opt, badyear, testing, directisa;
  bool arc_opt, jensen_opt, srm_opt, funky_opt;
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
    { 'u', (char *) "utc",       OPT_FLAG,   &utc,       0 },
    { 0,   (char *) "localtime", OPT_FLAG,   &local_opt, 0 },
    { 0,   (char *) "badyear",   OPT_FLAG,   &badyear,   0 },
    { 0,   (char *) "directisa", OPT_FLAG,   &directisa, 0 },
    { 0,   (char *) "test",      OPT_FLAG,   &testing,   0 },
    { 'D', (char *) "debug",     OPT_FLAG,   &debug,     0 },
    { 'A', (char *) "arc",       OPT_FLAG,   &arc_opt,   0 },
    { 'J', (char *) "jensen",    OPT_FLAG,   &jensen_opt,0 },
    { 'S', (char *) "srm",       OPT_FLAG,   &srm_opt,   0 },
    { 'F', (char *) "funky-toy", OPT_FLAG,   &funky_opt, 0 },
    { 0,   (char *) NULL,        OPT_END,    NULL,       0 }
  };
  int argc_parse;       /* argc, except we modify it as we parse */
  char **argv_parse;    /* argv, except we modify it as we parse */

  assume_interrupts_enabled();  /* Since we haven't messed with them yet */

  gettimeofday(&startup_time, NULL);  /* Remember what time we were invoked */

  /* set option defaults */
  show = set = systohc = hctosys = adjust = getepoch = setepoch = 
    version = utc = local_opt = badyear =
    directisa = testing = debug = 
    jensen_opt = arc_opt = srm_opt = funky_opt = FALSE;
  date_opt = NULL;
  epoch_opt = -1; 

  argc_parse = argc; argv_parse = argv;
  optParseOptions(&argc_parse, argv_parse, option_def, 0);
    /* Uses and sets argc_parse, argv_parse. 
       Sets show, set, systohc, hctosys, adjust, getepoch, setepoch,
       version, utc, localtime, badyear,
       directisa, testing, debug, 
       date_opt, epoch_opt,
       jensen_opt, arc_opt, srm_opt, funky_opt
       */
  
  if (argc_parse - 1 > 0) {
    fprintf(stderr, MYNAME " takes no non-option arguments.  "
            "You supplied %d.  See man page for complete syntax.\n",
            argc_parse - 1);
    exit(100);
  }

  if (show + set + systohc + hctosys + adjust + 
      getepoch + setepoch + version > 1) {
    fprintf(stderr, 
            "You have specified multiple function options to hwclock.\n"
            "You can only perform one function at a time.\n");
    exit(100);
  }

  if (set) {
    rc = interpret_date_string(date_opt, &set_time);  /* (time-consuming) */
    if (rc != 0) {
      fprintf(stderr, "%s: No usable set-to time given.  Cannot set clock.\n",
              MYNAME);
      exit(100);
    }
  }

  if (jensen_opt && !alpha_machine) {
    fprintf(stderr, "%s: Your options indicate that this is a Jensen model of "
            "DEC Alpha, but this is not an Alpha machine!\n", MYNAME);
    exit(100);
  }

  if (srm_opt && alpha_machine) {
    fprintf(stderr, "%s: Your options indicate that this machine keeps SRM "
            "console time, but only DEC Alphas have such a clock and this is "
            "not an Alpha!\n", MYNAME);
    exit(100);
  }
  if (arc_opt && alpha_machine) {
    fprintf(stderr, "%s: Your options indicate that this machine's clock"
            "keeps ARC console time, "
            "but only DEC Alphas have such a clock and this is "
            "not an Alpha!\n", MYNAME);
    exit(100);
  }

  if (directisa && !(isa_machine || alpha_machine)) {
    fprintf(stderr, "%s: You have requested direct access to the ISA Hardware "
            "Clock using machine instructions from the user process.  "
            "But this method only works on an ISA machine with an x86 "
            "CPU, or a similar machine such as DEC Alpha.  "
            "This is not one.\n", MYNAME);
    exit(100);
  }

  if (utc && local_opt) {
    fprintf(stderr, "%s: The --utc and --localtime options are mutually "
            "exclusive.  You specified both.\n", MYNAME);
    exit(100);
  }


  if (!(show | set | systohc | hctosys | adjust | getepoch | setepoch |
        version)) 
    show = TRUE; /* default to show */

  
  if (getuid() == 0) permitted = TRUE;
  else {
    /* program is designed to run setuid (in some situations) -- be secure! */
    if (set || hctosys || systohc || adjust) {
      fprintf(stderr, 
              "%s: Sorry, only the superuser can change the "
              "Hardware Clock.\n", MYNAME);
      permitted = FALSE;
    } else if (setepoch) {
      fprintf(stderr, 
              "%s: Sorry, only the superuser can change "
              "the Hardware Clock epoch in the kernel.\n", MYNAME);
      permitted = FALSE;
    } else permitted = TRUE;
  }

  if (!permitted) retcode = 2;
  else {
    retcode = 0;
    if (version) {
      report_version();
    } else if (getepoch || setepoch) {
      manipulate_epoch(getepoch, setepoch, epoch_opt, testing);
    } else {
      determine_clock_access_method(directisa, jensen_opt, &clock_access);
      if (clock_access == NOCLOCK)
        fprintf(stderr, "%s: Cannot access the Hardware Clock via any known "
                "method.  Use --debug option to see the details of our "
                "search for an access method.\n", MYNAME);
      else
        manipulate_clock(show, adjust, set, set_time, hctosys, systohc, 
                         startup_time, clock_access, utc, local_opt, badyear,
                         arc_opt, srm_opt, funky_opt, testing, &rc);
    }
  }
  exit(retcode);
}


/****************************************************************************

  History of this program:

  99.04.08 BJH   Version 2.5

  Make it work on Alphas without /dev/rtc.  Thanks to David Mosberger
  <davidm@azstarnet.com>, Jay Estabrook <jestabro@amt.tay1.dec.com>,
  Martin Ostermann <ost@coments.rwth-aachen.de>, Andries Brouwer
  <aeb@cwi.nl>.  Most of this code is lifted from another program
  called "clock" (not the original ancestor of hwclock) that has 
  circulated for use on Alpha.

  Make it work on Sparc.

  Add --badyear option.  Thanks to David J Coffin (dcoffin@shore.net)
  for the design of this.

  Add --localtime option, local/UTC value in adjtime file, and defaults
  for local/utc.

  Don't set CMOS memory Byte 50 (century byte).  On some machines,
  that byte not only isn't used as a century byte, but it is used for
  something else.

  Don't update the drift factor if the variation is so huge that it 
  probably wasn't due to drift.

  Compute drift factor with better precision.

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


