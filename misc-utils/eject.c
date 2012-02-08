/********************************************************************
 *
 *		L I N U X   E J E C T	C O M M A N D
 *
 *		  by Jeff Tranter (tranter@pobox.com)
 *
 ********************************************************************
 *
 * Copyright (C) 1994-2005 Jeff Tranter (tranter@pobox.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 ********************************************************************
 *
 * See the man page for a description of what this program does and what
 * the requirements to run it are.
 *
 */

#include "linux_version.h"
#include "c.h"
#include "nls.h"
#include "strutils.h"
#include "xalloc.h"

#define EJECT_DEFAULT_DEVICE "/dev/cdrom"

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <limits.h>
#include <err.h>
#include <stdarg.h>

#include <getopt.h>
#include <errno.h>
#include <regex.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <sys/mtio.h>
#include <linux/cdrom.h>
#include <linux/fd.h>
#include <sys/mount.h>
#include <scsi/scsi.h>
#include <scsi/sg.h>
#include <scsi/scsi_ioctl.h>
#include <sys/time.h>

/* Used by the ToggleTray() function. If ejecting the tray takes this
 * time or less, the tray was probably already ejected, so we close it
 * again.
 */
#define TRAY_WAS_ALREADY_OPEN_USECS  200000	/* about 0.2 seconds */


/* Global Variables */
static int a_option; /* command flags and arguments */
static int c_option;
static int d_option;
static int f_option;
static int h_option;
static int n_option;
static int q_option;
static int r_option;
static int s_option;
static int t_option;
static int T_option;
static int v_option;
static int x_option;
static int p_option;
static int m_option;
static int a_arg;
static long int c_arg;
static long int x_arg;

/*
 * These are the basenames of devices which can have multiple
 * partitions per device.
 */
const char *partitionDevice[] = {
	"hd",
	"sd",
	"xd",
	"dos_hd",
	"mfm",
	"ad",
	"ed",
	"ftl",
	"pd",
	0};


/* Display command usage on standard error and exit. */
static void usage()
{
fprintf(stderr,_(
  "Usage:\n"
  " eject [options] <name>\n"
  "Options:\n"
  " -h, --help         display command usage and exit\n"
  " -V  --version      display program version and exit\n"
  " -d, --default      display default device\n"
  " -a, --auto         turn auto-eject feature on or off\n"
  " -c, --changerslot  switch discs on a CD-ROM changer\n"
  " -t, --trayclose    close tray\n"
  " -T, --traytoggle   toggle tray\n"
  " -x, --cdspeed      set CD-ROM max speed\n"
  " -v, --verbose      enable verbose output\n"
  " -n, --noop         don't eject, just show device found\n"
  " -r, --cdrom        eject CD-ROM\n"
  " -s, --scsi         eject SCSI device\n"
  " -f, --loppy        eject floppy\n"
  " -q, --tape         eject tape\n"
  "  -p, --proc         use /proc/mounts instead of /etc/mtab\n"
  " -m, --no-unmount   do not unmount device even if it is mounted\n"
  "\n"
  "Parameter <name> can be a device file or a mount point.\n"
  "If omitted, name defaults to `%s'.\n"
  "By default tries -r, -s, -f, and -q in order until success.\n"),
	EJECT_DEFAULT_DEVICE);
  exit(1);
}


