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

void sig_finish(int);
void finish(int);
void done(void);
void fail(void);
void resize(int);
void fixtty(void);
void getmaster(void);
void getslave(void);
void doinput(void);
void dooutput(void);
void doshell(void);

char	*shell;
FILE	*fscript;
FILE	*timingfd;
int	master = -1;
int	slave;
pid_t	child;
pid_t	subchild;
int	childstatus;
char	*fname;

struct	termios tt;
struct	winsize win;
int	lb;
int	l;
#if !HAVE_LIBUTIL || !HAVE_PTY_H
char	line[] = "/dev/ptyXX";
#endif
int	aflg = 0;
char	*cflg = NULL;
int	eflg = 0;
int	fflg = 0;
int	qflg = 0;
int	tflg = 0;
int	forceflg = 0;
int	isterm;

sigset_t block_mask, unblock_mask;

int die;
int resized;

static void
die_if_link(char *fn) {
	struct stat s;

	if (forceflg)
		return;
	if (lstat(fn, &s) == 0 && (S_ISLNK(s.st_mode) || s.st_nlink > 1))
		errx(EXIT_FAILURE,
		     _("output file `%s' is a link\n"
		       "Use --force if you really want to use it.\n"
		       "Program not started."), fn);
}

static void __attribute__((__noreturn__))
usage(FILE *out)
{
	fputs(_("\nUsage:\n"), out);
	fprintf(out,
	      _(" %s [options] [file]\n"), program_invocation_short_name);

	fputs(_("\nOptions:\n"), out);
	fputs(_(" -a, --append            append the output\n"
		" -c, --command <command> run command rather than interactive shell\n"
		" -e, --return            return exit code of the child process\n"
		" -f, --flush             run flush after each write\n"
		"     --force             use output file even when it is a link\n"
		" -q, --quiet             be quiet\n"
		" -t, --timing[=<file>]   output timing data to stderr (or to FILE)\n"
		" -V, --version           output version information and exit\n"
		" -h, --help              display this help and exit\n\n"), out);

	exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}

/*
 * script -t prints time delays as floating point numbers
 * The example program (scriptreplay) that we provide to handle this
 * timing output is a perl script, and does not handle numbers in
 * locale format (not even when "use locale;" is added).
 * So, since these numbers are not for human consumption, it seems
 * easiest to set LC_NUMERIC here.
 */

