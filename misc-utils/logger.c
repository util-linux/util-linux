/*
 * Copyright (c) 1983, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * 1999-02-22 Arkadiusz Miśkiewicz <misiek@pld.ORG.PL>
 * - added Native Language Support
 * Sun Mar 21 1999 - Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 * - fixed strerr(errno) in gettext calls
 */

#include <errno.h>
#include <limits.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/time.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <getopt.h>
#include <pwd.h>

#include "all-io.h"
#include "c.h"
#include "closestream.h"
#include "nls.h"
#include "pathnames.h"
#include "strutils.h"
#include "xalloc.h"

#define	SYSLOG_NAMES
#include <syslog.h>

#ifdef HAVE_LIBSYSTEMD
# include <systemd/sd-daemon.h>
# include <systemd/sd-journal.h>
#endif

#ifdef HAVE_SYS_TIMEX_H
# include <sys/timex.h>
#endif

enum {
	TYPE_UDP = (1 << 1),
	TYPE_TCP = (1 << 2),
	ALL_TYPES = TYPE_UDP | TYPE_TCP
};

enum {
	AF_UNIX_ERRORS_OFF = 0,
	AF_UNIX_ERRORS_ON,
	AF_UNIX_ERRORS_AUTO
};

enum {
	OPT_PRIO_PREFIX = CHAR_MAX + 1,
	OPT_JOURNALD,
	OPT_RFC3164,
	OPT_RFC5424,
	OPT_SOCKET_ERRORS,
	OPT_ID
};

struct logger_ctl {
	int fd;
	int pri;
	pid_t pid;			/* zero when unwanted */
	char *tag;
	char *unix_socket;		/* -u <path> or default to _PATH_DEVLOG */
	char *server;
	char *port;
	int socket_type;
	void (*syslogfp)(const struct logger_ctl *ctl, const char *msg);
	unsigned int
			unix_socket_errors:1,	/* whether to report or not errors */
			prio_prefix:1,		/* read priority from intput */
			stderr_printout:1,	/* output message to stderr */
			rfc5424_time:1,		/* include time stamp */
			rfc5424_tq:1,		/* include time quality markup */
			rfc5424_host:1;		/* include hostname */
};

static char *get_prio_prefix(char *msg, int *prio)
{
	int p;
	char *end = NULL;
	int facility = *prio & LOG_FACMASK;

	errno = 0;
	p = strtoul(msg + 1, &end, 10);

	if (errno || !end || end == msg + 1 || end[0] != '>')
		return msg;

	if (p & LOG_FACMASK)
		facility = p & LOG_FACMASK;

	*prio = facility | (p & LOG_PRIMASK);
	return end + 1;
}

static int decode(const char *name, CODE *codetab)
{
	register CODE *c;

	if (name == NULL || *name == '\0')
		return -1;
	if (isdigit(*name)) {
		int num;
		char *end = NULL;

		num = strtol(name, &end, 10);
		if (errno || name == end || (end && *end))
			return -1;
		for (c = codetab; c->c_name; c++)
			if (num == c->c_val)
				return num;
		return -1;
	}
	for (c = codetab; c->c_name; c++)
		if (!strcasecmp(name, c->c_name))
			return (c->c_val);

	return -1;
}

static int pencode(char *s)
{
	int facility, level;
	char *separator;

	separator = strchr(s, '.');
	if (separator) {
		*separator = '\0';
		facility = decode(s, facilitynames);
		if (facility < 0)
			errx(EXIT_FAILURE, _("unknown facility name: %s"), s);
		s = ++separator;
	} else
		facility = LOG_USER;
	level = decode(s, prioritynames);
	if (level < 0)
		errx(EXIT_FAILURE, _("unknown priority name: %s"), s);
	if(facility == LOG_KERN)
		facility = LOG_USER; /* kern is forbidden */
	return ((level & LOG_PRIMASK) | (facility & LOG_FACMASK));
}