/* Handle command line options. */
static void parse_args(int argc, char **argv, char **device)
{
	const char *flags = "a:c:x:dfhnqrstTvVpm";

	static struct option long_options[] =
	{
		{"help",	no_argument,	   NULL, 'h'},
		{"verbose",	no_argument,	   NULL, 'v'},
		{"default",	no_argument,	   NULL, 'd'},
		{"auto",	required_argument, NULL, 'a'},
		{"changerslot", required_argument, NULL, 'c'},
		{"trayclose",	no_argument,	   NULL, 't'},
		{"traytoggle",	no_argument,	   NULL, 'T'},
		{"cdspeed",	required_argument, NULL, 'x'},
		{"noop",	no_argument,	   NULL, 'n'},
		{"cdrom",	no_argument,	   NULL, 'r'},
		{"scsi",	no_argument,	   NULL, 's'},
		{"floppy",	no_argument,	   NULL, 'f'},
		{"tape",	no_argument,	   NULL, 'q'},
		{"version",	no_argument,	   NULL, 'V'},
		{"proc",	no_argument,	   NULL, 'p'},
		{"no-unmount",	no_argument,	   NULL, 'm'},
		{0, 0, 0, 0}
	};
	int option_index;

	int c;


	while ((c = getopt_long(argc, argv, flags, long_options, &option_index)) != EOF) {
		switch (c) {
		  case 'a':
			  a_option = 1;
			  if (!strcmp(optarg, "0"))
				  a_arg = 0;
			  else if (!strcmp(optarg, "off"))
				  a_arg = 0;
			  else if (!strcmp(optarg, "1"))
				  a_arg = 1;
			  else if (!strcmp(optarg, "on"))
				  a_arg = 1;
			  else {
				  errx(1, _("invalid argument to --auto/-a option"));
			  }
			  break;
		  case 'c':
			  c_option = 1;
			  /* atoi() returns 0 on error, so "0" must be parsed separately */
			  if (!strcmp(optarg, "0"))
				  c_arg = 0;
			  else {
				  c_arg = strtol_or_err( optarg, _("invalid argument to --changerslot/-c option"));
				}
			  break;
		  case 'x':
			  x_option = 1;
			  if (!strcmp(optarg, "0"))
				  x_arg = 0;
			  else {
				  x_arg = strtol_or_err( optarg, _("invalid argument to --cdspeed/-x option"));
			  }
			  break;
		  case 'd':
			  d_option = 1;
			  break;
		  case 'f':
			  f_option = 1;
			  break;
		  case 'h':
			  usage();
			  exit(0);
			  break;
		  case 'm':
			  m_option = 1;
			  break;
		  case 'n':
			  n_option = 1;
			  break;
		  case 'p':
			  p_option = 1;
			  break;
		  case 'q':
			  q_option = 1;
			  break;
		  case 'r':
			  r_option = 1;
			  break;
		  case 's':
			  s_option = 1;
			  break;
		  case 't':
			  t_option = 1;
			  break;
		  case 'T':
			  T_option = 1;
			  break;
		  case 'v':
			  v_option = 1;
			  break;
		  case 'V':
			  printf(UTIL_LINUX_VERSION);
        exit(EXIT_SUCCESS);
			  break;
      default:
		  case '?':
			  usage();
			  break;
		}
	}
	/* check for a single additional argument */
	if ((argc - optind) > 1) {
		errx(1, _("%s: too many arguments"));
	}
	if ((argc - optind) == 1) { /* one argument */
		*device = xstrdup(argv[optind]);
	}
}

void e_close(int fp) {
	if (close(fp)==-1) {
		err(1, NULL);
	}
}

void e_fclose(FILE *fp) {
	if (fclose(fp)==-1) {
		err(1, NULL);
	}
}

/* Return 1 if file/device exists, 0 otherwise. */
static int FileExists(const char *name)
{

	/*
	 * access() uses the UID, not the EUID. This way a normal user
	 * cannot find out if a file (say, /root/fubar) exists or not, even
	 * if eject is SUID root
	 */
	if (access (name, F_OK) == 0) {
		return 1;
	}	else {
		return 0;
	}
}


/*
 * Given name, such as foo, see if any of the following exist:
 *
 * foo (if foo starts with '.' or '/')
 * /dev/foo
 * /media/foo
 * /mnt/foo
 * /dev/cdroms/foo
 * /dev/cdroms/foo0
 * /dev/dsk/foo
 * /dev/rdsk/foo
 * ./foo
 *
 * If found, return the full path. If not found, return 0.
 * Returns pointer to dynamically allocated string.
 */
