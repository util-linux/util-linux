/*
 * dmesg.c -- Print out the contents of the kernel ring buffer
 *
 * Copyright (C) 1993 Theodore Ts'o <tytso@athena.mit.edu>
 * Copyright (C) 2011 Karel Zak <kzak@redhat.com>
 *
 * This program comes with ABSOLUTELY NO WARRANTY.
 */
#include <stdio.h>
#include <getopt.h>
#include <stdlib.h>
#include <sys/klog.h>
#include <sys/syslog.h>
#include <sys/time.h>
#include <sys/sysinfo.h>
#include <ctype.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#include "c.h"
#include "colors.h"
#include "nls.h"
#include "strutils.h"
#include "xalloc.h"
#include "widechar.h"
#include "all-io.h"
#include "bitops.h"
#include "closestream.h"
#include "optutils.h"
#include "timeutils.h"
#include "monotonic.h"
#include "mangle.h"
#include "pager.h"

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

/*
 * Color scheme
 */
struct dmesg_color {
	const char *scheme;	/* name used in termina-colors.d/dmesg.scheme */
	const char *dflt;	/* default color ESC sequence */
};

enum {
	DMESG_COLOR_SUBSYS,
	DMESG_COLOR_TIME,
	DMESG_COLOR_TIMEBREAK,
	DMESG_COLOR_ALERT,
	DMESG_COLOR_CRIT,
	DMESG_COLOR_ERR,
	DMESG_COLOR_WARN,
	DMESG_COLOR_SEGFAULT
};

static const struct dmesg_color colors[] =
{
	[DMESG_COLOR_SUBSYS]    = { "subsys",	UL_COLOR_BROWN },
	[DMESG_COLOR_TIME]	= { "time",     UL_COLOR_GREEN },
	[DMESG_COLOR_TIMEBREAK]	= { "timebreak",UL_COLOR_GREEN UL_COLOR_BOLD },
	[DMESG_COLOR_ALERT]	= { "alert",    UL_COLOR_REVERSE UL_COLOR_RED },
	[DMESG_COLOR_CRIT]	= { "crit",     UL_COLOR_BOLD UL_COLOR_RED },
	[DMESG_COLOR_ERR]       = { "err",      UL_COLOR_RED },
	[DMESG_COLOR_WARN]	= { "warn",     UL_COLOR_BOLD },
	[DMESG_COLOR_SEGFAULT]	= { "segfault", UL_COLOR_HALFBRIGHT UL_COLOR_RED }
};

#define dmesg_enable_color(_id) \
		color_scheme_enable(colors[_id].scheme, colors[_id].dflt);

/*
 * Priority and facility names
 */
struct dmesg_name {
	const char *name;
	const char *help;
};

/*
 * Priority names -- based on sys/syslog.h
 */
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
 * We want to use the codes as array indexes, so shift back...
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
	[FAC_BASE(LOG_FTP)]      = { "ftp",      N_("FTP daemon") },
};

/* supported methods to read message buffer
 */
enum {
	DMESG_METHOD_KMSG,	/* read messages from /dev/kmsg (default) */
	DMESG_METHOD_SYSLOG,	/* klogctl() buffer */
	DMESG_METHOD_MMAP	/* mmap file with records (see --file) */
};

enum {
	DMESG_TIMEFTM_NONE = 0,
	DMESG_TIMEFTM_CTIME,		/* [ctime] */
	DMESG_TIMEFTM_CTIME_DELTA,	/* [ctime <delta>] */
	DMESG_TIMEFTM_DELTA,		/* [<delta>] */
	DMESG_TIMEFTM_RELTIME,		/* [relative] */
	DMESG_TIMEFTM_TIME,		/* [time] */
	DMESG_TIMEFTM_TIME_DELTA,	/* [time <delta>] */
	DMESG_TIMEFTM_ISO8601		/* 2013-06-13T22:11:00,123456+0100 */
};
#define is_timefmt(c, f) ((c)->time_fmt == (DMESG_TIMEFTM_ ##f))

struct dmesg_control {
	/* bit arrays -- see include/bitops.h */
	char levels[ARRAY_SIZE(level_names) / NBBY + 1];
	char facilities[ARRAY_SIZE(facility_names) / NBBY + 1];

	struct timeval	lasttime;	/* last printed timestamp */
	struct tm	lasttm;		/* last localtime */
	struct timeval	boot_time;	/* system boot time */
	time_t		suspended_time;	/* time spent in suspended state */

	int		action;		/* SYSLOG_ACTION_* */
	int		method;		/* DMESG_METHOD_* */

	size_t		bufsize;	/* size of syslog buffer */

	int		kmsg;		/* /dev/kmsg file descriptor */
	ssize_t		kmsg_first_read;/* initial read() return code */
	char		kmsg_buf[BUFSIZ];/* buffer to read kmsg data */

	/*
	 * For the --file option we mmap whole file. The unnecessary (already
	 * printed) pages are always unmapped. The result is that we have in
	 * memory only the currently used page(s).
	 */
	char		*filename;
	char		*mmap_buff;
	size_t		pagesize;
	unsigned int	time_fmt;	/* time format */

	unsigned int	follow:1,	/* wait for new messages */
			end:1,		/* seek to the of buffer */
			raw:1,		/* raw mode */
			noesc:1,	/* no escape */
			fltr_lev:1,	/* filter out by levels[] */
			fltr_fac:1,	/* filter out by facilities[] */
			decode:1,	/* use "facility: level: " prefix */
			pager:1,	/* pipe output into a pager */
			color:1,	/* colorize messages */
			force_prefix:1;	/* force timestamp and decode prefix
					   on each line */
	int		indent;		/* due to timestamps if newline */
};

struct dmesg_record {
	const char	*mesg;
	size_t		mesg_size;

	int		level;
	int		facility;
	struct timeval  tv;

	const char	*next;		/* buffer with next unparsed record */
	size_t		next_size;	/* size of the next buffer */
};

#define INIT_DMESG_RECORD(_r)  do { \
		(_r)->mesg = NULL; \
		(_r)->mesg_size = 0; \
		(_r)->facility = -1; \
		(_r)->level = -1; \
		(_r)->tv.tv_sec = 0; \
		(_r)->tv.tv_usec = 0; \
	} while (0)

