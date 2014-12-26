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

struct script_control {
	char *shell;		/* shell to be executed */
	char *cflg;		/* command to be executed */
	char *fname;		/* output file path */
	FILE *typescriptfp;	/* output file pointer */
	FILE *timingfp;		/* timing file pointer */
	int master;		/* pseudoterminal master file descriptor */
	int slave;		/* pseudoterminal slave file descriptor */
	pid_t child;		/* child pid */
	pid_t subchild;		/* subchild pid */
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
	 resized:1,		/* has terminal been resized */
	 die:1;			/* terminate program */
	sigset_t sigset;	/* catch SIGCHLD and SIGWINCH with signalfd() */
	int sigfd;		/* file descriptor for signalfd() */
};
struct script_control *gctl;	/* global control structure, used in signal handlers */

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

/*
 * Stop extremely silly gcc complaint on %c:
 *  warning: `%c' yields only last 2 digits of year in some locales
 */
static void my_strftime(char *buf, size_t len, const char *fmt, const struct tm *tm)
{
	strftime(buf, len, fmt, tm);
}

static void __attribute__((__noreturn__)) done(struct script_control *ctl)
{
	time_t tvec;

	if (ctl->subchild) {
		/* output process */
		if (ctl->typescriptfp) {
			if (!ctl->qflg) {
				char buf[BUFSIZ];
				tvec = time((time_t *)NULL);
				my_strftime(buf, sizeof buf, "%c\n", localtime(&tvec));
				fprintf(ctl->typescriptfp, _("\nScript done on %s"), buf);
			}
			if (close_stream(ctl->typescriptfp) != 0)
				errx(EXIT_FAILURE, _("write error"));
			ctl->typescriptfp = NULL;
		}
		if (ctl->timingfp && close_stream(ctl->timingfp) != 0)
			errx(EXIT_FAILURE, _("write error"));
		ctl->timingfp = NULL;

		close(ctl->master);
		ctl->master = -1;
	} else {
		/* input process */
		if (ctl->isterm)
			tcsetattr(STDIN_FILENO, TCSADRAIN, &ctl->tt);
		if (!ctl->qflg)
			printf(_("Script done, file is %s\n"), ctl->fname);
#ifdef HAVE_LIBUTEMPTER
		if (ctl->master >= 0)
			utempter_remove_record(ctl->master);
#endif
		kill(ctl->child, SIGTERM);	/* make sure we don't create orphans */
	}

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

static void wait_for_empty_fd(struct script_control *ctl, int fd)
{
	struct pollfd fds[] = {
		{.fd = fd, .events = POLLIN}
	};

	while (ctl->die == 0 && poll(fds, 1, 100) == 1) ;
}

static void finish(struct script_control *ctl, int wait)
{
	int status;
	pid_t pid;
	int errsv = errno;
	int options = wait ? 0 : WNOHANG;

	while ((pid = wait3(&status, options, 0)) > 0)
		if (pid == ctl->child) {
			ctl->childstatus = status;
			ctl->die = 1;
		}

	errno = errsv;
}

static void doinput(struct script_control *ctl)
{
	int errsv = 0;
	ssize_t cc = 0;
	char ibuf[BUFSIZ];
	fd_set readfds;

	/* close things irrelevant for this process */
	if (ctl->typescriptfp)
		fclose(ctl->typescriptfp);
	if (ctl->timingfp)
		fclose(ctl->timingfp);
	ctl->typescriptfp = ctl->timingfp = NULL;

	FD_ZERO(&readfds);

	while (!ctl->die) {
		FD_SET(STDIN_FILENO, &readfds);

		errno = 0;
		/* wait for input or signal (including SIGCHLD) */
		if ((cc = pselect(STDIN_FILENO + 1, &readfds, NULL, NULL, NULL,
				  NULL)) > 0) {

			if ((cc = read(STDIN_FILENO, ibuf, BUFSIZ)) > 0) {
				if (write_all(ctl->master, ibuf, cc)) {
					warn(_("write failed"));
					fail(ctl);
				}
			}
		}

		if (cc < 0 && errno == EINTR && ctl->resized) {
			/* transmit window change information to the child */
			if (ctl->isterm) {
				ioctl(STDIN_FILENO, TIOCGWINSZ, (char *)&ctl->win);
				ioctl(ctl->slave, TIOCSWINSZ, (char *)&ctl->win);
			}
			ctl->resized = 0;

		} else if (cc <= 0 && errno != EINTR) {
			errsv = errno;
			break;
		}
	}

	/* To be sure that we don't miss any data */
	wait_for_empty_fd(ctl, ctl->slave);
	wait_for_empty_fd(ctl, ctl->master);

	if (ctl->die == 0 && cc == 0 && errsv == 0) {
		/*
		 * Forward EOF from stdin (detected by read() above) to slave
		 * (shell) to correctly terminate the session. It seems we have
		 * to wait for empty terminal FDs otherwise EOF maybe ignored
		 * (why?) and typescript is incomplete.      -- kzak Dec-2013
		 *
		 * We usually use this when stdin is not a tty, for example:
		 * echo "ps" | script
		 */
		char c = DEF_EOF;

		if (write_all(ctl->master, &c, sizeof(char))) {
			warn(_("write failed"));
			fail(ctl);
		}

		/* wait for "exit" message from shell before we print "Script
		 * done" in done() */
		wait_for_empty_fd(ctl, ctl->master);
	}

	if (!ctl->die)
		finish(ctl, 1);	/* wait for children */
	done(ctl);
}

static void sig_finish(int dummy __attribute__((__unused__)))
{
	finish(gctl, 0);
}

static void resize(int dummy __attribute__((__unused__)))
{
	gctl->resized = 1;
}

static void dooutput(struct script_control *ctl)
{
	ssize_t cc;
	char obuf[BUFSIZ];
	struct timeval tv;
	double oldtime = time(NULL), newtime;
	int errsv = 0;
	fd_set readfds;

	close(STDIN_FILENO);
#ifdef HAVE_LIBUTIL
	close(ctl->slave);
#endif
	if (ctl->tflg && !ctl->timingfp)
		ctl->timingfp = fdopen(STDERR_FILENO, "w");

	if (!ctl->qflg) {
		time_t tvec = script_time((time_t *)NULL);
		my_strftime(obuf, sizeof obuf, "%c\n", localtime(&tvec));
		fprintf(ctl->typescriptfp, _("Script started on %s"), obuf);
	}

	FD_ZERO(&readfds);

	do {
		if (ctl->die || errsv == EINTR) {
			struct pollfd fds[] = {
				{.fd = ctl->sigfd, .events = POLLIN | POLLERR | POLLHUP},
				{.fd = ctl->master, .events = POLLIN}
			};
			if (poll(fds, 1, 50) <= 0)
				break;
		}

		FD_SET(ctl->master, &readfds);
		errno = 0;

		/* wait for input or signal (including SIGCHLD) */
		if ((cc = pselect(ctl->master + 1, &readfds, NULL, NULL, NULL,
				  NULL)) > 0) {

			cc = read(ctl->master, obuf, sizeof(obuf));
		}
		errsv = errno;

		if (ctl->tflg)
			gettimeofday(&tv, NULL);

		if (errsv == EINTR && cc <= 0)
			continue;	/* try it again */
		if (cc <= 0)
			break;
		if (ctl->tflg && ctl->timingfp) {
			newtime = tv.tv_sec + (double)tv.tv_usec / 1000000;
			fprintf(ctl->timingfp, "%f %zd\n", newtime - oldtime, cc);
			oldtime = newtime;
		}
		if (fwrite_all(obuf, 1, cc, ctl->typescriptfp)) {
			warn(_("cannot write script file"));
			fail(ctl);
		}
		if (ctl->fflg) {
			fflush(ctl->typescriptfp);
			if (ctl->tflg && ctl->timingfp)
				fflush(ctl->timingfp);
		}
		if (write_all(STDOUT_FILENO, obuf, cc)) {
			warn(_("write failed"));
			fail(ctl);
		}
	} while (1);

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

/*
 * script -t prints time delays as floating point numbers
 * The example program (scriptreplay) that we provide to handle this
 * timing output is a perl script, and does not handle numbers in
 * locale format (not even when "use locale;" is added).
 * So, since these numbers are not for human consumption, it seems
 * easiest to set LC_NUMERIC here.
 */
int main(int argc, char **argv)
{
	struct script_control ctl = {
#if !HAVE_LIBUTIL || !HAVE_PTY_H
		.line = "/dev/ptyXX",
#endif
		.master = -1,
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
	setlocale(LC_NUMERIC, "C");	/* see comment above */
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);
	gctl = &ctl;

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
		case '?':
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
	if (ctl.child == 0) {

		ctl.subchild = ctl.child = fork();

		if (ctl.child < 0) {
			warn(_("fork failed"));
			fail(&ctl);
		}
		if (ctl.child)
			dooutput(&ctl);
		else
			doshell(&ctl);
	}
	doinput(&ctl);

	return EXIT_SUCCESS;
}
