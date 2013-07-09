/*
 * uuidd.c --- UUID-generation daemon
 *
 * Copyright (C) 2007  Theodore Ts'o
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU Public
 * License.
 * %End-Header%
 */
#include <stdio.h>
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#include <unistd.h>
#include <inttypes.h>
#include <errno.h>
#include <err.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#ifdef HAVE_GETOPT_H
#include <getopt.h>
#else
extern int getopt(int argc, char * const argv[], const char *optstring);
extern char *optarg;
extern int optind;
#endif

#include "uuid.h"
#include "uuidd.h"
#include "all-io.h"
#include "c.h"
#include "closestream.h"
#include "strutils.h"

#ifdef USE_SOCKET_ACTIVATION
#include "sd-daemon.h"
#endif

#include "nls.h"

#ifdef __GNUC__
#define CODE_ATTR(x) __attribute__(x)
#else
#define CODE_ATTR(x)
#endif

/* length of textual representation of UUID, including trailing \0 */
#define UUID_STR_LEN	37

/* length of binary representation of UUID */
#define UUID_LEN	(sizeof(uuid_t))

/* server loop control structure */
struct uuidd_cxt_t {
	int	timeout;
	unsigned int	debug: 1,
			quiet: 1,
			no_fork: 1,
			no_sock: 1;
};

static void __attribute__ ((__noreturn__)) usage(FILE * out)
{
	fputs(_("\nUsage:\n"), out);
	fprintf(out,
	      _(" %s [options]\n"), program_invocation_short_name);

	fputs(_("\nOptions:\n"), out);
	fputs(_(" -p, --pid <path>        path to pid file\n"
		" -s, --socket <path>     path to socket\n"
		" -T, --timeout <sec>     specify inactivity timeout\n"
		" -k, --kill              kill running daemon\n"
		" -r, --random            test random-based generation\n"
		" -t, --time              test time-based generation\n"
		" -n, --uuids <num>       request number of uuids\n"
		" -P, --no-pid            do not create pid file\n"
		" -F, --no-fork           do not daemonize using double-fork\n"
		" -S, --socket-activation do not create listening socket\n"
		" -d, --debug             run in debugging mode\n"
		" -q, --quiet             turn on quiet mode\n"
		" -V, --version           output version information and exit\n"
		" -h, --help              display this help and exit\n\n"), out);

	exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}

static void create_daemon(void)
{
	uid_t euid;

	if (daemon(0, 0))
		err(EXIT_FAILURE, "daemon");

	euid = geteuid();
	if (setreuid(euid, euid) < 0)
		err(EXIT_FAILURE, "setreuid");
}

static const char *cleanup_pidfile, *cleanup_socket;

static void terminate_intr(int signo CODE_ATTR((unused)))
{
	if (cleanup_pidfile)
		unlink(cleanup_pidfile);
	if (cleanup_socket)
		unlink(cleanup_socket);
	exit(EXIT_SUCCESS);
}

