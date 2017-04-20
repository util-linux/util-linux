/*
 * hwclock.c
 *
 * clock.c was written by Charles Hedrick, hedrick@cs.rutgers.edu, Apr 1992
 * Modified for clock adjustments - Rob Hooft <hooft@chem.ruu.nl>, Nov 1992
 * Improvements by Harald Koenig <koenig@nova.tat.physik.uni-tuebingen.de>
 * and Alan Modra <alan@spri.levels.unisa.edu.au>.
 *
 * Major rewrite by Bryan Henderson <bryanh@giraffe-data.com>, 96.09.19.
 * The new program is called hwclock. New features:
 *
 *	- You can set the hardware clock without also modifying the system
 *	  clock.
 *	- You can read and set the clock with finer than 1 second precision.
 *	- When you set the clock, hwclock automatically refigures the drift
 *	  rate, based on how far off the clock was before you set it.
 *
 * Reshuffled things, added sparc code, and re-added alpha stuff
 * by David Mosberger <davidm@azstarnet.com>
 * and Jay Estabrook <jestabro@amt.tay1.dec.com>
 * and Martin Ostermann <ost@coments.rwth-aachen.de>, aeb@cwi.nl, 990212.
 *
 * Fix for Award 2094 bug, Dave Coffin (dcoffin@shore.net) 11/12/98
 * Change of local time handling, Stefan Ring <e9725446@stud3.tuwien.ac.at>
 * Change of adjtime handling, James P. Rutledge <ao112@rgfn.epcc.edu>.
 *
 * Distributed under GPL
 */
/*
 * Explanation of `adjusting' (Rob Hooft):
 *
 * The problem with my machine is that its CMOS clock is 10 seconds
 * per day slow. With this version of clock.c, and my '/etc/rc.local'
 * reading '/etc/clock -au' instead of '/etc/clock -u -s', this error
 * is automatically corrected at every boot.
 *
 * To do this job, the program reads and writes the file '/etc/adjtime'
 * to determine the correction, and to save its data. In this file are
 * three numbers:
 *
 *	1) the correction in seconds per day. (So if your clock runs 5
 *	   seconds per day fast, the first number should read -5.0)
 *	2) the number of seconds since 1/1/1970 the last time the program
 *	   was used
 *	3) the remaining part of a second which was leftover after the last
 *	   adjustment
 *
 * Installation and use of this program:
 *
 *	a) create a file '/etc/adjtime' containing as the first and only
 *	   line: '0.0 0 0.0'
 *	b) run 'clock -au' or 'clock -a', depending on whether your cmos is
 *	   in universal or local time. This updates the second number.
 *	c) set your system time using the 'date' command.
 *	d) update your cmos time using 'clock -wu' or 'clock -w'
 *	e) replace the first number in /etc/adjtime by your correction.
 *	f) put the command 'clock -au' or 'clock -a' in your '/etc/rc.local'
 */

#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define OPTUTILS_EXIT_CODE EX_USAGE

#include "c.h"
#include "closestream.h"
#include "nls.h"
#include "optutils.h"
#include "pathnames.h"
#include "strutils.h"
#include "hwclock.h"
#include "timeutils.h"
#include "env.h"
#include "xalloc.h"

#ifdef HAVE_LIBAUDIT
#include <libaudit.h>
static int hwaudit_fd = -1;
#endif

/* The struct that holds our hardware access routines */
static struct clock_ops *ur;

/* Maximal clock adjustment in seconds per day.
   (adjtime() glibc call has 2145 seconds limit on i386, so it is good enough for us as well,
   43219 is a maximal safe value preventing exact_adjustment overflow.) */
#define MAX_DRIFT 2145.0

struct adjtime {
	/*
	 * This is information we keep in the adjtime file that tells us how
	 * to do drift corrections. Elements are all straight from the
	 * adjtime file, so see documentation of that file for details.
	 * Exception is <dirty>, which is an indication that what's in this
	 * structure is not what's in the disk file (because it has been
	 * updated since read from the disk file).
	 */
	bool dirty;
	/* line 1 */
	double drift_factor;
	time_t last_adj_time;
	double not_adjusted;
	/* line 2 */
	time_t last_calib_time;
	/*
	 * The most recent time that we set the clock from an external
	 * authority (as opposed to just doing a drift adjustment)
	 */
	/* line 3 */
	enum a_local_utc { UTC = 0, LOCAL, UNKNOWN } local_utc;
	/*
	 * To which time zone, local or UTC, we most recently set the
	 * hardware clock.
	 */
};

/*
 * time_t to timeval conversion.
 */
static struct timeval t2tv(time_t timet)
{
	struct timeval rettimeval;

	rettimeval.tv_sec = timet;
	rettimeval.tv_usec = 0;
	return rettimeval;
}

/*
 * The difference in seconds between two times in "timeval" format.
 */
double time_diff(struct timeval subtrahend, struct timeval subtractor)
{
	return (subtrahend.tv_sec - subtractor.tv_sec)
	    + (subtrahend.tv_usec - subtractor.tv_usec) / 1E6;
}

/*
 * The time, in "timeval" format, which is <increment> seconds after the
 * time <addend>. Of course, <increment> may be negative.
 */
static struct timeval time_inc(struct timeval addend, double increment)
{
	struct timeval newtime;

	newtime.tv_sec = addend.tv_sec + (int)increment;
	newtime.tv_usec = addend.tv_usec + (increment - (int)increment) * 1E6;

	/*
	 * Now adjust it so that the microsecond value is between 0 and 1
	 * million.
	 */
	if (newtime.tv_usec < 0) {
		newtime.tv_usec += 1E6;
		newtime.tv_sec -= 1;
	} else if (newtime.tv_usec >= 1E6) {
		newtime.tv_usec -= 1E6;
		newtime.tv_sec += 1;
	}
	return newtime;
}

static bool
hw_clock_is_utc(const struct hwclock_control *ctl,
		const struct adjtime adjtime)
{
	bool ret;

	if (ctl->utc)
		ret = TRUE;	/* --utc explicitly given on command line */
	else if (ctl->local_opt)
		ret = FALSE;	/* --localtime explicitly given */
	else
		/* get info from adjtime file - default is UTC */
		ret = (adjtime.local_utc != LOCAL);
	if (ctl->debug)
		printf(_("Assuming hardware clock is kept in %s time.\n"),
		       ret ? _("UTC") : _("local"));
	return ret;
}

/*
 * Read the adjustment parameters out of the /etc/adjtime file.
 *
 * Return them as the adjtime structure <*adjtime_p>. If there is no
 * /etc/adjtime file, return defaults. If values are missing from the file,
 * return defaults for them.
 *
 * return value 0 if all OK, !=0 otherwise.
 */
static int read_adjtime(const struct hwclock_control *ctl,
			struct adjtime *adjtime_p)
{
	FILE *adjfile;
	char line1[81];		/* String: first line of adjtime file */
	char line2[81];		/* String: second line of adjtime file */
	char line3[81];		/* String: third line of adjtime file */

	if (access(ctl->adj_file_name, R_OK) != 0)
		return 0;

	adjfile = fopen(ctl->adj_file_name, "r");	/* open file for reading */
	if (adjfile == NULL) {
		warn(_("cannot open %s"), ctl->adj_file_name);
		return EX_OSFILE;
	}

	if (!fgets(line1, sizeof(line1), adjfile))
		line1[0] = '\0';	/* In case fgets fails */
	if (!fgets(line2, sizeof(line2), adjfile))
		line2[0] = '\0';	/* In case fgets fails */
	if (!fgets(line3, sizeof(line3), adjfile))
		line3[0] = '\0';	/* In case fgets fails */

