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
#include <sys/syslog.h>
#include <ctype.h>

#include "c.h"
#include "nls.h"
#include "strutils.h"
#include "xalloc.h"
#include "widechar.h"
#include "writeall.h"
#include "bitops.h"

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

#ifndef LOG_PRIMASK
#define	LOG_PRIMASK	0x07	/* mask to extract priority part (internal) */
				/* extract priority */
#define	LOG_PRI(p)	((p) & LOG_PRIMASK)
#endif

#ifndef LOG_FACMASK
#define	LOG_FACMASK	0x03f8	/* mask to extract facility part */
				/* facility of pri */
#define	LOG_FAC(p)	(((p) & LOG_FACMASK) >> 3)
#endif

/*
 * Priority names, based on sys/syslog.h
 */
struct dmesg_name {
	const char *name;
	const char *help;
};

static const struct dmesg_name level_names[] =
{
	[LOG_EMERG]   = { "emerg", N_("system is unusable") },
	[LOG_ALERT]   = { "alert", N_("action must be taken immediately") },
	[LOG_CRIT]    = { "crit",  N_("critical conditions") },
	[LOG_ERR]     = { "err",   N_("error conditions") },
	[LOG_WARNING] = { "warn",  N_("warning conditions") },
	[LOG_NOTICE]  = { "notice",N_("normal but significant condition") },
	[LOG_INFO]    = { "info",  N_("informational") },
	[LOG_DEBUG]   = { "debug", N_("debug-level messages") }
};

/*
 * sys/syslog.h uses (f << 3) for all facility codes.
 * We want to use the codes as array idexes, so shift back...
 *
 * Note that libc LOG_FAC() macro returns the base codes, not the
 * shifted code :-)
 */
#define FAC_BASE(f)	((f) >> 3)

static const struct dmesg_name facility_names[] =
{
	[FAC_BASE(LOG_KERN)]     = { "kern",     N_("kernel messages") },
	[FAC_BASE(LOG_USER)]     = { "user",     N_("random user-level messages") },
	[FAC_BASE(LOG_MAIL)]     = { "mail",     N_("mail system") },
	[FAC_BASE(LOG_DAEMON)]   = { "daemon",   N_("system daemons") },
	[FAC_BASE(LOG_AUTH)]     = { "auth",     N_("security/authorization messages") },
	[FAC_BASE(LOG_SYSLOG)]   = { "syslog",   N_("messages generated internally by syslogd") },
	[FAC_BASE(LOG_LPR)]      = { "lpr",      N_("line printer subsystem") },
	[FAC_BASE(LOG_NEWS)]     = { "news",     N_("network news subsystem") },
	[FAC_BASE(LOG_UUCP)]     = { "uucp",     N_("UUCP subsystem") },
	[FAC_BASE(LOG_CRON)]     = { "cron",     N_("clock daemon") },
	[FAC_BASE(LOG_AUTHPRIV)] = { "authpriv", N_("security/authorization messages (private)") },
	[FAC_BASE(LOG_FTP)]      = { "ftp",      N_("ftp daemon") },
};

/* dmesg flags */
#define DMESG_FL_RAW		(1 << 1)
#define DMESG_FL_LEVEL		(1 << 2)
#define DMESG_FL_FACILITY	(1 << 3)
#define DMESG_FL_DECODE		(1 << 4)

static void __attribute__((__noreturn__)) usage(FILE *out)
{
	int i;

	fprintf(out, _(
		"\nUsage:\n"
		" %s [options]\n"), program_invocation_short_name);

	fprintf(out, _(
		"\nOptions:\n"
		" -C, --clear               clear the kernel ring buffer\n"
		" -c, --read-clear          read and clear all messages\n"
		" -d, --console-off         disable printing messages to console\n"
		" -e, --console-on          enable printing messages to console\n"
		" -x, --decode              decode facility and level to readable string\n"
		" -h, --help                display this help and exit\n"
		" -l, --level=LIST          restrict output to defined levels\n"
		" -n, --console-level=LEVEL set level of messages printed to console\n"
		" -r, --raw                 print the raw message buffer\n"
		" -s, --buffer-size=SIZE    buffer size to query the kernel ring buffer\n"
		" -V, --version             output version information and exit\n\n"));

	fprintf(out, _("Supported log facilities:\n"));
	for (i = 0; i < ARRAY_SIZE(level_names); i++) {
		fprintf(stderr, " %7s - %s\n",
				facility_names[i].name,
				_(facility_names[i].help));
	}

	fprintf(out, _("\nSupported log levels (priorities):\n"));
	for (i = 0; i < ARRAY_SIZE(level_names); i++) {
		fprintf(stderr, " %7s - %s\n",
				level_names[i].name,
				_(level_names[i].help));
	}

	fputc('\n', out);

	exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}