static char *find_device(const char *name)
{
	char *buf;

	buf = (char *) xmalloc(strlen(name)+14); /* to allow for "/dev/cdroms/ + "0" + null */

	if ((name[0] == '.') || (name[0] == '/')) {
		strcpy(buf, name);
		if (FileExists(buf))
			return buf;
	}

	strcpy(buf, "/dev/");
	strcat(buf, name);
	if (FileExists(buf))
		return buf;

	strcpy(buf, "/media/");
	strcat(buf, name);
	if (FileExists(buf))
		return buf;

	strcpy(buf, "/mnt/");
	strcat(buf, name);
	if (FileExists(buf))
		return buf;

	/* for devfs under Linux */
	strcpy(buf, "/dev/cdroms/");
	strcat(buf, name);
	if (FileExists(buf))
		return buf;

	strcpy(buf, "/dev/cdroms/");
	strcat(buf, name);
	strcat(buf, "0");
	if (FileExists(buf))
		return buf;

	/* for devfs under Solaris */
	strcpy(buf, "/dev/rdsk/");
	strcat(buf, name);
	if (FileExists(buf))
		return buf;

	strcpy(buf, "/dev/dsk/");
	strcat(buf, name);
	if (FileExists(buf))
		return buf;

	strcpy(buf, "./");
	strcat(buf, name);
	if (FileExists(buf))
		return buf;

	free(buf);
	buf = 0;
	return 0;
}


/* Set or clear auto-eject mode. */
static void AutoEject(int fd, int onOff)
{
	int status;

	status = ioctl(fd, CDROMEJECT_SW, onOff);
	if (status != 0) {
		err(1, _("CD-ROM auto-eject command failed"));
	}
}


/*
 * Changer select. CDROM_SELECT_DISC is preferred, older kernels used
 * CDROMLOADFROMSLOT.
 */
static void ChangerSelect(int fd, int slot)
{
	int status;

#ifdef CDROM_SELECT_DISC
	status = ioctl(fd, CDROM_SELECT_DISC, slot);
	if (status < 0) {
		err(1, _("CD-ROM select disc command failed"));
	}
#elif defined CDROMLOADFROMSLOT
	status = ioctl(fd, CDROMLOADFROMSLOT, slot);
	if (status != 0) {
		err(1, _("CD-ROM load from slot command failed"));
	}
#else
    warnx( _("IDE/ATAPI CD-ROM changer not supported by this kernel\n") );
#endif
}


/*
 * Close tray. Not supported by older kernels.
 */
static void CloseTray(int fd)
{
	int status;

#ifdef CDROMCLOSETRAY
	status = ioctl(fd, CDROMCLOSETRAY);
	if (status != 0) {
		err(1, _("CD-ROM tray close command failed"));
	}
#else
    warnx( _("CD-ROM tray close command not supported by this kernel\n"));
#endif
}

/*
 * Toggle tray.
 *
 * Written by Benjamin Schwenk <benjaminschwenk@yahoo.de> and
 * Sybren Stuvel <sybren@thirdtower.com>
 *
 * Not supported by older kernels because it might use
 * CloseTray().
 *
 */
static void ToggleTray(int fd)
{
	struct timeval time_start, time_stop;
	int time_elapsed;

#ifdef CDROMCLOSETRAY

	/* Try to open the CDROM tray and measure the time therefor
	 * needed.  In my experience the function needs less than 0.05
	 * seconds if the tray was already open, and at least 1.5 seconds
	 * if it was closed.  */
	gettimeofday(&time_start, NULL);

	/* Send the CDROMEJECT command to the device. */
	if (ioctl(fd, CDROMEJECT, 0) < 0) {
		perror("ioctl");
		exit(1);
	}

	/* Get the second timestamp, to measure the time needed to open
	 * the tray.  */
	gettimeofday(&time_stop, NULL);

	time_elapsed = (time_stop.tv_sec * 1000000 + time_stop.tv_usec) -
		(time_start.tv_sec * 1000000 + time_start.tv_usec);

	/* If the tray "opened" too fast, we can be nearly sure, that it
	 * was already open. In this case, close it now. Else the tray was
	 * closed before. This would mean that we are done.  */
	if (time_elapsed < TRAY_WAS_ALREADY_OPEN_USECS)
		CloseTray(fd);

#else
    warnx( _("CD-ROM tray toggle command not supported by this kernel"));
#endif

}

