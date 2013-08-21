/*
 * Copyright (C) 1994-2005 Jeff Tranter (tranter@pobox.com)
 * Copyright (C) 2012 Karel Zak <kzak@redhat.com>
 * Copyright (C) Michal Luscon <mluscon@redhat.com>
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
 */

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

#include <libmount.h>

#include "c.h"
#include "closestream.h"
#include "nls.h"
#include "strutils.h"
#include "xalloc.h"
#include "pathnames.h"
#include "sysfs.h"

/*
 * sg_io_hdr_t driver_status -- see kernel include/scsi/scsi.h
 */
#ifndef DRIVER_SENSE
# define DRIVER_SENSE	0x08
#endif


#define EJECT_DEFAULT_DEVICE "/dev/cdrom"


/* Used by the toggle_tray() function. If ejecting the tray takes this
 * time or less, the tray was probably already ejected, so we close it
 * again.
 */
#define TRAY_WAS_ALREADY_OPEN_USECS  200000	/* about 0.2 seconds */

/* eject(1) is able to eject only 'removable' devices (attribute in /sys)
 * _or_ devices connected by hotplug subsystem.
 */
static const char * const hotplug_subsystems[] = {
	"usb",
	"ieee1394",
	"pcmcia",
	"mmc",
	"ccw"
};

/* Global Variables */
static int a_option; /* command flags and arguments */
static int c_option;
static int d_option;
static int f_option;
static int F_option;
static int n_option;
static int q_option;
static int r_option;
static int s_option;
static int t_option;
static int T_option;
static int X_option;
static int v_option;
static int x_option;
static int p_option;
static int m_option;
static int M_option;
static int i_option;
static int a_arg;
static int i_arg;
static long int c_arg;
static long int x_arg;

struct libmnt_table *mtab;

static void vinfo(const char *fmt, va_list va)
{
	fprintf(stdout, "%s: ", program_invocation_short_name);
	vprintf(fmt, va);
	fputc('\n', stdout);
}

static inline void verbose(const char *fmt, ...)
{
	va_list va;

	if (!v_option)
		return;

	va_start(va, fmt);
	vinfo(fmt, va);
	va_end(va);
}

static inline void info(const char *fmt, ...)
{
	va_list va;
	va_start(va, fmt);
	vinfo(fmt, va);
	va_end(va);
}

static void __attribute__ ((__noreturn__)) usage(FILE * out)
{
	fputs(USAGE_HEADER, out);

	fprintf(out,
		_(" %s [options] [<device>|<mountpoint>]\n"), program_invocation_short_name);

	fputs(USAGE_OPTIONS, out);
	fputs(_(" -a, --auto <on|off>         turn auto-eject feature on or off\n"
		" -c, --changerslot <slot>    switch discs on a CD-ROM changer\n"
		" -d, --default               display default device\n"
		" -f, --floppy                eject floppy\n"
		" -F, --force                 don't care about device type\n"
		" -i, --manualeject <on|off>  toggle manual eject protection on/off\n"
		" -m, --no-unmount            do not unmount device even if it is mounted\n"
		" -M, --no-partitions-unmount do not unmount another partitions\n"
		" -n, --noop                  don't eject, just show device found\n"
		" -p, --proc                  use /proc/mounts instead of /etc/mtab\n"
		" -q, --tape                  eject tape\n"
		" -r, --cdrom                 eject CD-ROM\n"
		" -s, --scsi                  eject SCSI device\n"
		" -t, --trayclose             close tray\n"
		" -T, --traytoggle            toggle tray\n"
		" -v, --verbose               enable verbose output\n"
		" -x, --cdspeed <speed>       set CD-ROM max speed\n"
		" -X, --listspeed             list CD-ROM available speeds\n"),
		out);

	fputs(USAGE_SEPARATOR, out);
	fputs(USAGE_HELP, out);
	fputs(USAGE_VERSION, out);

	fputs(_("\nBy default tries -r, -s, -f, and -q in order until success.\n"), out);
	fprintf(out, USAGE_MAN_TAIL("eject(1)"));

	exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}