int
main(int argc, char **argv) {
	struct sigaction sa;
	int ch;

	enum { FORCE_OPTION = CHAR_MAX + 1 };

	static const struct option longopts[] = {
		{ "append",	no_argument,	   NULL, 'a' },
		{ "command",	required_argument, NULL, 'c' },
		{ "return",	no_argument,	   NULL, 'e' },
		{ "flush",	no_argument,	   NULL, 'f' },
		{ "force",	no_argument,	   NULL, FORCE_OPTION, },
		{ "quiet",	no_argument,	   NULL, 'q' },
		{ "timing",	optional_argument, NULL, 't' },
		{ "version",	no_argument,	   NULL, 'V' },
		{ "help",	no_argument,	   NULL, 'h' },
		{ NULL,		0, NULL, 0 }
	};

	setlocale(LC_ALL, "");
	setlocale(LC_NUMERIC, "C");	/* see comment above */
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	while ((ch = getopt_long(argc, argv, "ac:efqt::Vh", longopts, NULL)) != -1)
		switch(ch) {
		case 'a':
			aflg = 1;
			break;
		case 'c':
			cflg = optarg;
			break;
		case 'e':
			eflg = 1;
			break;
		case 'f':
			fflg = 1;
			break;
		case FORCE_OPTION:
			forceflg = 1;
			break;
		case 'q':
			qflg = 1;
			break;
		case 't':
			if (optarg && !(timingfd = fopen(optarg, "w")))
				err(EXIT_FAILURE, _("cannot open %s"), optarg);
			tflg = 1;
			break;
		case 'V':
			printf(_("%s from %s\n"), program_invocation_short_name,
						  PACKAGE_STRING);
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
		fname = argv[0];
	else {
		fname = DEFAULT_OUTPUT;
		die_if_link(fname);
	}

	if ((fscript = fopen(fname, aflg ? "a" : "w")) == NULL) {
		warn(_("cannot open %s"), fname);
		fail();
	}

	shell = getenv("SHELL");
	if (shell == NULL)
		shell = _PATH_BSHELL;

	getmaster();
	if (!qflg)
		printf(_("Script started, file is %s\n"), fname);
	fixtty();

#ifdef HAVE_LIBUTEMPTER
	utempter_add_record(master, NULL);
#endif
	/* setup SIGCHLD handler */
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	sa.sa_handler = sig_finish;
	sigaction(SIGCHLD, &sa, NULL);

	/* init mask for SIGCHLD */
	sigprocmask(SIG_SETMASK, NULL, &block_mask);
	sigaddset(&block_mask, SIGCHLD);

	sigprocmask(SIG_SETMASK, &block_mask, &unblock_mask);
	child = fork();
	sigprocmask(SIG_SETMASK, &unblock_mask, NULL);

	if (child < 0) {
		warn(_("fork failed"));
		fail();
	}
	if (child == 0) {

		sigprocmask(SIG_SETMASK, &block_mask, NULL);
		subchild = child = fork();
		sigprocmask(SIG_SETMASK, &unblock_mask, NULL);

		if (child < 0) {
			warn(_("fork failed"));
			fail();
		}
		if (child)
			dooutput();
		else
			doshell();
	} else {
		sa.sa_handler = resize;
		sigaction(SIGWINCH, &sa, NULL);
	}
	doinput();

	return EXIT_SUCCESS;
}

static void wait_for_empty_fd(int fd)
{
	struct pollfd fds[] = {
		{ .fd = fd, .events = POLLIN }
	};

	while (die == 0 && poll(fds, 1, 100) == 1);
}

void
doinput(void) {
	int errsv = 0;
	ssize_t cc = 0;
	char ibuf[BUFSIZ];
	fd_set readfds;

	/* close things irrelevant for this process */
	if (fscript)
		fclose(fscript);
	if (timingfd)
		fclose(timingfd);
	fscript = timingfd = NULL;

	FD_ZERO(&readfds);

	/* block SIGCHLD */
	sigprocmask(SIG_SETMASK, &block_mask, &unblock_mask);

	while (die == 0) {
		FD_SET(STDIN_FILENO, &readfds);

		errno = 0;
		/* wait for input or signal (including SIGCHLD) */
		if ((cc = pselect(STDIN_FILENO + 1, &readfds, NULL, NULL, NULL,
			&unblock_mask)) > 0) {

			if ((cc = read(STDIN_FILENO, ibuf, BUFSIZ)) > 0) {
				if (write_all(master, ibuf, cc)) {
					warn (_("write failed"));
					fail();
				}
			}
		}

		if (cc < 0 && errno == EINTR && resized)
		{
			/* transmit window change information to the child */
			if (isterm) {
				ioctl(STDIN_FILENO, TIOCGWINSZ, (char *)&win);
				ioctl(slave, TIOCSWINSZ, (char *)&win);
			}
			resized = 0;

		} else if (cc <= 0 && errno != EINTR) {
			errsv = errno;
			break;
		}
	}

	/* unblock SIGCHLD */
	sigprocmask(SIG_SETMASK, &unblock_mask, NULL);

	/* To be sure that we don't miss any data */
	wait_for_empty_fd(slave);
	wait_for_empty_fd(master);

	if (die == 0 && cc == 0 && errsv == 0) {
		/*
		 * Forward EOF from stdin (detected by read() above) to slave
		 * (shell) to correctly terminate the session. It seems we have
		 * to wait for empty terminal FDs otherwise EOF maybe ignored
		 * (why?) and typescript is incomplete.      -- kzak Dec-2013
		 *
		 * We usually use this when stdin is not a tty, for example:
		 * echo "ps" | script
		 */
		int c = DEF_EOF;

		if (write_all(master, &c, 1)) {
			warn (_("write failed"));
			fail();
		}

		/* wait for "exit" message from shell before we print "Script
		 * done" in done() */
		wait_for_empty_fd(master);
	}

	if (!die)
		finish(1);	/* wait for children */
	done();
}

void
finish(int wait) {
	int status;
	pid_t pid;
	int errsv = errno;
	int options = wait ? 0 : WNOHANG;

	while ((pid = wait3(&status, options, 0)) > 0)
		if (pid == child) {
			childstatus = status;
			die = 1;
		}

	errno = errsv;
}

void
sig_finish(int dummy __attribute__ ((__unused__))) {
	finish(0);
}

void
resize(int dummy __attribute__ ((__unused__))) {
	resized = 1;
}

/*
 * Stop extremely silly gcc complaint on %c:
 *  warning: `%c' yields only last 2 digits of year in some locales
 */
static void
my_strftime(char *buf, size_t len, const char *fmt, const struct tm *tm) {
	strftime(buf, len, fmt, tm);
}

void
dooutput(void) {
	ssize_t cc;
	char obuf[BUFSIZ];
	struct timeval tv;
	double oldtime=time(NULL), newtime;
	int errsv = 0;
	fd_set readfds;

	close(STDIN_FILENO);
#ifdef HAVE_LIBUTIL
	close(slave);
#endif
	if (tflg && !timingfd)
		timingfd = fdopen(STDERR_FILENO, "w");

	if (!qflg) {
		time_t tvec = time((time_t *)NULL);
		my_strftime(obuf, sizeof obuf, "%c\n", localtime(&tvec));
		fprintf(fscript, _("Script started on %s"), obuf);
	}

	FD_ZERO(&readfds);

	do {
		if (die || errsv == EINTR) {
			struct pollfd fds[] = {{ .fd = master, .events = POLLIN }};
			if (poll(fds, 1, 50) <= 0)
				break;
		}

		/* block SIGCHLD */
		sigprocmask(SIG_SETMASK, &block_mask, &unblock_mask);

		FD_SET(master, &readfds);
		errno = 0;

		/* wait for input or signal (including SIGCHLD) */
		if ((cc = pselect(master+1, &readfds, NULL, NULL, NULL,
			&unblock_mask)) > 0) {

			cc = read(master, obuf, sizeof (obuf));
		}
		errsv = errno;

		/* unblock SIGCHLD */
		sigprocmask(SIG_SETMASK, &unblock_mask, NULL);

		if (tflg)
			gettimeofday(&tv, NULL);

		if (errsv == EINTR && cc <= 0)
			continue;	/* try it again */
		if (cc <= 0)
			break;
		if (tflg && timingfd) {
			newtime = tv.tv_sec + (double) tv.tv_usec / 1000000;
			fprintf(timingfd, "%f %zd\n", newtime - oldtime, cc);
			oldtime = newtime;
		}
		if (fwrite_all(obuf, 1, cc, fscript)) {
			warn (_("cannot write script file"));
			fail();
		}
		if (fflg) {
			fflush(fscript);
			if (tflg && timingfd)
				fflush(timingfd);
		}
		if (write_all(STDOUT_FILENO, obuf, cc)) {
			warn (_("write failed"));
			fail();
		}
	} while(1);

	done();
}

void
doshell(void) {
	char *shname;

	getslave();

	/* close things irrelevant for this process */
	close(master);
	if (fscript)
		fclose(fscript);
	if (timingfd)
		fclose(timingfd);
	fscript = timingfd = NULL;

	dup2(slave, STDIN_FILENO);
	dup2(slave, STDOUT_FILENO);
	dup2(slave, STDERR_FILENO);
	close(slave);

	master = -1;

	shname = strrchr(shell, '/');
	if (shname)
		shname++;
	else
		shname = shell;

	/*
	 * When invoked from within /etc/csh.login, script spawns a csh shell
	 * that spawns programs that cannot be killed with a SIGTERM. This is
	 * because csh has a documented behavior wherein it disables all
	 * signals when processing the /etc/csh.* files.
	 *
	 * Let's restore the default behavior.
	 */
	signal(SIGTERM, SIG_DFL);

	if (access(shell, X_OK) == 0) {
		if (cflg)
			execl(shell, shname, "-c", cflg, NULL);
		else
			execl(shell, shname, "-i", NULL);
	} else {
		if (cflg)
			execlp(shname, "-c", cflg, NULL);
		else
			execlp(shname, "-i", NULL);
	}
	warn(_("failed to execute %s"), shell);
	fail();
}

void
fixtty(void) {
	struct termios rtt;

	if (!isterm)
		return;

	rtt = tt;
	cfmakeraw(&rtt);
	rtt.c_lflag &= ~ECHO;
	tcsetattr(STDIN_FILENO, TCSANOW, &rtt);
}

void
fail(void) {

	kill(0, SIGTERM);
	done();
}

void __attribute__((__noreturn__))
done(void) {
	time_t tvec;

	if (subchild) {
		/* output process */
		if (fscript) {
			if (!qflg) {
				char buf[BUFSIZ];
				tvec = time((time_t *)NULL);
				my_strftime(buf, sizeof buf, "%c\n", localtime(&tvec));
				fprintf(fscript, _("\nScript done on %s"), buf);
			}
			if (close_stream(fscript) != 0)
				errx(EXIT_FAILURE, _("write error"));
			fscript = NULL;
		}
		if (timingfd && close_stream(timingfd) != 0)
			errx(EXIT_FAILURE, _("write error"));
		timingfd = NULL;

		close(master);
		master = -1;
	} else {
		/* input process */
		if (isterm)
			tcsetattr(STDIN_FILENO, TCSADRAIN, &tt);
		if (!qflg)
			printf(_("Script done, file is %s\n"), fname);
#ifdef HAVE_LIBUTEMPTER
		if (master >= 0)
			utempter_remove_record(master);
#endif
		kill(child, SIGTERM);	/* make sure we don't create orphans */
	}

	if(eflg) {
		if (WIFSIGNALED(childstatus))
			exit(WTERMSIG(childstatus) + 0x80);
		else
			exit(WEXITSTATUS(childstatus));
	}
	exit(EXIT_SUCCESS);
}

void
getmaster(void) {
#if defined(HAVE_LIBUTIL) && defined(HAVE_PTY_H)
	int rc;

	isterm = isatty(STDIN_FILENO);

	if (isterm) {
		if (tcgetattr(STDIN_FILENO, &tt) != 0)
			err(EXIT_FAILURE, _("failed to get terminal attributes"));
		ioctl(STDIN_FILENO, TIOCGWINSZ, (char *) &win);
		rc = openpty(&master, &slave, NULL, &tt, &win);
	} else
		rc = openpty(&master, &slave, NULL, NULL, NULL);

	if (rc < 0) {
		warn(_("openpty failed"));
		fail();
	}
#else
	char *pty, *bank, *cp;
	struct stat stb;

	isterm = isatty(STDIN_FILENO);

	pty = &line[strlen("/dev/ptyp")];
	for (bank = "pqrs"; *bank; bank++) {
		line[strlen("/dev/pty")] = *bank;
		*pty = '0';
		if (stat(line, &stb) < 0)
			break;
		for (cp = "0123456789abcdef"; *cp; cp++) {
			*pty = *cp;
			master = open(line, O_RDWR);
			if (master >= 0) {
				char *tp = &line[strlen("/dev/")];
				int ok;

				/* verify slave side is usable */
				*tp = 't';
				ok = access(line, R_OK|W_OK) == 0;
				*tp = 'p';
				if (ok) {
					if (isterm) {
						tcgetattr(STDIN_FILENO, &tt);
						ioctl(STDIN_FILENO, TIOCGWINSZ,
								(char *)&win);
					}
					return;
				}
				close(master);
				master = -1;
			}
		}
	}
	master = -1;
	warn(_("out of pty's"));
	fail();
#endif /* not HAVE_LIBUTIL */
}

void
getslave(void) {
#ifndef HAVE_LIBUTIL
	line[strlen("/dev/")] = 't';
	slave = open(line, O_RDWR);
	if (slave < 0) {
		warn(_("cannot open %s"), line);
		fail();
	}
	if (isterm) {
		tcsetattr(slave, TCSANOW, &tt);
		ioctl(slave, TIOCSWINSZ, (char *)&win);
	}
#endif
	setsid();
	ioctl(slave, TIOCSCTTY, 0);
}
