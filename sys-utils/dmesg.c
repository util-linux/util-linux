/*
 * dmesg.c -- Print out the contents of the kernel ring buffer
 *
 * Copyright (C) 1993 Theodore Ts'o <tytso@athena.mit.edu>
 * Copyright (C) 2011 Karel Zak <kzak@redhat.com>
 *
 * This program comes with ABSOLUTELY NO WARRANTY.
 */
#include <linux/unistd.h>
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
#include "nls.h"
#include "strutils.h"
#include "xalloc.h"
#include "widechar.h"
#include "all-io.h"
#include "bitops.h"
#include "closestream.h"
#include "optutils.h"
#include "mangle.h"

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

/* supported methods to read message buffer
 */
enum {
	DMESG_METHOD_KMSG,	/* read messages from /dev/kmsg (default) */
	DMESG_METHOD_SYSLOG,	/* klogctl() buffer */
	DMESG_METHOD_MMAP	/* mmap file with records (see --file) */
};

struct dmesg_control {
	/* bit arrays -- see include/bitops.h */
	char levels[ARRAY_SIZE(level_names) / NBBY + 1];
	char facilities[ARRAY_SIZE(facility_names) / NBBY + 1];

	struct timeval	lasttime;	/* last printed timestamp */
	struct tm	lasttm;		/* last localtime */
	time_t		boot_time;	/* system boot time */

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

	unsigned int	follow:1,	/* wait for new messages */
			raw:1,		/* raw mode */
			fltr_lev:1,	/* filter out by levels[] */
			fltr_fac:1,	/* filter out by facilities[] */
			decode:1,	/* use "facility: level: " prefix */
			notime:1,	/* don't print timestamp */
			delta:1,	/* show time deltas */
			reltime:1,	/* show human readable relative times */
			ctime:1;	/* show human readable time */
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


static void __attribute__((__noreturn__)) usage(FILE *out)
{
	size_t i;

	fputs(_("\nUsage:\n"), out);
	fprintf(out,
	      _(" %s [options]\n"), program_invocation_short_name);

	fputs(_("\nOptions:\n"), out);
	fputs(_(" -C, --clear                 clear the kernel ring buffer\n"
		" -c, --read-clear            read and clear all messages\n"
		" -D, --console-off           disable printing messages to console\n"
		" -d, --show-delta            show time delta between printed messages\n"
		" -e, --reltime               show local time and time delta in readable format\n"
		" -E, --console-on            enable printing messages to console\n"
		" -F, --file <file>           use the file instead of the kernel log buffer\n"
		" -f, --facility <list>       restrict output to defined facilities\n"
		" -h, --help                  display this help and exit\n"
		" -k, --kernel                display kernel messages\n"
		" -l, --level <list>          restrict output to defined levels\n"
		" -n, --console-level <level> set level of messages printed to console\n"
		" -r, --raw                   print the raw message buffer\n"
		" -S, --syslog                force to use syslog(2) rather than /dev/kmsg\n"
		" -s, --buffer-size <size>    buffer size to query the kernel ring buffer\n"
		" -T, --ctime                 show human readable timestamp (could be \n"
		"                             inaccurate if you have used SUSPEND/RESUME)\n"
		" -t, --notime                don't print messages timestamp\n"
		" -u, --userspace             display userspace messages\n"
		" -V, --version               output version information and exit\n"
		" -w, --follow                wait for new messages\n"
		" -x, --decode                decode facility and level to readable string\n"), out);

	fputs(_("\nSupported log facilities:\n"), out);
	for (i = 0; i < ARRAY_SIZE(level_names); i++) {
		fprintf(stderr, " %7s - %s\n",
				facility_names[i].name,
				_(facility_names[i].help));
	}

	fputs(_("\nSupported log levels (priorities):\n"), out);
	for (i = 0; i < ARRAY_SIZE(level_names); i++) {
		fprintf(stderr, " %7s - %s\n",
				level_names[i].name,
				_(level_names[i].help));
	}

	fputc('\n', out);

	exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}

/*
 * LEVEL     ::= <number> | <name>
 *  <number> ::= number in range <0..N>, where N < ARRAY_SIZE(level_names)
 *  <name>   ::= case-insensitive text
 */
static int parse_level(const char *str, size_t len)
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
		    x >= 0 && (size_t) x < ARRAY_SIZE(level_names))
			return x;
	} else {
		size_t i;

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

static time_t get_boot_time(void)
{
	struct sysinfo info;
	struct timeval tv;

	if (sysinfo(&info) != 0)
		warn(_("sysinfo failed"));
	else if (gettimeofday(&tv, NULL) != 0)
		warn(_("gettimeofday failed"));
	else
		return tv.tv_sec -= info.uptime;
	return 0;
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
		err(EXIT_FAILURE, _("stat failed %s"), ctl->filename);

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
	}

	return n;
}