	fclose(adjfile);

	sscanf(line1, "%lf %ld %lf",
	       &adjtime_p->drift_factor,
	       &adjtime_p->last_adj_time,
	       &adjtime_p->not_adjusted);

	sscanf(line2, "%ld", &adjtime_p->last_calib_time);

	if (!strcmp(line3, "UTC\n")) {
		adjtime_p->local_utc = UTC;
	} else if (!strcmp(line3, "LOCAL\n")) {
		adjtime_p->local_utc = LOCAL;
	} else {
		adjtime_p->local_utc = UNKNOWN;
		if (line3[0]) {
			warnx(_("Warning: unrecognized third line in adjtime file\n"
				"(Expected: `UTC' or `LOCAL' or nothing.)"));
		}
	}

	if (ctl->debug) {
		printf(_
		       ("Last drift adjustment done at %ld seconds after 1969\n"),
		       (long)adjtime_p->last_adj_time);
		printf(_("Last calibration done at %ld seconds after 1969\n"),
		       (long)adjtime_p->last_calib_time);
		printf(_("Hardware clock is on %s time\n"),
		       (adjtime_p->local_utc ==
			LOCAL) ? _("local") : (adjtime_p->local_utc ==
					       UTC) ? _("UTC") : _("unknown"));
	}

	return 0;
}

/*
 * Wait until the falling edge of the Hardware Clock's update flag so that
 * any time that is read from the clock immediately after we return will be
 * exact.
 *
 * The clock only has 1 second precision, so it gives the exact time only
 * once per second, right on the falling edge of the update flag.
 *
 * We wait (up to one second) either blocked waiting for an rtc device or in
 * a CPU spin loop. The former is probably not very accurate.
 *
 * Return 0 if it worked, nonzero if it didn't.
 */
static int synchronize_to_clock_tick(const struct hwclock_control *ctl)
{
	int rc;

	if (ctl->debug)
		printf(_("Waiting for clock tick...\n"));

	rc = ur->synchronize_to_clock_tick(ctl);

	if (ctl->debug) {
		if (rc)
			printf(_("...synchronization failed\n"));
		else
			printf(_("...got clock tick\n"));
	}

	return rc;
}

/*
 * Convert a time in broken down format (hours, minutes, etc.) into standard
 * unix time (seconds into epoch). Return it as *systime_p.
 *
 * The broken down time is argument <tm>. This broken down time is either
 * in local time zone or UTC, depending on value of logical argument
 * "universal". True means it is in UTC.
 *
 * If the argument contains values that do not constitute a valid time, and
 * mktime() recognizes this, return *valid_p == false and *systime_p
 * undefined. However, mktime() sometimes goes ahead and computes a
 * fictional time "as if" the input values were valid, e.g. if they indicate
 * the 31st day of April, mktime() may compute the time of May 1. In such a
 * case, we return the same fictional value mktime() does as *systime_p and
 * return *valid_p == true.
 */
static void
mktime_tz(const struct hwclock_control *ctl, struct tm tm,
	  bool *valid_p, time_t *systime_p)
{
	if (ctl->universal)
		*systime_p = timegm(&tm);
	else
		*systime_p = mktime(&tm);
	if (*systime_p == -1) {
		/*
		 * This apparently (not specified in mktime() documentation)
		 * means the 'tm' structure does not contain valid values
		 * (however, not containing valid values does _not_ imply
		 * mktime() returns -1).
		 */
		*valid_p = FALSE;
		if (ctl->debug)
			printf(_("Invalid values in hardware clock: "
				 "%4d/%.2d/%.2d %.2d:%.2d:%.2d\n"),
			       tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
			       tm.tm_hour, tm.tm_min, tm.tm_sec);
	} else {
		*valid_p = TRUE;
		if (ctl->debug)
			printf(_
			       ("Hw clock time : %4d/%.2d/%.2d %.2d:%.2d:%.2d = "
				"%ld seconds since 1969\n"), tm.tm_year + 1900,
			       tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min,
			       tm.tm_sec, (long)*systime_p);
	}
}

/*
 * Read the hardware clock and return the current time via <tm> argument.
 *
 * Use the method indicated by <method> argument to access the hardware
 * clock.
 */
static int
read_hardware_clock(const struct hwclock_control *ctl,
		    bool * valid_p, time_t *systime_p)
{
	struct tm tm;
	int err;

	err = ur->read_hardware_clock(ctl, &tm);
	if (err)
		return err;

	if (ctl->debug)
		printf(_
		       ("Time read from Hardware Clock: %4d/%.2d/%.2d %02d:%02d:%02d\n"),
		       tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour,
		       tm.tm_min, tm.tm_sec);
	mktime_tz(ctl, tm, valid_p, systime_p);

	return 0;
}

/*
 * Set the Hardware Clock to the time <newtime>, in local time zone or UTC,
 * according to <universal>.
 */
static void
set_hardware_clock(const struct hwclock_control *ctl, const time_t newtime)
{
	struct tm new_broken_time;
	/*
	 * Time to which we will set Hardware Clock, in broken down format,
	 * in the time zone of caller's choice
	 */

	if (ctl->universal)
		new_broken_time = *gmtime(&newtime);
	else
		new_broken_time = *localtime(&newtime);

	if (ctl->debug)
		printf(_("Setting Hardware Clock to %.2d:%.2d:%.2d "
			 "= %ld seconds since 1969\n"),
		       new_broken_time.tm_hour, new_broken_time.tm_min,
		       new_broken_time.tm_sec, (long)newtime);

	if (ctl->testing)
		printf(_("Test mode: clock was not changed\n"));
	else
		ur->set_hardware_clock(ctl, &new_broken_time);
}

/*
 * Set the Hardware Clock to the time "sethwtime", in local time zone or
 * UTC, according to "universal".
 *
 * Wait for a fraction of a second so that "sethwtime" is the value of the
 * Hardware Clock as of system time "refsystime", which is in the past. For
 * example, if "sethwtime" is 14:03:05 and "refsystime" is 12:10:04.5 and
 * the current system time is 12:10:06.0: Wait .5 seconds (to make exactly 2
 * seconds since "refsystime") and then set the Hardware Clock to 14:03:07,
 * thus getting a precise and retroactive setting of the clock.
 *
 * (Don't be confused by the fact that the system clock and the Hardware
 * Clock differ by two hours in the above example. That's just to remind you
 * that there are two independent time scales here).
 *
 * This function ought to be able to accept set times as fractional times.
 * Idea for future enhancement.
 */