static int unix_socket(struct logger_ctl *ctl, const char *path, const int socket_type)
{
	int fd, i;
	static struct sockaddr_un s_addr;	/* AF_UNIX address of local logger */

	if (strlen(path) >= sizeof(s_addr.sun_path))
		errx(EXIT_FAILURE, _("openlog %s: pathname too long"), path);

	s_addr.sun_family = AF_UNIX;
	strcpy(s_addr.sun_path, path);

	for (i = 2; i; i--) {
		int st = -1;

		if (i == 2 && socket_type & TYPE_UDP)
			st = SOCK_DGRAM;
		if (i == 1 && socket_type & TYPE_TCP)
			st = SOCK_STREAM;
		if (st == -1 || (fd = socket(AF_UNIX, st, 0)) == -1)
			continue;
		if (connect(fd, (struct sockaddr *)&s_addr, sizeof(s_addr)) == -1) {
			close(fd);
			continue;
		}
		break;
	}

	if (i == 0) {
		if (ctl->unix_socket_errors)
			err(EXIT_FAILURE, _("socket %s"), path);
		else
			/* See --socket-errors manual page entry for
			 * explanation of this strange exit.  */
			exit(EXIT_SUCCESS);
	}
	return fd;
}

static int inet_socket(const char *servername, const char *port,
		       const int socket_type)
{
	int fd, errcode, i;
	struct addrinfo hints, *res;
	const char *p = port;

	for (i = 2; i; i--) {
		memset(&hints, 0, sizeof(hints));
		if (i == 2 && socket_type & TYPE_UDP) {
			hints.ai_socktype = SOCK_DGRAM;
			if (port == NULL)
				p = "syslog";
		}
		if (i == 1 && socket_type & TYPE_TCP) {
			hints.ai_socktype = SOCK_STREAM;
			if (port == NULL)
				p = "syslog-conn";
		}
		if (hints.ai_socktype == 0)
			continue;
		hints.ai_family = AF_UNSPEC;
		errcode = getaddrinfo(servername, p, &hints, &res);
		if (errcode != 0)
			errx(EXIT_FAILURE, _("failed to resolve name %s port %s: %s"),
			     servername, p, gai_strerror(errcode));
		if ((fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol)) == -1) {
			freeaddrinfo(res);
			continue;
		}
		if (connect(fd, res->ai_addr, res->ai_addrlen) == -1) {
			freeaddrinfo(res);
			close(fd);
			continue;
		}

		freeaddrinfo(res);
		break;
	}

	if (i == 0)
		errx(EXIT_FAILURE, _("failed to connect to %s port %s"), servername, p);

	return fd;
}

#ifdef HAVE_LIBSYSTEMD
static int journald_entry(FILE *fp)
{
	struct iovec *iovec;
	char *buf = NULL;
	ssize_t sz;
	int n, lines, vectors = 8, ret;
	size_t dummy = 0;

	iovec = xmalloc(vectors * sizeof(struct iovec));
	for (lines = 0; /* nothing */ ; lines++) {
		buf = NULL;
		sz = getline(&buf, &dummy, fp);
		if (sz == -1)
			break;
		if (0 < sz && buf[sz - 1] == '\n') {
			sz--;
			buf[sz] = '\0';
		}
		if (lines == vectors) {
			vectors *= 2;
			if (IOV_MAX < vectors)
				errx(EXIT_FAILURE, _("maximum input lines (%d) exceeded"), IOV_MAX);
			iovec = xrealloc(iovec, vectors * sizeof(struct iovec));
		}
		iovec[lines].iov_base = buf;
		iovec[lines].iov_len = sz;
	}
	ret = sd_journal_sendv(iovec, lines);
	for (n = 0; n < lines; n++)
		free(iovec[n].iov_base);
	free(iovec);
	return ret;
}
#endif

static char *xgetlogin(void)
{
	char *cp;
	struct passwd *pw;

	if (!(cp = getlogin()) || !*cp)
		cp = (pw = getpwuid(geteuid()))? pw->pw_name : "<someone>";
	return cp;
}


