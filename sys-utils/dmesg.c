/* dmesg.c -- Print out the contents of the kernel ring buffer
 * Created: Sat Oct  9 16:19:47 1993
 * Revised: Thu Oct 28 21:52:17 1993 by faith@cs.unc.edu
 * Copyright 1993 Theodore Ts'o (tytso@athena.mit.edu)
 * This program comes with ABSOLUTELY NO WARRANTY.
 * Modifications by Rick Sladkey (jrs@world.std.com)
 * Larger buffersize 3 June 1998 by Nicolai Langfeldt, based on a patch
 * by Peeter Joot.  This was also suggested by John Hudson.
 * 1999-02-22 Arkadiusz Mi¶kiewicz <misiek@pld.ORG.PL>
 * - added Native Language Support
 *
 */

/*
 * Commands to sys_syslog:
 *
 *      0 -- Close the log.  Currently a NOP.
 *      1 -- Open the log. Currently a NOP.
 *      2 -- Read from the log.
 *      3 -- Read all messages remaining in the ring buffer.
 *      4 -- Read and clear all messages remaining in the ring buffer
 *      5 -- Clear ring buffer.
 *      6 -- Disable printk's to console
 *      7 -- Enable printk's to console
 *      8 -- Set level of messages printed to console
 *      9 -- Return number of unread characters in the log buffer
 *           [supported since 2.4.10]
 *
 * Only function 3 is allowed to non-root processes.
 */

#include <linux/unistd.h>
#include <stdio.h>
#include <getopt.h>
#include <stdlib.h>
#include "nls.h"

#if __GNU_LIBRARY__ < 5

#ifndef __alpha__
# define __NR_klogctl __NR_syslog
  static inline _syscall3(int, klogctl, int, type, char *, b, int, len);
#else /* __alpha__ */
#define klogctl syslog
#endif

#else
# include <sys/klog.h>
#endif

static char *progname;

static void
usage(void) {
	fprintf(stderr,
		_("Usage: %s [-c] [-n level] [-s bufsize]\n"), progname);
}

int
main(int argc, char *argv[]) {
	char *buf;
	int  sz;
	int  bufsize = 0;
	int  i;
	int  n;
	int  c;
	int  level = 0;
	int  lastc;
	int  cmd = 3;		/* Read all messages in the ring buffer */

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

	progname = argv[0];
	while ((c = getopt(argc, argv, "cn:s:")) != -1) {
		switch (c) {
		case 'c':
			cmd = 4;	/* Read and clear all messages */
			break;
		case 'n':
			cmd = 8;	/* Set level of messages */
			level = atoi(optarg);
			break;
		case 's':
			bufsize = atoi(optarg);
			if (bufsize < 4096)
				bufsize = 4096;
			break;
		case '?':
		default:
			usage();
			exit(1);
		}
	}
	argc -= optind;
	argv += optind;
   
	if (argc > 1) {
		usage();
		exit(1);
	}

	if (cmd == 8) {
		n = klogctl(cmd, NULL, level);
		if (n < 0) {
			perror("klogctl");
			exit(1);
		}
		exit(0);
	}

	if (!bufsize) {
		n = klogctl(10, NULL, 0);	/* read ringbuffer size */
		if (n > 0)
			bufsize = n;
	}

	if (bufsize) {
		sz = bufsize + 8;
		buf = (char *) malloc(sz);
		n = klogctl(cmd, buf, sz);
	} else {
		sz = 16392;
		while (1) {
			buf = (char *) malloc(sz);
			n = klogctl(3, buf, sz);	/* read only */
			if (n != sz || sz > (1<<28))
				break;
			free(buf);
			sz *= 4;
		}

		if (n > 0 && cmd == 4)
			n = klogctl(cmd, buf, sz);	/* read and clear */
	}

	if (n < 0) {
		perror("klogctl");
		exit(1);
	}

	lastc = '\n';
	for (i = 0; i < n; i++) {
		if ((i == 0 || buf[i - 1] == '\n') && buf[i] == '<') {
			i++;
			while (buf[i] >= '0' && buf[i] <= '9')
				i++;
			if (buf[i] == '>')
				i++;
		}
		lastc = buf[i];
		putchar(lastc);
	}
	if (lastc != '\n')
		putchar('\n');
	return 0;
}