/*
 * Select Speed of CD-ROM drive.
 * Thanks to Roland Krivanek (krivanek@fmph.uniba.sk)
 * http://dmpc.dbp.fmph.uniba.sk/~krivanek/cdrom_speed/
 */
static void SelectSpeedCdrom(int fd, int speed)
{
	int status;

#ifdef CDROM_SELECT_SPEED
	status = ioctl(fd, CDROM_SELECT_SPEED, speed);
	if (status != 0) {
		err(1, _("CD-ROM select speed command failed"));
	}
#else
    warnx( _("CD-ROM select speed command not supported by this kernel"));
#endif
}


/*
 * Eject using CDROMEJECT ioctl. Return 1 if successful, 0 otherwise.
 */
static int EjectCdrom(int fd)
{
	int status;

	status = ioctl(fd, CDROMEJECT);
	return (status == 0);
}


/*
 * Eject using SCSI commands. Return 1 if successful, 0 otherwise.
 */
static int EjectScsi(int fd)
{
	int status;
	struct sdata {
		int  inlen;
		int  outlen;
		char cmd[256];
	} scsi_cmd;

	scsi_cmd.inlen	= 0;
	scsi_cmd.outlen = 0;
	scsi_cmd.cmd[0] = ALLOW_MEDIUM_REMOVAL;
	scsi_cmd.cmd[1] = 0;
	scsi_cmd.cmd[2] = 0;
	scsi_cmd.cmd[3] = 0;
	scsi_cmd.cmd[4] = 0;
	scsi_cmd.cmd[5] = 0;
	status = ioctl(fd, SCSI_IOCTL_SEND_COMMAND, (void *)&scsi_cmd);
	if (status != 0)
		return 0;

	scsi_cmd.inlen	= 0;
	scsi_cmd.outlen = 0;
	scsi_cmd.cmd[0] = START_STOP;
	scsi_cmd.cmd[1] = 0;
	scsi_cmd.cmd[2] = 0;
	scsi_cmd.cmd[3] = 0;
	scsi_cmd.cmd[4] = 1;
	scsi_cmd.cmd[5] = 0;
	status = ioctl(fd, SCSI_IOCTL_SEND_COMMAND, (void *)&scsi_cmd);
	if (status != 0)
		return 0;

	scsi_cmd.inlen	= 0;
	scsi_cmd.outlen = 0;
	scsi_cmd.cmd[0] = START_STOP;
	scsi_cmd.cmd[1] = 0;
	scsi_cmd.cmd[2] = 0;
	scsi_cmd.cmd[3] = 0;
	scsi_cmd.cmd[4] = 2;
	scsi_cmd.cmd[5] = 0;
	status = ioctl(fd, SCSI_IOCTL_SEND_COMMAND, (void *)&scsi_cmd);
	if (status != 0)
		return 0;

	/* force kernel to reread partition table when new disc inserted */
	status = ioctl(fd, BLKRRPART);
	return (status == 0);
}


/*
 * Eject using FDEJECT ioctl. Return 1 if successful, 0 otherwise.
 */
static int EjectFloppy(int fd)
{
	int status;

	status = ioctl(fd, FDEJECT);
	return (status == 0);
}


/*
 * Eject using tape ioctl. Return 1 if successful, 0 otherwise.
 */