static int read_kmsg(struct dmesg_control *ctl);

static int set_level_color(int log_level, const char *mesg, size_t mesgsz)
{
	int id = -1;

	switch (log_level) {
	case LOG_ALERT:
		id = DMESG_COLOR_ALERT;
		break;
	case LOG_CRIT:
		id = DMESG_COLOR_CRIT;
		break;
	case LOG_ERR:
		id = DMESG_COLOR_ERR;
		break;
	case LOG_WARNING:
		id = DMESG_COLOR_WARN;
		break;
	default:
		break;
	}

	/* well, sometimes the messages contains important keywords, but in
	 * non-warning/error messages
	 */
	if (id < 0 && memmem(mesg, mesgsz, "segfault at", 11))
		id = DMESG_COLOR_SEGFAULT;

	if (id >= 0)
		dmesg_enable_color(id);

	return id >= 0 ? 0 : -1;
}

static void __attribute__((__noreturn__)) usage(void)
{
	FILE *out = stdout;
	size_t i;

	fputs(USAGE_HEADER, out);
	fprintf(out, _(" %s [options]\n"), program_invocation_short_name);

	fputs(USAGE_SEPARATOR, out);
	fputs(_("Display or control the kernel ring buffer.\n"), out);

	fputs(USAGE_OPTIONS, out);
	fputs(_(" -C, --clear                 clear the kernel ring buffer\n"), out);
	fputs(_(" -c, --read-clear            read and clear all messages\n"), out);
	fputs(_(" -D, --console-off           disable printing messages to console\n"), out);
	fputs(_(" -E, --console-on            enable printing messages to console\n"), out);
	fputs(_(" -F, --file <file>           use the file instead of the kernel log buffer\n"), out);
	fputs(_(" -f, --facility <list>       restrict output to defined facilities\n"), out);
	fputs(_(" -H, --human                 human readable output\n"), out);
	fputs(_(" -k, --kernel                display kernel messages\n"), out);
	fprintf(out,
	      _(" -L, --color[=<when>]        colorize messages (%s, %s or %s)\n"), "auto", "always", "never");
	fprintf(out,
	        "                               %s\n", USAGE_COLORS_DEFAULT);
	fputs(_(" -l, --level <list>          restrict output to defined levels\n"), out);
	fputs(_(" -n, --console-level <level> set level of messages printed to console\n"), out);
	fputs(_(" -P, --nopager               do not pipe output into a pager\n"), out);
	fputs(_(" -p, --force-prefix          force timestamp output on each line of multi-line messages\n"), out);
	fputs(_(" -r, --raw                   print the raw message buffer\n"), out);
	fputs(_("     --noescape              don't escape unprintable character\n"), out);
	fputs(_(" -S, --syslog                force to use syslog(2) rather than /dev/kmsg\n"), out);
	fputs(_(" -s, --buffer-size <size>    buffer size to query the kernel ring buffer\n"), out);
	fputs(_(" -u, --userspace             display userspace messages\n"), out);
	fputs(_(" -w, --follow                wait for new messages\n"), out);
	fputs(_(" -W, --follow-new            wait and print only new messages\n"), out);
	fputs(_(" -x, --decode                decode facility and level to readable string\n"), out);
	fputs(_(" -d, --show-delta            show time delta between printed messages\n"), out);
	fputs(_(" -e, --reltime               show local time and time delta in readable format\n"), out);
	fputs(_(" -T, --ctime                 show human-readable timestamp (may be inaccurate!)\n"), out);
	fputs(_(" -t, --notime                don't show any timestamp with messages\n"), out);
	fputs(_("     --time-format <format>  show timestamp using the given format:\n"
		"                               [delta|reltime|ctime|notime|iso]\n"
		"Suspending/resume will make ctime and iso timestamps inaccurate.\n"), out);
	fputs(USAGE_SEPARATOR, out);
	printf(USAGE_HELP_OPTIONS(29));
	fputs(_("\nSupported log facilities:\n"), out);
	for (i = 0; i < ARRAY_SIZE(level_names); i++)
		fprintf(out, " %7s - %s\n",
			facility_names[i].name,
			_(facility_names[i].help));

	fputs(_("\nSupported log levels (priorities):\n"), out);
	for (i = 0; i < ARRAY_SIZE(level_names); i++)
		fprintf(out, " %7s - %s\n",
			level_names[i].name,
			_(level_names[i].help));

	printf(USAGE_MAN_TAIL("dmesg(1)"));
	exit(EXIT_SUCCESS);
}

/*
 * LEVEL     ::= <number> | <name>
 *  <number> ::= @len is set:  number in range <0..N>, where N < ARRAY_SIZE(level_names)
 *           ::= @len not set: number in range <1..N>, where N <= ARRAY_SIZE(level_names)
 *  <name>   ::= case-insensitive text
 *
 *  Note that @len argument is not set when parsing "-n <level>" command line
 *  option. The console_level is interpreted as "log level less than the value".
 *
 *  For example "dmesg -n 8" or "dmesg -n debug" enables debug console log
 *  level by klogctl(SYSLOG_ACTION_CONSOLE_LEVEL, NULL, 8). The @str argument
 *  has to be parsed to number in range <1..8>.
 */
static int parse_level(const char *str, size_t len)
{
	int offset = 0;

	if (!str)
		return -1;
	if (!len) {
		len = strlen(str);
		offset = 1;
	}
	errno = 0;

	if (isdigit(*str)) {
		char *end = NULL;
		long x = strtol(str, &end, 10) - offset;

		if (!errno && end && end > str && (size_t) (end - str) == len &&
		    x >= 0 && (size_t) x < ARRAY_SIZE(level_names))
			return x + offset;
	} else {
		size_t i;

		for (i = 0; i < ARRAY_SIZE(level_names); i++) {
			const char *n = level_names[i].name;

			if (strncasecmp(str, n, len) == 0 && *(n + len) == '\0')
				return i + offset;
		}
	}

	if (errno)
		err(EXIT_FAILURE, _("failed to parse level '%s'"), str);

	errx(EXIT_FAILURE, _("unknown level '%s'"), str);
	return -1;
}

/*
 * FACILITY  ::= <number> | <name>
 *  <number> ::= number in range <0..N>, where N < ARRAY_SIZE(facility_names)
 *  <name>   ::= case-insensitive text
 */
