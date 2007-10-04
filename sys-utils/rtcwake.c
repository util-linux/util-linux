/*
 * rtcwake -- enter a system sleep state until specified wakeup time.
 *
 * This uses cross-platform Linux interfaces to enter a system sleep state,
 * and leave it no later than a specified time.  It uses any RTC framework
 * driver that supports standard driver model wakeup flags.
 *
 * This is normally used like the old "apmsleep" utility, to wake from a
 * suspend state like ACPI S1 (standby) or S3 (suspend-to-RAM).  Most
 * platforms can implement those without analogues of BIOS, APM, or ACPI.
 *
 * On some systems, this can also be used like "nvram-wakeup", waking
 * from states like ACPI S4 (suspend to disk).  Not all systems have
 * persistent media that are appropriate for such suspend modes.
 *
 * The best way to set the system's RTC is so that it holds the current
 * time in UTC.  Use the "-l" flag to tell this program that the system
 * RTC uses a local timezone instead (maybe you dual-boot MS-Windows).
 */

#include <stdio.h>
#include <getopt.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>

#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/types.h>

#include <linux/rtc.h>

#include "nls.h"

/* constants from legacy PC/AT hardware */
#define	RTC_PF	0x40
#define	RTC_AF	0x20
#define	RTC_UF	0x10

#define MAX_LINE		1024

static char		*progname;

#define VERSION_STRING		"rtcwake from " PACKAGE_STRING
#define RTC_PATH		"/sys/class/rtc/%s/device/power/wakeup"
#define SYS_POWER_SATE_PATH	"/sys/power/state"
#define ADJTIME_PATH		"/etc/adjtime"
#define DEFAULT_DEVICE		"/dev/rtc0"
#define DEFAULT_MODE		"suspend"

enum ClockMode {
	CM_AUTO,
	CM_UTC,
	CM_LOCAL
};

static unsigned		verbose;
enum ClockMode		clock_mode = CM_AUTO;

static struct option long_options[] = {
	{"auto",	no_argument,		0, 'a'},
	{"local",	no_argument,		0, 'l'},
	{"utc",		no_argument,		0, 'u'},
	{"verbose",	no_argument,		0, 'v'},
	{"version",	no_argument,		0, 'V'},
	{"help",	no_argument,		0, 'h'},
	{"mode",	required_argument,	0, 'm'},
	{"device",	required_argument,	0, 'd'},
	{"seconds",	required_argument,	0, 's'},
	{"time",	required_argument,	0, 't'},
	{0,		0,			0, 0  }
};

static void usage(int retval)
{
	printf(_("usage: %s [options]\n"
		"    -d | --device <device>    select rtc device (rtc0|rtc1|...)\n"
		"    -l | --local              RTC uses local timezone\n"
		"    -m | --mode               standby|mem|... sleep mode\n"
		"    -s | --seconds <seconds>  seconds to sleep\n"
		"    -t | --time <time_t>      time to wake\n"
		"    -u | --utc                RTC uses UTC\n"
		"    -v | --verbose            verbose messages\n"
		"    -V | --version            show version\n"),
			progname);
	exit(retval);
}

static int may_wakeup(const char *devname)
{
	char	buf[128], *s;
	FILE	*f;

	/* strip the '/dev/' from the devname here */
	snprintf(buf, sizeof buf, RTC_PATH, devname + strlen("/dev/"));
	f = fopen(buf, "r");
	if (!f) {
		perror(buf);
		return 0;
	}
	s = fgets(buf, sizeof buf, f);
	fclose(f);
	if (!s)
		return 0;

	s = strchr(buf, '\n');
	if (!s)
		return 0;
	*s = 0;

	/* wakeup events could be disabled or not supported */
	return strcmp(buf, "enabled") == 0;
}

/* all times should be in UTC */
static time_t	sys_time;
static time_t	rtc_time;