static int EjectTape(int fd)
{
	int status;
	struct mtop op;

	op.mt_op = MTOFFL; /* rewind and eject */
	op.mt_count = 0;   /* not used */
	status = ioctl(fd, MTIOCTOP, &op);
	return (status == 0);
}


/* Unmount a device. */
static void Unmount(const char *fullName)
{
	int status;

	switch (fork()) {
	  case 0: /* child */
		  seteuid(getuid()); /* reduce likelyhood of security holes when running setuid */
		  if(p_option)
			  execl("/bin/umount", "/bin/umount", fullName, "-n", NULL);
		  else
			  execl("/bin/umount", "/bin/umount", fullName, NULL);
		  errx(1, _("unable to exec /bin/umount of `%s'"), fullName);
		  break;
	  case -1:
		  warn( _("unable to fork"));
		  break;
	  default: /* parent */
		  wait(&status);
		  if (WIFEXITED(status) == 0) {
			  errx(1, _("unmount of `%s' did not exit normally\n"), fullName);
		  }
		  if (WEXITSTATUS(status) != 0) {
			  errx(1, _("unmount of `%s' failed\n"), fullName);
		  }
		  break;
	}
}


/* Open a device file. */
static int OpenDevice(const char *fullName)
{
	int fd = open(fullName, O_RDONLY|O_NONBLOCK);
	if (fd == -1) {
		errx(1 , _("unable to open `%s'\n"), fullName);
	}
	return fd;
}


/*
 * Get major and minor device numbers for a device file name, so we
 * can check for duplicate devices.
 */
static int GetMajorMinor(const char *name, int *maj, int *min)
{
	struct stat sstat;
	*maj = *min = -1;
	if (stat(name, &sstat) == -1)
		return -1;
	if (! S_ISBLK(sstat.st_mode))
		return -1;
	*maj = major(sstat.st_rdev);
	*min = minor(sstat.st_rdev);
	return 0;
}


/*
 * See if device has been mounted by looking in mount table.  If so, set
 * device name and mount point name, and return 1, otherwise return 0.
 */
static int MountedDevice(const char *name, char **mountName, char **deviceName)
{
	FILE *fp;
	char line[1024];
	char s1[1024];
	char s2[1024];
	int rc;

	int maj;
	int min;

	GetMajorMinor(name, &maj, &min);

	fp = fopen((p_option ? "/proc/mounts" : "/etc/mtab"), "r");
	if (fp == NULL)
	{
		err(1, _("unable to open %s"), (p_option ? "/proc/mounts" : "/etc/mtab"));
	}

	while (fgets(line, sizeof(line), fp) != 0) {
		rc = sscanf(line, "%1023s %1023s", s1, s2);
		if (rc >= 2) {
			int mtabmaj, mtabmin;
			GetMajorMinor(s1, &mtabmaj, &mtabmin);
			if (((strcmp(s1, name) == 0) || (strcmp(s2, name) == 0)) ||
				((maj != -1) && (maj == mtabmaj) && (min == mtabmin))) {
				e_fclose(fp);
				*deviceName = strdup(s1);
				*mountName = strdup(s2);
				return 1;
			}
		}
	}
	*deviceName = 0;
	*mountName = 0;
	e_fclose(fp);
	return 0;
}


/*
 * See if device can be mounted by looking in /etc/fstab.
 * If so, set device name and mount point name, and return 1,
 * otherwise return 0.
 */
static int MountableDevice(const char *name, char **mountName, char **deviceName)
{
	FILE *fp;
	char line[1024];
	char s1[1024];
	char s2[1024];
	int rc;

	fp = fopen("/etc/fstab", "r");
	if (fp == NULL) {
/*
 * /etc/fstab may be unreadable in some situations due to passwords in the
 * file.
 */
/*		fprintf(stderr, _("%s: unable to open /etc/fstab: %s\n"), programName, strerror(errno));
		exit(1);*/
		if (v_option) {
			warn( _("unable to open /etc/fstab") );
		}
		return -1;
	}

	while (fgets(line, sizeof(line), fp) != 0) {
		rc = sscanf(line, "%1023s %1023s", s1, s2);
		if (rc >= 2 && s1[0] != '#' && strcmp(s2, name) == 0) {
			e_fclose(fp);
			*deviceName = strdup(s1);
			*mountName = strdup(s2);
			return 1;
		}
	}
	e_fclose(fp);
	return 0;
}