static void
set_hardware_clock_exact(const struct hwclock_control *ctl,
			 const time_t sethwtime,
			 const struct timeval refsystime)
{
	/*
	 * The Hardware Clock can only be set to any integer time plus one
	 * half second.	 The integer time is required because there is no
	 * interface to set or get a fractional second.	 The additional half
	 * second is because the Hardware Clock updates to the following
	 * second precisely 500 ms (not 1 second!) after you release the
	 * divider reset (after setting the new time) - see description of
	 * DV2, DV1, DV0 in Register A in the MC146818A data sheet (and note
	 * that although that document doesn't say so, real-world code seems
	 * to expect that the SET bit in Register B functions the same way).
	 * That means that, e.g., when you set the clock to 1:02:03, it
	 * effectively really sets it to 1:02:03.5, because it will update to
	 * 1:02:04 only half a second later.  Our caller passes the desired
	 * integer Hardware Clock time in sethwtime, and the corresponding
	 * system time (which may have a fractional part, and which may or may
	 * not be the same!) in refsystime.  In an ideal situation, we would
	 * then apply sethwtime to the Hardware Clock at refsystime+500ms, so
	 * that when the Hardware Clock ticks forward to sethwtime+1s half a
	 * second later at refsystime+1000ms, everything is in sync.  So we
	 * spin, waiting for gettimeofday() to return a time at or after that
	 * time (refsystime+500ms) up to a tolerance value, initially 1ms.  If
	 * we miss that time due to being preempted for some other process,
	 * then we increase the margin a little bit (initially 1ms, doubling
	 * each time), add 1 second (or more, if needed to get a time that is
	 * in the future) to both the time for which we are waiting and the
	 * time that we will apply to the Hardware Clock, and start waiting
	 * again.
	 * 
	 * For example, the caller requests that we set the Hardware Clock to
	 * 1:02:03, with reference time (current system time) = 6:07:08.250.
	 * We want the Hardware Clock to update to 1:02:04 at 6:07:09.250 on
	 * the system clock, and the first such update will occur 0.500
	 * seconds after we write to the Hardware Clock, so we spin until the
	 * system clock reads 6:07:08.750.  If we get there, great, but let's
	 * imagine the system is so heavily loaded that our process is
	 * preempted and by the time we get to run again, the system clock
	 * reads 6:07:11.990.  We now want to wait until the next xx:xx:xx.750
	 * time, which is 6:07:12.750 (4.5 seconds after the reference time),
	 * at which point we will set the Hardware Clock to 1:02:07 (4 seconds
	 * after the originally requested time).  If we do that successfully,
	 * then at 6:07:13.250 (5 seconds after the reference time), the
	 * Hardware Clock will update to 1:02:08 (5 seconds after the
	 * originally requested time), and all is well thereafter.
	 */

	time_t newhwtime = sethwtime;
	double target_time_tolerance_secs = 0.001;  /* initial value */
	double tolerance_incr_secs = 0.001;	    /* initial value */
	const double RTC_SET_DELAY_SECS = 0.5;	    /* 500 ms */
	const struct timeval RTC_SET_DELAY_TV = { 0, RTC_SET_DELAY_SECS * 1E6 };

	struct timeval targetsystime;
	struct timeval nowsystime;
	struct timeval prevsystime = refsystime;
	double deltavstarget;

	timeradd(&refsystime, &RTC_SET_DELAY_TV, &targetsystime);

	while (1) {
		double ticksize;

		/* FOR TESTING ONLY: inject random delays of up to 1000ms */
		if (ctl->debug >= 10) {
			int usec = random() % 1000000;
			printf(_("sleeping ~%d usec\n"), usec);
			xusleep(usec);
		}

		gettimeofday(&nowsystime, NULL);
		deltavstarget = time_diff(nowsystime, targetsystime);
		ticksize = time_diff(nowsystime, prevsystime);
		prevsystime = nowsystime;

		if (ticksize < 0) {
			if (ctl->debug)
				printf(_("time jumped backward %.6f seconds "
					 "to %ld.%06ld - retargeting\n"),
				       ticksize, nowsystime.tv_sec,
				       nowsystime.tv_usec);
			/* The retarget is handled at the end of the loop. */
		} else if (deltavstarget < 0) {
			/* deltavstarget < 0 if current time < target time */
			if (ctl->debug >= 2)
				printf(_("%ld.%06ld < %ld.%06ld (%.6f)\n"),
				       nowsystime.tv_sec,
				       nowsystime.tv_usec,
				       targetsystime.tv_sec,
				       targetsystime.tv_usec,
				       deltavstarget);
			continue;  /* not there yet - keep spinning */
		} else if (deltavstarget <= target_time_tolerance_secs) {
			/* Close enough to the target time; done waiting. */
			break;
		} else /* (deltavstarget > target_time_tolerance_secs) */ {
			/*
			 * We missed our window.  Increase the tolerance and
			 * aim for the next opportunity.
			 */
			if (ctl->debug)
				printf(_("missed it - %ld.%06ld is too far "
					 "past %ld.%06ld (%.6f > %.6f)\n"),
				       nowsystime.tv_sec,
				       nowsystime.tv_usec,
				       targetsystime.tv_sec,
				       targetsystime.tv_usec,
				       deltavstarget,
				       target_time_tolerance_secs);
			target_time_tolerance_secs += tolerance_incr_secs;
			tolerance_incr_secs *= 2;
		}

		/*
		 * Aim for the same offset (tv_usec) within the second in
		 * either the current second (if that offset hasn't arrived
		 * yet), or the next second.
		 */
		if (nowsystime.tv_usec < targetsystime.tv_usec)
			targetsystime.tv_sec = nowsystime.tv_sec;
		else
			targetsystime.tv_sec = nowsystime.tv_sec + 1;
	}

	newhwtime = sethwtime
		    + (int)(time_diff(nowsystime, refsystime)
			    - RTC_SET_DELAY_SECS /* don't count this */
			    + 0.5 /* for rounding */);
	if (ctl->debug)
		printf(_("%ld.%06ld is close enough to %ld.%06ld (%.6f < %.6f)\n"
			 "Set RTC to %ld (%ld + %d; refsystime = %ld.%06ld)\n"),
		       nowsystime.tv_sec, nowsystime.tv_usec,
		       targetsystime.tv_sec, targetsystime.tv_usec,
		       deltavstarget, target_time_tolerance_secs,
		       newhwtime, sethwtime,
		       (int)(newhwtime - sethwtime),
		       refsystime.tv_sec, refsystime.tv_usec);

	set_hardware_clock(ctl, newhwtime);
}

/*
 * Put the time "hwctime" on standard output in display format. Except if
 * hclock_valid == false, just tell standard output that we don't know what
 * time it is.
 */
static void
display_time(const bool hclock_valid, struct timeval hwctime)
{
	if (!hclock_valid)
		warnx(_
		      ("The Hardware Clock registers contain values that are "
		       "either invalid (e.g. 50th day of month) or beyond the range "
		       "we can handle (e.g. Year 2095)."));
	else {
		char buf[ISO_8601_BUFSIZ];

		strtimeval_iso(&hwctime, ISO_8601_DATE|ISO_8601_TIME|ISO_8601_DOTUSEC|
					 ISO_8601_TIMEZONE|ISO_8601_SPACE,
					 buf, sizeof(buf));
		printf("%s\n", buf);
	}
}

/*
 * Set the System Clock to time 'newtime'.
 *
 * Also set the kernel time zone value to the value indicated by the TZ
 * environment variable and/or /usr/lib/zoneinfo/, interpreted as tzset()
 * would interpret them.
 *
 * If this is the first call of settimeofday since boot, then this also sets
 * the kernel variable persistent_clock_is_local so that NTP 11 minute mode
 * will update the Hardware Clock with the proper timescale. If the Hardware
 * Clock's timescale configuration is changed then a reboot is required for
 * persistent_clock_is_local to be updated.
 *
 * EXCEPT: if hclock_valid is false, just issue an error message saying
 * there is no valid time in the Hardware Clock to which to set the system
 * time.
 *
 * If 'testing' is true, don't actually update anything -- just say we would
 * have.
 */
