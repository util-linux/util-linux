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
#include <stdlib.h>
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
#include <getopt.h>
#include <sys/signalfd.h>
#include <poll.h>

#include "uuid.h"
#include "uuidd.h"
#include "all-io.h"
#include "c.h"
#include "closestream.h"
#include "strutils.h"
#include "optutils.h"
#include "monotonic.h"
#include "timer.h"

#ifdef HAVE_LIBSYSTEMD
# include <systemd/sd-daemon.h>
#endif

#include "nls.h"

/* length of binary representation of UUID */
#define UUID_LEN	(sizeof(uuid_t))

/* server loop control structure */
struct uuidd_cxt_t {
	const char	*cleanup_pidfile;
	const char	*cleanup_socket;
	uint32_t	timeout;
	unsigned int	debug: 1,
			quiet: 1,
			no_fork: 1,
			no_sock: 1;
};

static void __attribute__((__noreturn__)) usage(void)
{
	FILE *out = stdout;
	fputs(USAGE_HEADER, out);
	fprintf(out, _(" %s [options]\n"), program_invocation_short_name);
	fputs(USAGE_SEPARATOR, out);
	fputs(_("A daemon for generating UUIDs.\n"), out);
	fputs(USAGE_OPTIONS, out);
	fputs(_(" -p, --pid <path>        path to pid file\n"), out);
	fputs(_(" -s, --socket <path>     path to socket\n"), out);
	fputs(_(" -T, --timeout <sec>     specify inactivity timeout\n"), out);
	fputs(_(" -k, --kill              kill running daemon\n"), out);
	fputs(_(" -r, --random            test random-based generation\n"), out);
	fputs(_(" -t, --time              test time-based generation\n"), out);
	fputs(_(" -n, --uuids <num>       request number of uuids\n"), out);
	fputs(_(" -P, --no-pid            do not create pid file\n"), out);
	fputs(_(" -F, --no-fork           do not daemonize using double-fork\n"), out);
	fputs(_(" -S, --socket-activation do not create listening socket\n"), out);
	fputs(_(" -d, --debug             run in debugging mode\n"), out);
	fputs(_(" -q, --quiet             turn on quiet mode\n"), out);
	fputs(USAGE_SEPARATOR, out);
	printf(USAGE_HELP_OPTIONS(25));
	printf(USAGE_MAN_TAIL("uuidd(8)"));
	exit(EXIT_SUCCESS);
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
	assert(strlen(socket_path) < sizeof(srv_addr.sun_path));
	xstrncpy(srv_addr.sun_path, socket_path, sizeof(srv_addr.sun_path));

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
 * Return file descriptor of the created pid_file.
 */
static int create_pidfile(struct uuidd_cxt_t *cxt, const char *pidfile_path)
{
	int		fd_pidfile;
	struct flock	fl;

	fd_pidfile = open(pidfile_path, O_CREAT | O_RDWR, 0664);
	if (fd_pidfile < 0) {
		if (!cxt->quiet)
			warn(_("cannot open %s"), pidfile_path);
		exit(EXIT_FAILURE);
	}
	cxt->cleanup_pidfile = pidfile_path;

	fl.l_type = F_WRLCK;
	fl.l_whence = SEEK_SET;
	fl.l_start = 0;
	fl.l_len = 0;
	fl.l_pid = 0;
	while (fcntl(fd_pidfile, F_SETLKW, &fl) < 0) {
		if ((errno == EAGAIN) || (errno == EINTR))
			continue;
		if (!cxt->quiet)
			warn(_("cannot lock %s"), pidfile_path);
		exit(EXIT_FAILURE);
	}

	return fd_pidfile;
}

/*
 * Create AF_UNIX, SOCK_STREAM socket and bind to @socket_path
 *
 * If @will_fork is true, then make sure the descriptor
 * of the socket is >2, so that it won't be later closed
 * during create_daemon().
 *
 * Return file descriptor corresponding to created socket.
 */
static int create_socket(struct uuidd_cxt_t *uuidd_cxt,
			 const char *socket_path, int will_fork)
{
	struct sockaddr_un	my_addr;
	mode_t			save_umask;
	int			s;

	if ((s = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
		if (!uuidd_cxt->quiet)
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
	assert(strlen(socket_path) < sizeof(my_addr.sun_path));
	xstrncpy(my_addr.sun_path, socket_path, sizeof(my_addr.sun_path));
	unlink(socket_path);
	save_umask = umask(0);
	if (bind(s, (const struct sockaddr *) &my_addr,
		 sizeof(struct sockaddr_un)) < 0) {
		if (!uuidd_cxt->quiet)
			warn(_("couldn't bind unix socket %s"), socket_path);
		exit(EXIT_FAILURE);
	}
	umask(save_umask);
	uuidd_cxt->cleanup_socket = socket_path;

	return s;
}

static void __attribute__((__noreturn__)) all_done(const struct uuidd_cxt_t *uuidd_cxt, int ret)
{
	if (uuidd_cxt->cleanup_pidfile)
		unlink(uuidd_cxt->cleanup_pidfile);
	if (uuidd_cxt->cleanup_socket)
		unlink(uuidd_cxt->cleanup_socket);
	exit(ret);
}

static void handle_signal(const struct uuidd_cxt_t *uuidd_cxt, int fd)
{
	struct signalfd_siginfo info;
	ssize_t bytes;

	bytes = read(fd, &info, sizeof(info));
	if (bytes != sizeof(info)) {
		if (errno == EAGAIN)
			return;
		warn(_("receiving signal failed"));
		info.ssi_signo = 0;
	}
	if (info.ssi_signo == SIGPIPE)
		return;		/* ignored */
	all_done(uuidd_cxt, EXIT_SUCCESS);
}

static void timeout_handler(int sig __attribute__((__unused__)),
			    siginfo_t * info,
			    void *context __attribute__((__unused__)))
{
#ifdef HAVE_TIMER_CREATE
	if (info->si_code == SI_TIMER)
#endif
		errx(EXIT_FAILURE, _("timed out"));
}

static void server_loop(const char *socket_path, const char *pidfile_path,
			struct uuidd_cxt_t *uuidd_cxt)
{
	struct sockaddr_un	from_addr;
	socklen_t		fromlen;
	int32_t			reply_len = 0;
	uuid_t			uu;
	char			reply_buf[1024], *cp;
	char			op, str[UUID_STR_LEN];
	int			i, ns, len;
	int			num;		/* intentionally uninitialized */
	int			s = 0;
	int			fd_pidfile = -1;
	int			ret;
	struct pollfd		pfd[2];
	sigset_t		sigmask;
	int			sigfd;
	enum {
				POLLFD_SIGNAL = 0,
				POLLFD_SOCKET
	};

#ifdef HAVE_LIBSYSTEMD
	if (!uuidd_cxt->no_sock)	/* no_sock implies no_fork and no_pid */
#endif
	{
		struct ul_timer timer;
		struct itimerval timeout;

		memset(&timeout, 0, sizeof timeout);
		timeout.it_value.tv_sec = 30;
		if (setup_timer(&timer, &timeout, &timeout_handler))
			err(EXIT_FAILURE, _("cannot set up timer"));
		if (pidfile_path)
			fd_pidfile = create_pidfile(uuidd_cxt, pidfile_path);
		ret = call_daemon(socket_path, UUIDD_OP_GETPID, reply_buf,
				  sizeof(reply_buf), 0, NULL);
		cancel_timer(&timer);
		if (ret > 0) {
			if (!uuidd_cxt->quiet)
				warnx(_("uuidd daemon is already running at pid %s"),
					reply_buf);
			exit(EXIT_FAILURE);
		}

		s = create_socket(uuidd_cxt, socket_path,
				  (!uuidd_cxt->debug || !uuidd_cxt->no_fork));
		if (listen(s, SOMAXCONN) < 0) {
			if (!uuidd_cxt->quiet)
				warn(_("couldn't listen on unix socket %s"), socket_path);
			exit(EXIT_FAILURE);
		}

		if (!uuidd_cxt->debug && !uuidd_cxt->no_fork)
			create_daemon();

		if (pidfile_path) {
			sprintf(reply_buf, "%8d\n", getpid());
			if (ftruncate(fd_pidfile, 0))
				err(EXIT_FAILURE, _("could not truncate file: %s"), pidfile_path);
			write_all(fd_pidfile, reply_buf, strlen(reply_buf));
			if (fd_pidfile > 1 && close_fd(fd_pidfile) != 0)
				err(EXIT_FAILURE, _("write failed: %s"), pidfile_path);
		}

	}

#ifdef HAVE_LIBSYSTEMD
	if (uuidd_cxt->no_sock) {
		const int r = sd_listen_fds(0);

		if (r < 0) {
			errno = r * -1;
			err(EXIT_FAILURE, _("sd_listen_fds() failed"));
		} else if (r == 0)
			errx(EXIT_FAILURE,
			     _("no file descriptors received, check systemctl status uuidd.socket"));
		else if (1 < r)
			errx(EXIT_FAILURE,
			     _("too many file descriptors received, check uuidd.socket"));
		s = SD_LISTEN_FDS_START + 0;
	}
#endif

	sigemptyset(&sigmask);
	sigaddset(&sigmask, SIGHUP);
	sigaddset(&sigmask, SIGINT);
	sigaddset(&sigmask, SIGTERM);
	sigaddset(&sigmask, SIGALRM);
	sigaddset(&sigmask, SIGPIPE);
	/* Block signals so that they aren't handled according to their
	 * default dispositions */
	sigprocmask(SIG_BLOCK, &sigmask, NULL);
	if ((sigfd = signalfd(-1, &sigmask, 0)) < 0)
		err(EXIT_FAILURE, _("cannot set signal handler"));

	pfd[POLLFD_SIGNAL].fd = sigfd;
	pfd[POLLFD_SOCKET].fd = s;
	pfd[POLLFD_SIGNAL].events = pfd[POLLFD_SOCKET].events = POLLIN | POLLERR | POLLHUP;

	while (1) {
		ret = poll(pfd, ARRAY_SIZE(pfd),
				uuidd_cxt->timeout ?
					(int) uuidd_cxt->timeout * 1000 : -1);
		if (ret < 0) {
			if (errno == EAGAIN)
				continue;
			warn(_("poll failed"));
				all_done(uuidd_cxt, EXIT_FAILURE);
		}
		if (ret == 0) {		/* true when poll() times out */
			if (uuidd_cxt->debug)
				fprintf(stderr, _("timeout [%d sec]\n"), uuidd_cxt->timeout),
			all_done(uuidd_cxt, EXIT_SUCCESS);
		}
		if (pfd[POLLFD_SIGNAL].revents != 0)
			handle_signal(uuidd_cxt, sigfd);
		if (pfd[POLLFD_SOCKET].revents == 0)
			continue;
		fromlen = sizeof(from_addr);
		ns = accept(s, (struct sockaddr *) &from_addr, &fromlen);
		if (ns < 0) {
			if ((errno == EAGAIN) || (errno == EINTR))
				continue;
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
	static const ul_excl_t excl[] = {
		{ 'P', 'p' },
		{ 'd', 'q' },
		{ 'r', 't' },
		{ 0 }
	};
	int excl_st[ARRAY_SIZE(excl)] = UL_EXCL_STATUS_INIT;

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	close_stdout_atexit();

	while ((c =
		getopt_long(argc, argv, "p:s:T:krtn:PFSdqVh", longopts,
			    NULL)) != -1) {
		err_exclusive_options(c, longopts, excl, excl_st);
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
			pidfile_path = optarg;
			break;
		case 'P':
			no_pid = 1;
			break;
		case 'F':
			uuidd_cxt.no_fork = 1;
			break;
		case 'S':
#ifdef HAVE_LIBSYSTEMD
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
			print_version(EXIT_SUCCESS);
		case 'h':
			usage();
		default:
			errtryhelp(EXIT_FAILURE);
		}
	}

	if (strlen(socket_path) >= sizeof(((struct sockaddr_un *)0)->sun_path))
		errx(EXIT_FAILURE, _("socket name too long: %s"), socket_path);

	if (!no_pid && !pidfile_path)
		pidfile_path = UUIDD_PIDFILE_PATH;

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