/*
 * Step through mount table and unmount all devices that match a regular
 * expression.
 */
static void UnmountDevices(const char *pattern)
{
	regex_t preg;
	FILE *fp;
	char s1[1024];
	char s2[1024];
	char line[1024];
	int status;

	if (regcomp(&preg, pattern, REG_EXTENDED)!=0) {
    err(0, _("regcomp"));
	}

	fp = fopen((p_option ? "/proc/mounts" : "/etc/mtab"), "r");
	if (fp == NULL)
	{
		err(1, _("unable to open %s"),(p_option ? "/proc/mounts" : "/etc/mtab"));
	}

	while (fgets(line, sizeof(line), fp) != 0) {
		status = sscanf(line, "%1023s %1023s", s1, s2);
		if (status >= 2) {
			status = regexec(&preg, s1, 0, 0, 0);
			if (status == 0) {
				if (v_option)
					printf(_("%s: unmounting `%s'\n"), program_invocation_short_name, s1);
				Unmount(s1);
				regfree(&preg);
			}
		}
	}
	e_fclose(fp);
}


/* Check if name is a symbolic link. If so, return what it points to. */
static char *SymLink(const char *name)
{
	int status;
	char s1[PATH_MAX];
	char s2[PATH_MAX];
	char s4[PATH_MAX];
	char result[PATH_MAX];
	char *s3;

	memset(s1, 0, sizeof(s1));
	memset(s2, 0, sizeof(s2));
	memset(s4, 0, sizeof(s4));
	memset(result, 0, sizeof(result));

	status = readlink(name, s1, sizeof(s1) - 1);

	if (status == -1)
		return 0;

	s1[status] = 0;
	if (s1[0] == '/') { /* absolute link */
		return strdup(s1);
	} else { /* relative link, add base name */
		strncpy(s2, name, sizeof(s2)-1);
		s3 = strrchr(s2, '/');
		if (s3 != 0) {
			s3[1] = 0;
			snprintf(result, sizeof(result)-1, "%s%s", s2, s1);
		}
	}
	realpath(result, s4);
	return strdup(s4);
}


/*
 * Given a name, see if it matches a pattern for a device that can have
 * multiple partitions.  If so, return a regular expression that matches
 * partitions for that device, otherwise return 0.
 */
static char *MultiplePartitions(const char *name)
{
	int i = 0;
	int status;
	regex_t preg;
	char pattern[256];
	char *result = 0;

	for (i = 0; partitionDevice[i] != 0; i++) {
		/* look for ^/dev/foo[a-z]([0-9]?[0-9])?$, e.g. /dev/hda1 */
		strcpy(pattern, "^/dev/");
		strcat(pattern, partitionDevice[i]);
		strcat(pattern, "[a-z]([0-9]?[0-9])?$");
		regcomp(&preg, pattern, REG_EXTENDED|REG_NOSUB);
		status = regexec(&preg, name, 1, 0, 0);
		regfree(&preg);
		if (status == 0) {
			result = (char *) malloc(strlen(name) + 25);
			strcpy(result, name);
			result[strlen(partitionDevice[i]) + 6] = 0;
			strcat(result, "([0-9]?[0-9])?$");
			if (v_option)
				printf(_("%s: `%s' is a multipartition device\n"), program_invocation_short_name, name);
			return result;
		}
	}
	if (v_option)
		printf(_("%s: `%s' is not a multipartition device\n"), program_invocation_short_name, name);
	return 0;
}