static int
set_system_clock(const struct hwclock_control *ctl, const bool hclock_valid,
		 const struct timeval newtime)
{
	int retcode;

	if (!hclock_valid) {
		warnx(_
		      ("The Hardware Clock does not contain a valid time, so "
		       "we cannot set the System Time from it."));
		retcode = 1;
	} else {
		const struct timeval *tv_null = NULL;
		struct tm *broken;
		int minuteswest;
		int rc = 0;

		broken = localtime(&newtime.tv_sec);
#ifdef HAVE_TM_GMTOFF
		minuteswest = -broken->tm_gmtoff / 60;	/* GNU extension */
#else
		minuteswest = timezone / 60;
		if (broken->tm_isdst)
			minuteswest -= 60;
#endif

		if (ctl->debug) {
			printf(_("Calling settimeofday:\n"));
			printf(_("\ttv.tv_sec = %ld, tv.tv_usec = %ld\n"),
			       newtime.tv_sec, newtime.tv_usec);
			printf(_("\ttz.tz_minuteswest = %d\n"), minuteswest);
		}
		if (ctl->testing) {
			printf(_
			       ("Test mode: clock was not changed\n"));
			retcode = 0;
		} else {
			const struct timezone tz = { minuteswest, 0 };

			/* Set kernel persistent_clock_is_local so that 11 minute
			 * mode does not clobber the Hardware Clock with UTC. This
			 * is only available on first call of settimeofday after boot.
			 */
			if (!ctl->universal)
				rc = settimeofday(tv_null, &tz);
			if (!rc)
				rc = settimeofday(&newtime, &tz);
			if (rc) {
				if (errno == EPERM) {
					warnx(_
					      ("Must be superuser to set system clock."));
					retcode = EX_NOPERM;
				} else {
					warn(_("settimeofday() failed"));
					retcode = 1;
				}
			} else
				retcode = 0;
		}
	}
	return retcode;
}

/*
 * Reset the System Clock from local time to UTC, based on its current value
 * and the timezone unless universal is TRUE.
 *
 * Also set the kernel time zone value to the value indicated by the TZ
 * environment variable and/or /usr/lib/zoneinfo/, interpreted as tzset()
 * would interpret them.
 *
 * If 'testing' is true, don't actually update anything -- just say we would
 * have.
 */
static int set_system_clock_timezone(const struct hwclock_control *ctl)
{
	int retcode;
	struct timeval tv;
	struct tm *broken;
	int minuteswest;

	gettimeofday(&tv, NULL);
	if (ctl->debug) {
		struct tm broken_time;
		char ctime_now[200];

		broken_time = *gmtime(&tv.tv_sec);
		strftime(ctime_now, sizeof(ctime_now), "%Y/%m/%d %H:%M:%S",
			 &broken_time);
		printf(_("Current system time: %ld = %s\n"), tv.tv_sec,
		       ctime_now);
	}

	broken = localtime(&tv.tv_sec);
#ifdef HAVE_TM_GMTOFF
	minuteswest = -broken->tm_gmtoff / 60;	/* GNU extension */
#else
	minuteswest = timezone / 60;
	if (broken->tm_isdst)
		minuteswest -= 60;
#endif

	if (ctl->debug) {
		struct tm broken_time;
		char ctime_now[200];

		gettimeofday(&tv, NULL);
		if (!ctl->universal)
			tv.tv_sec += minuteswest * 60;

		broken_time = *gmtime(&tv.tv_sec);
		strftime(ctime_now, sizeof(ctime_now), "%Y/%m/%d %H:%M:%S",
			 &broken_time);

		printf(_("Calling settimeofday:\n"));
		printf(_("\tUTC: %s\n"), ctime_now);
		printf(_("\ttv.tv_sec = %ld, tv.tv_usec = %ld\n"),
		       tv.tv_sec, tv.tv_usec);
		printf(_("\ttz.tz_minuteswest = %d\n"), minuteswest);
	}
	if (ctl->testing) {
		printf(_
		       ("Test mode: clock was not changed\n"));
		retcode = 0;
	} else {
		const struct timezone tz_utc = { 0, 0 };
		const struct timezone tz = { minuteswest, 0 };
		const struct timeval *tv_null = NULL;
		int rc = 0;

		/* The first call to settimeofday after boot will assume the systemtime
		 * is in localtime, and adjust it according to the given timezone to
		 * compensate. If the systemtime is in fact in UTC, then this is wrong
		 * so we first do a dummy call to make sure the time is not shifted.
		 */
		if (ctl->universal)
			rc = settimeofday(tv_null, &tz_utc);

		/* Now we set the real timezone. Due to the above dummy call, this will
		 * only warp the systemtime if the RTC is not in UTC. */
		if (!rc)
			rc = settimeofday(tv_null, &tz);

		if (rc) {
			if (errno == EPERM) {
				warnx(_
				      ("Must be superuser to set system clock."));
				retcode = EX_NOPERM;
			} else {
				warn(_("settimeofday() failed"));
				retcode = 1;
			}
		} else
			retcode = 0;
	}
	return retcode;
}

/*
 * Refresh the last calibrated and last adjusted timestamps in <*adjtime_p>
 * to facilitate future drift calculations based on this set point.
 *
 * With the --update-drift option:
 * Update the drift factor in <*adjtime_p> based on the fact that the
 * Hardware Clock was just calibrated to <nowtime> and before that was
 * set to the <hclocktime> time scale.
 *
 * EXCEPT: if <hclock_valid> is false, assume Hardware Clock was not set
 * before to anything meaningful and regular adjustments have not been done,
 * so don't adjust the drift factor.
 */
static void
adjust_drift_factor(const struct hwclock_control *ctl,
		    struct adjtime *adjtime_p,
		    const struct timeval nowtime,
		    const bool hclock_valid,
		    const struct timeval hclocktime)
{
	if (!ctl->update) {
		if (ctl->debug)
			printf(_("Not adjusting drift factor because the "
				 "--update-drift option was not used.\n"));
	} else if (!hclock_valid) {
		if (ctl->debug)
			printf(_("Not adjusting drift factor because the "
				 "Hardware Clock previously contained "
				 "garbage.\n"));
	} else if (adjtime_p->last_calib_time == 0) {
		if (ctl->debug)
			printf(_("Not adjusting drift factor because last "
				 "calibration time is zero,\n"
				 "so history is bad and calibration startover "
				 "is necessary.\n"));
	} else if ((hclocktime.tv_sec - adjtime_p->last_calib_time) < 4 * 60 * 60) {
		if (ctl->debug)
			printf(_("Not adjusting drift factor because it has "
				 "been less than four hours since the last "
				 "calibration.\n"));
	} else {
		/*
		 * At adjustment time we drift correct the hardware clock
		 * according to the contents of the adjtime file and refresh
		 * its last adjusted timestamp.
		 *
		 * At calibration time we set the Hardware Clock and refresh
		 * both timestamps in <*adjtime_p>.
		 *
		 * Here, with the --update-drift option, we also update the
		 * drift factor in <*adjtime_p>.
		 *
		 * Let us do computation in doubles. (Floats almost suffice,
		 * but 195 days + 1 second equals 195 days in floats.)
		 */
		const double sec_per_day = 24.0 * 60.0 * 60.0;
		double factor_adjust;
		double drift_factor;
		struct timeval last_calib;

		last_calib = t2tv(adjtime_p->last_calib_time);
		/*
		 * Correction to apply to the current drift factor.
		 *
		 * Simplified: uncorrected_drift / days_since_calibration.
		 *
		 * hclocktime is fully corrected with the current drift factor.
		 * Its difference from nowtime is the missed drift correction.
		 */
		factor_adjust = time_diff(nowtime, hclocktime) /
				(time_diff(nowtime, last_calib) / sec_per_day);

		drift_factor = adjtime_p->drift_factor + factor_adjust;
		if (fabs(drift_factor) > MAX_DRIFT) {
			if (ctl->debug)
				printf(_("Clock drift factor was calculated as "
					 "%f seconds/day.\n"
					 "It is far too much. Resetting to zero.\n"),
				       drift_factor);
			drift_factor = 0;
		} else {
			if (ctl->debug)
				printf(_("Clock drifted %f seconds in the past "
					 "%f seconds\nin spite of a drift factor of "
					 "%f seconds/day.\n"
					 "Adjusting drift factor by %f seconds/day\n"),
				       time_diff(nowtime, hclocktime),
				       time_diff(nowtime, last_calib),
				       adjtime_p->drift_factor, factor_adjust);
		}

		adjtime_p->drift_factor = drift_factor;
	}
	adjtime_p->last_calib_time = nowtime.tv_sec;

	adjtime_p->last_adj_time = nowtime.tv_sec;

	adjtime_p->not_adjusted = 0;

	adjtime_p->dirty = TRUE;
}