/* this creates a timestamp based on current time according to the
 * fine rules of RFC3164, most importantly it ensures in a portable
 * way that the month day is correctly written (with a SP instead
 * of a leading 0). The function uses a static buffer which is
 * overwritten on the next call (just like ctime() does).
 */
static const char *
rfc3164_current_time(void)
{
	static char time[32];
	struct timeval tv;
	struct tm *tm;
	static char *monthnames[] = { "Jan", "Feb", "Mar", "Apr",
			"May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

	gettimeofday(&tv, NULL);
	tm = localtime(&tv.tv_sec);
	snprintf(time, sizeof(time),"%s %2d %2.2d:%2.2d:%2.2d",
		monthnames[tm->tm_mon], tm->tm_mday,
		tm->tm_hour, tm->tm_min, tm->tm_sec); 
	return time;
}

static void syslog_rfc3164(const struct logger_ctl *ctl, const char *msg)
{
	char *buf, pid[30], *cp, *hostname, *dot;
	time_t now;
	int len;

	*pid = '\0';
	if (ctl->fd < 0)
		return;
	if (ctl->pid)
		snprintf(pid, sizeof(pid), "[%d]", ctl->pid);

	cp = ctl->tag ? ctl->tag : xgetlogin();

	hostname = xgethostname();
	dot = strchr(hostname, '.');
	if (dot)
		*dot = '\0';

	len = xasprintf(&buf, "<%d>%.15s %s %.200s%s: %.400s",
		 ctl->pri, rfc3164_current_time(), hostname, cp, pid, msg);

	if (write_all(ctl->fd, buf, len) < 0)
		warn(_("write failed"));
	if (ctl->stderr_printout)
		fprintf(stderr, "%s\n", buf);

	free(hostname);
	free(buf);
}

static void syslog_rfc5424(const  struct logger_ctl *ctl, const char *msg)
{
	char *buf, *tag = NULL, *hostname = NULL;
	char pid[32], time[64], timeq[80];
#ifdef HAVE_SYS_TIMEX_H
	struct ntptimeval ntptv;
#endif
	struct timeval tv;
	struct tm *tm;
	int len;

	*pid = *time = *timeq = '\0';
	if (ctl->fd < 0)
		return;

	if (ctl->rfc5424_time) {
		gettimeofday(&tv, NULL);
		if ((tm = localtime(&tv.tv_sec)) != NULL) {
			char fmt[64];

			const size_t i = strftime(fmt, sizeof(fmt),
					" %Y-%m-%dT%H:%M:%S.%%06u%z ", tm);
			/* patch TZ info to comply with RFC3339 (we left SP at end) */
			fmt[i-1] = fmt[i-2];
			fmt[i-2] = fmt[i-3];
			fmt[i-3] = ':';
			snprintf(time, sizeof(time), fmt, tv.tv_usec);
		} else
			err(EXIT_FAILURE, _("localtime() failed"));
	}

	if (ctl->rfc5424_host) {
		hostname = xgethostname();
		/* Arbitrary looking 'if (var < strlen()) checks originate from
		 * RFC 5424 - 6 Syslog Message Format definition.  */
		if (255 < strlen(hostname))
			errx(EXIT_FAILURE, _("hostname '%s' is too long"),
			     hostname);
	}

	tag = ctl->tag ? ctl->tag : xgetlogin();

	if (48 < strlen(tag))
		errx(EXIT_FAILURE, _("tag '%s' is too long"), tag);

	if (ctl->pid)
		snprintf(pid, sizeof(pid), " %d", ctl->pid);

	if (ctl->rfc5424_tq) {
#ifdef HAVE_SYS_TIMEX_H
		if (ntp_gettime(&ntptv) == TIME_OK)
			snprintf(timeq, sizeof(timeq),
				 " [timeQuality tzKnown=\"1\" isSynced=\"1\" syncAccuracy=\"%ld\"]",
				 ntptv.maxerror);
		else
#endif
			snprintf(timeq, sizeof(timeq),
				 " [timeQuality tzKnown=\"1\" isSynced=\"0\"]");
	}

	len = xasprintf(&buf, "<%d>1%s%s%s %s -%s%s %s", ctl->pri, time,
		  hostname ? " " : "",
		  hostname ? hostname : "",
		  tag, pid, timeq, msg);

	if (write_all(ctl->fd, buf, len) < 0)
		warn(_("write failed"));

	if (ctl->stderr_printout)
		fprintf(stderr, "%s\n", buf);

	free(hostname);
	free(buf);
}

static void parse_rfc5424_flags(struct logger_ctl *ctl, char *optarg)
{
	char *in, *tok;

	in = optarg;
	while ((tok = strtok(in, ","))) {
		in = NULL;
		if (!strcmp(tok, "notime")) {
			ctl->rfc5424_time = 0;
			ctl->rfc5424_tq = 0;
		} else if (!strcmp(tok, "notq"))
			ctl->rfc5424_tq = 0;
		else if (!strcmp(tok, "nohost"))
			ctl->rfc5424_host = 0;
		else
			warnx(_("ignoring unknown option argument: %s"), tok);
	}
}

static int parse_unix_socket_errors_flags(char *optarg)
{
	if (!strcmp(optarg, "off"))
		return AF_UNIX_ERRORS_OFF;
	if (!strcmp(optarg, "on"))
		return AF_UNIX_ERRORS_ON;
	if (!strcmp(optarg, "auto"))
		return AF_UNIX_ERRORS_AUTO;
	warnx(_("invalid argument: %s: using automatic errors"), optarg);
	return AF_UNIX_ERRORS_AUTO;
}

static void syslog_local(const struct logger_ctl *ctl, const char *msg)
{
	char *buf, *tag;
	char pid[32];
	int len;

	tag = ctl->tag ? ctl->tag : program_invocation_short_name;

	if (ctl->pid)
		snprintf(pid, sizeof(pid), "[%d]", ctl->pid);
	else
		pid[0] = '\0';

	len = xasprintf(&buf, "<%d>%s %s%s: %s", ctl->pri, rfc3164_current_time(),
		tag, pid, msg);
	if (write_all(ctl->fd, buf, len) < 0)
		warn(_("write failed"));
	if (ctl->stderr_printout)
		fprintf(stderr, "%s\n", buf);
	free(buf);
}

static void logger_open(struct logger_ctl *ctl)
{
	if (ctl->server) {
		ctl->fd = inet_socket(ctl->server, ctl->port, ctl->socket_type);
		if (!ctl->syslogfp)
			ctl->syslogfp = syslog_rfc5424;
		return;
	}
	if (!ctl->unix_socket)
		ctl->unix_socket = _PATH_DEVLOG;

	ctl->fd = unix_socket(ctl, ctl->unix_socket, ctl->socket_type);
	if (!ctl->syslogfp)
		ctl->syslogfp = syslog_local;
}

static void logger_command_line(const struct logger_ctl *ctl, char **argv)
{
	char buf[4096];
	char *p = buf;
	const char *endp = buf + sizeof(buf) - 2;
	size_t len;

	while (*argv) {
		len = strlen(*argv);
		if (endp < p + len && p != buf) {
			ctl->syslogfp(ctl, buf);
			p = buf;
		}
		if (sizeof(buf) - 1 < len) {
			ctl->syslogfp(ctl, *argv++);
			continue;
		}
		if (p != buf)
			*p++ = ' ';
		memmove(p, *argv++, len);
		*(p += len) = '\0';
	}
	if (p != buf)
		ctl->syslogfp(ctl, buf);
}

static void logger_stdin(struct logger_ctl *ctl)
{
	char *msg;
	int default_priority = ctl->pri;
	char buf[1024];

	while (fgets(buf, sizeof(buf), stdin) != NULL) {
		int len = strlen(buf);

		/* some glibc versions are buggy, they add an additional
		 * newline which is removed here.  */
		if (0 < len && buf[len - 1] == '\n')
			buf[len - 1] = '\0';
		msg = buf;
		ctl->pri = default_priority;
		if (ctl->prio_prefix && msg[0] == '<')
			msg = get_prio_prefix(msg, &ctl->pri);
		ctl->syslogfp(ctl, msg);
	}
}

static void logger_close(const struct logger_ctl *ctl)
{
	if (close(ctl->fd) != 0)
		err(EXIT_FAILURE, _("close failed"));
}

static void __attribute__ ((__noreturn__)) usage(FILE *out)
{
	fputs(USAGE_HEADER, out);
	fprintf(out, _(" %s [options] [<message>]\n"), program_invocation_short_name);

	fputs(USAGE_SEPARATOR, out);
	fputs(_("Enter messages into the system log.\n"), out);

	fputs(USAGE_OPTIONS, out);
	fputs(_(" -i                       log the logger command's PID\n"), out);
	fputs(_("     --id[=<id>]          log the given <id>, or otherwise the PID\n"), out);
	fputs(_(" -f, --file <file>        log the contents of this file\n"), out);
	fputs(_(" -p, --priority <prio>    mark given message with this priority\n"), out);
	fputs(_("     --prio-prefix        look for a prefix on every line read from stdin\n"), out);
	fputs(_(" -s, --stderr             output message to standard error as well\n"), out);
	fputs(_(" -t, --tag <tag>          mark every line with this tag\n"), out);
	fputs(_(" -n, --server <name>      write to this remote syslog server\n"), out);
	fputs(_(" -P, --port <number>      use this UDP port\n"), out);
	fputs(_(" -T, --tcp                use TCP only\n"), out);
	fputs(_(" -d, --udp                use UDP only\n"), out);
	fputs(_("     --rfc3164            use the obsolete BSD syslog protocol\n"), out);
	fputs(_("     --rfc5424[=<snip>]   use the syslog protocol (the default);\n"
		"                            <snip> can be notime, or notq, and/or nohost\n"), out);
	fputs(_(" -u, --socket <socket>    write to this Unix socket\n"), out);
	fputs(_("     --socket-errors[=<on|off|auto>]\n"
		"                          print connection errors when using Unix sockets\n"), out);
#ifdef HAVE_LIBSYSTEMD
	fputs(_("     --journald[=<file>]  write journald entry\n"), out);
#endif

	fputs(USAGE_SEPARATOR, out);
	fputs(USAGE_HELP, out);
	fputs(USAGE_VERSION, out);
	fprintf(out, USAGE_MAN_TAIL("logger(1)"));

	exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}

/*
 * logger -- read and log utility
 *
 *	Reads from an input and arranges to write the result on the system
 *	log.
 */
int main(int argc, char **argv)
{
	struct logger_ctl ctl = {
		.fd = -1,
		.pid = 0,
		.pri = LOG_USER | LOG_NOTICE,
		.prio_prefix = 0,
		.tag = NULL,
		.unix_socket = NULL,
		.unix_socket_errors = 0,
		.server = NULL,
		.port = NULL,
		.socket_type = ALL_TYPES,
		.rfc5424_time = 1,
		.rfc5424_tq = 1,
		.rfc5424_host = 1,
	};
	int ch;
	int stdout_reopened = 0;
	int unix_socket_errors_mode = AF_UNIX_ERRORS_AUTO;
#ifdef HAVE_LIBSYSTEMD
	FILE *jfd = NULL;
#endif
	static const struct option longopts[] = {
		{ "id",		optional_argument,  0, OPT_ID },
		{ "stderr",	no_argument,	    0, 's' },
		{ "file",	required_argument,  0, 'f' },
		{ "priority",	required_argument,  0, 'p' },
		{ "tag",	required_argument,  0, 't' },
		{ "socket",	required_argument,  0, 'u' },
		{ "socket-errors", required_argument, 0, OPT_SOCKET_ERRORS },
		{ "udp",	no_argument,	    0, 'd' },
		{ "tcp",	no_argument,	    0, 'T' },
		{ "server",	required_argument,  0, 'n' },
		{ "port",	required_argument,  0, 'P' },
		{ "version",	no_argument,	    0, 'V' },
		{ "help",	no_argument,	    0, 'h' },
		{ "prio-prefix", no_argument, 0, OPT_PRIO_PREFIX },
		{ "rfc3164",	no_argument,  0, OPT_RFC3164 },
		{ "rfc5424",	optional_argument,  0, OPT_RFC5424 },
#ifdef HAVE_LIBSYSTEMD
		{ "journald",   optional_argument,  0, OPT_JOURNALD },
#endif
		{ NULL,		0, 0, 0 }
	};

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	while ((ch = getopt_long(argc, argv, "f:ip:st:u:dTn:P:Vh",
					    longopts, NULL)) != -1) {
		switch (ch) {
		case 'f':		/* file to log */
			if (freopen(optarg, "r", stdin) == NULL)
				err(EXIT_FAILURE, _("file %s"), optarg);
			stdout_reopened = 1;
			break;
		case 'i':		/* log process id also */
			ctl.pid = getpid();
			break;
		case OPT_ID:
			if (optarg) {
				const char *p = optarg;

				if (*p == '=')
					p++;
				ctl.pid = strtoul_or_err(optarg, _("failed to parse id"));
			} else
				ctl.pid = getpid();
			break;
		case 'p':		/* priority */
			ctl.pri = pencode(optarg);
			break;
		case 's':		/* log to standard error */
			ctl.stderr_printout = 1;
			break;
		case 't':		/* tag */
			ctl.tag = optarg;
			break;
		case 'u':		/* unix socket */
			ctl.unix_socket = optarg;
			break;
		case 'd':
			ctl.socket_type = TYPE_UDP;
			break;
		case 'T':
			ctl.socket_type = TYPE_TCP;
			break;
		case 'n':
			ctl.server = optarg;
			break;
		case 'P':
			ctl.port = optarg;
			break;
		case 'V':
			printf(UTIL_LINUX_VERSION);
			exit(EXIT_SUCCESS);
		case 'h':
			usage(stdout);
		case OPT_PRIO_PREFIX:
			ctl.prio_prefix = 1;
			break;
		case OPT_RFC3164:
			ctl.syslogfp = syslog_rfc3164;
			break;
		case OPT_RFC5424:
			ctl.syslogfp = syslog_rfc5424;
			if (optarg)
				parse_rfc5424_flags(&ctl, optarg);
			break;
#ifdef HAVE_LIBSYSTEMD
		case OPT_JOURNALD:
			if (optarg) {
				jfd = fopen(optarg, "r");
				if (!jfd)
					err(EXIT_FAILURE, _("cannot open %s"),
					    optarg);
			} else
				jfd = stdin;
			break;
#endif
		case OPT_SOCKET_ERRORS:
			unix_socket_errors_mode = parse_unix_socket_errors_flags(optarg);
			break;
		case '?':
		default:
			usage(stderr);
		}
	}
	argc -= optind;
	argv += optind;
	if (stdout_reopened && argc)
		warnx(_("--file <file> and <message> are mutually exclusive, message is ignored"));
#ifdef HAVE_LIBSYSTEMD
	if (jfd) {
		int ret = journald_entry(jfd);
		if (stdin != jfd)
			fclose(jfd);
		if (ret)
			errx(EXIT_FAILURE, _("journald entry could not be written"));
		return EXIT_SUCCESS;
	}
#endif
	switch (unix_socket_errors_mode) {
	case AF_UNIX_ERRORS_OFF:
		ctl.unix_socket_errors = 0;
		break;
	case AF_UNIX_ERRORS_ON:
		ctl.unix_socket_errors = 1;
		break;
	case AF_UNIX_ERRORS_AUTO:
#ifdef HAVE_LIBSYSTEMD
		ctl.unix_socket_errors = sd_booted();
#else
		ctl.unix_socket_errors = 0;
#endif
		break;
	default:
		abort();
	}
	logger_open(&ctl);
	if (0 < argc)
		logger_command_line(&ctl, argv);
	else
		/* Note. --file <arg> reopens stdin making the below
		 * function to be used for file inputs. */
		logger_stdin(&ctl);
	logger_close(&ctl);
	return EXIT_SUCCESS;
}
