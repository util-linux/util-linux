/*
From t-matsuu@protein.osaka-u.ac.jp Sat Jan 22 13:43:20 2000
Date: Sat, 22 Jan 2000 21:42:54 +0900 (JST)
To: Andries.Brouwer@cwi.nl
Subject: Please merge the source for PPC
From: MATSUURA Takanori <t-matsuu@protein.osaka-u.ac.jp>

Even now, it is used clock-1.1 based source on Linux for PowerPC
architecture, attached on this mail.

Please merge this source in main util-linux source.

But I'm not an author of this source, but Paul Mackerras.
http://linuxcare.com.au/paulus/
shows details of him.

MATSUURA Takanori @ Division of Protein Chemistry, 
                     Institute for Protein Research, Osaka University, Japan
E-Mail: t-matsuu@protein.osaka-u.ac.jp
Web Page: http://www.protein.osaka-u.ac.jp/chemistry/matsuura/
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include <time.h>
#include <fcntl.h>
#include <getopt.h>
#include <sys/time.h>

#include <asm/cuda.h>

/*
 * Adapted for Power Macintosh by Paul Mackerras.
 */

/* V1.0
 * CMOS clock manipulation - Charles Hedrick, hedrick@cs.rutgers.edu, Apr 1992
 * 
 * clock [-u] -r  - read cmos clock
 * clock [-u] -w  - write cmos clock from system time
 * clock [-u] -s  - set system time from cmos clock
 * clock [-u] -a  - set system time from cmos clock, adjust the time to
 *                  correct for systematic error, and put it back to the cmos.
 *  -u indicates cmos clock is kept in universal time
 *
 * The program is designed to run setuid, since we need to be able to
 * write to the CUDA.
 *
 *********************
 * V1.1
 * Modified for clock adjustments - Rob Hooft, hooft@chem.ruu.nl, Nov 1992
 * Also moved error messages to stderr. The program now uses getopt.
 * Changed some exit codes. Made 'gcc 2.3 -Wall' happy.
 *
 * I think a small explanation of the adjustment routine should be given
 * here. The problem with my machine is that its CMOS clock is 10 seconds 
 * per day slow. With this version of clock.c, and my '/etc/rc.local' 
 * reading '/etc/clock -au' instead of '/etc/clock -u -s', this error 
 * is automatically corrected at every boot. 
 *
 * To do this job, the program reads and writes the file '/etc/adjtime' 
 * to determine the correction, and to save its data. In this file are 
 * three numbers: 
 *
 * 1) the correction in seconds per day (So if your clock runs 5 
 *    seconds per day fast, the first number should read -5.0)
 * 2) the number of seconds since 1/1/1970 the last time the program was
 *    used.
 * 3) the remaining part of a second which was leftover after the last 
 *    adjustment
 *
 * Installation and use of this program:
 *
 * a) create a file '/etc/adjtime' containing as the first and only line:
 *    '0.0 0 0.0'
 * b) run 'clock -au' or 'clock -a', depending on whether your cmos is in
 *    universal or local time. This updates the second number.
 * c) set your system time using the 'date' command.
 * d) update your cmos time using 'clock -wu' or 'clock -w'
 * e) replace the first number in /etc/adjtime by your correction.
 * f) put the command 'clock -au' or 'clock -a' in your '/etc/rc.local'
 *
 * If the adjustment doesn't work for you, try contacting me by E-mail.
 *
 ******
 * V1.2
 *
 * Applied patches by Harald Koenig (koenig@nova.tat.physik.uni-tuebingen.de)
 * Patched and indented by Rob Hooft (hooft@EMBL-Heidelberg.DE)
 * 
 * A free quote from a MAIL-message (with spelling corrections):
 *
 * "I found the explanation and solution for the CMOS reading 0xff problem
 *  in the 0.99pl13c (ALPHA) kernel: the RTC goes offline for a small amount
 *  of time for updating. Solution is included in the kernel source 
 *  (linux/kernel/time.c)."
 *
 * "I modified clock.c to fix this problem and added an option (now default,
 *  look for USE_INLINE_ASM_IO) that I/O instructions are used as inline
 *  code and not via /dev/port (still possible via #undef ...)."
 *
 * With the new code, which is partially taken from the kernel sources, 
 * the CMOS clock handling looks much more "official".
 * Thanks Harald (and Torsten for the kernel code)!
 *
 ******
 * V1.3
 * Canges from alan@spri.levels.unisa.edu.au (Alan Modra):
 * a) Fix a few typos in comments and remove reference to making
 *    clock -u a cron job.  The kernel adjusts cmos time every 11
 *    minutes - see kernel/sched.c and kernel/time.c set_rtc_mmss().
 *    This means we should really have a cron job updating
 *    /etc/adjtime every 11 mins (set last_time to the current time
 *    and not_adjusted to ???).
 * b) Swapped arguments of outb() to agree with asm/io.h macro of the
 *    same name.  Use outb() from asm/io.h as it's slightly better.
 * c) Changed CMOS_READ and CMOS_WRITE to inline functions.  Inserted
 *    cli()..sti() pairs in appropriate places to prevent possible
 *    errors, and changed ioperm() call to iopl() to allow cli.
 * d) Moved some variables around to localise them a bit.
 * e) Fixed bug with clock -ua or clock -us that cleared environment
 *    variable TZ.  This fix also cured the annoying display of bogus
 *    day of week on a number of machines. (Use mktime(), ctime()
 *    rather than asctime() )
 * f) Use settimeofday() rather than stime().  This one is important
 *    as it sets the kernel's timezone offset, which is returned by
 *    gettimeofday(), and used for display of MSDOS and OS2 file
 *    times.
 * g) faith@cs.unc.edu added -D flag for debugging
 *
 * V1.4: alan@SPRI.Levels.UniSA.Edu.Au (Alan Modra)
 *       Wed Feb  8 12:29:08 1995, fix for years > 2000.
 *       faith@cs.unc.edu added -v option to print version.
 *
 * August 1996 Tom Dyas (tdyas@eden.rutgers.edu)
 *       Converted to be compatible with the SPARC /dev/rtc driver.
 *
 */

