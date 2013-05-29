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
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <getopt.h>

#include "c.h"
#include "closestream.h"
#include "nls.h"
#include "strutils.h"

#define	SYSLOG_NAMES
#include <syslog.h>

enum {
	TYPE_UDP = (1 << 1),
	TYPE_TCP = (1 << 2),
	ALL_TYPES = TYPE_UDP | TYPE_TCP
};

enum {
	OPT_PRIO_PREFIX = CHAR_MAX + 1
};


static char* get_prio_prefix(char *msg, int *prio)
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

static int decode(char *name, CODE *codetab)
{
	register CODE *c;

	if (isdigit(*name))
		return (atoi(name));

	for (c = codetab; c->c_name; c++)
		if (!strcasecmp(name, c->c_name))
			return (c->c_val);

	return -1;
}

static int pencode(char *s)
{
	char *save;
	int fac, lev;

	for (save = s; *s && *s != '.'; ++s);
	if (*s) {
		*s = '\0';
		fac = decode(save, facilitynames);
		if (fac < 0)
			errx(EXIT_FAILURE, _("unknown facility name: %s."), save);
		*s++ = '.';
	}
	else {
		fac = LOG_USER;
		s = save;
	}
	lev = decode(s, prioritynames);
	if (lev < 0)
		errx(EXIT_FAILURE, _("unknown priority name: %s."), save);
	return ((lev & LOG_PRIMASK) | (fac & LOG_FACMASK));
}

static int unix_socket(const char *path, const int socket_type)
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

	if (i == 0)
		err(EXIT_FAILURE, _("socket %s"), path);

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

static void mysyslog(int fd, int logflags, int pri, char *tag, char *msg)
{
       char buf[1000], pid[30], *cp, *tp;
       time_t now;

       if (fd > -1) {
               if (logflags & LOG_PID)
                       snprintf (pid, sizeof(pid), "[%d]", getpid());
	       else
		       pid[0] = 0;
               if (tag)
		       cp = tag;
	       else {
		       cp = getlogin();
		       if (!cp)
			       cp = "<someone>";
	       }
               (void)time(&now);
	       tp = ctime(&now)+4;

               snprintf(buf, sizeof(buf), "<%d>%.15s %.200s%s: %.400s",
			pri, tp, cp, pid, msg);

               if (write(fd, buf, strlen(buf)+1) < 0)
                       return; /* error */
       }
}