static int get_basetimes(int fd)
{
	struct tm	tm;
	struct rtc_time	rtc;

	/* this process works in RTC time, except when working
	 * with the system clock (which always uses UTC).
	 */
	if (clock_mode == CM_UTC)
		setenv("TZ", "UTC", 1);
	tzset();

	/* read rtc and system clocks "at the same time", or as
	 * precisely (+/- a second) as we can read them.
	 */
	if (ioctl(fd, RTC_RD_TIME, &rtc) < 0) {
		perror(_("read rtc time"));
		return 0;
	}
	sys_time = time(0);
	if (sys_time == (time_t)-1) {
		perror(_("read system time"));
		return 0;
	}

	/* convert rtc_time to normal arithmetic-friendly form,
	 * updating tm.tm_wday as used by asctime().
	 */
	memset(&tm, 0, sizeof tm);
	tm.tm_sec = rtc.tm_sec;
	tm.tm_min = rtc.tm_min;
	tm.tm_hour = rtc.tm_hour;
	tm.tm_mday = rtc.tm_mday;
	tm.tm_mon = rtc.tm_mon;
	tm.tm_year = rtc.tm_year;
	tm.tm_isdst = rtc.tm_isdst;	/* stays unspecified? */
	rtc_time = mktime(&tm);

	if (rtc_time == (time_t)-1) {
		perror(_("convert rtc time"));
		return 0;
	}

	if (verbose) {
		if (clock_mode == CM_LOCAL) {
			printf("\ttzone   = %ld\n", timezone);
			printf("\ttzname  = %s\n", tzname[daylight]);
			gmtime_r(&rtc_time, &tm);
		}
		printf("\tsystime = %ld, (UTC) %s\n",
				(long) sys_time, asctime(gmtime(&sys_time)));
		printf("\trtctime = %ld, (UTC) %s\n",
				(long) rtc_time, asctime(&tm));
	}

	return 1;
}

static int setup_alarm(int fd, time_t *wakeup)
{
	struct tm		*tm;
	struct rtc_wkalrm	wake;

	tm = gmtime(wakeup);

	wake.time.tm_sec = tm->tm_sec;
	wake.time.tm_min = tm->tm_min;
	wake.time.tm_hour = tm->tm_hour;
	wake.time.tm_mday = tm->tm_mday;
	wake.time.tm_mon = tm->tm_mon;
	wake.time.tm_year = tm->tm_year;
	wake.time.tm_wday = tm->tm_wday;
	wake.time.tm_yday = tm->tm_yday;
	wake.time.tm_isdst = tm->tm_isdst;

	/* many rtc alarms only support up to 24 hours from 'now' ... */
	if ((rtc_time + (24 * 60 * 60)) > *wakeup) {
		if (ioctl(fd, RTC_ALM_SET, &wake.time) < 0) {
			perror(_("set rtc alarm"));
			return 0;
		}
		if (ioctl(fd, RTC_AIE_ON, 0) < 0) {
			perror(_("enable rtc alarm"));
			return 0;
		}

		/* ... so use the "more than 24 hours" request only if we must */
	} else {
		/* avoid an extra AIE_ON call */
		wake.enabled = 1;

		if (ioctl(fd, RTC_WKALM_SET, &wake) < 0) {
			perror(_("set rtc wake alarm"));
			return 0;
		}
	}

	return 1;
}

static void suspend_system(const char *suspend)
{
	FILE	*f = fopen(SYS_POWER_SATE_PATH, "w");

	if (!f) {
		perror(SYS_POWER_SATE_PATH);
		return;
	}

	fprintf(f, "%s\n", suspend);
	fflush(f);

	/* this executes after wake from suspend */
	fclose(f);
}


static int read_clock_mode(void)
{
	FILE *fp;
	char linebuf[MAX_LINE];

	fp = fopen(ADJTIME_PATH, "r");
	if (!fp)
		return 0;

	/* skip first line */
	if (!fgets(linebuf, MAX_LINE, fp)) {
		fclose(fp);
		return 0;
	}

	/* skip second line */
	if (!fgets(linebuf, MAX_LINE, fp)) {
		fclose(fp);
		return 0;
	}

	/* read third line */
	if (!fgets(linebuf, MAX_LINE, fp)) {
		fclose(fp);
		return 0;
	}

	if (strncmp(linebuf, "UTC", 3) == 0)
		clock_mode = CM_UTC;
	else if (strncmp(linebuf, "LOCAL", 5) == 0)
		clock_mode = CM_LOCAL;

	fclose(fp);

	return 1;
}