static int parse_facility(const char *str, size_t len)
{
	if (!str)
		return -1;
	if (!len)
		len = strlen(str);
	errno = 0;

	if (isdigit(*str)) {
		char *end = NULL;
		long x = strtol(str, &end, 10);

		if (!errno && end && end > str && (size_t) (end - str) == len &&
		    x >= 0 && (size_t) x < ARRAY_SIZE(facility_names))
			return x;
	} else {
		size_t i;

		for (i = 0; i < ARRAY_SIZE(facility_names); i++) {
			const char *n = facility_names[i].name;

			if (strncasecmp(str, n, len) == 0 && *(n + len) == '\0')
				return i;
		}
	}

	if (errno)
		err(EXIT_FAILURE, _("failed to parse facility '%s'"), str);

	errx(EXIT_FAILURE, _("unknown facility '%s'"), str);
	return -1;
}

/*
 * Parses numerical prefix used for all messages in kernel ring buffer.
 *
 * Priorities/facilities are encoded into a single 32-bit quantity, where the
 * bottom 3 bits are the priority (0-7) and the top 28 bits are the facility
 * (0-big number).
 *
 * Note that the number has to end with '>' or ',' char.
 */
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

		if (*lev < 0 || (size_t) *lev > ARRAY_SIZE(level_names))
			*lev = -1;
		if (*fac < 0 || (size_t) *fac > ARRAY_SIZE(facility_names))
			*fac = -1;
		return end + 1;		/* skip '<' or ',' */
	}

	return str;
}

/*
 * Parses timestamp from syslog message prefix, expected format:
 *
 *	seconds.microseconds]
 *
 * the ']' is the timestamp field terminator.
 */
static const char *parse_syslog_timestamp(const char *str0, struct timeval *tv)
{
	const char *str = str0;
	char *end = NULL;

	if (!str0)
		return str0;

	errno = 0;
	tv->tv_sec = strtol(str, &end, 10);

	if (!errno && end && *end == '.' && *(end + 1)) {
		str = end + 1;
		end = NULL;
		tv->tv_usec = strtol(str, &end, 10);
	}
	if (errno || !end || end == str || *end != ']')
		return str0;

	return end + 1;	/* skip ']' */
}

/*
 * Parses timestamp from /dev/kmsg, expected formats:
 *
 *	microseconds,
 *	microseconds;
 *
 * the ',' is fields separators and ';' items terminator (for the last item)
 */
static const char *parse_kmsg_timestamp(const char *str0, struct timeval *tv)
{
	const char *str = str0;
	char *end = NULL;
	uint64_t usec;

	if (!str0)
		return str0;

	errno = 0;
	usec = strtoumax(str, &end, 10);

	if (!errno && end && (*end == ';' || *end == ',')) {
		tv->tv_usec = usec % 1000000;
		tv->tv_sec = usec / 1000000;
	} else
		return str0;

	return end + 1;	/* skip separator */
}


static double time_diff(struct timeval *a, struct timeval *b)
{
	return (a->tv_sec - b->tv_sec) + (a->tv_usec - b->tv_usec) / 1E6;
}

static int get_syslog_buffer_size(void)
{
	int n = klogctl(SYSLOG_ACTION_SIZE_BUFFER, NULL, 0);

	return n > 0 ? n : 0;
}

/*
 * Reads messages from regular file by mmap
 */
static ssize_t mmap_file_buffer(struct dmesg_control *ctl, char **buf)
{
	struct stat st;
	int fd;

	if (!ctl->filename)
		return -1;

	fd = open(ctl->filename, O_RDONLY);
	if (fd < 0)
		err(EXIT_FAILURE, _("cannot open %s"), ctl->filename);
	if (fstat(fd, &st))
		err(EXIT_FAILURE, _("stat of %s failed"), ctl->filename);

	*buf = mmap(NULL, st.st_size, PROT_READ, MAP_SHARED, fd, 0);
	if (*buf == MAP_FAILED)
		err(EXIT_FAILURE, _("cannot mmap: %s"), ctl->filename);
	ctl->mmap_buff = *buf;
	ctl->pagesize = getpagesize();
	close(fd);

	return st.st_size;
}

/*
 * Reads messages from kernel ring buffer by klogctl()
 */
static ssize_t read_syslog_buffer(struct dmesg_control *ctl, char **buf)
{
	size_t sz;
	int rc = -1;

	if (ctl->bufsize) {
		sz = ctl->bufsize + 8;
		*buf = xmalloc(sz * sizeof(char));
		rc = klogctl(ctl->action, *buf, sz);
	} else {
		sz = 16392;
		while (1) {
			*buf = xmalloc(sz * sizeof(char));
			rc = klogctl(SYSLOG_ACTION_READ_ALL, *buf, sz);
			if (rc < 0)
				break;
			if ((size_t) rc != sz || sz > (1 << 28))
				break;
			free(*buf);
			*buf = NULL;
			sz *= 4;
		}

		if (rc > 0 && ctl->action == SYSLOG_ACTION_READ_CLEAR)
			rc = klogctl(SYSLOG_ACTION_READ_CLEAR, *buf, sz);
	}

	return rc;
}

/*
 * Top level function to read messages
 */
static ssize_t read_buffer(struct dmesg_control *ctl, char **buf)
{
	ssize_t n = -1;

	switch (ctl->method) {
	case DMESG_METHOD_MMAP:
		n = mmap_file_buffer(ctl, buf);
		break;
	case DMESG_METHOD_SYSLOG:
		if (!ctl->bufsize)
			ctl->bufsize = get_syslog_buffer_size();

		n = read_syslog_buffer(ctl, buf);
		break;
	case DMESG_METHOD_KMSG:
		/*
		 * Since kernel 3.5.0
		 */
		n = read_kmsg(ctl);
		if (n == 0 && ctl->action == SYSLOG_ACTION_READ_CLEAR)
			n = klogctl(SYSLOG_ACTION_CLEAR, NULL, 0);
		break;
	default:
		abort();	/* impossible method -> drop core */
	}

	return n;
}