/*
 * Calculate the drift correction currently needed for the
 * Hardware Clock based on the last time it was adjusted,
 * and the current drift factor, as stored in the adjtime file.
 *
 * The total drift adjustment needed is stored at tdrift_p.
 *
 */
static void
calculate_adjustment(const struct hwclock_control *ctl,
		     const double factor,
		     const time_t last_time,
		     const double not_adjusted,
		     const time_t systime, struct timeval *tdrift_p)
{
	double exact_adjustment;

	exact_adjustment =
	    ((double)(systime - last_time)) * factor / (24 * 60 * 60)
	    + not_adjusted;
	tdrift_p->tv_sec = (time_t) floor(exact_adjustment);
	tdrift_p->tv_usec = (exact_adjustment -
				 (double)tdrift_p->tv_sec) * 1E6;
	if (ctl->debug) {
		printf(P_("Time since last adjustment is %ld second\n",
			"Time since last adjustment is %ld seconds\n",
		       (systime - last_time)),
		       (systime - last_time));
		printf(_("Calculated Hardware Clock drift is %ld.%06ld seconds\n"),
		       tdrift_p->tv_sec, tdrift_p->tv_usec);
	}
}

/*
 * Write the contents of the <adjtime> structure to its disk file.
 *
 * But if the contents are clean (unchanged since read from disk), don't
 * bother.
 */
static void save_adjtime(const struct hwclock_control *ctl,
			 const struct adjtime *adjtime)
{
	char *content;		/* Stuff to write to disk file */
	FILE *fp;
	int err = 0;

	if (!adjtime->dirty)
		return;

	xasprintf(&content, "%f %ld %f\n%ld\n%s\n",
		  adjtime->drift_factor,
		  adjtime->last_adj_time,
		  adjtime->not_adjusted,
		  adjtime->last_calib_time,
		  (adjtime->local_utc == LOCAL) ? "LOCAL" : "UTC");

	if (ctl->testing) {
		if (ctl->debug){
			printf(_("Test mode: %s was not updated with:\n%s"),
			       ctl->adj_file_name, content);
		}
		free(content);
		return;
	}

	fp = fopen(ctl->adj_file_name, "w");
	if (fp == NULL) {
		warn(_("Could not open file with the clock adjustment parameters "
		       "in it (%s) for writing"), ctl->adj_file_name);
		err = 1;
	} else if (fputs(content, fp) < 0 || close_stream(fp) != 0) {
		warn(_("Could not update file with the clock adjustment "
		       "parameters (%s) in it"), ctl->adj_file_name);
		err = 1;
	}
	free(content);
	if (err)
		warnx(_("Drift adjustment parameters not updated."));
}

/*
 * Do the adjustment requested, by 1) setting the Hardware Clock (if
 * necessary), and 2) updating the last-adjusted time in the adjtime
 * structure.
 *
 * Do not update anything if the Hardware Clock does not currently present a
 * valid time.
 *
 * <hclock_valid> means the Hardware Clock contains a valid time.
 *
 * <hclocktime> is the drift corrected time read from the Hardware Clock.
 *
 * <read_time> was the system time when the <hclocktime> was read, which due
 * to computational delay could be a short time ago. It is used to define a
 * trigger point for setting the Hardware Clock. The fractional part of the
 * Hardware clock set time is subtracted from read_time to 'refer back', or
 * delay, the trigger point.  Fractional parts must be accounted for in this
 * way, because the Hardware Clock can only be set to a whole second.
 *
 * <universal>: the Hardware Clock is kept in UTC.
 *
 * <testing>:  We are running in test mode (no updating of clock).
 *
 */
static void
do_adjustment(const struct hwclock_control *ctl, struct adjtime *adjtime_p,
	      const bool hclock_valid, const struct timeval hclocktime,
	      const struct timeval read_time)
{
	if (!hclock_valid) {
		warnx(_("The Hardware Clock does not contain a valid time, "
			"so we cannot adjust it."));
		adjtime_p->last_calib_time = 0;	/* calibration startover is required */
		adjtime_p->last_adj_time = 0;
		adjtime_p->not_adjusted = 0;
		adjtime_p->dirty = TRUE;
	} else if (adjtime_p->last_adj_time == 0) {
		if (ctl->debug)
			printf(_("Not setting clock because last adjustment time is zero, "
				 "so history is bad.\n"));
	} else if (fabs(adjtime_p->drift_factor) > MAX_DRIFT) {
		if (ctl->debug)
			printf(_("Not setting clock because drift factor %f is far too high.\n"),
				adjtime_p->drift_factor);
	} else {
		set_hardware_clock_exact(ctl, hclocktime.tv_sec,
					 time_inc(read_time,
						  -(hclocktime.tv_usec / 1E6)));
		adjtime_p->last_adj_time = hclocktime.tv_sec;
		adjtime_p->not_adjusted = 0;
		adjtime_p->dirty = TRUE;
	}
}

static void determine_clock_access_method(const struct hwclock_control *ctl)
{
	ur = NULL;

	if (ctl->directisa)
		ur = probe_for_cmos_clock();
#ifdef __linux__
	if (!ur)
		ur = probe_for_rtc_clock(ctl);
#endif
	if (ur) {
		if (ctl->debug)
			puts(ur->interface_name);

	} else {
		if (ctl->debug)
			printf(_("No usable clock interface found.\n"));
		warnx(_("Cannot access the Hardware Clock via "
			"any known method."));
		if (!ctl->debug)
			warnx(_("Use the --debug option to see the "
				"details of our search for an access "
				"method."));
		hwclock_exit(ctl, EX_SOFTWARE);
	}
}

/*
 * Do all the normal work of hwclock - read, set clock, etc.
 *
 * Issue output to stdout and error message to stderr where appropriate.
 *
 * Return rc == 0 if everything went OK, rc != 0 if not.
 */