#define VERSION "1.4"

/* Here the information for time adjustments is kept. */
#define ADJPATH "/etc/adjtime"

/* Apparently the RTC on PowerMacs stores seconds since 1 Jan 1904 */
#define RTC_OFFSET	2082844800

/* used for debugging the code. */
/*#define KEEP_OFF */

/* Globals */
int readit = 0;
int adjustit = 0;
int writeit = 0;
int setit = 0;
int universal = 0;
int debug = 0;

time_t mkgmtime(struct tm *);

volatile void 
usage ( void )
{
  (void) fprintf (stderr, 
    "clock [-u] -r|w|s|a|v\n"
    "  r: read and print CMOS clock\n"
    "  w: write CMOS clock from system time\n"
    "  s: set system time from CMOS clock\n"
    "  a: get system time and adjust CMOS clock\n"
    "  u: CMOS clock is in universal time\n"
    "  v: print version (" VERSION ") and exit\n"
  );
  exit(EXIT_FAILURE);
}

int adb_fd;

void
adb_init ( void )
{
  adb_fd = open ("/dev/adb", 2);
  if (adb_fd < 0)
    {
      perror ("unable to open /dev/adb read/write : ");
      exit(EXIT_FAILURE);
    }
}

unsigned char get_packet[2] = { (unsigned char) CUDA_PACKET, 
				(unsigned char) CUDA_GET_TIME };
unsigned char set_packet[6] = { (unsigned char) CUDA_PACKET, 
				(unsigned char) CUDA_SET_TIME };