static int fwrite_hex(const char *buf, size_t size, FILE *out)
{
	size_t i;

	for (i = 0; i < size; i++) {
		int rc = fprintf(out, "\\x%02hhx", buf[i]);
		if (rc < 0)
			return rc;
	}
	return 0;
}

/*
 * Prints to 'out' and non-printable chars are replaced with \x<hex> sequences.
 */
static void safe_fwrite(struct dmesg_control *ctl, const char *buf, size_t size, int indent, FILE *out)
{
	size_t i;
#ifdef HAVE_WIDECHAR
	mbstate_t s;
	wchar_t wc;
	memset(&s, 0, sizeof (s));
#endif
	for (i = 0; i < size; i++) {
		const char *p = buf + i;
		int rc, hex = 0;
		size_t len = 1;

		if (!ctl->noesc) {
			if (*p == '\0') {
				hex = 1;
				goto doprint;
			}
#ifdef HAVE_WIDECHAR
			len = mbrtowc(&wc, p, size - i, &s);

			if (len == 0)				/* L'\0' */
				return;

			if (len == (size_t)-1 || len == (size_t)-2) {		/* invalid sequence */
				memset(&s, 0, sizeof (s));
				len = hex = 1;
				i += len - 1;
			} else if (len > 1) {
				if (!iswprint(wc) && !iswspace(wc))	/* non-printable multibyte */
					hex = 1;
				i += len - 1;
			} else
#endif
			{
				len = 1;
				if (!isprint((unsigned char) *p) &&
				    !isspace((unsigned char) *p))        /* non-printable */
					hex = 1;
			}
		}

doprint:
		if (hex)
			rc = fwrite_hex(p, len, out);
		else if (*p == '\n' && *(p + 1) && indent) {
		        rc = fwrite(p, 1, len, out) != len;
			if (fprintf(out, "%*s", indent, "") != indent)
				rc |= 1;
		} else
			rc = fwrite(p, 1, len, out) != len;

		if (rc != 0) {
			if (errno != EPIPE)
				err(EXIT_FAILURE, _("write failed"));
			exit(EXIT_SUCCESS);
		}
	}
}

static const char *skip_item(const char *begin, const char *end, const char *sep)
{
	while (begin < end) {
		int c = *begin++;

		if (c == '\0' || strchr(sep, c))
			break;
	}

	return begin;
}

/*
 * Parses one record from syslog(2) buffer
 */
static int get_next_syslog_record(struct dmesg_control *ctl,
				  struct dmesg_record *rec)
{
	size_t i;
	const char *begin = NULL;

	if (ctl->method != DMESG_METHOD_MMAP &&
	    ctl->method != DMESG_METHOD_SYSLOG)
		return -1;

	if (!rec->next || !rec->next_size)
		return 1;

	INIT_DMESG_RECORD(rec);

	/*
	 * Unmap already printed file data from memory
	 */
	if (ctl->mmap_buff && (size_t) (rec->next - ctl->mmap_buff) > ctl->pagesize) {
		void *x = ctl->mmap_buff;

		ctl->mmap_buff += ctl->pagesize;
		munmap(x, ctl->pagesize);
	}

	for (i = 0; i < rec->next_size; i++) {
		const char *p = rec->next + i;
		const char *end = NULL;

		if (!begin)
			begin = p;
		if (i + 1 == rec->next_size) {
			end = p + 1;
			i++;
		} else if (*p == '\n' && *(p + 1) == '<')
			end = p;

		if (begin && !*begin)
			begin = NULL;	/* zero(s) at the end of the buffer? */
		if (!begin || !end)
			continue;
		if (end <= begin)
			continue;	/* error or empty line? */

		if (*begin == '<') {
			if (ctl->fltr_lev || ctl->fltr_fac || ctl->decode || ctl->color)
				begin = parse_faclev(begin + 1, &rec->facility,
						     &rec->level);
			else
				begin = skip_item(begin, end, ">");
		}

		if (*begin == '[' && (*(begin + 1) == ' ' ||
				      isdigit(*(begin + 1)))) {

			if (!is_timefmt(ctl, NONE))
				begin = parse_syslog_timestamp(begin + 1, &rec->tv);
			else
				begin = skip_item(begin, end, "]");

			if (begin < end && *begin == ' ')
				begin++;
		}

		rec->mesg = begin;
		rec->mesg_size = end - begin;

		/* Don't count \n from the last message to the message size */
		if (*end != '\n' && *(end - 1) == '\n')
			rec->mesg_size--;

		rec->next_size -= end - rec->next;
		rec->next = rec->next_size > 0 ? end + 1 : NULL;
		if (rec->next_size > 0)
			rec->next_size--;

		return 0;
	}

	return 1;
}

static int accept_record(struct dmesg_control *ctl, struct dmesg_record *rec)
{
	if (ctl->fltr_lev && (rec->facility < 0 ||
			      !isset(ctl->levels, rec->level)))
		return 0;

	if (ctl->fltr_fac && (rec->facility < 0 ||
			      !isset(ctl->facilities, rec->facility)))
		return 0;

	return 1;
}

static void raw_print(struct dmesg_control *ctl, const char *buf, size_t size)
{
	int lastc = '\n';

	if (!ctl->mmap_buff) {
		/*
		 * Print whole ring buffer
		 */
		safe_fwrite(ctl, buf, size, 0, stdout);
		lastc = buf[size - 1];
	} else {
		/*
		 * Print file in small chunks to save memory
		 */
		while (size) {
			size_t sz = size > ctl->pagesize ? ctl->pagesize : size;
			char *x = ctl->mmap_buff;

			safe_fwrite(ctl, x, sz, 0, stdout);
			lastc = x[sz - 1];
			size -= sz;
			ctl->mmap_buff += sz;
			munmap(x, sz);
		}
	}

	if (lastc != '\n')
		putchar('\n');
}

static struct tm *record_localtime(struct dmesg_control *ctl,
				   struct dmesg_record *rec,
				   struct tm *tm)
{
	time_t t = ctl->boot_time.tv_sec + ctl->suspended_time + rec->tv.tv_sec;
	return localtime_r(&t, tm);
}

static char *record_ctime(struct dmesg_control *ctl,
			  struct dmesg_record *rec,
			  char *buf, size_t bufsiz)
{
	struct tm tm;

	record_localtime(ctl, rec, &tm);

	if (strftime(buf, bufsiz, "%a %b %e %H:%M:%S %Y", &tm) == 0)
		*buf = '\0';
	return buf;
}