static int call_daemon(const char *socket_path, int op, char *buf,
		       size_t buflen, int *num, const char **err_context)
{
	char op_buf[8];
	int op_len;
	int s;
	ssize_t ret;
	int32_t reply_len = 0;
	struct sockaddr_un srv_addr;

	if (((op == UUIDD_OP_BULK_TIME_UUID) ||
	     (op == UUIDD_OP_BULK_RANDOM_UUID)) && !num) {
		if (err_context)
			*err_context = _("bad arguments");
		errno = EINVAL;
		return -1;
	}

	if ((s = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
		if (err_context)
			*err_context = _("socket");
		return -1;
	}

	srv_addr.sun_family = AF_UNIX;
	strncpy(srv_addr.sun_path, socket_path, sizeof(srv_addr.sun_path));
	srv_addr.sun_path[sizeof(srv_addr.sun_path) - 1] = '\0';

	if (connect(s, (const struct sockaddr *) &srv_addr,
		    sizeof(struct sockaddr_un)) < 0) {
		if (err_context)
			*err_context = _("connect");
		close(s);
		return -1;
	}

	if (op == UUIDD_OP_BULK_RANDOM_UUID) {
		if ((*num) * UUID_LEN > buflen - 4)
			*num = (buflen - 4) / UUID_LEN;
	}
	op_buf[0] = op;
	op_len = 1;
	if ((op == UUIDD_OP_BULK_TIME_UUID) ||
	    (op == UUIDD_OP_BULK_RANDOM_UUID)) {
		memcpy(op_buf + 1, num, sizeof(int));
		op_len += sizeof(int);
	}

	ret = write_all(s, op_buf, op_len);
	if (ret < 0) {
		if (err_context)
			*err_context = _("write");
		close(s);
		return -1;
	}

	ret = read_all(s, (char *) &reply_len, sizeof(reply_len));
	if (ret < 0) {
		if (err_context)
			*err_context = _("read count");
		close(s);
		return -1;
	}
	if (reply_len < 0 || (size_t) reply_len > buflen) {
		if (err_context)
			*err_context = _("bad response length");
		close(s);
		return -1;
	}
	ret = read_all(s, (char *) buf, reply_len);

	if ((ret > 0) && (op == UUIDD_OP_BULK_TIME_UUID)) {
		if (reply_len >= (int) (UUID_LEN + sizeof(int)))
			memcpy(buf + UUID_LEN, num, sizeof(int));
		else
			*num = -1;
	}
	if ((ret > 0) && (op == UUIDD_OP_BULK_RANDOM_UUID)) {
		if (reply_len >= (int) sizeof(int))
			memcpy(buf, num, sizeof(int));
		else
			*num = -1;
	}

	close(s);

	return ret;
}

/*
 * Exclusively create and open a pid file with path @pidfile_path
 *
 * Set cleanup_pidfile global variable for the cleanup
 * handler. @pidfile_path must not be NULL.
 *
 * Return file descriptor of the created pid_file.
 */
static int create_pidfile(const char *pidfile_path, int quiet)
{
	int		fd_pidfile;
	struct flock	fl;

	fd_pidfile = open(pidfile_path, O_CREAT | O_RDWR, 0664);
	if (fd_pidfile < 0) {
		if (!quiet)
			warn(_("cannot open %s"), pidfile_path);
		exit(EXIT_FAILURE);
	}
	cleanup_pidfile = pidfile_path;

	fl.l_type = F_WRLCK;
	fl.l_whence = SEEK_SET;
	fl.l_start = 0;
	fl.l_len = 0;
	fl.l_pid = 0;
	while (fcntl(fd_pidfile, F_SETLKW, &fl) < 0) {
		if ((errno == EAGAIN) || (errno == EINTR))
			continue;
		if (!quiet)
			warn(_("cannot lock %s"), pidfile_path);
		exit(EXIT_FAILURE);
	}

	return fd_pidfile;
}

/*
 * Create AF_UNIX, SOCK_STREAM socket and bind to @socket_path
 *
 * If @will_fork is true, then make sure the descriptor
 * of the socket is >2, so that it wont be later closed
 * during create_daemon().
 *
 * Return file descriptor corresponding to created socket.
 */
static int create_socket(const char *socket_path, int will_fork, int quiet)
{
	struct sockaddr_un	my_addr;
	mode_t			save_umask;
	int 			s;

	if ((s = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
		if (!quiet)
			warn(_("couldn't create unix stream socket"));
		exit(EXIT_FAILURE);
	}

	/*
	 * Make sure the socket isn't using fd numbers 0-2 to avoid it
	 * getting closed by create_daemon()
	 */
	while (will_fork && s <= 2) {
		s = dup(s);
		if (s < 0)
			err(EXIT_FAILURE, "dup");
	}

	/*
	 * Create the address we will be binding to.
	 */
	my_addr.sun_family = AF_UNIX;
	strncpy(my_addr.sun_path, socket_path, sizeof(my_addr.sun_path));
	my_addr.sun_path[sizeof(my_addr.sun_path) - 1] = '\0';
	unlink(socket_path);
	save_umask = umask(0);
	if (bind(s, (const struct sockaddr *) &my_addr,
		 sizeof(struct sockaddr_un)) < 0) {
		if (!quiet)
			warn(_("couldn't bind unix socket %s"), socket_path);
		exit(EXIT_FAILURE);
	}
	umask(save_umask);
	cleanup_socket = socket_path;

	return s;
}

static void server_loop(const char *socket_path, const char *pidfile_path,
			const struct uuidd_cxt_t *uuidd_cxt)
{
	struct sockaddr_un	from_addr;
	socklen_t		fromlen;
	int32_t			reply_len = 0;
	uuid_t			uu;
	char			reply_buf[1024], *cp;
	char			op, str[UUID_STR_LEN];
	int			i, ns, len, num;
	int			s = 0;
	int			fd_pidfile = -1;
	int			ret;

#ifdef USE_SOCKET_ACTIVATION
	if (!uuidd_cxt->no_sock)	/* no_sock implies no_fork and no_pid */
#endif
	{

		signal(SIGALRM, terminate_intr);
		alarm(30);
		if (pidfile_path)
			fd_pidfile = create_pidfile(pidfile_path, uuidd_cxt->quiet);

		ret = call_daemon(socket_path, UUIDD_OP_GETPID, reply_buf,
				  sizeof(reply_buf), 0, NULL);
		if (ret > 0) {
			if (!uuidd_cxt->quiet)
				warnx(_("uuidd daemon is already running at pid %s"),
				       reply_buf);
			exit(EXIT_FAILURE);
		}
		alarm(0);

		s = create_socket(socket_path,
				  (!uuidd_cxt->debug || !uuidd_cxt->no_fork),
				  uuidd_cxt->quiet);
		if (listen(s, SOMAXCONN) < 0) {
			if (!uuidd_cxt->quiet)
				warn(_("couldn't listen on unix socket %s"), socket_path);
			exit(EXIT_FAILURE);
		}

		if (!uuidd_cxt->debug && !uuidd_cxt->no_fork)
			create_daemon();

		if (pidfile_path) {
			sprintf(reply_buf, "%8d\n", getpid());
			ignore_result( ftruncate(fd_pidfile, 0) );
			write_all(fd_pidfile, reply_buf, strlen(reply_buf));
			if (fd_pidfile > 1)
				close(fd_pidfile); /* Unlock the pid file */
		}

	}

	signal(SIGHUP, terminate_intr);
	signal(SIGINT, terminate_intr);
	signal(SIGTERM, terminate_intr);
	signal(SIGALRM, terminate_intr);
	signal(SIGPIPE, SIG_IGN);

#ifdef USE_SOCKET_ACTIVATION
	if (uuidd_cxt->no_sock) {
		if (sd_listen_fds(0) != 1)
			errx(EXIT_FAILURE, _("no or too many file descriptors received"));

		s = SD_LISTEN_FDS_START + 0;
	}
#endif

	while (1) {
		fromlen = sizeof(from_addr);
		if (uuidd_cxt->timeout > 0)
			alarm(uuidd_cxt->timeout);
		ns = accept(s, (struct sockaddr *) &from_addr, &fromlen);
		alarm(0);
		if (ns < 0) {
			if ((errno == EAGAIN) || (errno == EINTR))
				continue;
			else
				err(EXIT_FAILURE, "accept");
		}
		len = read(ns, &op, 1);
		if (len != 1) {
			if (len < 0)
				warn(_("read failed"));
			else
				warnx(_("error reading from client, len = %d"),
						len);
			goto shutdown_socket;
		}
		if ((op == UUIDD_OP_BULK_TIME_UUID) ||
		    (op == UUIDD_OP_BULK_RANDOM_UUID)) {
			if (read_all(ns, (char *) &num, sizeof(num)) != 4)
				goto shutdown_socket;
			if (uuidd_cxt->debug)
				fprintf(stderr, _("operation %d, incoming num = %d\n"),
				       op, num);
		} else if (uuidd_cxt->debug)
			fprintf(stderr, _("operation %d\n"), op);

		switch (op) {
		case UUIDD_OP_GETPID:
			sprintf(reply_buf, "%d", getpid());
			reply_len = strlen(reply_buf) + 1;
			break;
		case UUIDD_OP_GET_MAXOP:
			sprintf(reply_buf, "%d", UUIDD_MAX_OP);
			reply_len = strlen(reply_buf) + 1;
			break;
		case UUIDD_OP_TIME_UUID:
			num = 1;
			__uuid_generate_time(uu, &num);
			if (uuidd_cxt->debug) {
				uuid_unparse(uu, str);
				fprintf(stderr, _("Generated time UUID: %s\n"), str);
			}
			memcpy(reply_buf, uu, sizeof(uu));
			reply_len = sizeof(uu);
			break;
		case UUIDD_OP_RANDOM_UUID:
			num = 1;
			__uuid_generate_random(uu, &num);
			if (uuidd_cxt->debug) {
				uuid_unparse(uu, str);
				fprintf(stderr, _("Generated random UUID: %s\n"), str);
			}
			memcpy(reply_buf, uu, sizeof(uu));
			reply_len = sizeof(uu);
			break;
		case UUIDD_OP_BULK_TIME_UUID:
			__uuid_generate_time(uu, &num);
			if (uuidd_cxt->debug) {
				uuid_unparse(uu, str);
				fprintf(stderr, P_("Generated time UUID %s "
						   "and %d following\n",
						   "Generated time UUID %s "
						   "and %d following\n", num - 1),
				       str, num - 1);
			}
			memcpy(reply_buf, uu, sizeof(uu));
			reply_len = sizeof(uu);
			memcpy(reply_buf + reply_len, &num, sizeof(num));
			reply_len += sizeof(num);
			break;
		case UUIDD_OP_BULK_RANDOM_UUID:
			if (num < 0)
				num = 1;
			if (num > 1000)
				num = 1000;
			if (num * UUID_LEN > (int) (sizeof(reply_buf) - sizeof(num)))
				num = (sizeof(reply_buf) - sizeof(num)) / UUID_LEN;
			__uuid_generate_random((unsigned char *) reply_buf +
					      sizeof(num), &num);
			if (uuidd_cxt->debug) {
				fprintf(stderr, P_("Generated %d UUID:\n",
						   "Generated %d UUIDs:\n", num), num);
				for (i = 0, cp = reply_buf + sizeof(num);
				     i < num;
				     i++, cp += UUID_LEN) {
					uuid_unparse((unsigned char *)cp, str);
					fprintf(stderr, "\t%s\n", str);
				}
			}
			reply_len = (num * UUID_LEN) + sizeof(num);
			memcpy(reply_buf, &num, sizeof(num));
			break;
		default:
			if (uuidd_cxt->debug)
				fprintf(stderr, _("Invalid operation %d\n"), op);
			goto shutdown_socket;
		}
		write_all(ns, (char *) &reply_len, sizeof(reply_len));
		write_all(ns, reply_buf, reply_len);
	shutdown_socket:
		close(ns);
	}
}

static void __attribute__ ((__noreturn__)) unexpected_size(int size)
{
	errx(EXIT_FAILURE, _("Unexpected reply length from server %d"), size);
}

int main(int argc, char **argv)
{
	const char	*socket_path = UUIDD_SOCKET_PATH;
	const char	*pidfile_path = NULL;
	const char	*pidfile_path_param = NULL;
	const char	*err_context = NULL;
	char		buf[1024], *cp;
	char		str[UUID_STR_LEN];
	uuid_t		uu;
	int		i, c, ret;
	int		do_type = 0, do_kill = 0, num = 0;
	int		no_pid = 0;
	int		s_flag = 0;

	struct uuidd_cxt_t uuidd_cxt = { .timeout = 0 };

	static const struct option longopts[] = {
		{"pid", required_argument, NULL, 'p'},
		{"socket", required_argument, NULL, 's'},
		{"timeout", required_argument, NULL, 'T'},
		{"kill", no_argument, NULL, 'k'},
		{"random", no_argument, NULL, 'r'},
		{"time", no_argument, NULL, 't'},
		{"uuids", required_argument, NULL, 'n'},
		{"no-pid", no_argument, NULL, 'P'},
		{"no-fork", no_argument, NULL, 'F'},
		{"socket-activation", no_argument, NULL, 'S'},
		{"debug", no_argument, NULL, 'd'},
		{"quiet", no_argument, NULL, 'q'},
		{"version", no_argument, NULL, 'V'},
		{"help", no_argument, NULL, 'h'},
		{NULL, 0, NULL, 0}
	};

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	while ((c =
		getopt_long(argc, argv, "p:s:T:krtn:PFSdqVh", longopts,
			    NULL)) != -1) {
		switch (c) {
		case 'd':
			uuidd_cxt.debug = 1;
			break;
		case 'k':
			do_kill++;
			break;
		case 'n':
			num = strtou32_or_err(optarg,
						_("failed to parse --uuids"));
			break;
		case 'p':
			pidfile_path_param = optarg;
			break;
		case 'P':
			no_pid = 1;
			break;
		case 'F':
			uuidd_cxt.no_fork = 1;
			break;
		case 'S':
#ifdef USE_SOCKET_ACTIVATION
			uuidd_cxt.no_sock = 1;
			uuidd_cxt.no_fork = 1;
			no_pid = 1;
#else
			errx(EXIT_FAILURE, _("uuidd has been built without "
					     "support for socket activation"));
#endif
			break;
		case 'q':
			uuidd_cxt.quiet = 1;
			break;
		case 'r':
			do_type = UUIDD_OP_RANDOM_UUID;
			break;
		case 's':
			socket_path = optarg;
			s_flag = 1;
			break;
		case 't':
			do_type = UUIDD_OP_TIME_UUID;
			break;
		case 'T':
			uuidd_cxt.timeout = strtou32_or_err(optarg,
						_("failed to parse --timeout"));
			break;
		case 'V':
			printf(UTIL_LINUX_VERSION);
			return EXIT_SUCCESS;
		case 'h':
			usage(stdout);
		default:
			usage(stderr);
		}
	}

	if (no_pid && pidfile_path_param && !uuidd_cxt.quiet)
		warnx(_("Both --pid and --no-pid specified. Ignoring --no-pid."));

	if (!no_pid && !pidfile_path_param)
		pidfile_path = UUIDD_PIDFILE_PATH;
	else if (pidfile_path_param)
		pidfile_path = pidfile_path_param;

	/* custom socket path and socket-activation make no sense */
	if (s_flag && uuidd_cxt.no_sock && !uuidd_cxt.quiet)
		warnx(_("Both --socket-activation and --socket specified. "
			"Ignoring --socket."));

	if (num && do_type) {
		ret = call_daemon(socket_path, do_type + 2, buf,
				  sizeof(buf), &num, &err_context);
		if (ret < 0)
			err(EXIT_FAILURE, _("error calling uuidd daemon (%s)"),
					err_context ? : _("unexpected error"));

		if (do_type == UUIDD_OP_TIME_UUID) {
			if (ret != sizeof(uu) + sizeof(num))
				unexpected_size(ret);

			uuid_unparse((unsigned char *) buf, str);

			printf(P_("%s and %d subsequent UUID\n",
				  "%s and %d subsequent UUIDs\n", num - 1),
			       str, num - 1);
		} else {
			printf(_("List of UUIDs:\n"));
			cp = buf + 4;
			if (ret != (int) (sizeof(num) + num * sizeof(uu)))
				unexpected_size(ret);
			for (i = 0; i < num; i++, cp += UUID_LEN) {
				uuid_unparse((unsigned char *) cp, str);
				printf("\t%s\n", str);
			}
		}
		return EXIT_SUCCESS;
	}
	if (do_type) {
		ret = call_daemon(socket_path, do_type, (char *) &uu,
				  sizeof(uu), 0, &err_context);
		if (ret < 0)
			err(EXIT_FAILURE, _("error calling uuidd daemon (%s)"),
					err_context ? : _("unexpected error"));
		if (ret != sizeof(uu))
		        unexpected_size(ret);

		uuid_unparse(uu, str);

		printf("%s\n", str);
		return EXIT_SUCCESS;
	}

	if (do_kill) {
		ret = call_daemon(socket_path, UUIDD_OP_GETPID, buf, sizeof(buf), 0, NULL);
		if ((ret > 0) && ((do_kill = atoi((char *) buf)) > 0)) {
			ret = kill(do_kill, SIGTERM);
			if (ret < 0) {
				if (!uuidd_cxt.quiet)
					warn(_("couldn't kill uuidd running "
						  "at pid %d"), do_kill);
				return EXIT_FAILURE;
			}
			if (!uuidd_cxt.quiet)
				printf(_("Killed uuidd running at pid %d.\n"),
				       do_kill);
		}
		return EXIT_SUCCESS;
	}

	server_loop(socket_path, pidfile_path, &uuidd_cxt);
	return EXIT_SUCCESS;
}
