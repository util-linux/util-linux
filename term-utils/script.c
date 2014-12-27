/*
 * Copyright (c) 1980 Regents of the University of California.
 * All rights reserved.
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
 *      This product includes software developed by the University of
 *      California, Berkeley and its contributors.
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
 */

/*
 * 1999-02-22 Arkadiusz Miśkiewicz <misiek@pld.ORG.PL>
 * - added Native Language Support
 *
 * 2000-07-30 Per Andreas Buer <per@linpro.no> - added "q"-option
 *
 * 2014-05-30 Csaba Kos <csaba.kos@gmail.com>
 * - fixed a rare deadlock after child termination
 */

/*
 * script
 */
#include <stdio.h>
#include <stdlib.h>
#include <paths.h>
#include <time.h>
#include <sys/stat.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <getopt.h>
#include <unistd.h>
#include <fcntl.h>
#include <limits.h>
#include <locale.h>
#include <stddef.h>
#include <sys/wait.h>
#include <poll.h>
#include <sys/signalfd.h>
#include <assert.h>

#include "closestream.h"
#include "nls.h"
#include "c.h"
#include "ttyutils.h"
#include "all-io.h"

#if defined(HAVE_LIBUTIL) && defined(HAVE_PTY_H)
# include <pty.h>
#endif

#ifdef HAVE_LIBUTEMPTER
# include <utempter.h>
#endif

#define DEFAULT_OUTPUT "typescript"

enum { POLLFDS = 3 };

struct script_control {
	char *shell;		/* shell to be executed */
	char *cflg;		/* command to be executed */
	char *fname;		/* output file path */
	FILE *typescriptfp;	/* output file pointer */
	FILE *timingfp;		/* timing file pointer */
	int master;		/* pseudoterminal master file descriptor */
	int slave;		/* pseudoterminal slave file descriptor */
	int poll_timeout;	/* poll() timeout, used in end of execution */
	pid_t child;		/* child pid */
	int childstatus;	/* child process exit value */
	struct termios tt;	/* slave terminal runtime attributes */
	struct winsize win;	/* terminal window size */
#if !HAVE_LIBUTIL || !HAVE_PTY_H
	char line *;		/* terminal line */
#endif
	unsigned int
	 aflg:1,		/* append output */
	 eflg:1,		/* return child exit value */
	 fflg:1,		/* flush after each write */
	 qflg:1,		/* suppress most output */
	 tflg:1,		/* include timing file */
	 forceflg:1,		/* write output to links */
	 isterm:1,		/* is child process running as terminal */
	 die:1;			/* terminate program */
	sigset_t sigset;	/* catch SIGCHLD and SIGWINCH with signalfd() */
	int sigfd;		/* file descriptor for signalfd() */
};

/*
 * For tests we want to be able to control time output
 */
#ifdef TEST_SCRIPT
static inline time_t script_time(time_t *t)
{
	const char *str = getenv("SCRIPT_TEST_SECOND_SINCE_EPOCH");
	time_t sec;

	if (str && sscanf(str, "%ld", &sec) == 1)
		return sec;
	return time(t);
}
#else	/* !TEST_SCRIPT */
# define script_time(x) time(x)
#endif

static void __attribute__((__noreturn__)) usage(FILE *out)
{
	fputs(USAGE_HEADER, out);
	fprintf(out, _(" %s [options] [file]\n"), program_invocation_short_name);

	fputs(USAGE_SEPARATOR, out);
	fputs(_("Make a typescript of a terminal session.\n"), out);

	fputs(USAGE_OPTIONS, out);
	fputs(_(" -a, --append            append the output\n"
		" -c, --command <command> run command rather than interactive shell\n"
		" -e, --return            return exit code of the child process\n"
		" -f, --flush             run flush after each write\n"
		"     --force             use output file even when it is a link\n"
		" -q, --quiet             be quiet\n"
		" -t, --timing[=<file>]   output timing data to stderr (or to FILE)\n"
		" -V, --version           output version information and exit\n"
		" -h, --help              display this help and exit\n\n"), out);

	fprintf(out, USAGE_MAN_TAIL("script(1)"));
	exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}

static void die_if_link(const struct script_control *ctl)
{
	struct stat s;

	if (ctl->forceflg)
		return;
	if (lstat(ctl->fname, &s) == 0 && (S_ISLNK(s.st_mode) || s.st_nlink > 1))
		errx(EXIT_FAILURE,
		     _("output file `%s' is a link\n"
		       "Use --force if you really want to use it.\n"
		       "Program not started."), ctl->fname);
}