/* Handle command line options. */
static void parse_args(int argc, char **argv, char **device)
{
	static const struct option long_opts[] =
	{
		{"auto",	required_argument, NULL, 'a'},
		{"cdrom",	no_argument,	   NULL, 'r'},
		{"cdspeed",	required_argument, NULL, 'x'},
		{"changerslot", required_argument, NULL, 'c'},
		{"default",	no_argument,	   NULL, 'd'},
		{"floppy",	no_argument,	   NULL, 'f'},
		{"force",       no_argument,       NULL, 'F'},
		{"help",	no_argument,	   NULL, 'h'},
		{"listspeed",   no_argument,       NULL, 'X'},
		{"manualeject", required_argument, NULL, 'i'},
		{"noop",	no_argument,	   NULL, 'n'},
		{"no-unmount",	no_argument,	   NULL, 'm'},
		{"no-partitions-unmount", no_argument, NULL, 'M' },
		{"proc",	no_argument,	   NULL, 'p'},
		{"scsi",	no_argument,	   NULL, 's'},
		{"tape",	no_argument,	   NULL, 'q'},
		{"trayclose",	no_argument,	   NULL, 't'},
		{"traytoggle",	no_argument,	   NULL, 'T'},
		{"verbose",	no_argument,	   NULL, 'v'},
		{"version",	no_argument,	   NULL, 'V'},
		{0, 0, 0, 0}
	};
	int c;

	while ((c = getopt_long(argc, argv,
				"a:c:i:x:dfFhnqrstTXvVpmM", long_opts, NULL)) != -1) {
		switch (c) {
		case 'a':
			a_option = 1;
			if (!strcmp(optarg, "0") || !strcmp(optarg, "off"))
				a_arg = 0;
			else if (!strcmp(optarg, "1") || !strcmp(optarg, "on"))
				a_arg = 1;
			else
				errx(EXIT_FAILURE, _("invalid argument to --auto/-a option"));
			break;
		case 'c':
			c_option = 1;
			c_arg = strtoul_or_err(optarg, _("invalid argument to --changerslot/-c option"));
			break;
		case 'x':
			x_option = 1;
			x_arg = strtoul_or_err(optarg, _("invalid argument to --cdspeed/-x option"));
			break;
		case 'd':
			d_option = 1;
			break;
		case 'f':
			f_option = 1;
			break;
		case 'F':
			F_option = 1;
			break;
		case 'h':
			usage(stdout);
			break;
		case 'i':
			i_option = 1;
			if (!strcmp(optarg, "0") || !strcmp(optarg, "off"))
				i_arg = 0;
			else if (!strcmp(optarg, "1") || !strcmp(optarg, "on"))
				i_arg = 1;
			else
				errx(EXIT_FAILURE, _("invalid argument to --manualeject/-i option"));
			break;
		case 'm':
			m_option = 1;
			break;
		case 'M':
			M_option = 1;
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
		case 'X':
			X_option = 1;
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
			usage(stderr);
			break;
		}
	}

	/* check for a single additional argument */
	if ((argc - optind) > 1)
		errx(EXIT_FAILURE, _("too many arguments"));

	if ((argc - optind) == 1)
		*device = xstrdup(argv[optind]);
}

/*
 * Given name, such as foo, see if any of the following exist:
 *
 * foo (if foo starts with '.' or '/')
 * /dev/foo
 *
 * If found, return the full path. If not found, return 0.
 * Returns pointer to dynamically allocated string.
 */
static char *find_device(const char *name)
{
	if (!name)
		return NULL;

	if ((*name == '.' || *name == '/') && access(name, F_OK) == 0)
		return xstrdup(name);
	else {
		char buf[PATH_MAX];

		snprintf(buf, sizeof(buf), "/dev/%s", name);
		if (access(buf, F_OK) == 0)
			return xstrdup(buf);
	}

	return NULL;
}

/* Set or clear auto-eject mode. */
static void auto_eject(int fd, int on)
{
	int status = -1;

#if defined(CDROM_SET_OPTIONS) && defined(CDROM_CLEAR_OPTIONS)
	if (on)
		status = ioctl(fd, CDROM_SET_OPTIONS, CDO_AUTO_EJECT);
	else
		status = ioctl(fd, CDROM_CLEAR_OPTIONS, CDO_AUTO_EJECT);
#else
	errno = ENOSYS;
#endif
	if (status < 0)
		err(EXIT_FAILURE,_("CD-ROM auto-eject command failed"));
}