static int parse_level(const char *str, size_t len)
{
	int i;

	if (!str)
		return -1;
	if (!len)
		len = strlen(str);
	errno = 0;

	if (isdigit(*str)) {
		char *end = NULL;

		i = strtol(str, &end, 10);
		if (!errno && end && end > str && end - str == len &&
		    i >= 0 && i < ARRAY_SIZE(level_names))
			return i;
	} else {
		for (i = 0; i < ARRAY_SIZE(level_names); i++) {
			const char *n = level_names[i].name;

			if (strncasecmp(str, n, len) == 0 && *(n + len) == '\0')
				return i;
		}
	}

	if (errno)
		err(EXIT_FAILURE, _("failed to parse level '%s'"), str);

	errx(EXIT_FAILURE, _("unknown level '%s'"), str);
	return -1;
}

static const char *parse_faclev(const char *str, int *fac, int *lev)
{
	long num;
	char *end = NULL;

	if (!str)
		return str;

	errno = 0;
	num = strtol(str, &end, 10);

	if (!errno && end && end > str) {
		*fac = LOG_FAC(num);
		*lev = LOG_PRI(num);

		if (*lev > ARRAY_SIZE(level_names))
			*lev = -1;
		if (*fac > ARRAY_SIZE(facility_names))
			*fac = -1;
		return end + 1;		/* skip '<' */
	}

	return str;
}