static void __attribute__((__noreturn__)) done(struct script_control *ctl)
{
	if (ctl->isterm)
		tcsetattr(STDIN_FILENO, TCSADRAIN, &ctl->tt);
	if (!ctl->qflg)
		printf(_("Script done, file is %s\n"), ctl->fname);
#ifdef HAVE_LIBUTEMPTER
	if (ctl->master >= 0)
		utempter_remove_record(ctl->master);
#endif
	kill(ctl->child, SIGTERM);	/* make sure we don't create orphans */

	if (ctl->eflg) {
		if (WIFSIGNALED(ctl->childstatus))
			exit(WTERMSIG(ctl->childstatus) + 0x80);
		else
			exit(WEXITSTATUS(ctl->childstatus));
	}
	exit(EXIT_SUCCESS);
}

static void fail(struct script_control *ctl)
{
	kill(0, SIGTERM);
	done(ctl);
}

static void finish(struct script_control *ctl, int wait)
{
	int status;
	pid_t pid;
	int options = wait ? 0 : WNOHANG;

	while ((pid = wait3(&status, options, NULL)) > 0)
		if (pid == ctl->child)
			ctl->childstatus = status;
}

static void write_output(struct script_control *ctl, char *obuf,
			    ssize_t bytes, double *oldtime)
{

	if (ctl->tflg && ctl->timingfp) {
		struct timeval tv;
		double newtime;

		gettimeofday(&tv, NULL);
		newtime = tv.tv_sec + (double)tv.tv_usec / 1000000;
		fprintf(ctl->timingfp, "%f %zd\n", newtime - *oldtime, bytes);
		if (ctl->fflg)
			fflush(ctl->timingfp);
		*oldtime = newtime;
	}
	if (fwrite_all(obuf, 1, bytes, ctl->typescriptfp)) {
		warn(_("cannot write script file"));
		fail(ctl);
	}
	if (ctl->fflg)
		fflush(ctl->typescriptfp);
	if (write_all(STDOUT_FILENO, obuf, bytes)) {
		warn(_("write failed"));
		fail(ctl);
	}
}

static void handle_io(struct script_control *ctl, int fd, double *oldtime, int i)
{
	char buf[BUFSIZ];
	ssize_t bytes;

	bytes = read(fd, buf, sizeof(buf));
	if (bytes < 0) {
		if (errno == EAGAIN)
			return;
		fail(ctl);
	}
	if (i == 0) {
		if (write_all(ctl->master, buf, bytes)) {
			warn(_("write failed"));
			fail(ctl);
		}
		/* without sync write_output() will write both input &
		 * shell output that looks like double echoing */
		fdatasync(ctl->master);
		if (!ctl->isterm && feof(stdin)) {
			char c = DEF_EOF;
			write_all(ctl->master, &c, sizeof(char));
		}
	} else
		write_output(ctl, buf, bytes, oldtime);
}

static void handle_signal(struct script_control *ctl, int fd)
{
	struct signalfd_siginfo info;
	ssize_t bytes;

	bytes = read(fd, &info, sizeof(info));
	assert(bytes == sizeof(info));
	switch (info.ssi_signo) {
	case SIGCHLD:
		finish(ctl, 0);
		ctl->poll_timeout = 10;
		return;
	case SIGWINCH:
		if (ctl->isterm) {
			ioctl(STDIN_FILENO, TIOCGWINSZ, (char *)&ctl->win);
			ioctl(ctl->slave, TIOCSWINSZ, (char *)&ctl->win);
		}
		break;
	default:
		abort();
	}
}

static void do_io(struct script_control *ctl)
{
	struct pollfd pfd[POLLFDS];
	int ret, i;
	double oldtime = time(NULL);
	time_t tvec = script_time((time_t *)NULL);
	char buf[128];

	if (ctl->tflg && !ctl->timingfp)
		ctl->timingfp = fdopen(STDERR_FILENO, "w");

	pfd[0].fd = STDIN_FILENO;
	pfd[0].events = POLLIN;
	pfd[1].fd = ctl->master;
	pfd[1].events = POLLIN;
	pfd[2].fd = ctl->sigfd;
	pfd[2].events = POLLIN | POLLERR | POLLHUP;

	strftime(buf, sizeof buf, "%c\n", localtime(&tvec));
	fprintf(ctl->typescriptfp, _("Script started on %s"), buf);

	while (!ctl->die) {
		/* wait for input or signal */
		ret = poll(pfd, POLLFDS, ctl->poll_timeout);
		if (ret < 0) {
			if (errno == EAGAIN)
				continue;
			warn(_("poll failed"));
			fail(ctl);
		}
		if (ret == 0)
			ctl->die = 1;
		for (i = 0; i < POLLFDS; i++) {
			if (pfd[i].revents == 0)
				continue;
			if (i < 2) {
				handle_io(ctl, pfd[i].fd, &oldtime, i);
				continue;
			}
			if (i == 2) {
				handle_signal(ctl, pfd[i].fd);
				if (!ctl->isterm && -1 < ctl->poll_timeout)
					/* In situation such as 'date' in
					* $ echo date | ./script
					* ignore input when shell has exited.  */
					pfd[0].fd = -1;
			}
		}
	}
	if (!ctl->die)
		finish(ctl, 1); /* wait for children */
	if (!ctl->qflg && ctl->typescriptfp) {
		tvec = script_time((time_t *)NULL);
		strftime(buf, sizeof buf, "%c\n", localtime(&tvec));
		fprintf(ctl->typescriptfp, _("\nScript done on %s"), buf);
	}
	done(ctl);
}