/*
 * Stops CDROM from opening on manual eject pressing the button.
 * This can be useful when you carry your laptop
 * in your bag while it's on and no CD inserted in it's drive.
 * Implemented as found in Documentation/ioctl/cdrom.txt
 *
 * TODO: Maybe we should check this also:
 * EDRIVE_CANT_DO_THIS   Door lock function not supported.
 * EBUSY                 Attempt to unlock when multiple users
 *                       have the drive open and not CAP_SYS_ADMIN
 */
static void manual_eject(int fd, int on)
{
	if (ioctl(fd, CDROM_LOCKDOOR, on) < 0)
		err(EXIT_FAILURE, _("CD-ROM lock door command failed"));

	if (on)
		info(_("CD-Drive may NOT be ejected with device button"));
	else
		info(_("CD-Drive may be ejected with device button"));
}

/*
 * Changer select. CDROM_SELECT_DISC is preferred, older kernels used
 * CDROMLOADFROMSLOT.
 */
static void changer_select(int fd, int slot)
{
#ifdef CDROM_SELECT_DISC
	if (ioctl(fd, CDROM_SELECT_DISC, slot) < 0)
		err(EXIT_FAILURE, _("CD-ROM select disc command failed"));

#elif defined CDROMLOADFROMSLOT
	if (ioctl(fd, CDROMLOADFROMSLOT, slot) != 0)
		err(EXIT_FAILURE, _("CD-ROM load from slot command failed"));
#else
	warnx(_("IDE/ATAPI CD-ROM changer not supported by this kernel\n") );
#endif
}

/*
 * Close tray. Not supported by older kernels.
 */
static void close_tray(int fd)
{
	int status;

#if defined(CDROMCLOSETRAY) || defined(CDIOCCLOSE)
#if defined(CDROMCLOSETRAY)
	status = ioctl(fd, CDROMCLOSETRAY);
#elif defined(CDIOCCLOSE)
	status = ioctl(fd, CDIOCCLOSE);
#endif
	if (status != 0)
		err(EXIT_FAILURE, _("CD-ROM tray close command failed"));
#else
	warnx(_("CD-ROM tray close command not supported by this kernel\n"));
#endif
}

/*
 * Eject using CDROMEJECT ioctl.
 */