int 
main (int argc, char **argv )
{
  struct tm tm, *tmp;
  time_t systime;
  time_t last_time;
  time_t clock_time;
  int i, arg;
  double factor;
  double not_adjusted;
  int adjustment = 0;
  /*   unsigned char save_control, save_freq_select; */
  unsigned char reply[16];

  while ((arg = getopt (argc, argv, "rwsuaDv")) != -1)
    {
      switch (arg)
	{
	case 'r':
	  readit = 1;
	  break;
	case 'w':
	  writeit = 1;
	  break;
	case 's':
	  setit = 1;
	  break;
	case 'u':
	  universal = 1;
	  break;
	case 'a':
	  adjustit = 1;
	  break;
        case 'D':
	  debug = 1;
	  break;
	case 'v':
	  (void) fprintf( stderr, "clock " VERSION "\n" );
	  exit(EXIT_SUCCESS);
	default:
	  usage ();
	}
    }

    /* If we are in MkLinux do not even bother trying to set the clock */
    if(!access("/proc/osfmach3/version", R_OK))
    {           /* We're running MkLinux */
     if ( readit | writeit | setit | adjustit )
	printf("You must change the clock setting in MacOS.\n");
     exit(0);
    }

  if (readit + writeit + setit + adjustit > 1)
    usage ();			/* only allow one of these */

  if (!(readit | writeit | setit | adjustit))	/* default to read */
    readit = 1;

  adb_init ();

  if (adjustit)
    {				/* Read adjustment parameters first */
      FILE *adj;
      if ((adj = fopen (ADJPATH, "r")) == NULL)
	{
	  perror (ADJPATH);
	  exit(EXIT_FAILURE);
	}
      if (fscanf (adj, "%lf %d %lf", &factor, (int *) (&last_time), 
		  &not_adjusted) < 0)
	{
	  perror (ADJPATH);
	  exit(EXIT_FAILURE);
	}
      (void) fclose (adj);
      if (debug) (void) printf(
			"Last adjustment done at %d seconds after 1/1/1970\n",
			(int) last_time);
    }

  if (readit || setit || adjustit)
    {
      int ii;

      if (write(adb_fd, get_packet, sizeof(get_packet)) < 0) {
	perror("write adb");
	exit(EXIT_FAILURE);
      }
      ii = (int) read(adb_fd, reply, sizeof(reply));
      if (ii < 0) {
	perror("read adb");
	exit(EXIT_FAILURE);
      }
      if (ii != 7)
	(void) fprintf(stderr, 
		       "Warning: bad reply length from CUDA (%d)\n", ii);
      clock_time = (time_t) ((reply[3] << 24) + (reply[4] << 16)
	                     + (reply[5] << 8)) + (time_t) reply[6];
      clock_time -= RTC_OFFSET;

      if (universal) {
	systime = clock_time;
      } else {
	tm = *gmtime(&clock_time);
	(void) printf("time in rtc is %s", asctime(&tm));
	tm.tm_isdst = -1;		/* don't know whether it's DST */
	systime = mktime(&tm);
      }
    }

  if (readit)
    {
      (void) printf ("%s", ctime (&systime ));
    }

  if (setit || adjustit)
    {
      struct timeval tv;
      struct timezone tz;

/* program is designed to run setuid, be secure! */

      if (getuid () != 0)
	{			
	  (void) fprintf (stderr, 
			  "Sorry, must be root to set or adjust time\n");
	  exit(EXIT_FAILURE);
	}

      if (adjustit)
	{			/* the actual adjustment */
	  double exact_adjustment;

	  exact_adjustment = ((double) (systime - last_time))
	    * factor / (24 * 60 * 60)
	    + not_adjusted;
	  if (exact_adjustment > 0.)
	    adjustment = (int) (exact_adjustment + 0.5);
	  else
	    adjustment = (int) (exact_adjustment - 0.5);
	  not_adjusted = exact_adjustment - (double) adjustment;
	  systime += adjustment;
	  if (debug) {
	     (void) printf ("Time since last adjustment is %d seconds\n",
		     (int) (systime - last_time));
	     (void) printf ("Adjusting time by %d seconds\n",
		     adjustment);
	     (void) printf ("remaining adjustment is %.3f seconds\n",
		     not_adjusted);
	  }
	}
#ifndef KEEP_OFF
      tv.tv_sec = systime;
      tv.tv_usec = 0;
      tz.tz_minuteswest = timezone / 60;
      tz.tz_dsttime = daylight;

      if (settimeofday (&tv, &tz) != 0)
        {
	  (void) fprintf (stderr,
		   "Unable to set time -- probably you are not root\n");
	  exit(EXIT_FAILURE);
	}
      
      if (debug) {
	 (void) printf( "Called settimeofday:\n" );
	 (void) printf( "\ttv.tv_sec = %ld, tv.tv_usec = %ld\n",
		 tv.tv_sec, tv.tv_usec );
	 (void) printf( "\ttz.tz_minuteswest = %d, tz.tz_dsttime = %d\n",
		 tz.tz_minuteswest, tz.tz_dsttime );
      }
#endif
    }
  
  if (writeit || (adjustit && adjustment != 0))
    {
      systime = time (NULL);

      if (universal) {
	clock_time = systime;

      } else {
	tmp = localtime(&systime);
	clock_time = mkgmtime(tmp);
      }

      clock_time += RTC_OFFSET;
      set_packet[2] = clock_time >> 24;
      set_packet[3] = clock_time >> 16;
      set_packet[4] = clock_time >> 8;
      set_packet[5] = (unsigned char) clock_time;

      if (write(adb_fd, set_packet, sizeof(set_packet)) < 0) {
	perror("write adb (set)");
	exit(EXIT_FAILURE);
      }
      i = (int) read(adb_fd, reply, sizeof(reply));
      if (debug) {
	int j;
	(void) printf("set reply %d bytes:", i);
	for (j = 0; j < i; ++j) 
	    (void) printf(" %.2x", (unsigned int) reply[j]);
	(void) printf("\n");
      }
      if (i != 3 || reply[1] != (unsigned char) 0)
	(void) fprintf(stderr, "Warning: error %d setting RTC\n", 
		       (int) reply[1]);

      if (debug) {
	clock_time -= RTC_OFFSET;
	(void) printf("set RTC to %s", asctime(gmtime(&clock_time)));
      }
    }
  else
    if (debug) (void) printf ("CMOS clock unchanged.\n");
  /* Save data for next 'adjustit' call */
  if (adjustit)
    {
      FILE *adj;
      if ((adj = fopen (ADJPATH, "w")) == NULL)
	{
	  perror (ADJPATH);
	  exit(EXIT_FAILURE);
	}
      (void) fprintf (adj, "%f %d %f\n", factor, (int) systime, not_adjusted);
      (void) fclose (adj);
    }
  exit(EXIT_SUCCESS);
}