static char *short_ctime(struct tm *tm, char *buf, size_t bufsiz)
{
	if (strftime(buf, bufsiz, "%b%e %H:%M", tm) == 0)
		*buf = '\0';
	return buf;
}

static char *iso_8601_time(struct dmesg_control *ctl, struct dmesg_record *rec,
			   char *buf, size_t bufsz)
{
	struct timeval tv = {
		.tv_sec = ctl->boot_time.tv_sec + ctl->suspended_time + rec->tv.tv_sec,
		.tv_usec = rec->tv.tv_usec
	};

	if (strtimeval_iso(&tv,	ISO_TIMESTAMP_COMMA_T, buf, bufsz) != 0)
		return NULL;

	return buf;
}

static double record_count_delta(struct dmesg_control *ctl,
				 struct dmesg_record *rec)
{
	double delta = 0;

	if (timerisset(&ctl->lasttime))
		delta = time_diff(&rec->tv, &ctl->lasttime);

	ctl->lasttime = rec->tv;
	return delta;
}

static const char *get_subsys_delimiter(const char *mesg, size_t mesg_size)
{
	const char *p = mesg;
	size_t sz = mesg_size;

	while (sz > 0) {
		const char *d = strnchr(p, sz, ':');
		if (!d)
			return NULL;
		sz -= d - p + 1;
		if (sz) {
			if (isblank(*(d + 1)))
				return d;
			p = d + 1;
		}
	}
	return NULL;
}

static void print_record(struct dmesg_control *ctl,
			 struct dmesg_record *rec)
{
	char buf[128];
	char fpbuf[32] = "\0";
	char tsbuf[64] = "\0";
	size_t mesg_size = rec->mesg_size;
	int timebreak = 0;
	char *mesg_copy = NULL;
	const char *line = NULL;

	if (!accept_record(ctl, rec))
		return;

	if (!rec->mesg_size) {
		putchar('\n');
		return;
	}

	/*
	 * Compose syslog(2) compatible raw output -- used for /dev/kmsg for
	 * backward compatibility with syslog(2) buffers only
	 */
	if (ctl->raw) {
		ctl->indent = snprintf(tsbuf, sizeof(tsbuf),
				       "<%d>[%5ld.%06ld] ",
				       LOG_MAKEPRI(rec->facility, rec->level),
				       (long) rec->tv.tv_sec,
				       (long) rec->tv.tv_usec);
		goto full_output;
	}

	/* Store decode information (facility & priority level) in a buffer */
	if (ctl->decode &&
	    (rec->level > -1) && (rec->level < (int) ARRAY_SIZE(level_names)) &&
	    (rec->facility > -1) &&
	    (rec->facility < (int) ARRAY_SIZE(facility_names)))
		snprintf(fpbuf, sizeof(fpbuf), "%-6s:%-6s: ",
			 facility_names[rec->facility].name,
			 level_names[rec->level].name);

	/* Store the timestamp in a buffer */
	switch (ctl->time_fmt) {
		double delta;
		struct tm cur;
	case DMESG_TIMEFTM_NONE:
		ctl->indent = 0;
		break;
	case DMESG_TIMEFTM_CTIME:
		ctl->indent = snprintf(tsbuf, sizeof(tsbuf), "[%s] ",
				      record_ctime(ctl, rec, buf, sizeof(buf)));
		break;
	case DMESG_TIMEFTM_CTIME_DELTA:
		ctl->indent = snprintf(tsbuf, sizeof(tsbuf), "[%s <%12.06f>] ",
				      record_ctime(ctl, rec, buf, sizeof(buf)),
				      record_count_delta(ctl, rec));
		break;
	case DMESG_TIMEFTM_DELTA:
		ctl->indent = snprintf(tsbuf, sizeof(tsbuf), "[<%12.06f>] ",
				      record_count_delta(ctl, rec));
		break;
	case DMESG_TIMEFTM_RELTIME:
		record_localtime(ctl, rec, &cur);
		delta = record_count_delta(ctl, rec);
		if (cur.tm_min != ctl->lasttm.tm_min ||
		    cur.tm_hour != ctl->lasttm.tm_hour ||
		    cur.tm_yday != ctl->lasttm.tm_yday) {
			timebreak = 1;
			ctl->indent = snprintf(tsbuf, sizeof(tsbuf), "[%s] ",
					      short_ctime(&cur, buf,
							  sizeof(buf)));
		} else {
			if (delta < 10)
				ctl->indent = snprintf(tsbuf, sizeof(tsbuf),
						"[  %+8.06f] ",  delta);
			else
				ctl->indent = snprintf(tsbuf, sizeof(tsbuf),
						"[ %+9.06f] ", delta);
		}
		ctl->lasttm = cur;
		break;
	case DMESG_TIMEFTM_TIME:
		ctl->indent = snprintf(tsbuf, sizeof(tsbuf), "[%5ld.%06ld] ",
				      (long)rec->tv.tv_sec,
				      (long)rec->tv.tv_usec);
		break;
	case DMESG_TIMEFTM_TIME_DELTA:
		ctl->indent = snprintf(tsbuf, sizeof(tsbuf), "[%5ld.%06ld <%12.06f>] ",
				      (long)rec->tv.tv_sec,
				      (long)rec->tv.tv_usec,
				      record_count_delta(ctl, rec));
		break;
	case DMESG_TIMEFTM_ISO8601:
		ctl->indent = snprintf(tsbuf, sizeof(tsbuf), "%s ",
				      iso_8601_time(ctl, rec, buf,
						    sizeof(buf)));
		break;
	default:
		abort();
	}

	ctl->indent += strlen(fpbuf);

full_output:
	/* Output the decode information */
	if (*fpbuf)
		fputs(fpbuf, stdout);

	/* Output the timestamp buffer */
	if (*tsbuf) {
		/* Colorize the timestamp */
		if (ctl->color)
			dmesg_enable_color(timebreak ? DMESG_COLOR_TIMEBREAK :
						       DMESG_COLOR_TIME);
		if (ctl->time_fmt != DMESG_TIMEFTM_RELTIME) {
			fputs(tsbuf, stdout);
		} else {
			/*
			 * For relative timestamping, the first line's
			 * timestamp is the offset and all other lines will
			 * report an offset of 0.000000.
			 */
			if (!line)
				fputs(tsbuf, stdout);
			else
				printf("[  +0.000000] ");
		}
		if (ctl->color)
			color_disable();
	}