static int list_to_bitarray(const char *list,
			    int (*name2id)(const char *name, size_t namesz),
			    char *ary)
{
	const char *begin = NULL, *p;

	for (p = list; p && *p; p++) {
		const char *end = NULL;
		int id;

		if (!begin)
			begin = p;		/* begin of the level name */
		if (*p == ',')
			end = p;		/* terminate the name */
		if (*(p + 1) == '\0')
			end = p + 1;		/* end of string */
		if (!begin || !end)
			continue;
		if (end <= begin)
			return -1;

		id = name2id(begin, end - begin);
		if (id < 0)
			return id;
		setbit(ary, id);
		begin = NULL;
		if (end && !*end)
			break;
	}
	return 0;
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

static int fwrite_hex(const char *buf, size_t size, FILE *out)
{
	int i;

	for (i = 0; i < size; i++) {
		int rc = fprintf(out, "\\x%02x", buf[i]);
		if (rc < 0)
			return rc;
	}
	return 0;
}

static void safe_fwrite(const char *buf, size_t size, FILE *out)
{
	int i;
#ifdef HAVE_WIDECHAR
	mbstate_t s;
	memset(&s, 0, sizeof (s));
#endif
	for (i = 0; i < size; i++) {
		const char *p = buf + i;
		int rc, hex = 0;

#ifdef HAVE_WIDECHAR
		wchar_t wc;
		size_t len = mbrtowc(&wc, p, size - i, &s);

		if (len == 0)				/* L'\0' */
			return;

		if (len < 0) {				/* invalid sequence */
			memset(&s, 0, sizeof (s));
			len = hex = 1;

		} else if (len > 1 && !iswprint(wc)) {	/* non-printable multibyte */
			hex = 1;
		} else
#endif
		{
			if (!isprint((unsigned int) *p) &&
			    !isspace((unsigned int) *p))		/* non-printable */
				hex = 1;
		}
		if (hex)
			rc = fwrite_hex(p, len, out);
		else
			rc = fwrite(p, 1, len, out) != len;
		if (rc != 0)
			err(EXIT_FAILURE, _("write failed"));
	}
}

static void print_buffer(const char *buf, size_t size, int flags, char *levels)
{
	int i;
	const char *begin = NULL;

	if (flags & DMESG_FL_RAW) {
		/* print whole buffer */
		safe_fwrite(buf, size, stdout);
		if (buf[size - 1] != '\n')
			putchar('\n');
		return;
	}

	for (i = 0; i < size; i++) {
		const char *p = buf + i, *end = NULL;
		int fac = -1, lev = -1;

		if (!begin)
			begin = p;
		if (*p == '\n')
			end = p;
		if (i + 1 == size) {
			end = p + 1;
			i++;
		}
		if (!begin || !end)
			continue;
		if (end <= begin)
			continue;	/* error or empty line? */

		if (*begin == '<') {
			if ((flags & DMESG_FL_LEVEL) ||
			    (flags & DMESG_FL_FACILITY) ||
			    (flags & DMESG_FL_DECODE)) {
				/* parse facility and level */
				begin = parse_faclev(begin + 1, &fac, &lev);
			} else {
				/* ignore facility and level */
				while (begin < end) {
					begin++;
					if (*(begin - 1) == '>')
						break;
				}
			}
		}

		if (begin < end &&
		    (lev < 0 || !(flags & DMESG_FL_LEVEL) || isset(levels, lev))) {
			size_t sz =  end - begin;

			if ((flags & DMESG_FL_DECODE) && lev >= 0 && fac >= 0) {
				printf("%-6s:%-6s: ", facility_names[fac].name,
						level_names[lev].name);
			}
			safe_fwrite(begin, sz, stdout);
			if (*(begin + sz - 1) != '\n')
				putchar('\n');
		}
		begin = NULL;
	}
}

int main(int argc, char *argv[])
{
	char *buf = NULL;
	int  bufsize = 0;
	int  n;
	int  c;
	int  console_level = 0;
	int  cmd = -1;
	int  flags = 0;
	char levels[ARRAY_SIZE(level_names) / NBBY + 1] = { 0 };

	static const struct option longopts[] = {
		{ "clear",         no_argument,	      NULL, 'C' },
		{ "read-clear",    no_argument,	      NULL, 'c' },
		{ "raw",           no_argument,       NULL, 'r' },
		{ "buffer-size",   required_argument, NULL, 's' },
		{ "decode",        no_argument,	      NULL, 'x' },
		{ "console-level", required_argument, NULL, 'n' },
		{ "level",         required_argument, NULL, 'l' },
		{ "console-off",   no_argument,       NULL, 'd' },
		{ "console-on",    no_argument,       NULL, 'e' },
		{ "version",       no_argument,	      NULL, 'V' },
		{ "help",          no_argument,	      NULL, 'h' },
		{ NULL,	           0, NULL, 0 }
	};

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

	while ((c = getopt_long(argc, argv, "Ccdehl:rn:s:Vx", longopts, NULL)) != -1) {

		if (cmd != -1 && strchr("Ccnde", c))
			errx(EXIT_FAILURE, "%s %s",
				"--{clear,read-clear,console-level,console-on,console-off}",
				_("options are mutually exclusive"));

		switch (c) {
		case 'C':
			cmd = SYSLOG_ACTION_CLEAR;
			break;
		case 'c':
			cmd = SYSLOG_ACTION_READ_CLEAR;
			break;
		case 'd':
			cmd = SYSLOG_ACTION_CONSOLE_OFF;
			break;
		case 'e':
			cmd = SYSLOG_ACTION_CONSOLE_ON;
			break;
		case 'n':
			cmd = SYSLOG_ACTION_CONSOLE_LEVEL;
			console_level = parse_level(optarg, 0);
			break;
		case 'l':
			flags |= DMESG_FL_LEVEL;
			list_to_bitarray(optarg, parse_level, levels);
			break;
		case 'r':
			flags |= DMESG_FL_RAW;
			break;
		case 'x':
			flags |= DMESG_FL_DECODE;
			break;
		case 's':
			bufsize = strtol_or_err(optarg,
					_("failed to parse buffer size"));
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

	if (cmd == -1)
		cmd = SYSLOG_ACTION_READ_ALL;	/* default */

	if ((flags & DMESG_FL_RAW) && ((flags != DMESG_FL_RAW)))
		errx(EXIT_FAILURE, _("options --level and --facility cannot be used for raw output"));

	switch (cmd) {
	case SYSLOG_ACTION_READ_ALL:
	case SYSLOG_ACTION_READ_CLEAR:
		if (!bufsize)
			bufsize = get_buffer_size();
		n = read_buffer(&buf, bufsize, cmd == SYSLOG_ACTION_READ_CLEAR);
		if (n > 0)
			print_buffer(buf, n, flags, levels);
		free(buf);
		break;
	case SYSLOG_ACTION_CLEAR:
	case SYSLOG_ACTION_CONSOLE_OFF:
	case SYSLOG_ACTION_CONSOLE_ON:
		n = klogctl(cmd, NULL, 0);
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