static int eject_cdrom(int fd)
{
#if defined(CDROMEJECT)
	int ret = ioctl(fd, CDROM_LOCKDOOR, 0);
	if (ret < 0)
		return 0;
	return ioctl(fd, CDROMEJECT) >= 0;
#elif defined(CDIOCEJECT)
	return ioctl(fd, CDIOCEJECT) >= 0;
#else
	warnx(_("CD-ROM eject unsupported"));
	errno = ENOSYS;
	return 0;
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
static void toggle_tray(int fd)
{
	struct timeval time_start, time_stop;
	int time_elapsed;

#ifdef CDROM_DRIVE_STATUS
	/* First ask the CDROM for info, otherwise fall back to manual.  */
	switch (ioctl(fd, CDROM_DRIVE_STATUS)) {
	case CDS_TRAY_OPEN:
		close_tray(fd);
		return;

	case CDS_NO_DISC:
	case CDS_DISC_OK:
		if (!eject_cdrom(fd))
			err(EXIT_FAILURE, _("CD-ROM eject command failed"));
		return;
	case CDS_NO_INFO:
		warnx(_("no CD-ROM information available"));
		return;
	case CDS_DRIVE_NOT_READY:
		warnx(_("CD-ROM drive is not ready"));
		return;
	default:
		abort();
	}
#endif

	/* Try to open the CDROM tray and measure the time therefor
	 * needed.  In my experience the function needs less than 0.05
	 * seconds if the tray was already open, and at least 1.5 seconds
	 * if it was closed.  */
	gettimeofday(&time_start, NULL);

	/* Send the CDROMEJECT command to the device. */
	if (!eject_cdrom(fd))
		err(EXIT_FAILURE, _("CD-ROM eject command failed"));

	/* Get the second timestamp, to measure the time needed to open
	 * the tray.  */
	gettimeofday(&time_stop, NULL);

	time_elapsed = (time_stop.tv_sec * 1000000 + time_stop.tv_usec) -
		(time_start.tv_sec * 1000000 + time_start.tv_usec);

	/* If the tray "opened" too fast, we can be nearly sure, that it
	 * was already open. In this case, close it now. Else the tray was
	 * closed before. This would mean that we are done.  */
	if (time_elapsed < TRAY_WAS_ALREADY_OPEN_USECS)
		close_tray(fd);
}

/*
 * Select Speed of CD-ROM drive.
 * Thanks to Roland Krivanek (krivanek@fmph.uniba.sk)
 * http://dmpc.dbp.fmph.uniba.sk/~krivanek/cdrom_speed/
 */
static void select_speed(int fd, int speed)
{
#ifdef CDROM_SELECT_SPEED
	if (ioctl(fd, CDROM_SELECT_SPEED, speed) != 0)
		err(EXIT_FAILURE, _("CD-ROM select speed command failed"));
#else
	warnx(_("CD-ROM select speed command not supported by this kernel"));
#endif
}

/*
 * Read Speed of CD-ROM drive. From Linux 2.6.13, the current speed
 * is correctly reported
 */
static int read_speed(const char *devname)
{
	int drive_number = -1;
	char *name;
	FILE *f;

	f = fopen(_PATH_PROC_CDROMINFO, "r");
	if (!f)
		err(EXIT_FAILURE, _("cannot open %s"), _PATH_PROC_CDROMINFO);

	name = strrchr(devname, '/') + 1;

	while (name && !feof(f)) {
		char line[512];
		char *str;

		if (!fgets(line, sizeof(line), f))
			break;

		/* find drive number in line "drive name" */
		if (drive_number == -1) {
			if (strncmp(line, "drive name:", 11) == 0) {
				str = strtok(&line[11], "\t ");
				drive_number = 0;
				while (str && strncmp(name, str, strlen(name)) != 0) {
					drive_number++;
					str = strtok(NULL, "\t ");
					if (!str)
						errx(EXIT_FAILURE,
						     _("%s: failed to finding CD-ROM name"),
						     _PATH_PROC_CDROMINFO);
				}
			}
		/* find line "drive speed" and read the correct speed */
		} else {
			if (strncmp(line, "drive speed:", 12) == 0) {
				int i;

				str = strtok(&line[12], "\t ");
				for (i = 1; i < drive_number; i++)
					str = strtok(NULL, "\t ");

				if (!str)
					errx(EXIT_FAILURE,
						_("%s: failed to read speed"),
						_PATH_PROC_CDROMINFO);
				fclose(f);
				return atoi(str);
			}
		}
	}

	errx(EXIT_FAILURE, _("failed to read speed"));
}

/*
 * List Speed of CD-ROM drive.
 */
static void list_speeds(const char *name, int fd)
{
#ifdef CDROM_SELECT_SPEED
	int max_speed, curr_speed = 0, prev_speed;

	select_speed(fd, 0);
	max_speed = read_speed(name);

	while (curr_speed < max_speed) {
		prev_speed = curr_speed;
		select_speed(fd, prev_speed + 1);
		curr_speed = read_speed(name);
		if (curr_speed > prev_speed)
			printf("%d ", curr_speed);
		else
			curr_speed = prev_speed + 1;
	}

	printf("\n");
#else
	warnx(_("CD-ROM select speed command not supported by this kernel"));
#endif
}

/*
 * Eject using SCSI SG_IO commands. Return 1 if successful, 0 otherwise.
 */
static int eject_scsi(int fd)
{
	int status, k;
	sg_io_hdr_t io_hdr;
	unsigned char allowRmBlk[6] = {ALLOW_MEDIUM_REMOVAL, 0, 0, 0, 0, 0};
	unsigned char startStop1Blk[6] = {START_STOP, 0, 0, 0, 1, 0};
	unsigned char startStop2Blk[6] = {START_STOP, 0, 0, 0, 2, 0};
	unsigned char inqBuff[2];
	unsigned char sense_buffer[32];

	if ((ioctl(fd, SG_GET_VERSION_NUM, &k) < 0) || (k < 30000)) {
		verbose(_("not an sg device, or old sg driver"));
		return 0;
	}

	memset(&io_hdr, 0, sizeof(sg_io_hdr_t));
	io_hdr.interface_id = 'S';
	io_hdr.cmd_len = 6;
	io_hdr.mx_sb_len = sizeof(sense_buffer);
	io_hdr.dxfer_direction = SG_DXFER_NONE;
	io_hdr.dxfer_len = 0;
	io_hdr.dxferp = inqBuff;
	io_hdr.sbp = sense_buffer;
	io_hdr.timeout = 10000;

	io_hdr.cmdp = allowRmBlk;
	status = ioctl(fd, SG_IO, (void *)&io_hdr);
	if (status < 0 || io_hdr.host_status || io_hdr.driver_status)
		return 0;

	io_hdr.cmdp = startStop1Blk;
	status = ioctl(fd, SG_IO, (void *)&io_hdr);
	if (status < 0 || io_hdr.host_status)
		return 0;

	/* Ignore errors when there is not medium -- in this case driver sense
	 * buffer sets MEDIUM NOT PRESENT (3a) bit. For more details see:
	 * http://www.tldp.org/HOWTO/archived/SCSI-Programming-HOWTO/SCSI-Programming-HOWTO-22.html#sec-sensecodes
	 * -- kzak Jun 2013
	 */
	if (io_hdr.driver_status != 0 &&
	    !(io_hdr.driver_status == DRIVER_SENSE && io_hdr.sbp &&
		                                      io_hdr.sbp[12] == 0x3a))
		return 0;

	io_hdr.cmdp = startStop2Blk;
	status = ioctl(fd, SG_IO, (void *)&io_hdr);
	if (status < 0 || io_hdr.host_status || io_hdr.driver_status)
		return 0;

	/* force kernel to reread partition table when new disc inserted */
	ioctl(fd, BLKRRPART);
	return 1;
}

/*
 * Eject using FDEJECT ioctl. Return 1 if successful, 0 otherwise.
 */
static int eject_floppy(int fd)
{
	return ioctl(fd, FDEJECT) >= 0;
}


/*
 * Rewind and eject using tape ioctl. Return 1 if successful, 0 otherwise.
 */
static int eject_tape(int fd)
{
	struct mtop op = { .mt_op = MTOFFL, .mt_count = 0 };

	return ioctl(fd, MTIOCTOP, &op) >= 0;
}


/* umount a device. */
static void umount_one(const char *name)
{
	int status;

	if (!name)
		return;

	verbose(_("%s: unmounting"), name);

	switch (fork()) {
	case 0: /* child */
		if (setgid(getgid()) < 0)
			err(EXIT_FAILURE, _("cannot set group id"));

		if (setuid(getuid()) < 0)
			err(EXIT_FAILURE, _("cannot set user id"));

		if (p_option)
			execl("/bin/umount", "/bin/umount", name, "-n", NULL);
		else
			execl("/bin/umount", "/bin/umount", name, NULL);

		errx(EXIT_FAILURE, _("unable to exec /bin/umount of `%s'"), name);

	case -1:
		warn( _("unable to fork"));
		break;

	default: /* parent */
		wait(&status);
		if (WIFEXITED(status) == 0)
			errx(EXIT_FAILURE,
			     _("unmount of `%s' did not exit normally"), name);

		if (WEXITSTATUS(status) != 0)
			errx(EXIT_FAILURE, _("unmount of `%s' failed\n"), name);
		break;
	}
}

/* Open a device file. */
static int open_device(const char *name)
{
	int fd = open(name, O_RDWR|O_NONBLOCK);

	if (fd < 0)
		fd = open(name, O_RDONLY|O_NONBLOCK);
	if (fd == -1)
		err(EXIT_FAILURE, _("cannot open %s"), name);
	return fd;
}

/*
 * See if device has been mounted by looking in mount table.  If so, set
 * device name and mount point name, and return 1, otherwise return 0.
 */
static int device_get_mountpoint(char **devname, char **mnt)
{
	struct libmnt_fs *fs;
	int rc;

	*mnt = NULL;

	if (!mtab) {
		struct libmnt_cache *cache;

		mtab = mnt_new_table();
		if (!mtab)
			err(EXIT_FAILURE, _("failed to initialize libmount table"));

		cache = mnt_new_cache();
		mnt_table_set_cache(mtab, cache);
		mnt_unref_cache(cache);

		if (p_option)
			rc = mnt_table_parse_file(mtab, _PATH_PROC_MOUNTINFO);
		else
			rc = mnt_table_parse_mtab(mtab, NULL);
		if (rc)
			err(EXIT_FAILURE, _("failed to parse mount table"));
	}

	fs = mnt_table_find_source(mtab, *devname, MNT_ITER_BACKWARD);
	if (!fs) {
		/* maybe 'devname' is mountpoint rather than a real device */
		fs = mnt_table_find_target(mtab, *devname, MNT_ITER_BACKWARD);
		if (fs) {
			free(*devname);
			*devname = xstrdup(mnt_fs_get_source(fs));
		}
	}

	if (fs)
		*mnt = xstrdup(mnt_fs_get_target(fs));
	return *mnt ? 0 : -1;
}

static char *get_disk_devname(const char *device)
{
	struct stat st;
	dev_t diskno = 0;
	char diskname[128];

	if (stat(device, &st) != 0)
		return NULL;

	/* get whole-disk devno */
	if (sysfs_devno_to_wholedisk(st.st_rdev, diskname,
				sizeof(diskname), &diskno) != 0)
		return NULL;

	return st.st_rdev == diskno ? NULL : find_device(diskname);
}

static int umount_partitions(const char *disk, int checkonly)
{
	struct sysfs_cxt cxt = UL_SYSFSCXT_EMPTY;
	dev_t devno;
	DIR *dir = NULL;
	struct dirent *d;
	int count = 0;

	devno = sysfs_devname_to_devno(disk, NULL);
	if (sysfs_init(&cxt, devno, NULL) != 0)
		return 0;

	/* open /sys/block/<wholedisk> */
	if (!(dir = sysfs_opendir(&cxt, NULL)))
		goto done;

	/* scan for partition subdirs */
	while ((d = readdir(dir))) {
		if (!strcmp(d->d_name, ".") || !strcmp(d->d_name, ".."))
			continue;

		if (sysfs_is_partition_dirent(dir, d, disk)) {
			char *mnt = NULL;
			char *dev = find_device(d->d_name);

			if (dev && device_get_mountpoint(&dev, &mnt) == 0) {
				verbose(_("%s: mounted on %s"), dev, mnt);
				if (!checkonly)
					umount_one(mnt);
				count++;
			}
			free(dev);
			free(mnt);
		}
	}

done:
	if (dir)
		closedir(dir);
	sysfs_deinit(&cxt);

	return count;
}

static int is_hotpluggable_subsystem(const char *name)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(hotplug_subsystems); i++)
		if (strcmp(name, hotplug_subsystems[i]) == 0)
			return 1;

	return 0;
}