	/*
	 * A kernel message may contain several lines of output, separated
	 * by '\n'.  If the timestamp and decode outputs are forced then each
	 * line of the message must be displayed with that information.
	 */
	if (ctl->force_prefix) {
		if (!line) {
			mesg_copy = xstrdup(rec->mesg);
			line = strtok(mesg_copy, "\n");
			if (!line)
				goto done;	/* only when something is wrong */
			mesg_size = strlen(line);
		}
	} else {
		line = rec->mesg;
		mesg_size = rec->mesg_size;
	}

	/* Colorize kernel message output */
	if (ctl->color) {
		/* Subsystem prefix */
		const char *subsys = get_subsys_delimiter(line, mesg_size);
		int has_color = 0;

		if (subsys) {
			dmesg_enable_color(DMESG_COLOR_SUBSYS);
			safe_fwrite(ctl, line, subsys - line, ctl->indent, stdout);
			color_disable();

			mesg_size -= subsys - line;
			line = subsys;
		}
		/* Error, alert .. etc. colors */
		has_color = set_level_color(rec->level, line, mesg_size) == 0;
		safe_fwrite(ctl, line, mesg_size, ctl->indent, stdout);
		if (has_color)
			color_disable();
	} else
		safe_fwrite(ctl, line, mesg_size, ctl->indent, stdout);

	/* Get the next line */
	if (ctl->force_prefix) {
		line = strtok(NULL, "\n");
		if (line && *line) {
			putchar('\n');
			mesg_size = strlen(line);
			goto full_output;
		}
		free(mesg_copy);
	}

done:
	putchar('\n');
}

/*
 * Prints the 'buf' kernel ring buffer; the messages are filtered out according
 * to 'levels' and 'facilities' bitarrays.
 */
static void print_buffer(struct dmesg_control *ctl,
			const char *buf, size_t size)
{
	struct dmesg_record rec = { .next = buf, .next_size = size };

	if (ctl->raw) {
		raw_print(ctl, buf, size);
		return;
	}

	while (get_next_syslog_record(ctl, &rec) == 0)
		print_record(ctl, &rec);
}

static ssize_t read_kmsg_one(struct dmesg_control *ctl)
{
	ssize_t size;

	/* kmsg returns EPIPE if record was modified while reading */
	do {
		size = read(ctl->kmsg, ctl->kmsg_buf,
			    sizeof(ctl->kmsg_buf) - 1);
	} while (size < 0 && errno == EPIPE);

	return size;
}

static int init_kmsg(struct dmesg_control *ctl)
{
	int mode = O_RDONLY;

	if (!ctl->follow)
		mode |= O_NONBLOCK;
	else
		setlinebuf(stdout);

	ctl->kmsg = open("/dev/kmsg", mode);
	if (ctl->kmsg < 0)
		return -1;

	/*
	 * Seek after the last record available at the time
	 * the last SYSLOG_ACTION_CLEAR was issued.
	 *
	 * ... otherwise SYSLOG_ACTION_CLEAR will have no effect for kmsg.
	 */
	lseek(ctl->kmsg, 0, ctl->end ? SEEK_END : SEEK_DATA);

	/*
	 * Old kernels (<3.5) can successfully open /dev/kmsg for read-only,
	 * but read() returns -EINVAL :-(((
	 *
	 * Let's try to read the first record. The record is later processed in
	 * read_kmsg().
	 */
	ctl->kmsg_first_read = read_kmsg_one(ctl);
	if (ctl->kmsg_first_read < 0) {
		close(ctl->kmsg);
		ctl->kmsg = -1;
		return -1;
	}

	return 0;
}

/*
 * /dev/kmsg record format:
 *
 *     faclev,seqnum,timestamp[optional, ...];message\n
 *      TAGNAME=value
 *      ...
 *
 * - fields are separated by ','
 * - last field is terminated by ';'
 *
 */
#define LAST_KMSG_FIELD(s)	(!s || !*s || *(s - 1) == ';')

static int parse_kmsg_record(struct dmesg_control *ctl,
			     struct dmesg_record *rec,
			     char *buf,
			     size_t sz)
{
	const char *p = buf, *end;

	if (sz == 0 || !buf || !*buf)
		return -1;

	end = buf + (sz - 1);
	INIT_DMESG_RECORD(rec);

	while (p < end && isspace(*p))
		p++;

	/* A) priority and facility */
	if (ctl->fltr_lev || ctl->fltr_fac || ctl->decode ||
	    ctl->raw || ctl->color)
		p = parse_faclev(p, &rec->facility, &rec->level);
	else
		p = skip_item(p, end, ",");
	if (LAST_KMSG_FIELD(p))
		goto mesg;

	/* B) sequence number */
	p = skip_item(p, end, ",;");
	if (LAST_KMSG_FIELD(p))
		goto mesg;

	/* C) timestamp */
	if (is_timefmt(ctl, NONE))
		p = skip_item(p, end, ",;");
	else
		p = parse_kmsg_timestamp(p, &rec->tv);
	if (LAST_KMSG_FIELD(p))
		goto mesg;

	/* D) optional fields (ignore) */
	p = skip_item(p, end, ";");

mesg:
	/* E) message text */
	rec->mesg = p;
	p = skip_item(p, end, "\n");
	if (!p)
		return -1;

	/* The message text is terminated by \n, but it's possible that the
	 * message contains another stuff behind this linebreak; in this case
	 * the previous skip_item() returns pointer to the stuff behind \n.
	 * Let's normalize all these situations and make sure we always point to
	 * the \n.
	 *
	 * Note that the next unhexmangle_to_buffer() will replace \n by \0.
	 */
	if (*p && *p != '\n')
		p--;

	/*
	 * Kernel escapes non-printable characters, unfortunately kernel
	 * definition of "non-printable" is too strict. On UTF8 console we can
	 * print many chars, so let's decode from kernel.
	 */
	rec->mesg_size = unhexmangle_to_buffer(rec->mesg,
				(char *) rec->mesg, p - rec->mesg + 1);

	rec->mesg_size--;	/* don't count \0 */

	/* F) message tags (ignore) */

	return 0;
}

