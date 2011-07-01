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

/* Close the log.  Currently a NOP. */
#define SYSLOG_ACTION_CLOSE          0
/* Open the log. Currently a NOP. */
#define SYSLOG_ACTION_OPEN           1
/* Read from the log. */
#define SYSLOG_ACTION_READ           2
/* Read all messages remaining in the ring buffer. (allowed for non-root) */
#define SYSLOG_ACTION_READ_ALL       3
/* Read and clear all messages remaining in the ring buffer */
#define SYSLOG_ACTION_READ_CLEAR     4
/* Clear ring buffer. */
#define SYSLOG_ACTION_CLEAR          5
/* Disable printk's to console */
#define SYSLOG_ACTION_CONSOLE_OFF    6
/* Enable printk's to console */
#define SYSLOG_ACTION_CONSOLE_ON     7
/* Set level of messages printed to console */
#define SYSLOG_ACTION_CONSOLE_LEVEL  8
/* Return number of unread characters in the log buffer */
#define SYSLOG_ACTION_SIZE_UNREAD    9
/* Return size of the log buffer */
#define SYSLOG_ACTION_SIZE_BUFFER   10

/* dmesg flags */
#define DMESG_FL_RAW	(1 << 1)

static void __attribute__((__noreturn__)) usage(FILE *out)
{
	fprintf(out, _(
		"\nUsage:\n"
		" %s [options]\n"), program_invocation_short_name);

	fprintf(out, _(
		"\nOptions:\n"
		" -c, --read-clear          read and clear all messages\n"
		" -r, --raw                 print the raw message buffer\n"
		" -s, --buffer-size=SIZE    buffer size to query the kernel ring buffer\n"
		" -n, --console-level=LEVEL set level of messages printed to console\n"
		" -V, --version             output version information and exit\n"
		" -h, --help                display this help and exit\n\n"));

	exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}

static int get_buffer_size()
{
	int n = klogctl(SYSLOG_ACTION_SIZE_BUFFER, NULL, 0);

	return n > 0 ? n : 0;
}

static int read_buffer(char **buf, size_t bufsize, int clear)
{
	size_t sz;
	int rc = -1;
	int cmd = clear ? SYSLOG_ACTION_READ_CLEAR :
			  SYSLOG_ACTION_READ_ALL;

	if (bufsize) {
		sz = bufsize + 8;
		*buf = xmalloc(sz * sizeof(char));
		rc = klogctl(cmd, *buf, sz);
	} else {
		sz = 16392;
		while (1) {
			*buf = xmalloc(sz * sizeof(char));
			rc = klogctl(SYSLOG_ACTION_READ_ALL, *buf, sz);
			if (rc != sz || sz > (1 << 28))
				break;
			free(*buf);
			*buf = NULL;
			sz *= 4;
		}

		if (rc > 0 && clear)
			rc = klogctl(SYSLOG_ACTION_READ_CLEAR, *buf, sz);
	}

	return rc;
}

static void print_buffer(const char *buf, size_t size, int flags)
{
	int lastc = '\n';
	int i;

	for (i = 0; i < size; i++) {
		if (!(flags & DMESG_FL_RAW) &&
		    (i == 0 || buf[i - 1] == '\n') && buf[i] == '<') {
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
}

int main(int argc, char *argv[])
{
	char *buf = NULL;
	int  bufsize = 0;
	int  n;
	int  c;
	int  console_level = 0;
	int  cmd = SYSLOG_ACTION_READ_ALL;
	int  flags = 0;

	static const struct option longopts[] = {
		{ "read-clear",    no_argument,	      NULL, 'c' },
		{ "raw",           no_argument,       NULL, 'r' },
		{ "buffer-size",   required_argument, NULL, 's' },
		{ "console-level", required_argument, NULL, 'n' },
		{ "version",       no_argument,	      NULL, 'V' },
		{ "help",          no_argument,	      NULL, 'h' },
		{ NULL,	           0, NULL, 0 }
	};

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

	while ((c = getopt_long(argc, argv, "chrn:s:V", longopts, NULL)) != -1) {
		switch (c) {
		case 'c':
			cmd = SYSLOG_ACTION_READ_CLEAR;
			break;
		case 'n':
			cmd = SYSLOG_ACTION_CONSOLE_LEVEL;
			console_level = strtol_or_err(optarg,
						_("failed to parse level"));
			break;
		case 'r':
			flags |= DMESG_FL_RAW;
			break;
		case 's':
			bufsize = strtol_or_err(optarg, _("failed to parse buffer size"));
			if (bufsize < 4096)
				bufsize = 4096;
			break;
		case 'V':
			printf(_("%s from %s\n"), program_invocation_short_name,
						  PACKAGE_STRING);
			return EXIT_SUCCESS;
		case 'h':
			usage(stdout);
			break;
		case '?':
		default:
			usage(stderr);
		}
	}
	argc -= optind;
	argv += optind;

	if (argc > 1)
		usage(stderr);

	switch (cmd) {
	case SYSLOG_ACTION_READ_ALL:
	case SYSLOG_ACTION_READ_CLEAR:
		if (!bufsize)
			bufsize = get_buffer_size();
		n = read_buffer(&buf, bufsize, cmd == SYSLOG_ACTION_READ_CLEAR);
		if (n > 0)
			print_buffer(buf, n, flags);
		free(buf);
		break;
	case SYSLOG_ACTION_CONSOLE_LEVEL:
		n = klogctl(cmd, NULL, console_level);
		break;
	default:
		errx(EXIT_FAILURE, _("unsupported command"));
		break;
	}

	if (n < 0)
		err(EXIT_FAILURE, _("klogctl failed"));

	return EXIT_SUCCESS;
}