static void getslave(struct script_control *ctl)
{
#ifndef HAVE_LIBUTIL
	ctl->line[strlen("/dev/")] = 't';
	ctl->slave = open(ctl->line, O_RDWR);
	if (ctl->slave < 0) {
		warn(_("cannot open %s"), ctl->line);
		fail(ctl);
	}
	if (ctl->isterm) {
		tcsetattr(ctl->slave, TCSANOW, &ctl->tt);
		ioctl(ctl->slave, TIOCSWINSZ, (char *)&ctl->win);
	}
#endif
	setsid();
	ioctl(ctl->slave, TIOCSCTTY, 0);
}

static void doshell(struct script_control *ctl)
{
	char *shname;

	getslave(ctl);

	/* close things irrelevant for this process */
	close(ctl->master);
	if (ctl->typescriptfp)
		fclose(ctl->typescriptfp);
	if (ctl->timingfp)
		fclose(ctl->timingfp);
	ctl->typescriptfp = ctl->timingfp = NULL;

	dup2(ctl->slave, STDIN_FILENO);
	dup2(ctl->slave, STDOUT_FILENO);
	dup2(ctl->slave, STDERR_FILENO);
	close(ctl->slave);

	ctl->master = -1;

	shname = strrchr(ctl->shell, '/');
	if (shname)
		shname++;
	else
		shname = ctl->shell;

	/*
	 * When invoked from within /etc/csh.login, script spawns a csh shell
	 * that spawns programs that cannot be killed with a SIGTERM. This is
	 * because csh has a documented behavior wherein it disables all
	 * signals when processing the /etc/csh.* files.
	 *
	 * Let's restore the default behavior.
	 */
	signal(SIGTERM, SIG_DFL);

	if (access(ctl->shell, X_OK) == 0) {
		if (ctl->cflg)
			execl(ctl->shell, shname, "-c", ctl->cflg, NULL);
		else
			execl(ctl->shell, shname, "-i", NULL);
	} else {
		if (ctl->cflg)
			execlp(shname, "-c", ctl->cflg, NULL);
		else
			execlp(shname, "-i", NULL);
	}
	warn(_("failed to execute %s"), ctl->shell);
	fail(ctl);
}

static void fixtty(struct script_control *ctl)
{
	struct termios rtt;

	if (!ctl->isterm)
		return;

	rtt = ctl->tt;
	cfmakeraw(&rtt);
	rtt.c_lflag &= ~ECHO;
	tcsetattr(STDIN_FILENO, TCSANOW, &rtt);
}

static void getmaster(struct script_control *ctl)
{
#if defined(HAVE_LIBUTIL) && defined(HAVE_PTY_H)
	int rc;

	ctl->isterm = isatty(STDIN_FILENO);

	if (ctl->isterm) {
		if (tcgetattr(STDIN_FILENO, &ctl->tt) != 0)
			err(EXIT_FAILURE, _("failed to get terminal attributes"));
		ioctl(STDIN_FILENO, TIOCGWINSZ, (char *)&ctl->win);
		rc = openpty(&ctl->master, &ctl->slave, NULL, &ctl->tt, &ctl->win);
	} else
		rc = openpty(&ctl->master, &ctl->slave, NULL, NULL, NULL);

	if (rc < 0) {
		warn(_("openpty failed"));
		fail(ctl);
	}
#else
	char *pty, *bank, *cp;
	struct stat stb;

	ctl->isterm = isatty(STDIN_FILENO);

	pty = &ctl->line[strlen("/dev/ptyp")];
	for (bank = "pqrs"; *bank; bank++) {
		ctl->line[strlen("/dev/pty")] = *bank;
		*pty = '0';
		if (stat(ctl->line, &stb) < 0)
			break;
		for (cp = "0123456789abcdef"; *cp; cp++) {
			*pty = *cp;
			ctl->master = open(ctl->line, O_RDWR);
			if (ctl->master >= 0) {
				char *tp = &ctl->line[strlen("/dev/")];
				int ok;

				/* verify slave side is usable */
				*tp = 't';
				ok = access(ctl->line, R_OK | W_OK) == 0;
				*tp = 'p';
				if (ok) {
					if (ctl->isterm) {
						tcgetattr(STDIN_FILENO, &ctl->tt);
						ioctl(STDIN_FILENO, TIOCGWINSZ, (char *)&ctl->win);
					}
					return;
				}
				close(ctl->master);
				ctl->master = -1;
			}
		}
	}
	ctl->master = -1;
	warn(_("out of pty's"));
	fail(ctl);
#endif				/* not HAVE_LIBUTIL */
}