/*
 * Note that each read() call for /dev/kmsg returns always one record. It means
 * that we don't have to read whole message buffer before the records parsing.
 *
 * So this function does not compose one huge buffer (like read_syslog_buffer())
 * and print_buffer() is unnecessary. All is done in this function.
 *
 * Returns 0 on success, -1 on error.
 */
static int read_kmsg(struct dmesg_control *ctl)
{
	struct dmesg_record rec;
	ssize_t sz;

	if (ctl->method != DMESG_METHOD_KMSG || ctl->kmsg < 0)
		return -1;

	/*
	 * The very first read() call is done in kmsg_init() where we test
	 * /dev/kmsg usability. The return code from the initial read() is
	 * stored in ctl->kmsg_first_read;
	 */
	sz = ctl->kmsg_first_read;

	while (sz > 0) {
		*(ctl->kmsg_buf + sz) = '\0';	/* for debug messages */

		if (parse_kmsg_record(ctl, &rec,
				      ctl->kmsg_buf, (size_t) sz) == 0)
			print_record(ctl, &rec);

		sz = read_kmsg_one(ctl);
	}

	return 0;
}

static int which_time_format(const char *s)
{
	if (!strcmp(s, "notime"))
		return DMESG_TIMEFTM_NONE;
	if (!strcmp(s, "ctime"))
		return DMESG_TIMEFTM_CTIME;
	if (!strcmp(s, "delta"))
		return DMESG_TIMEFTM_DELTA;
	if (!strcmp(s, "reltime"))
		return DMESG_TIMEFTM_RELTIME;
	if (!strcmp(s, "iso"))
		return DMESG_TIMEFTM_ISO8601;
	errx(EXIT_FAILURE, _("unknown time format: %s"), s);
}

#ifdef TEST_DMESG
static inline int dmesg_get_boot_time(struct timeval *tv)
{
	char *str = getenv("DMESG_TEST_BOOTIME");
	uintmax_t sec, usec;

	if (str && sscanf(str, "%ju.%ju", &sec, &usec) == 2) {
		tv->tv_sec = sec;
		tv->tv_usec = usec;
		return tv->tv_sec >= 0 && tv->tv_usec >= 0 ? 0 : -EINVAL;
	}

	return get_boot_time(tv);
}

static inline time_t dmesg_get_suspended_time(void)
{
	if (getenv("DMESG_TEST_BOOTIME"))
		return 0;
	return get_suspended_time();
}
#else
# define dmesg_get_boot_time	get_boot_time
# define dmesg_get_suspended_time	get_suspended_time
#endif