static int
manipulate_clock(const struct hwclock_control *ctl, const time_t set_time,
		 const struct timeval startup_time, struct adjtime *adjtime)
{
	/* The time at which we read the Hardware Clock */
	struct timeval read_time;
	/*
	 * The Hardware Clock gives us a valid time, or at
	 * least something close enough to fool mktime().
	 */
	bool hclock_valid = FALSE;
	/*
	 * Tick synchronized time read from the Hardware Clock and
	 * then drift correct for all operations except --show.
	 */
	struct timeval hclocktime = { 0, 0 };
	/* Total Hardware Clock drift correction needed. */
	struct timeval tdrift;
	/* local return code */
	int rc = 0;

	if (!ctl->systz && !ctl->predict && ur->get_permissions())
		return EX_NOPERM;

	if ((ctl->set || ctl->systohc || ctl->adjust) &&
	    (adjtime->local_utc == UTC) != ctl->universal) {
		adjtime->local_utc = ctl->universal ? UTC : LOCAL;
		adjtime->dirty = TRUE;
	}

	if (ctl->show || ctl->get || ctl->adjust || ctl->hctosys
	    || (!ctl->noadjfile && !ctl->systz && !ctl->predict)) {
		/* data from HW-clock are required */
		rc = synchronize_to_clock_tick(ctl);

		/*
		 * We don't error out if the user is attempting to set the
		 * RTC and synchronization timeout happens - the RTC could
		 * be functioning but contain invalid time data so we still
		 * want to allow a user to set the RTC time.
		 */
		if (rc == RTC_BUSYWAIT_FAILED && !ctl->set && !ctl->systohc)
			return EX_IOERR;
		gettimeofday(&read_time, NULL);

		/*
		 * If we can't synchronize to a clock tick,
		 * we likely can't read from the RTC so
		 * don't bother reading it again.
		 */
		if (!rc) {
			rc = read_hardware_clock(ctl, &hclock_valid,
						 &hclocktime.tv_sec);
			if (rc && !ctl->set && !ctl->systohc)
				return EX_IOERR;
		}
	}
	/*
	 * Calculate Hardware Clock drift for --predict with the user
	 * supplied --date option time, and with the time read from the
	 * Hardware Clock for all other operations.  Apply drift correction
	 * to the Hardware Clock time for everything except --show and
	 * --predict.  For --predict negate the drift correction, because we
	 * want to 'predict' a future Hardware Clock time that includes drift.
	 */
	hclocktime = ctl->predict ? t2tv(set_time) : hclocktime;
	calculate_adjustment(ctl, adjtime->drift_factor,
			     adjtime->last_adj_time,
			     adjtime->not_adjusted,
			     hclocktime.tv_sec, &tdrift);
	if (!ctl->show && !ctl->predict)
		hclocktime = time_inc(tdrift, hclocktime.tv_sec);
	if (ctl->show || ctl->get) {
		display_time(hclock_valid,
			     time_inc(hclocktime, -time_diff
				      (read_time, startup_time)));
	} else if (ctl->set) {
		set_hardware_clock_exact(ctl, set_time, startup_time);
		if (!ctl->noadjfile)
			adjust_drift_factor(ctl, adjtime,
					    time_inc(t2tv(set_time), time_diff
						     (read_time, startup_time)),
					    hclock_valid, hclocktime);
	} else if (ctl->adjust) {
		if (tdrift.tv_sec > 0 || tdrift.tv_sec < -1)
			do_adjustment(ctl, adjtime, hclock_valid,
				      hclocktime, read_time);
		else
			printf(_("Needed adjustment is less than one second, "
				 "so not setting clock.\n"));
	} else if (ctl->systohc) {
		struct timeval nowtime, reftime;
		/*
		 * We can only set_hardware_clock_exact to a
		 * whole seconds time, so we set it with
		 * reference to the most recent whole
		 * seconds time.
		 */
		gettimeofday(&nowtime, NULL);
		reftime.tv_sec = nowtime.tv_sec;
		reftime.tv_usec = 0;
		set_hardware_clock_exact(ctl, (time_t) reftime.tv_sec, reftime);
		if (!ctl->noadjfile)
			adjust_drift_factor(ctl, adjtime, nowtime,
					    hclock_valid, hclocktime);
	} else if (ctl->hctosys) {
		rc = set_system_clock(ctl, hclock_valid, hclocktime);
		if (rc) {
			printf(_("Unable to set system clock.\n"));
			return rc;
		}
	} else if (ctl->systz) {
		rc = set_system_clock_timezone(ctl);
		if (rc) {
			printf(_("Unable to set system clock.\n"));
			return rc;
		}
	} else if (ctl->predict) {
		hclocktime = time_inc(hclocktime, (double)
				      -(tdrift.tv_sec + tdrift.tv_usec / 1E6));
		if (ctl->debug) {
			printf(_
			       ("At %ld seconds after 1969, RTC is predicted to read %ld seconds after 1969.\n"),
			       set_time, hclocktime.tv_sec);
		}
		display_time(TRUE, hclocktime);
	}
	if (!ctl->noadjfile)
		save_adjtime(ctl, adjtime);
	return 0;
}

/**
 * Get or set the kernel RTC driver's epoch on Alpha machines.
 * ISA machines are hard coded for 1900.
 */
#if defined(__linux__) && defined(__alpha__)
static void
manipulate_epoch(const struct hwclock_control *ctl)
{
	if (ctl->getepoch) {
		unsigned long epoch;

		if (get_epoch_rtc(ctl, &epoch))
			warnx(_
			      ("Unable to get the epoch value from the kernel."));
		else
			printf(_("Kernel is assuming an epoch value of %lu\n"),
			       epoch);
	} else if (ctl->setepoch) {
		if (ctl->epoch_option == 0)
			warnx(_
			      ("To set the epoch value, you must use the 'epoch' "
			       "option to tell to what value to set it."));
		else if (ctl->testing)
			printf(_
			       ("Not setting the epoch to %lu - testing only.\n"),
			       ctl->epoch_option);
		else if (set_epoch_rtc(ctl))
			printf(_
			       ("Unable to set the epoch value in the kernel.\n"));
	}
}
#endif		/* __linux__ __alpha__ */

static void out_version(void)
{
	printf(UTIL_LINUX_VERSION);
}

/*
 * usage - Output (error and) usage information
 *
 * This function is called both directly from main to show usage information
 * and as fatal function from shhopt if some argument is not understood. In
 * case of normal usage info FMT should be NULL. In that case the info is
 * printed to stdout. If FMT is given usage will act like fprintf( stderr,
 * fmt, ... ), show a usage information and terminate the program
 * afterwards.
 */
static void usage(const struct hwclock_control *ctl, const char *fmt, ...)
{
	FILE *usageto;
	va_list ap;

	usageto = fmt ? stderr : stdout;

	fputs(USAGE_HEADER, usageto);
	fputs(_(" hwclock [function] [option...]\n"), usageto);

	fputs(USAGE_SEPARATOR, usageto);
	fputs(_("Query or set the hardware clock.\n"), usageto);

	fputs(_("\nFunctions:\n"), usageto);
	fputs(_(" -h, --help           show this help text and exit\n"
		" -r, --show           read hardware clock and print result\n"
		"     --get            read hardware clock and print drift corrected result\n"
		"     --set            set the RTC to the time given with --date\n"), usageto);
	fputs(_(" -s, --hctosys        set the system time from the hardware clock\n"
		" -w, --systohc        set the hardware clock from the current system time\n"
		"     --systz          set the system time based on the current timezone\n"
		"     --adjust         adjust the RTC to account for systematic drift since\n"
		"                        the clock was last set or adjusted\n"), usageto);
#if defined(__linux__) && defined(__alpha__)
	fputs(_("     --getepoch       print out the kernel's hardware clock epoch value\n"
		"     --setepoch       set the kernel's hardware clock epoch value to the \n"
		"                        value given with --epoch\n"), usageto);
#endif
	fputs(_("     --predict        predict RTC reading at time given with --date\n"
		" -V, --version        display version information and exit\n"), usageto);

	fputs(USAGE_OPTIONS, usageto);
	fputs(_(" -u, --utc            the hardware clock is kept in UTC\n"
		"     --localtime      the hardware clock is kept in local time\n"), usageto);
#ifdef __linux__
	fputs(_(" -f, --rtc <file>     special /dev/... file to use instead of default\n"), usageto);
#endif
	fprintf(usageto, _(
		"     --directisa      access the ISA bus directly instead of %s\n"
		"     --date <time>    specifies the time to which to set the hardware clock\n"), _PATH_RTC_DEV);
#if defined(__linux__) && defined(__alpha__)
	fputs(_("     --epoch <year>   specifies the hardware clock's epoch value\n"), usageto);
#endif
	fprintf(usageto, _(
		"     --update-drift   update drift factor in %1$s (requires\n"
		"                        --set or --systohc)\n"
		"     --noadjfile      do not access %1$s; this requires the use of\n"
		"                        either --utc or --localtime\n"
		"     --adjfile <file> specifies the path to the adjust file;\n"
		"                        the default is %1$s\n"), _PATH_ADJTIME);
	fputs(_("     --test           do not update anything, just show what would happen\n"
		" -D, --debug          debugging mode\n" "\n"), usageto);

	if (fmt) {
		va_start(ap, fmt);
		vfprintf(usageto, fmt, ap);
		va_end(ap);
	}

	fflush(usageto);
	hwclock_exit(ctl, fmt ? EX_USAGE : EX_OK);
}

