/*
 * raw.c: User mode tool to bind and query raw character devices.
 *
 * Stephen Tweedie, 1999, 2000
 *
 * This file may be redistributed under the terms of the GNU General
 * Public License, version 2.
 * 
 * Copyright Red Hat Software, 1999, 2000
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/sysmacros.h>
#include <linux/raw.h>
#include <linux/major.h>

#ifdef OLD_RAW_DEVS
#define RAWCTLDEV "/dev/raw"
#define RAWDEVDIR "/dev/"
#else
#define RAWCTLDEV "/dev/rawctl"
#define RAWDEVDIR "/dev/raw/"
#endif
#define DEVFS_RAWCTLDEV "/dev/raw/rawctl"


char *	progname;
int	do_query = 0;
int	do_query_all = 0;

int	master_fd;
int	raw_minor;

void open_raw_ctl(void);
int  query(int minor, int quiet);
int  bind (int minor, int block_major, int block_minor);


static void usage(int err) 
{
	fprintf(stderr,
		"Usage:\n"
		"  %s " RAWDEVDIR "rawN <major> <minor>\n"
		"  %s " RAWDEVDIR "rawN /dev/<blockdev>\n"
		"  %s -q " RAWDEVDIR "rawN\n"
		"  %s -qa\n",
		progname, progname, progname, progname);
	exit(err);
}


int main(int argc, char *argv[])
{
	int  c;
	char * raw_name;
	char * block_name;
	int  err;
	int  block_major, block_minor;	
	int  i;

	struct stat statbuf;
	
	progname = argv[0];
	
	while ((c = getopt(argc, argv, "ahq")) != -1) {
		switch (c) {
		case 'a':
			do_query_all = 1;
			break;
		case 'h':
			usage(0);
		case 'q':
			do_query = 1;
			break;
		default:
			usage(1);
		}
	}
	
	/*
	 * Check for, and open, the master raw device, /dev/raw
	 */
	
	open_raw_ctl();
	
	if (do_query_all) {
		if (optind < argc)
			usage(1);
		for (i=1; i<255; i++)
			query(i, 1);
		exit(0);
	}
	
	/*
	 * It's a bind or a single query.  Either way we need a raw device.
	 */

	if (optind >= argc)
		usage(1);
	raw_name = argv[optind++];

	err = stat(raw_name, &statbuf);
	if (err) {
		fprintf (stderr, "Cannot locate raw device '%s' (%s)\n",
			 raw_name, strerror(errno));
		exit(2);
	}
	
	if (!S_ISCHR(statbuf.st_mode)) {
		fprintf (stderr, "raw device '%s' is not a character dev\n",
			 raw_name);
		exit(2);
	}
	if (major(statbuf.st_rdev) != RAW_MAJOR) {
		fprintf (stderr, "Device '%s' is not a raw dev\n",
			 raw_name);
		exit(2);
	}

	raw_minor = minor(statbuf.st_rdev);

	if (do_query)
		return query(raw_minor, 0);
	
	/* 
	 * It's not a query, so we still have some parsing to do.  Have
	 * we been given a block device filename or a major/minor pair? 
	 */

	switch (argc - optind) {
	case 1:
		block_name = argv[optind];
		err = stat(block_name, &statbuf);
		if (err) {
			fprintf (stderr,
				 "Cannot locate block device '%s' (%s)\n",
				 block_name, strerror(errno));
			exit(2);
		}
		
		if (!S_ISBLK(statbuf.st_mode)) {
			fprintf (stderr, "Device '%s' is not a block dev\n",
				 block_name);
			exit(2);
		}
		
		block_major = major(statbuf.st_rdev);
		block_minor = minor(statbuf.st_rdev);
		break;
				
	case 2:
		block_major = strtol(argv[optind], 0, 0);
		block_minor = strtol(argv[optind+1], 0, 0);
		break;
		
	default:
		block_major = block_minor = 0; /* just to keep gcc happy */
		usage(1);
	}
	
	return bind(raw_minor, block_major, block_minor);
	return 0;
	
}


void open_raw_ctl(void)
{
	int errsv;

	master_fd = open(RAWCTLDEV, O_RDWR, 0);
	if (master_fd < 0) {
		errsv = errno;
		master_fd = open(DEVFS_RAWCTLDEV, O_RDWR, 0);
		if (master_fd < 0) {
			fprintf (stderr, 
				 "Cannot open master raw device '"
				 RAWCTLDEV
				 "' (%s)\n", strerror(errsv));
			exit(2);
		}
	}
}

int query(int minor, int quiet)
{
	struct raw_config_request rq;
	int err;
	
	rq.raw_minor = minor;
	err = ioctl(master_fd, RAW_GETBIND, &rq);
	if (err < 0) {
		if (quiet && errno == ENODEV)
			return 3;
		fprintf (stderr, 
			 "Error querying raw device (%s)\n",
			 strerror(errno));
		exit(3);
	}
	if (quiet && !rq.block_major && !rq.block_minor)
		return 0;
	printf (RAWDEVDIR "raw%d:	bound to major %d, minor %d\n",
		minor, (int) rq.block_major, (int) rq.block_minor);
	return 0;
}

int bind(int minor, int block_major, int block_minor)
{
	struct raw_config_request rq;
	int err;
	
	rq.raw_minor   = minor;
	rq.block_major = block_major;
	rq.block_minor = block_minor;
	err = ioctl(master_fd, RAW_SETBIND, &rq);
	if (err < 0) {
		fprintf (stderr, 
			 "Error setting raw device (%s)\n",
			 strerror(errno));
		exit(3);
	}
	printf (RAWDEVDIR "raw%d:	bound to major %d, minor %d\n",
		raw_minor, (int) rq.block_major, (int) rq.block_minor);
	return 0;
}