int main(int argc, char *argv[])
{
	char *buf = NULL;
	int  c, nopager = 0;
	int  console_level = 0;
	int  klog_rc = 0;
	int  delta = 0;
	ssize_t n;
	static struct dmesg_control ctl = {
		.filename = NULL,
		.action = SYSLOG_ACTION_READ_ALL,
		.method = DMESG_METHOD_KMSG,
		.kmsg = -1,
		.time_fmt = DMESG_TIMEFTM_TIME,
		.indent = 0,
	};
	int colormode = UL_COLORMODE_UNDEF;
	enum {
		OPT_TIME_FORMAT = CHAR_MAX + 1,
		OPT_NOESC
	};

	static const struct option longopts[] = {
		{ "buffer-size",   required_argument, NULL, 's' },
		{ "clear",         no_argument,	      NULL, 'C' },
		{ "color",         optional_argument, NULL, 'L' },
		{ "console-level", required_argument, NULL, 'n' },
		{ "console-off",   no_argument,       NULL, 'D' },
		{ "console-on",    no_argument,       NULL, 'E' },
		{ "decode",        no_argument,	      NULL, 'x' },
		{ "file",          required_argument, NULL, 'F' },
		{ "facility",      required_argument, NULL, 'f' },
		{ "follow",        no_argument,       NULL, 'w' },
		{ "follow-new",    no_argument,       NULL, 'W' },
		{ "human",         no_argument,       NULL, 'H' },
		{ "help",          no_argument,	      NULL, 'h' },
		{ "kernel",        no_argument,       NULL, 'k' },
		{ "level",         required_argument, NULL, 'l' },
		{ "syslog",        no_argument,       NULL, 'S' },
		{ "raw",           no_argument,       NULL, 'r' },
		{ "read-clear",    no_argument,	      NULL, 'c' },
		{ "reltime",       no_argument,       NULL, 'e' },
		{ "show-delta",    no_argument,	      NULL, 'd' },
		{ "ctime",         no_argument,       NULL, 'T' },
		{ "noescape",      no_argument,       NULL, OPT_NOESC },
		{ "notime",        no_argument,       NULL, 't' },
		{ "nopager",       no_argument,       NULL, 'P' },
		{ "userspace",     no_argument,       NULL, 'u' },
		{ "version",       no_argument,	      NULL, 'V' },
		{ "time-format",   required_argument, NULL, OPT_TIME_FORMAT },
		{ "force-prefix",  no_argument,       NULL, 'p' },
		{ NULL,	           0, NULL, 0 }
	};

	static const ul_excl_t excl[] = {	/* rows and cols in ASCII order */
		{ 'C','D','E','c','n','r' },	/* clear,off,on,read-clear,level,raw*/
		{ 'H','r' },			/* human, raw */
		{ 'L','r' },			/* color, raw */
		{ 'S','w' },			/* syslog,follow */
		{ 'T','r' },			/* ctime, raw */
		{ 'd','r' },			/* delta, raw */
		{ 'e','r' },			/* reltime, raw */
		{ 'r','x' },			/* raw, decode */
		{ 'r','t' },			/* notime, raw */
		{ 0 }
	};
	int excl_st[ARRAY_SIZE(excl)] = UL_EXCL_STATUS_INIT;

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	close_stdout_atexit();

	while ((c = getopt_long(argc, argv, "CcDdEeF:f:HhkL::l:n:iPprSs:TtuVWwx",
				longopts, NULL)) != -1) {

		err_exclusive_options(c, longopts, excl, excl_st);

		switch (c) {
		case 'C':
			ctl.action = SYSLOG_ACTION_CLEAR;
			break;
		case 'c':
			ctl.action = SYSLOG_ACTION_READ_CLEAR;
			break;
		case 'D':
			ctl.action = SYSLOG_ACTION_CONSOLE_OFF;
			break;
		case 'd':
			delta = 1;
			break;
		case 'E':
			ctl.action = SYSLOG_ACTION_CONSOLE_ON;
			break;
		case 'e':
			ctl.time_fmt = DMESG_TIMEFTM_RELTIME;
			break;
		case 'F':
			ctl.filename = optarg;
			ctl.method = DMESG_METHOD_MMAP;
			break;
		case 'f':
			ctl.fltr_fac = 1;
			if (string_to_bitarray(optarg,
					     ctl.facilities, parse_facility) < 0)
				return EXIT_FAILURE;
			break;
		case 'H':
			ctl.time_fmt = DMESG_TIMEFTM_RELTIME;
			colormode = UL_COLORMODE_AUTO;
			ctl.pager = 1;
			break;
		case 'k':
			ctl.fltr_fac = 1;
			setbit(ctl.facilities, FAC_BASE(LOG_KERN));
			break;
		case 'L':
			colormode = UL_COLORMODE_AUTO;
			if (optarg)
				colormode = colormode_or_err(optarg,
						_("unsupported color mode"));
			break;
		case 'l':
			ctl.fltr_lev= 1;
			if (string_to_bitarray(optarg,
					     ctl.levels, parse_level) < 0)
				return EXIT_FAILURE;
			break;
		case 'n':
			ctl.action = SYSLOG_ACTION_CONSOLE_LEVEL;
			console_level = parse_level(optarg, 0);
			break;
		case 'P':
			nopager = 1;
			break;
		case 'p':
			ctl.force_prefix = 1;
			break;
		case 'r':
			ctl.raw = 1;
			break;
		case 'S':
			ctl.method = DMESG_METHOD_SYSLOG;
			break;
		case 's':
			ctl.bufsize = strtou32_or_err(optarg,
					_("invalid buffer size argument"));
			if (ctl.bufsize < 4096)
				ctl.bufsize = 4096;
			break;
		case 'T':
			ctl.time_fmt = DMESG_TIMEFTM_CTIME;
			break;
		case 't':
			ctl.time_fmt = DMESG_TIMEFTM_NONE;
			break;
		case 'u':
			ctl.fltr_fac = 1;
			for (n = 1; (size_t) n < ARRAY_SIZE(facility_names); n++)
				setbit(ctl.facilities, n);
			break;
		case 'w':
			ctl.follow = 1;
			break;
		case 'W':
			ctl.follow = 1;
			ctl.end = 1;
			break;
		case 'x':
			ctl.decode = 1;
			break;
		case OPT_TIME_FORMAT:
			ctl.time_fmt = which_time_format(optarg);
			break;
		case OPT_NOESC:
			ctl.noesc = 1;
			break;

		case 'h':
			usage();
		case 'V':
			print_version(EXIT_SUCCESS);
		default:
			errtryhelp(EXIT_FAILURE);
		}
	}

	if (argc != optind) {
		warnx(_("bad usage"));
		errtryhelp(EXIT_FAILURE);
	}

	if ((is_timefmt(&ctl, RELTIME) ||
	     is_timefmt(&ctl, CTIME)   ||
	     is_timefmt(&ctl, ISO8601))) {
		if (dmesg_get_boot_time(&ctl.boot_time) != 0)
			ctl.time_fmt = DMESG_TIMEFTM_NONE;
		else
			ctl.suspended_time = dmesg_get_suspended_time();
	}

	if (delta)
		switch (ctl.time_fmt) {
		case DMESG_TIMEFTM_CTIME:
			ctl.time_fmt = DMESG_TIMEFTM_CTIME_DELTA;
			break;
		case DMESG_TIMEFTM_TIME:
			ctl.time_fmt = DMESG_TIMEFTM_TIME_DELTA;
			break;
		case DMESG_TIMEFTM_ISO8601:
			warnx(_("--show-delta is ignored when used together with iso8601 time format"));
			break;
		default:
			ctl.time_fmt = DMESG_TIMEFTM_DELTA;
		}


	ctl.color = colors_init(colormode, "dmesg") ? 1 : 0;
	if (ctl.follow)
		nopager = 1;
	ctl.pager = nopager ? 0 : ctl.pager;
	if (ctl.pager)
		pager_redirect();

	switch (ctl.action) {
	case SYSLOG_ACTION_READ_ALL:
	case SYSLOG_ACTION_READ_CLEAR:
		if (ctl.method == DMESG_METHOD_KMSG && init_kmsg(&ctl) != 0)
			ctl.method = DMESG_METHOD_SYSLOG;

		if (ctl.raw
		    && ctl.method != DMESG_METHOD_KMSG
		    && (ctl.fltr_lev || ctl.fltr_fac))
			    errx(EXIT_FAILURE, _("--raw can be used together with --level or "
				 "--facility only when reading messages from /dev/kmsg"));

		/* only kmsg supports multi-line messages */
		if (ctl.force_prefix && ctl.method != DMESG_METHOD_KMSG)
			ctl.force_prefix = 0;

		if (ctl.pager)
			pager_redirect();
		n = read_buffer(&ctl, &buf);
		if (n > 0)
			print_buffer(&ctl, buf, n);
		if (!ctl.mmap_buff)
			free(buf);
		if (n < 0)
			err(EXIT_FAILURE, _("read kernel buffer failed"));
		if (ctl.kmsg >= 0)
			close(ctl.kmsg);
		break;
	case SYSLOG_ACTION_CLEAR:
	case SYSLOG_ACTION_CONSOLE_OFF:
	case SYSLOG_ACTION_CONSOLE_ON:
		klog_rc = klogctl(ctl.action, NULL, 0);
		break;
	case SYSLOG_ACTION_CONSOLE_LEVEL:
		klog_rc = klogctl(ctl.action, NULL, console_level);
		break;
	default:
		errx(EXIT_FAILURE, _("unsupported command"));
		break;
	}


	if (klog_rc)
		err(EXIT_FAILURE, _("klogctl failed"));

	return EXIT_SUCCESS;
}