static void __attribute__ ((__noreturn__)) usage(FILE *out)
{
	fputs(USAGE_HEADER, out);
	fprintf(out, _(" %s [options] [<message>]\n"), program_invocation_short_name);

	fputs(USAGE_OPTIONS, out);
	fputs(_(" -T, --tcp             use TCP only\n"), out);
	fputs(_(" -d, --udp             use UDP only\n"), out);
	fputs(_(" -i, --id              log the process ID too\n"), out);
	fputs(_(" -f, --file <file>     log the contents of this file\n"), out);
	fputs(_(" -n, --server <name>   write to this remote syslog server\n"), out);
	fputs(_(" -P, --port <number>   use this UDP port\n"), out);
	fputs(_(" -p, --priority <prio> mark given message with this priority\n"), out);
	fputs(_("     --prio-prefix     look for a prefix on every line read from stdin\n"), out);
	fputs(_(" -s, --stderr          output message to standard error as well\n"), out);
	fputs(_(" -t, --tag <tag>       mark every line with this tag\n"), out);
	fputs(_(" -u, --socket <socket> write to this Unix socket\n"), out);

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
	int ch, logflags, pri, prio_prefix;
	char *tag, buf[1024];
	char *usock = NULL;
	char *server = NULL;
	char *port = NULL;
	int LogSock = -1, socket_type = ALL_TYPES;

	static const struct option longopts[] = {
		{ "id",		no_argument,	    0, 'i' },
		{ "stderr",	no_argument,	    0, 's' },
		{ "file",	required_argument,  0, 'f' },
		{ "priority",	required_argument,  0, 'p' },
		{ "tag",	required_argument,  0, 't' },
		{ "socket",	required_argument,  0, 'u' },
		{ "udp",	no_argument,	    0, 'd' },
		{ "tcp",	no_argument,	    0, 'T' },
		{ "server",	required_argument,  0, 'n' },
		{ "port",	required_argument,  0, 'P' },
		{ "version",	no_argument,	    0, 'V' },
		{ "help",	no_argument,	    0, 'h' },
		{ "prio-prefix", no_argument, 0, OPT_PRIO_PREFIX },
		{ NULL,		0, 0, 0 }
	};

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	tag = NULL;
	pri = LOG_NOTICE;
	logflags = 0;
	prio_prefix = 0;
	while ((ch = getopt_long(argc, argv, "f:ip:st:u:dTn:P:Vh",
					    longopts, NULL)) != -1) {
		switch (ch) {
		case 'f':		/* file to log */
			if (freopen(optarg, "r", stdin) == NULL)
				err(EXIT_FAILURE, _("file %s"),
				    optarg);
			break;
		case 'i':		/* log process id also */
			logflags |= LOG_PID;
			break;
		case 'p':		/* priority */
			pri = pencode(optarg);
			break;
		case 's':		/* log to standard error */
			logflags |= LOG_PERROR;
			break;
		case 't':		/* tag */
			tag = optarg;
			break;
		case 'u':		/* unix socket */
			usock = optarg;
			break;
		case 'd':
			socket_type = TYPE_UDP;
			break;
		case 'T':
			socket_type = TYPE_TCP;
			break;
		case 'n':
			server = optarg;
			break;
		case 'P':
			port = optarg;
			break;
		case 'V':
			printf(UTIL_LINUX_VERSION);
			exit(EXIT_SUCCESS);
		case 'h':
			usage(stdout);
		case OPT_PRIO_PREFIX:
			prio_prefix = 1;
			break;
		case '?':
		default:
			usage(stderr);
		}
	}
	argc -= optind;
	argv += optind;

	/* setup for logging */
	if (server)
		LogSock = inet_socket(server, port, socket_type);
	else if (usock)
		LogSock = unix_socket(usock, socket_type);
	else
		openlog(tag ? tag : getlogin(), logflags, 0);

	/* log input line if appropriate */
	if (argc > 0) {
		register char *p, *endp;
		size_t len;

		for (p = buf, endp = buf + sizeof(buf) - 2; *argv;) {
			len = strlen(*argv);
			if (p + len > endp && p > buf) {
			    if (!usock && !server)
				syslog(pri, "%s", buf);
			    else
				mysyslog(LogSock, logflags, pri, tag, buf);
				p = buf;
			}
			if (len > sizeof(buf) - 1) {
			    if (!usock && !server)
				syslog(pri, "%s", *argv++);
			    else
				mysyslog(LogSock, logflags, pri, tag, *argv++);
			} else {
				if (p != buf)
					*p++ = ' ';
				memmove(p, *argv++, len);
				*(p += len) = '\0';
			}
		}
		if (p != buf) {
		    if (!usock && !server)
			syslog(pri, "%s", buf);
		    else
			mysyslog(LogSock, logflags, pri, tag, buf);
		}
	} else {
		char *msg;
		int default_priority = pri;
		while (fgets(buf, sizeof(buf), stdin) != NULL) {
		    /* glibc is buggy and adds an additional newline,
		       so we have to remove it here until glibc is fixed */
		    int len = strlen(buf);

		    if (len > 0 && buf[len - 1] == '\n')
			    buf[len - 1] = '\0';

			msg = buf;
			pri = default_priority;
			if (prio_prefix && msg[0] == '<')
				msg = get_prio_prefix(msg, &pri);

		    if (!usock && !server)
			syslog(pri, "%s", msg);
		    else
			mysyslog(LogSock, logflags, pri, tag, msg);
		}
	}
	if (!usock && !server)
		closelog();
	else
		close(LogSock);

	return EXIT_SUCCESS;
}