/* Stolen from linux/arch/i386/kernel/time.c. */
/* Converts Gregorian date to seconds since 1970-01-01 00:00:00.
 * Assumes input in normal date format, i.e. 1980-12-31 23:59:59
 * => year=1980, mon=12, day=31, hour=23, min=59, sec=59.
 *
 * [For the Julian calendar (which was used in Russia before 1917,
 * Britain & colonies before 1752, anywhere else before 1582,
 * and is still in use by some communities) leave out the
 * -year/100+year/400 terms, and add 10.]
 *
 * This algorithm was first published by Gauss (I think).
 *
 * WARNING: this function will overflow on 2106-02-07 06:28:16 on
 * machines were long is 32-bit! (However, as time_t is signed, we
 * will already get problems at other places on 2038-01-19 03:14:08)
 */
time_t mkgmtime(struct tm *tm)
{
  int mon = tm->tm_mon + 1;
  int year = tm->tm_year + 1900;

  if (0 >= (int) (mon -= 2)) {	/* 1..12 -> 11,12,1..10 */
    mon += 12;	/* Puts Feb last since it has leap day */
    year -= 1;
  }
  return (((
	    (unsigned long)(year/4 - year/100 + year/400 + 367*mon/12) +
	      tm->tm_mday + year*365 - 719499
	    )*24 + tm->tm_hour /* now have hours */
	   )*60 + tm->tm_min /* now have minutes */
	  )*60 + tm->tm_sec; /* finally seconds */
}