#define SUBSYSTEM_LINKNAME	"/subsystem"

/*
 * For example:
 *
 * chain: /sys/dev/block/../../devices/pci0000:00/0000:00:1a.0/usb1/1-1/1-1.2/ \
 *                           1-1.2:1.0/host65/target65:0:0/65:0:0:0/block/sdb
 *
 * The function check if <chain>/subsystem symlink exists, if yes then returns
 * basename of the readlink result, and remove the last subdirectory from the
 * <chain> path.
 */
static char *get_subsystem(char *chain, char *buf, size_t bufsz)
{
	size_t len;
	char *p;

	if (!chain || !*chain)
		return NULL;

	len = strlen(chain);
	if (len + sizeof(SUBSYSTEM_LINKNAME) > PATH_MAX)
		return NULL;

	do {
		ssize_t sz;

		/* append "/subsystem" to the path */
		memcpy(chain + len, SUBSYSTEM_LINKNAME, sizeof(SUBSYSTEM_LINKNAME));

		/* try if subsystem symlink exists */
		sz = readlink(chain, buf, bufsz - 1);

		/* remove last subsystem from chain */
		chain[len] = '\0';
		p = strrchr(chain, '/');
		if (p) {
			*p = '\0';
			len = p - chain;
		}

		if (sz > 0) {
			/* we found symlink to subsystem, return basename */
			buf[sz] = '\0';
			return basename(buf);
		}

	} while (p);

	return NULL;
}