/*
 * Returns:
 *  EX_USAGE: bad invocation
 *  EX_NOPERM: no permission
 *  EX_OSFILE: cannot open /dev/rtc or /etc/adjtime
 *  EX_IOERR: ioctl error getting or setting the time
 *  0: OK (or not)
 *  1: failure
 */
int main(int argc, char **argv)
{
	struct hwclock_control ctl = { .show = 1 }; /* default op is show */
	struct timeval startup_time;
	struct adjtime adjtime = { 0 };
	struct timespec when = { 0 };
	/*
	 * The time we started up, in seconds into the epoch, including
	 * fractions.
	 */
	time_t set_time = 0;	/* Time to which user said to set Hardware Clock */
	int rc, c;

	/* Long only options. */
	enum {
		OPT_ADJFILE = CHAR_MAX + 1,
		OPT_DATE,
		OPT_DIRECTISA,
		OPT_EPOCH,
		OPT_GET,
		OPT_GETEPOCH,
		OPT_LOCALTIME,
		OPT_NOADJFILE,
		OPT_PREDICT,
		OPT_SET,
		OPT_SETEPOCH,
		OPT_SYSTZ,
		OPT_TEST,
		OPT_UPDATE
	};

	static const struct option longopts[] = {
		{ "adjust",       no_argument,       NULL, 'a'            },
		{ "help",         no_argument,       NULL, 'h'            },
		{ "show",         no_argument,       NULL, 'r'            },
		{ "hctosys",      no_argument,       NULL, 's'            },
		{ "utc",          no_argument,       NULL, 'u'            },
		{ "version",      no_argument,       NULL, 'v'            },
		{ "systohc",      no_argument,       NULL, 'w'            },
		{ "debug",        no_argument,       NULL, 'D'            },
		{ "set",          no_argument,       NULL, OPT_SET        },
#if defined(__linux__) && defined(__alpha__)
		{ "getepoch",     no_argument,       NULL, OPT_GETEPOCH   },
		{ "setepoch",     no_argument,       NULL, OPT_SETEPOCH   },
		{ "epoch",        required_argument, NULL, OPT_EPOCH      },
#endif
		{ "noadjfile",    no_argument,       NULL, OPT_NOADJFILE  },
		{ "localtime",    no_argument,       NULL, OPT_LOCALTIME  },
		{ "directisa",    no_argument,       NULL, OPT_DIRECTISA  },
		{ "test",         no_argument,       NULL, OPT_TEST       },
		{ "date",         required_argument, NULL, OPT_DATE       },
#ifdef __linux__
		{ "rtc",          required_argument, NULL, 'f'            },
#endif
		{ "adjfile",      required_argument, NULL, OPT_ADJFILE    },
		{ "systz",        no_argument,       NULL, OPT_SYSTZ      },
		{ "predict",      no_argument,       NULL, OPT_PREDICT    },
		{ "get",          no_argument,       NULL, OPT_GET        },
		{ "update-drift", no_argument,       NULL, OPT_UPDATE     },
		{ NULL, 0, NULL, 0 }
	};

	static const ul_excl_t excl[] = {	/* rows and cols in ASCII order */
		{ 'a','r','s','w',
		  OPT_GET, OPT_GETEPOCH, OPT_PREDICT,
		  OPT_SET, OPT_SETEPOCH, OPT_SYSTZ },
		{ 'u', OPT_LOCALTIME},
		{ OPT_ADJFILE, OPT_NOADJFILE },
		{ OPT_NOADJFILE, OPT_UPDATE },
		{ 0 }
	};
	int excl_st[ARRAY_SIZE(excl)] = UL_EXCL_STATUS_INIT;

	/* Remember what time we were invoked */
	gettimeofday(&startup_time, NULL);

#ifdef HAVE_LIBAUDIT
	hwaudit_fd = audit_open();
	if (hwaudit_fd < 0 && !(errno == EINVAL || errno == EPROTONOSUPPORT ||
				errno == EAFNOSUPPORT)) {
		/*
		 * You get these error codes only when the kernel doesn't
		 * have audit compiled in.
		 */
		warnx(_("Unable to connect to audit system"));
		return EX_NOPERM;
	}
#endif
	setlocale(LC_ALL, "");
#ifdef LC_NUMERIC
	/*
	 * We need LC_CTYPE and LC_TIME and LC_MESSAGES, but must avoid
	 * LC_NUMERIC since it gives problems when we write to /etc/adjtime.
	 *  - gqueri@mail.dotcom.fr
	 */
	setlocale(LC_NUMERIC, "C");
#endif
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	while ((c = getopt_long(argc, argv,
				"?hvVDarsuwAJSFf:", longopts, NULL)) != -1) {

		err_exclusive_options(c, longopts, excl, excl_st);

		switch (c) {
		case 'D':
			ctl.debug++;
			break;
		case 'a':
			ctl.adjust = 1;
			ctl.show = 0;
			ctl.hwaudit_on = 1;
			break;
		case 'r':
			ctl.show = 1;
			break;
		case 's':
			ctl.hctosys = 1;
			ctl.show = 0;
			ctl.hwaudit_on = 1;
			break;
		case 'u':
			ctl.utc = 1;
			break;
		case 'w':
			ctl.systohc = 1;
			ctl.show = 0;
			ctl.hwaudit_on = 1;
			break;
		case OPT_SET:
			ctl.set = 1;
			ctl.show = 0;
			ctl.hwaudit_on = 1;
			break;
#if defined(__linux__) && defined(__alpha__)
		case OPT_GETEPOCH:
			ctl.getepoch = 1;
			ctl.show = 0;
			break;
		case OPT_SETEPOCH:
			ctl.setepoch = 1;
			ctl.show = 0;
			ctl.hwaudit_on = 1;
			break;
		case OPT_EPOCH:
			ctl.epoch_option =	/* --epoch */
			    strtoul_or_err(optarg, _("invalid epoch argument"));
			break;
#endif
		case OPT_NOADJFILE:
			ctl.noadjfile = 1;
			break;
		case OPT_LOCALTIME:
			ctl.local_opt = 1;	/* --localtime */
			break;
		case OPT_DIRECTISA:
			ctl.directisa = 1;
			break;
		case OPT_TEST:
			ctl.testing = 1;	/* --test */
			break;
		case OPT_DATE:
			ctl.date_opt = optarg;	/* --date */
			break;
		case OPT_ADJFILE:
			ctl.adj_file_name = optarg;	/* --adjfile */
			break;
		case OPT_SYSTZ:
			ctl.systz = 1;		/* --systz */
			ctl.show = 0;
			break;
		case OPT_PREDICT:
			ctl.predict = 1;	/* --predict */
			ctl.show = 0;
			break;
		case OPT_GET:
			ctl.get = 1;		/* --get */
			ctl.show = 0;
			break;
		case OPT_UPDATE:
			ctl.update = 1;		/* --update-drift */
			break;
#ifdef __linux__
		case 'f':
			ctl.rtc_dev_name = optarg;	/* --rtc */
			break;
#endif
		case 'v':			/* --version */
		case 'V':
			out_version();
			return 0;
		case 'h':			/* --help */
			usage(&ctl, NULL);
		default:
			errtryhelp(EXIT_FAILURE);
		}
	}

	argc -= optind;
	argv += optind;

	if (argc > 0) {
		warnx(_("%d too many arguments given"), argc);
		errtryhelp(EXIT_FAILURE);
	}

	if (!ctl.adj_file_name)
		ctl.adj_file_name = _PATH_ADJTIME;

	if (ctl.noadjfile && !ctl.utc && !ctl.local_opt) {
		warnx(_("With --noadjfile, you must specify "
			"either --utc or --localtime"));
		hwclock_exit(&ctl, EX_USAGE);
	}

	if (ctl.set || ctl.predict) {
		if (!ctl.date_opt){
		warnx(_("--date is required for --set or --predict"));
		hwclock_exit(&ctl, EX_USAGE);
		}
		if (parse_date(&when, ctl.date_opt, NULL))
			set_time = when.tv_sec;
		else {
			warnx(_("invalid date '%s'"), ctl.date_opt);
			hwclock_exit(&ctl, EX_USAGE);
		}
	}

#if defined(__linux__) && defined(__alpha__)
	if (ctl.getepoch || ctl.setepoch) {
		manipulate_epoch(&ctl);
		hwclock_exit(&ctl, EX_OK);
	}
#endif

	if (ctl.debug)
		out_version();

	if (!ctl.systz && !ctl.predict)
		determine_clock_access_method(&ctl);

	if (!ctl.noadjfile && !(ctl.systz && (ctl.utc || ctl.local_opt))) {
		if ((rc = read_adjtime(&ctl, &adjtime)) != 0)
			hwclock_exit(&ctl, rc);
	} else
		/* Avoid writing adjtime file if we don't have to. */
		adjtime.dirty = FALSE;
	ctl.universal = hw_clock_is_utc(&ctl, adjtime);
	rc = manipulate_clock(&ctl, set_time, startup_time, &adjtime);
	hwclock_exit(&ctl, rc);
	return rc;		/* Not reached */
}

