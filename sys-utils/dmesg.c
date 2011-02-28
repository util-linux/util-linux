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
#include <sys/klog.h>
#include <ctype.h>

#include "c.h"
#include "nls.h"
#include "strutils.h"
#include "xalloc.h"

static void __attribute__ ((noreturn)) usage(void)
{
	fprintf(stderr,
		_("Usage: %s [-c] [-n level] [-r] [-s bufsize]\n"),
		program_invocation_short_name);
	exit(EXIT_FAILURE);

}

int main(int argc, char *argv[])
{
	char *buf = NULL;
	int  sz;
	int  bufsize = 0;
	int  i;
	int  n;
	int  c;
	int  level = 0;
	int  lastc;
	int  cmd = 3;		/* Read all messages in the ring buffer */
	int  raw = 0;

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

	while ((c = getopt(argc, argv, "crn:s:")) != -1) {
		switch (c) {
		case 'c':
			cmd = 4;	/* Read and clear all messages */
			break;
		case 'n':
			cmd = 8;	/* Set level of messages */
			level = strtol_or_err(optarg, _("failed to parse level"));
			break;
		case 'r':
			raw = 1;
			break;
		case 's':
			bufsize = strtol_or_err(optarg, _("failed to parse buffer size"));
			if (bufsize < 4096)
				bufsize = 4096;
			break;
		case '?':
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (argc > 1)
		usage();

	if (cmd == 8) {
		n = klogctl(cmd, NULL, level);
		if (n < 0)
			err(EXIT_FAILURE, _("klogctl failed"));

		return EXIT_SUCCESS;
	}

	if (!bufsize) {
		n = klogctl(10, NULL, 0);	/* read ringbuffer size */
		if (n > 0)
			bufsize = n;
	}

	if (bufsize) {
		sz = bufsize + 8;
		buf = xmalloc(sz * sizeof(char));
		n = klogctl(cmd, buf, sz);
	} else {
		sz = 16392;
		while (1) {
			buf = xmalloc(sz * sizeof(char));
			n = klogctl(3, buf, sz);	/* read only */
			if (n != sz || sz > (1 << 28))
				break;
			free(buf);
			sz *= 4;
		}

		if (n > 0 && cmd == 4)
			n = klogctl(cmd, buf, sz);	/* read and clear */
	}

	if (n < 0)
		err(EXIT_FAILURE, _("klogctl failed"));

	lastc = '\n';
	for (i = 0; i < n; i++) {
		if (!raw && (i == 0 || buf[i - 1] == '\n') && buf[i] == '<') {
			i++;
			while (isdigit(buf[i]))
				i++;
			if (buf[i] == '>')
				i++;
		}
		lastc = buf[i];
		putchar(lastc);
	}
	if (lastc != '\n')
		putchar('\n');
	free(buf);

	return EXIT_SUCCESS;
}