static int fwrite_hex(const char *buf, size_t size, FILE *out)
{
	size_t i;

	for (i = 0; i < size; i++) {
		int rc = fprintf(out, "\\x%02x", buf[i]);
		if (rc < 0)
			return rc;
	}
	return 0;
}

/*
 * Prints to 'out' and non-printable chars are replaced with \x<hex> sequences.
 */
static void safe_fwrite(const char *buf, size_t size, FILE *out)
{
	size_t i;
#ifdef HAVE_WIDECHAR
	mbstate_t s;
	memset(&s, 0, sizeof (s));
#endif
	for (i = 0; i < size; i++) {
		const char *p = buf + i;
		int rc, hex = 0;
	        size_t len = 1;

#ifdef HAVE_WIDECHAR
		wchar_t wc;
		len = mbrtowc(&wc, p, size - i, &s);

		if (len == 0)				/* L'\0' */
			return;

		if (len == (size_t)-1 || len == (size_t)-2) {		/* invalid sequence */
			memset(&s, 0, sizeof (s));
			len = hex = 1;
		} else if (len > 1 && !iswprint(wc)) {	/* non-printable multibyte */
			hex = 1;
		}
		i += len - 1;
#else
		if (!isprint((unsigned int) *p) &&
			!isspace((unsigned int) *p))        /* non-printable */
			hex = 1;
#endif
		if (hex)
			rc = fwrite_hex(p, len, out);
		else
			rc = fwrite(p, 1, len, out) != len;
		if (rc != 0)
			err(EXIT_FAILURE, _("write failed"));
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
			if (ctl->fltr_lev || ctl->fltr_fac || ctl->decode)
				begin = parse_faclev(begin + 1, &rec->facility,
						     &rec->level);
			else
				begin = skip_item(begin, end, ">");
		}

		if (*begin == '[' && (*(begin + 1) == ' ' ||
				      isdigit(*(begin + 1)))) {
			if (ctl->delta || ctl->ctime || ctl->reltime)
				begin = parse_syslog_timestamp(begin + 1, &rec->tv);
			else if (ctl->notime)
				begin = skip_item(begin, end, "]");

			if (begin < end && *begin == ' ')
				begin++;
		}

		rec->mesg = begin;
		rec->mesg_size = end - begin;

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
		safe_fwrite(buf, size, stdout);
		lastc = buf[size - 1];
	} else {
		/*
		 * Print file in small chunks to save memory
		 */
		while (size) {
			size_t sz = size > ctl->pagesize ? ctl->pagesize : size;
			char *x = ctl->mmap_buff;

			safe_fwrite(x, sz, stdout);
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
	time_t t = ctl->boot_time + rec->tv.tv_sec;
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

static double record_count_delta(struct dmesg_control *ctl,
				 struct dmesg_record *rec)
{
	double delta = 0;

	if (timerisset(&ctl->lasttime))
		delta = time_diff(&rec->tv, &ctl->lasttime);

	ctl->lasttime = rec->tv;
	return delta;
}

static void print_record(struct dmesg_control *ctl,
			 struct dmesg_record *rec)
{
	char buf[256];

	if (!accept_record(ctl, rec))
		return;

	if (!rec->mesg_size) {
		putchar('\n');
		return;
	}

	/*
	 * compose syslog(2) compatible raw output -- used for /dev/kmsg for
	 * backward compatibility with syslog(2) buffers only
	 */
	if (ctl->raw) {
		printf("<%d>[%5d.%06d] ",
			LOG_MAKEPRI(rec->facility, rec->level),
			(int) rec->tv.tv_sec,
			(int) rec->tv.tv_usec);

		goto mesg;
	}

	/*
	 * facility : priority :
	 */
	if (ctl->decode && rec->level >= 0 && rec->facility >= 0)
		printf("%-6s:%-6s: ", facility_names[rec->facility].name,
				      level_names[rec->level].name);

	/*
	 * [sec.usec <delta>] or [ctime <delta>]
	 */
	if (ctl->delta) {
		if (ctl->ctime)
			printf("[%s ", record_ctime(ctl, rec, buf, sizeof(buf)));
		else if (ctl->notime)
			putchar('[');
		else
			printf("[%5d.%06d ", (int) rec->tv.tv_sec,
					     (int) rec->tv.tv_usec);
		printf("<%12.06f>] ", record_count_delta(ctl, rec));

	/*
	 * [ctime]
	 */
	} else if (ctl->ctime)
		printf("[%s] ", record_ctime(ctl, rec, buf, sizeof(buf)));

	/*
	 * [reltime]
	 */
	else if (ctl->reltime) {
		double delta;
		struct tm cur;

		record_localtime(ctl, rec, &cur);
		delta = record_count_delta(ctl, rec);

		if (cur.tm_min  != ctl->lasttm.tm_min ||
		    cur.tm_hour != ctl->lasttm.tm_hour ||
		    cur.tm_yday != ctl->lasttm.tm_yday)
			printf("[%s] ", short_ctime(&cur, buf, sizeof(buf)));
		else if (delta < 10)
			printf("[  %+8.06f] ", delta);
		else
			printf("[ %+9.06f] ", delta);

		ctl->lasttm = cur;
	}

	/*
	 * In syslog output the timestamp is part of the message and we don't
	 * parse the timestamp by default. We parse the timestamp only if
	 * --show-delta or --ctime is specified.
	 *
	 * In kmsg output we always parse the timesptamp, so we have to compose
	 * the [sec.usec] string.
	 */
	if (ctl->method == DMESG_METHOD_KMSG &&
	    !ctl->notime && !ctl->delta && !ctl->ctime && !ctl->reltime)
		printf("[%5d.%06d] ", (int) rec->tv.tv_sec, (int) rec->tv.tv_usec);

mesg:
	safe_fwrite(rec->mesg, rec->mesg_size, stdout);

	if (*(rec->mesg + rec->mesg_size - 1) != '\n')
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

	ctl->kmsg = open("/dev/kmsg", mode);
	if (ctl->kmsg < 0)
		return -1;

	/*
	 * Seek after the last record available at the time
	 * the last SYSLOG_ACTION_CLEAR was issued.
	 *
	 * ... otherwise SYSLOG_ACTION_CLEAR will have no effect for kmsg.
	 */
	lseek(ctl->kmsg, 0, SEEK_DATA);

	/*
	 * Old kernels (<3.5) allow to successfully open /dev/kmsg for
	 * read-only, but read() returns -EINVAL :-(((
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
 *     faclev,seqnum,timestamp[optional, ...];messgage\n
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
	if (ctl->fltr_lev || ctl->fltr_fac || ctl->decode || ctl->raw)
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
	if (ctl->notime)
		p = skip_item(p, end, ",;");
	else
		p = parse_kmsg_timestamp(p, &rec->tv);
	if (LAST_KMSG_FIELD(p))
		goto mesg;

	/* D) optional fields (ignore) */
	p = skip_item(p, end, ";");
	if (LAST_KMSG_FIELD(p))
		goto mesg;

mesg:
	/* E) message text */
	rec->mesg = p;
	p = skip_item(p, end, "\n");

	if (!p)
		return -1;

	rec->mesg_size = p - rec->mesg;

	/*
	 * Kernel escapes non-printable characters, unfortuately kernel
	 * definition of "non-printable" is too strict. On UTF8 console we can
	 * print many chars, so let's decode from kernel.
	 */
	unhexmangle_to_buffer(rec->mesg, (char *) rec->mesg, rec->mesg_size + 1);

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

#define EXCL_ACT_ERR "--{clear,read-clear,console-level,console-on,console-off}"
#define EXCL_SYS_ERR "--{syslog,follow}"

int main(int argc, char *argv[])
{
	char *buf = NULL;
	int  c;
	int  console_level = 0;
	int  klog_rc = 0;
	ssize_t n;
	static struct dmesg_control ctl = {
		.filename = NULL,
		.action = SYSLOG_ACTION_READ_ALL,
		.method = DMESG_METHOD_KMSG,
		.kmsg = -1,
	};

	static const struct option longopts[] = {
		{ "buffer-size",   required_argument, NULL, 's' },
		{ "clear",         no_argument,	      NULL, 'C' },
		{ "console-level", required_argument, NULL, 'n' },
		{ "console-off",   no_argument,       NULL, 'D' },
		{ "console-on",    no_argument,       NULL, 'E' },
		{ "decode",        no_argument,	      NULL, 'x' },
		{ "file",          required_argument, NULL, 'F' },
		{ "facility",      required_argument, NULL, 'f' },
		{ "follow",        no_argument,       NULL, 'w' },
		{ "help",          no_argument,	      NULL, 'h' },
		{ "kernel",        no_argument,       NULL, 'k' },
		{ "level",         required_argument, NULL, 'l' },
		{ "syslog",        no_argument,       NULL, 'S' },
		{ "raw",           no_argument,       NULL, 'r' },
		{ "read-clear",    no_argument,	      NULL, 'c' },
		{ "reltime",       no_argument,       NULL, 'e' },
		{ "show-delta",    no_argument,	      NULL, 'd' },
		{ "ctime",         no_argument,       NULL, 'T' },
		{ "notime",        no_argument,       NULL, 't' },
		{ "userspace",     no_argument,       NULL, 'u' },
		{ "version",       no_argument,	      NULL, 'V' },
		{ NULL,	           0, NULL, 0 }
	};

	static const ul_excl_t excl[] = {	/* rows and cols in in ASCII order */
		{ 'C','D','E','c','n' },	/* clear,off,on,read-clear,level*/
		{ 'S','w' },			/* syslog,follow */
		{ 0 }
	};
	int excl_st[ARRAY_SIZE(excl)] = UL_EXCL_STATUS_INIT;

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	while ((c = getopt_long(argc, argv, "CcDdEeF:f:hkl:n:rSs:TtuVwx",
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
			ctl.delta = 1;
			break;
		case 'E':
			ctl.action = SYSLOG_ACTION_CONSOLE_ON;
			break;
		case 'e':
			ctl.boot_time = get_boot_time();
			if (ctl.boot_time)
				ctl.reltime = 1;
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
		case 'h':
			usage(stdout);
			break;
		case 'k':
			ctl.fltr_fac = 1;
			setbit(ctl.facilities, FAC_BASE(LOG_KERN));
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
			ctl.boot_time = get_boot_time();
			if (ctl.boot_time)
				ctl.ctime = 1;
			break;
		case 't':
			ctl.notime = 1;
			break;
		case 'u':
			ctl.fltr_fac = 1;
			for (n = 1; (size_t) n < ARRAY_SIZE(facility_names); n++)
				setbit(ctl.facilities, n);
			break;
		case 'V':
			printf(_("%s from %s\n"), program_invocation_short_name,
						  PACKAGE_STRING);
			return EXIT_SUCCESS;
		case 'w':
			ctl.follow = 1;
			break;
		case 'x':
			ctl.decode = 1;
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

	if (ctl.raw && (ctl.fltr_lev || ctl.fltr_fac || ctl.delta ||
			ctl.notime || ctl.ctime || ctl.decode))
		errx(EXIT_FAILURE, _("--raw can't be used together with level, "
		     "facility, decode, delta, ctime or notime options"));

	if (ctl.notime && (ctl.ctime || ctl.reltime))
		errx(EXIT_FAILURE, _("--notime can't be used together with --ctime or --reltime"));
	if (ctl.reltime && ctl.ctime)
		errx(EXIT_FAILURE, _("--reltime can't be used together with --ctime "));

	switch (ctl.action) {
	case SYSLOG_ACTION_READ_ALL:
	case SYSLOG_ACTION_READ_CLEAR:
		if (ctl.method == DMESG_METHOD_KMSG && init_kmsg(&ctl) != 0)
			ctl.method = DMESG_METHOD_SYSLOG;

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