static int is_hotpluggable(const char* device)
{
	struct sysfs_cxt cxt = UL_SYSFSCXT_EMPTY;
	char devchain[PATH_MAX];
	char subbuf[PATH_MAX];
	dev_t devno;
	int rc = 0;
	ssize_t sz;
	char *sub;

	devno = sysfs_devname_to_devno(device, NULL);
	if (sysfs_init(&cxt, devno, NULL) != 0)
		return 0;

	/* check /sys/dev/block/<maj>:<min>/removable attribute */
	if (sysfs_read_int(&cxt, "removable", &rc) == 0 && rc == 1) {
		verbose(_("%s: is removable device"), device);
		goto done;
	}

	/* read /sys/dev/block/<maj>:<min> symlink */
	sz = sysfs_readlink(&cxt, NULL, devchain, sizeof(devchain));
	if (sz <= 0 || sz + sizeof(_PATH_SYS_DEVBLOCK "/") > sizeof(devchain))
		goto done;

	devchain[sz++] = '\0';

	/* create absolute patch from the link */
	memmove(devchain + sizeof(_PATH_SYS_DEVBLOCK "/") - 1, devchain, sz);
	memcpy(devchain, _PATH_SYS_DEVBLOCK "/",
	       sizeof(_PATH_SYS_DEVBLOCK "/") - 1);

	while ((sub = get_subsystem(devchain, subbuf, sizeof(subbuf)))) {
		rc = is_hotpluggable_subsystem(sub);
		if (rc) {
			verbose(_("%s: connected by hotplug subsystem: %s"),
				device, sub);
			break;
		}
	}

done:
	sysfs_deinit(&cxt);
	return rc;
}