/* handle -x option */
void HandleXOption(char *deviceName)
{
	int fd; 	   /* file descriptor for device */
	if (x_option) {
		if (v_option)
		{
			if (x_arg == 0)
				printf(_("%s: setting CD-ROM speed to auto\n"), program_invocation_short_name);
			else
				printf(_("%s: setting CD-ROM speed to %dX\n"), program_invocation_short_name, x_arg);
		}
		fd = OpenDevice(deviceName);
		SelectSpeedCdrom(fd, x_arg);
		exit(0);
	}
}


/* main program */
int main(int argc, char **argv)
{

	const char *defaultDevice = EJECT_DEFAULT_DEVICE;  /* default if no name passed by user */
	int worked = 0;    /* set to 1 when successfully ejected */
	char *device = 0;  /* name passed from user */
	char *fullName;    /* expanded name */
	char *deviceName;  /* name of device */
	char *linkName;    /* name of device's symbolic link */
	char *mountName;   /* name of device's mount point */
	int fd;            /* file descriptor for device */
	int mounted = 0;   /* true if device is mounted */
	int mountable = 0; /* true if device is in /etc/fstab */
	char *pattern;     /* regex for device if multiple partitions */
	int ld = 6;        /* symbolic link max depth */

	setlocale(LC_ALL,"");
  bindtextdomain(PACKAGE, LOCALEDIR);
  textdomain(PACKAGE);

	/* program name is global variable used by other procedures */
	char *programName = program_invocation_short_name;

	/* parse the command line arguments */
	parse_args(argc, argv, &device);


	/* handle -d option */
	if (d_option) {
		printf(_("%s: default device: `%s'\n"), programName, defaultDevice);
		exit(0);
	}

	/* if no device, use default */
	if (device == 0) {
		device = strdup(defaultDevice);
		if (v_option)
			printf(_("%s: using default device `%s'\n"), programName, device);
	}

	/* Strip any trailing slash from name in case user used bash/tcsh
	   style filename completion (e.g. /mnt/cdrom/) */
	if (device[strlen(device)-1] == '/')
		device[strlen(device)-1] = 0;

	if (v_option)
		printf(_("%s: device name is `%s'\n"), programName, device);

	/* figure out full device or mount point name */
	fullName = find_device(device);
	if (fullName == 0) {
		errx(1, _("unable to find or open device for: `%s'"), device);
	}
	if (v_option)
		printf(_("%s: expanded name is `%s'\n"), programName, fullName);

	/* check for a symbolic link */
	while ((linkName = SymLink(fullName)) && (ld > 0)) {
		if (v_option)
			printf(_("%s: `%s' is a link to `%s'\n"), programName, fullName, linkName);
		free(fullName);
		fullName = strdup(linkName);
		free(linkName);
		linkName = 0;
		ld--;
	}
	/* handle max depth exceeded option */
	if (ld <= 0) {
		errx(1, _("maximum symbolic link depth exceeded: `%s'"), fullName);
	}

	/* if mount point, get device name */
	mounted = MountedDevice(fullName, &mountName, &deviceName);
	if (v_option) {
		if (mounted)
			printf(_("%s: `%s' is mounted at `%s'\n"), programName, deviceName, mountName);
		else
			printf(_("%s: `%s' is not mounted\n"), programName, fullName);
	}
	if (!mounted) {
		deviceName = strdup(fullName);
	}

	/* if not currently mounted, see if it is a possible mount point */
	if (!mounted) {
		mountable = MountableDevice(fullName, &mountName, &deviceName);
		/* if return value -1 then fstab could not be read */
		if (v_option && mountable >= 0) {
			if (mountable)
				printf(_("%s: `%s' can be mounted at `%s'\n"), programName, deviceName, mountName);
			else
				printf(_("%s: `%s' is not a mount point\n"), programName, fullName);
		}
	}

	/* handle -n option */
	if (n_option) {
		printf(_("%s: device is `%s'\n"), programName, deviceName);
		if (v_option)
			printf(_("%s: exiting due to -n/--noop option\n"), programName);
		exit(0);
	}

	/* handle -a option */
	if (a_option) {
		if (v_option) {
			if (a_arg)
				printf(_("%s: enabling auto-eject mode for `%s'\n"), programName, deviceName);
			else
				printf(_("%s: disabling auto-eject mode for `%s'\n"), programName, deviceName);
		}
		fd = OpenDevice(deviceName);
		AutoEject(fd, a_arg);
		exit(0);
	}

	/* handle -t option */
	if (t_option) {
		if (v_option)
			printf(_("%s: closing tray\n"), programName);
		fd = OpenDevice(deviceName);
		CloseTray(fd);
		HandleXOption(deviceName);
		exit(0);
	}

	/* handle -T option */
	if (T_option) {
		if (v_option)
			printf(_("%s: toggling tray\n"), programName);
		fd = OpenDevice(deviceName);
		ToggleTray(fd);
		HandleXOption(deviceName);
		exit(0);
	}

	/* handle -x option only */
	if (!c_option) HandleXOption(deviceName);

	/* unmount device if mounted */
	if ((m_option != 1) && mounted) {
		if (v_option)
			printf(_("%s: unmounting `%s'\n"), programName, deviceName);
		Unmount(deviceName);
	}

	/* if it is a multipartition device, unmount any other partitions on
	   the device */
	pattern = MultiplePartitions(deviceName);
	if ((m_option != 1) && (pattern != 0))
		UnmountDevices(pattern);

	/* handle -c option */
	if (c_option) {
		if (v_option)
			printf(_("%s: selecting CD-ROM disc #%d\n"), programName, c_arg);
		fd = OpenDevice(deviceName);
		ChangerSelect(fd, c_arg);
		HandleXOption(deviceName);
		exit(0);
	}

	/* if user did not specify type of eject, try all four methods */
	if ((r_option + s_option + f_option + q_option) == 0) {
		r_option = s_option = f_option = q_option = 1;
	}

	/* open device */
	fd = OpenDevice(deviceName);

	/* try various methods of ejecting until it works */
	if (r_option) {
		if (v_option)
			printf(_("%s: trying to eject `%s' using CD-ROM eject command\n"), programName, deviceName);
		worked = EjectCdrom(fd);
		if (v_option) {
			if (worked)
				printf(_("%s: CD-ROM eject command succeeded\n"), programName);
			else
				printf(_("%s: CD-ROM eject command failed\n"), programName);
		}
	}

	if (s_option && !worked) {
		if (v_option)
			printf(_("%s: trying to eject `%s' using SCSI commands\n"), programName, deviceName);
		worked = EjectScsi(fd);
		if (v_option) {
			if (worked)
				printf(_("%s: SCSI eject succeeded\n"), programName);
			else
				printf(_("%s: SCSI eject failed\n"), programName);
		}
	}

	if (f_option && !worked) {
		if (v_option)
			printf(_("%s: trying to eject `%s' using floppy eject command\n"), programName, deviceName);
		worked = EjectFloppy(fd);
		if (v_option) {
			if (worked)
				printf(_("%s: floppy eject command succeeded\n"), programName);
			else
				printf(_("%s: floppy eject command failed\n"), programName);
		}
	}

	if (q_option && !worked) {
		if (v_option)
			printf(_("%s: trying to eject `%s' using tape offline command\n"), programName, deviceName);
		worked = EjectTape(fd);
		if (v_option) {
			if (worked)
				printf(_("%s: tape offline command succeeded\n"), programName);
			else
				printf(_("%s: tape offline command failed\n"), programName);
		}
	}

	if (!worked) {
		err(1, _("unable to eject, last error"), programName, strerror(errno));
	}

	/* cleanup */
	e_close(fd);
	free(device);
	free(deviceName);
	free(fullName);
	free(linkName);
	free(mountName);
	free(pattern);
	exit(0);
}