void __attribute__((__noreturn__))
hwclock_exit(const struct hwclock_control *ctl
#ifndef HAVE_LIBAUDIT
	     __attribute__((__unused__))
#endif
	     , int status)
{
#ifdef HAVE_LIBAUDIT
	if (ctl->hwaudit_on && !ctl->testing) {
		audit_log_user_message(hwaudit_fd, AUDIT_USYS_CONFIG,
				       "op=change-system-time", NULL, NULL, NULL,
				       status ? 0 : 1);
		close(hwaudit_fd);
	}
#endif
	exit(status);
}

/*
 * History of this program:
 *
 * 98.08.12 BJH Version 2.4
 *
 * Don't use century byte from Hardware Clock. Add comments telling why.
 *
 * 98.06.20 BJH Version 2.3.
 *
 * Make --hctosys set the kernel timezone from TZ environment variable
 * and/or /usr/lib/zoneinfo. From Klaus Ripke (klaus@ripke.com).
 *
 * 98.03.05 BJH. Version 2.2.
 *
 * Add --getepoch and --setepoch.
 *
 * Fix some word length things so it works on Alpha.
 *
 * Make it work when /dev/rtc doesn't have the interrupt functions. In this
 * case, busywait for the top of a second instead of blocking and waiting
 * for the update complete interrupt.
 *
 * Fix a bunch of bugs too numerous to mention.
 *
 * 97.06.01: BJH. Version 2.1. Read and write the century byte (Byte 50) of
 * the ISA Hardware Clock when using direct ISA I/O. Problem discovered by
 * job (jei@iclnl.icl.nl).
 *
 * Use the rtc clock access method in preference to the KDGHWCLK method.
 * Problem discovered by Andreas Schwab <schwab@LS5.informatik.uni-dortmund.de>.
 *
 * November 1996: Version 2.0.1. Modifications by Nicolai Langfeldt
 * (janl@math.uio.no) to make it compile on linux 1.2 machines as well as
 * more recent versions of the kernel. Introduced the NO_CLOCK access method
 * and wrote feature test code to detect absence of rtc headers.
 *
 ***************************************************************************
 * Maintenance notes
 *
 * To compile this, you must use GNU compiler optimization (-O option) in
 * order to make the "extern inline" functions from asm/io.h (inb(), etc.)
 * compile. If you don't optimize, which means the compiler will generate no
 * inline functions, the references to these functions in this program will
 * be compiled as external references. Since you probably won't be linking
 * with any functions by these names, you will have unresolved external
 * references when you link.
 *
 * Here's some info on how we must deal with the time that elapses while
 * this program runs: There are two major delays as we run:
 *
 *	1) Waiting up to 1 second for a transition of the Hardware Clock so
 *	   we are synchronized to the Hardware Clock.
 *	2) Running the "date" program to interpret the value of our --date
 *	   option.
 *
 * Reading the /etc/adjtime file is the next biggest source of delay and
 * uncertainty.
 *
 * The user wants to know what time it was at the moment he invoked us, not
 * some arbitrary time later. And in setting the clock, he is giving us the
 * time at the moment we are invoked, so if we set the clock some time
 * later, we have to add some time to that.
 *
 * So we check the system time as soon as we start up, then run "date" and
 * do file I/O if necessary, then wait to synchronize with a Hardware Clock
 * edge, then check the system time again to see how much time we spent. We
 * immediately read the clock then and (if appropriate) report that time,
 * and additionally, the delay we measured.
 *
 * If we're setting the clock to a time given by the user, we wait some more
 * so that the total delay is an integral number of seconds, then set the
 * Hardware Clock to the time the user requested plus that integral number
 * of seconds. N.B. The Hardware Clock can only be set in integral seconds.
 *
 * If we're setting the clock to the system clock value, we wait for the
 * system clock to reach the top of a second, and then set the Hardware
 * Clock to the system clock's value.
 *
 * Here's an interesting point about setting the Hardware Clock:  On my
 * machine, when you set it, it sets to that precise time. But one can
 * imagine another clock whose update oscillator marches on a steady one
 * second period, so updating the clock between any two oscillator ticks is
 * the same as updating it right at the earlier tick. To avoid any
 * complications that might cause, we set the clock as soon as possible
 * after an oscillator tick.
 *
 * About synchronizing to the Hardware Clock when reading the time: The
 * precision of the Hardware Clock counters themselves is one second. You
 * can't read the counters and find out that is 12:01:02.5. But if you
 * consider the location in time of the counter's ticks as part of its
 * value, then its precision is as infinite as time is continuous! What I'm
 * saying is this: To find out the _exact_ time in the hardware clock, we
 * wait until the next clock tick (the next time the second counter changes)
 * and measure how long we had to wait. We then read the value of the clock
 * counters and subtract the wait time and we know precisely what time it
 * was when we set out to query the time.
 *
 * hwclock uses this method, and considers the Hardware Clock to have
 * infinite precision.
 */