/* handle -x option */
static void set_device_speed(char *name)
{
	int fd;

	if (!x_option)
		return;

	if (x_arg == 0)
		verbose(_("setting CD-ROM speed to auto"));
	else
		verbose(_("setting CD-ROM speed to %ldX"), x_arg);

	fd = open_device(name);
	select_speed(fd, x_arg);
	exit(EXIT_SUCCESS);
}


/* main program */
int main(int argc, char **argv)
{
	char *device = NULL;
	char *disk = NULL;
	char *mountpoint = NULL;
	int worked = 0;    /* set to 1 when successfully ejected */
	int fd;            /* file descriptor for device */

	setlocale(LC_ALL,"");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	/* parse the command line arguments */
	parse_args(argc, argv, &device);

	/* handle -d option */
	if (d_option) {
		info(_("default device: `%s'"), EJECT_DEFAULT_DEVICE);
		return EXIT_SUCCESS;
	}

	if (!device) {
		device = mnt_resolve_path(EJECT_DEFAULT_DEVICE, NULL);
		verbose(_("using default device `%s'"), device);
	} else {
		char *p;

		if (device[strlen(device)-1] == '/')
			device[strlen(device)-1] = '\0';

		/* figure out full device or mount point name */
		p = find_device(device);
		if (p)
			free(device);
		else
			p = device;

		device = mnt_resolve_spec(p, NULL);
		free(p);
	}

	if (!device)
		errx(EXIT_FAILURE, _("%s: unable to find device"), device);

	verbose(_("device name is `%s'"), device);

	device_get_mountpoint(&device, &mountpoint);
	if (mountpoint)
		verbose(_("%s: mounted on %s"), device, mountpoint);
	else
		verbose(_("%s: not mounted"), device);

	disk = get_disk_devname(device);
	if (disk) {
		verbose(_("%s: disc device: %s (disk device will be used for eject)"), device, disk);
		free(device);
		device = disk;
		disk = NULL;
	} else {
		struct stat st;

		if (stat(device, &st) != 0 || !S_ISBLK(st.st_mode))
			errx(EXIT_FAILURE, _("%s: not found mountpoint or device "
					"with the given name"), device);

		verbose(_("%s: is whole-disk device"), device);
	}

	if (F_option == 0 && is_hotpluggable(device) == 0)
		errx(EXIT_FAILURE, _("%s: is not hot-pluggable device"), device);

	/* handle -n option */
	if (n_option) {
		info(_("device is `%s'"), device);
		verbose(_("exiting due to -n/--noop option"));
		return EXIT_SUCCESS;
	}

	/* handle -i option */
	if (i_option) {
		fd = open_device(device);
		manual_eject(fd, i_arg);
		return EXIT_SUCCESS;
	}

	/* handle -a option */
	if (a_option) {
		if (a_arg)
			verbose(_("%s: enabling auto-eject mode"), device);
		else
			verbose(_("%s: disabling auto-eject mode"), device);
		fd = open_device(device);
		auto_eject(fd, a_arg);
		return EXIT_SUCCESS;
	}

	/* handle -t option */
	if (t_option) {
		verbose(_("%s: closing tray"), device);
		fd = open_device(device);
		close_tray(fd);
		set_device_speed(device);
		return EXIT_SUCCESS;
	}

	/* handle -T option */
	if (T_option) {
		verbose(_("%s: toggling tray"), device);
		fd = open_device(device);
		toggle_tray(fd);
		set_device_speed(device);
		return EXIT_SUCCESS;
	}

	/* handle -X option */
	if (X_option) {
		verbose(_("%s: listing CD-ROM speed"), device);
		fd = open_device(device);
		list_speeds(device, fd);
		return EXIT_SUCCESS;
	}

	/* handle -x option only */
	if (!c_option)
		set_device_speed(device);


	/*
	 * Unmount all partitions if -m is not specified; or umount given
	 * mountpoint if -M is specified, otherwise print error of another
	 * partition is mounted.
	 */
	if (!m_option) {
		int ct = umount_partitions(device, M_option);

		if (ct == 0 && mountpoint)
			umount_one(mountpoint); /* probably whole-device */

		if (M_option) {
			if (ct == 1 && mountpoint)
				umount_one(mountpoint);
			else if (ct)
				errx(EXIT_FAILURE, _("error: %s: device in use"), device);
		}
	}

	/* handle -c option */
	if (c_option) {
		verbose(_("%s: selecting CD-ROM disc #%ld"), device, c_arg);
		fd = open_device(device);
		changer_select(fd, c_arg);
		set_device_speed(device);
		return EXIT_SUCCESS;
	}

	/* if user did not specify type of eject, try all four methods */
	if (r_option + s_option + f_option + q_option == 0)
		r_option = s_option = f_option = q_option = 1;

	/* open device */
	fd = open_device(device);

	/* try various methods of ejecting until it works */
	if (r_option) {
		verbose(_("%s: trying to eject using CD-ROM eject command"), device);
		worked = eject_cdrom(fd);
		verbose(worked ? _("CD-ROM eject command succeeded") :
				 _("CD-ROM eject command failed"));
	}

	if (s_option && !worked) {
		verbose(_("%s: trying to eject using SCSI commands"), device);
		worked = eject_scsi(fd);
		verbose(worked ? _("SCSI eject succeeded") :
				 _("SCSI eject failed"));
	}

	if (f_option && !worked) {
		verbose(_("%s: trying to eject using floppy eject command"), device);
		worked = eject_floppy(fd);
		verbose(worked ? _("floppy eject command succeeded") :
				 _("floppy eject command failed"));
	}

	if (q_option && !worked) {
		verbose(_("%s: trying to eject using tape offline command"), device);
		worked = eject_tape(fd);
		verbose(worked ? _("tape offline command succeeded") :
				 _("tape offline command failed"));
	}

	if (!worked)
		errx(EXIT_FAILURE, _("unable to eject"));

	/* cleanup */
	close(fd);
	free(device);
	free(mountpoint);

	mnt_unref_table(mtab);

	return EXIT_SUCCESS;
}