int main(int argc, char **argv)
{
	char		*devname = DEFAULT_DEVICE;
	unsigned	seconds = 0;
	char		*suspend = DEFAULT_MODE;

	int		t;
	int		fd;
	time_t		alarm = 0;

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

	progname = basename(argv[0]);

	while ((t = getopt_long(argc, argv, "ahd:lm:s:t:uVv",
					long_options, NULL)) != EOF) {
		switch (t) {
		case 'a':
			/* CM_AUTO is default */
			break;

		case 'd':
			devname = strdup(optarg);
			break;

		case 'l':
			clock_mode = CM_LOCAL;
			break;

			/* what system power mode to use?  for now handle only
			 * standardized mode names; eventually when systems
			 * define their own state names, parse
			 * /sys/power/state.
			 *
			 * "on" is used just to test the RTC alarm mechanism,
			 * bypassing all the wakeup-from-sleep infrastructure.
			 */
		case 'm':
			if (strcmp(optarg, "standby") == 0
					|| strcmp(optarg, "mem") == 0
					|| strcmp(optarg, "disk") == 0
					|| strcmp(optarg, "on") == 0
			   ) {
				suspend = strdup(optarg);
				break;
			}
			fprintf(stderr, _("%s: unrecognized suspend state '%s'\n"),
					progname, optarg);
			usage(EXIT_FAILURE);

			/* alarm time, seconds-to-sleep (relative) */
		case 's':
			t = atoi(optarg);
			if (t < 0) {
				fprintf(stderr,
					_("%s: illegal interval %s seconds\n"),
					progname, optarg);
				usage(EXIT_FAILURE);
			}
			seconds = t;
			break;

			/* alarm time, time_t (absolute, seconds since
			 * 1/1 1970 UTC)
			 */
		case 't':
			t = atoi(optarg);
			if (t < 0) {
				fprintf(stderr,
					_("%s: illegal time_t value %s\n"),
					progname, optarg);
				usage(EXIT_FAILURE);
			}
			alarm = t;
			break;

		case 'u':
			clock_mode = CM_UTC;
			break;

		case 'v':
			verbose++;
			break;

		case 'V':
			printf(_("%s: version %s\n"), progname, VERSION_STRING);
			exit(EXIT_SUCCESS);

		case 'h':
			usage(EXIT_SUCCESS);

		default:
			usage(EXIT_FAILURE);
		}
	}

	if (clock_mode == CM_AUTO) {
		if (!read_clock_mode()) {
			printf(_("%s: assuming RTC uses UTC ...\n"), progname);
			clock_mode = CM_UTC;
		}
		if (verbose)
			pritf(lock_mode == CM_UTC ? _("Using UTC time.\n") :
					_("Using local time.\n"));
	}

	if (!alarm && !seconds) {
		fprintf(stderr, _("%s: must provide wake time\n"), progname);
		usage(EXIT_FAILURE);
	}

	/* when devname doesn't start with /dev, append it */
	if (strncmp(devname, "/dev/", strlen("/dev/")) != 0) {
		char *new_devname;

		new_devname = malloc(strlen(devname) + strlen("/dev/") + 1);
		if (!new_devname) {
			perror(_("malloc() failed"));
			exit(EXIT_FAILURE);
		}

		strcpy(new_devname, "/dev/");
		strcat(new_devname, devname);
		free(devname);
		devname = new_devname;
	}

	if (strcmp(suspend, "on") != 0 && !may_wakeup(devname)) {
		fprintf(stderr, _("%s: %s not enabled for wakeup events\n"),
				progname, devname);
		exit(EXIT_FAILURE);
	}

	/* this RTC must exist and (if we'll sleep) be wakeup-enabled */
	fd = open(devname, O_RDONLY);
	if (fd < 0) {
		perror(devname);
		exit(EXIT_FAILURE);
	}

	/* relative or absolute alarm time, normalized to time_t */
	if (!get_basetimes(fd))
		exit(EXIT_FAILURE);
	if (verbose)
		printf(_("alarm %ld, sys_time %ld, rtc_time %ld, seconds %u\n"),
				alarm, sys_time, rtc_time, seconds);
	if (alarm) {
		if (alarm < sys_time) {
			fprintf(stderr, _("%s: time doesn't go backward to %s\n"),
					progname, ctime(&alarm));
			exit(EXIT_FAILURE);
		}
		alarm += sys_time - rtc_time;
	} else
		alarm = rtc_time + seconds + 1;
	if (setup_alarm(fd, &alarm) < 0)
		exit(EXIT_FAILURE);

	sync();
	printf(_("%s: wakeup from \"%s\" using %s at %s\n"),
			progname, suspend, devname,
			ctime(&alarm));
	fflush(stdout);
	usleep(10 * 1000);

	if (strcmp(suspend, "on") != 0)
		suspend_system(suspend);
	else {
		unsigned long data;

		do {
			t = read(fd, &data, sizeof data);
			if (t < 0) {
				perror(_("rtc read"));
				break;
			}
			if (verbose)
				printf("... %s: %03lx\n", devname, data);
		} while (!(data & RTC_AF));
	}

	if (ioctl(fd, RTC_AIE_OFF, 0) < 0)
		perror(_("disable rtc alarm interrupt"));

	close(fd);

	exit(EXIT_SUCCESS);
}