int main(int argc, char **argv)
{
	struct script_control ctl = {
#if !HAVE_LIBUTIL || !HAVE_PTY_H
		.line = "/dev/ptyXX",
#endif
		.master = -1,
		.poll_timeout = -1,
		0
	};
	int ch;

	enum { FORCE_OPTION = CHAR_MAX + 1 };

	static const struct option longopts[] = {
		{"append", no_argument, NULL, 'a'},
		{"command", required_argument, NULL, 'c'},
		{"return", no_argument, NULL, 'e'},
		{"flush", no_argument, NULL, 'f'},
		{"force", no_argument, NULL, FORCE_OPTION,},
		{"quiet", no_argument, NULL, 'q'},
		{"timing", optional_argument, NULL, 't'},
		{"version", no_argument, NULL, 'V'},
		{"help", no_argument, NULL, 'h'},
		{NULL, 0, NULL, 0}
	};

	setlocale(LC_ALL, "");
	/*
	 * script -t prints time delays as floating point numbers.  The example
	 * program (scriptreplay) that we provide to handle this timing output
	 * is a perl script, and does not handle numbers in locale format (not
	 * even when "use locale;" is added).  So, since these numbers are not
	 * for human consumption, it seems easiest to set LC_NUMERIC here.
	 */
	setlocale(LC_NUMERIC, "C");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	while ((ch = getopt_long(argc, argv, "ac:efqt::Vh", longopts, NULL)) != -1)
		switch (ch) {
		case 'a':
			ctl.aflg = 1;
			break;
		case 'c':
			ctl.cflg = optarg;
			break;
		case 'e':
			ctl.eflg = 1;
			break;
		case 'f':
			ctl.fflg = 1;
			break;
		case FORCE_OPTION:
			ctl.forceflg = 1;
			break;
		case 'q':
			ctl.qflg = 1;
			break;
		case 't':
			if (optarg && !(ctl.timingfp = fopen(optarg, "w")))
				err(EXIT_FAILURE, _("cannot open %s"), optarg);
			ctl.tflg = 1;
			break;
		case 'V':
			printf(UTIL_LINUX_VERSION);
			exit(EXIT_SUCCESS);
			break;
		case 'h':
			usage(stdout);
			break;
		default:
			usage(stderr);
		}
	argc -= optind;
	argv += optind;

	if (argc > 0)
		ctl.fname = argv[0];
	else {
		ctl.fname = DEFAULT_OUTPUT;
		die_if_link(&ctl);
	}

	if ((ctl.typescriptfp = fopen(ctl.fname, ctl.aflg ? "a" : "w")) == NULL) {
		warn(_("cannot open %s"), ctl.fname);
		fail(&ctl);
	}

	ctl.shell = getenv("SHELL");
	if (ctl.shell == NULL)
		ctl.shell = _PATH_BSHELL;

	getmaster(&ctl);
	if (!ctl.qflg)
		printf(_("Script started, file is %s\n"), ctl.fname);
	fixtty(&ctl);

#ifdef HAVE_LIBUTEMPTER
	utempter_add_record(ctl.master, NULL);
#endif
	/* setup signal handler */
	assert(sigemptyset(&ctl.sigset) == 0);
	assert(sigaddset(&ctl.sigset, SIGCHLD) == 0);
	assert(sigaddset(&ctl.sigset, SIGWINCH) == 0);
	assert(sigprocmask(SIG_BLOCK, &ctl.sigset, NULL) == 0);
	if ((ctl.sigfd = signalfd(-1, &ctl.sigset, 0)) < 0)
		err(EXIT_FAILURE, _("cannot set signal handler"));

	fflush(stdout);
	ctl.child = fork();

	if (ctl.child < 0) {
		warn(_("fork failed"));
		fail(&ctl);
	}
	if (ctl.child == 0)
		doshell(&ctl);
	do_io(&ctl);
	/* should not happen, do_io() calls done() */
	return EXIT_FAILURE;
}
