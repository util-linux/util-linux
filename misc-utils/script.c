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
 * 1999-02-22 Arkadiusz Mi¶kiewicz <misiek@pld.ORG.PL>
 * - added Native Language Support
 *
 * 2000-07-30 Per Andreas Buer <per@linpro.no> - added "q"-option
 */

/*
 * script
 */
#include <stdio.h>
#include <stdlib.h>
#include <paths.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/file.h>
#include <sys/signal.h>
#include "nls.h"

#ifdef __linux__
#include <unistd.h>
#include <string.h>
#endif

#ifdef HAVE_LIBUTIL
#include <pty.h>
#endif

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
int	master;
int	slave;
int	child;
int	subchild;
char	*fname;

struct	termios tt;
struct	winsize win;
int	lb;
int	l;
#ifndef HAVE_LIBUTIL
char	line[] = "/dev/ptyXX";
#endif
int	aflg = 0;
char	*cflg = NULL;
int	fflg = 0;
int	qflg = 0;
int	tflg = 0;

static char *progname;

static void
die_if_link(char *fn) {
	struct stat s;

	if (lstat(fn, &s) == 0 && (S_ISLNK(s.st_mode) || s.st_nlink > 1)) {
		fprintf(stderr,
			_("Warning: `%s' is a link.\n"
			  "Use `%s [options] %s' if you really "
			  "want to use it.\n"
			  "Script not started.\n"),
			fn, progname, fn);
		exit(1);
	}
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
	extern int optind;
	char *p;
	int ch;

	progname = argv[0];
	if ((p = strrchr(progname, '/')) != NULL)
		progname = p+1;


	setlocale(LC_ALL, "");
	setlocale(LC_NUMERIC, "C");	/* see comment above */
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	
	if (argc == 2) {
		if (!strcmp(argv[1], "-V") || !strcmp(argv[1], "--version")) {
			printf(_("%s (%s)\n"),
			       progname, PACKAGE_STRING);
			return 0;
		}
	}

	while ((ch = getopt(argc, argv, "ac:fqt")) != -1)
		switch((char)ch) {
		case 'a':
			aflg++;
			break;
		case 'c':
			cflg = optarg;
			break;
		case 'f':
			fflg++;
			break;
		case 'q':
			qflg++;
			break;
		case 't':
			tflg++;
			break;
		case '?':
		default:
			fprintf(stderr,
				_("usage: script [-a] [-f] [-q] [-t] [file]\n"));
			exit(1);
		}
	argc -= optind;
	argv += optind;

	if (argc > 0)
		fname = argv[0];
	else {
		fname = "typescript";
		die_if_link(fname);
	}
	if ((fscript = fopen(fname, aflg ? "a" : "w")) == NULL) {
		perror(fname);
		fail();
	}

	shell = getenv("SHELL");
	if (shell == NULL)
		shell = _PATH_BSHELL;

	getmaster();
	if (!qflg)
		printf(_("Script started, file is %s\n"), fname);
	fixtty();

	(void) signal(SIGCHLD, finish);
	child = fork();
	if (child < 0) {
		perror("fork");
		fail();
	}
	if (child == 0) {
		subchild = child = fork();
		if (child < 0) {
			perror("fork");
			fail();
		}
		if (child)
			dooutput();
		else
			doshell();
	} else
		(void) signal(SIGWINCH, resize);
	doinput();

	return 0;
}

void
doinput() {
	register int cc;
	char ibuf[BUFSIZ];

	(void) fclose(fscript);

	while ((cc = read(0, ibuf, BUFSIZ)) > 0)
		(void) write(master, ibuf, cc);
	done();
}

#include <sys/wait.h>

void
finish(int dummy) {
	int status;
	register int pid;
	register int die = 0;

	while ((pid = wait3(&status, WNOHANG, 0)) > 0)
		if (pid == child)
			die = 1;

	if (die)
		done();
}

void
resize(int dummy) {
	/* transmit window change information to the child */
	(void) ioctl(0, TIOCGWINSZ, (char *)&win);
	(void) ioctl(slave, TIOCSWINSZ, (char *)&win);

	kill(child, SIGWINCH);
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
dooutput() {
	register int cc;
	time_t tvec;
	char obuf[BUFSIZ];
	struct timeval tv;
	double oldtime=time(NULL), newtime;

	(void) close(0);
#ifdef HAVE_LIBUTIL
	(void) close(slave);
#endif
	tvec = time((time_t *)NULL);
	my_strftime(obuf, sizeof obuf, "%c\n", localtime(&tvec));
	fprintf(fscript, _("Script started on %s"), obuf);

	for (;;) {
		if (tflg)
			gettimeofday(&tv, NULL);
		cc = read(master, obuf, sizeof (obuf));
		if (cc <= 0)
			break;
		if (tflg) {
			newtime = tv.tv_sec + (double) tv.tv_usec / 1000000;
			fprintf(stderr, "%f %i\n", newtime - oldtime, cc);
			oldtime = newtime;
		}
		(void) write(1, obuf, cc);
		(void) fwrite(obuf, 1, cc, fscript);
		if (fflg)
			(void) fflush(fscript);
	}
	done();
}

void
doshell() {
	char *shname;

#if 0
	int t;

	t = open(_PATH_TTY, O_RDWR);
	if (t >= 0) {
		(void) ioctl(t, TIOCNOTTY, (char *)0);
		(void) close(t);
	}
#endif

	getslave();
	(void) close(master);
	(void) fclose(fscript);
	(void) dup2(slave, 0);
	(void) dup2(slave, 1);
	(void) dup2(slave, 2);
	(void) close(slave);

	shname = strrchr(shell, '/');
	if (shname)
		shname++;
	else
		shname = shell;

	if (cflg)
		execl(shell, shname, "-c", cflg, 0);
	else
		execl(shell, shname, "-i", 0);

	perror(shell);
	fail();
}

void
fixtty() {
	struct termios rtt;

	rtt = tt;
	cfmakeraw(&rtt);
	rtt.c_lflag &= ~ECHO;
	(void) tcsetattr(0, TCSAFLUSH, &rtt);
}

void
fail() {

	(void) kill(0, SIGTERM);
	done();
}

void
done() {
	time_t tvec;

	if (subchild) {
		if (!qflg) {
			char buf[BUFSIZ];
			tvec = time((time_t *)NULL);
			my_strftime(buf, sizeof buf, "%c\n", localtime(&tvec));
			fprintf(fscript, _("\nScript done on %s"), buf);
		}
		(void) fclose(fscript);
		(void) close(master);
	} else {
		(void) tcsetattr(0, TCSAFLUSH, &tt);
		if (!qflg)
			printf(_("Script done, file is %s\n"), fname);
	}
	exit(0);
}

void
getmaster() {
#ifdef HAVE_LIBUTIL
	(void) tcgetattr(0, &tt);
	(void) ioctl(0, TIOCGWINSZ, (char *)&win);
	if (openpty(&master, &slave, NULL, &tt, &win) < 0) {
		fprintf(stderr, _("openpty failed\n"));
		fail();
	}
#else
	char *pty, *bank, *cp;
	struct stat stb;

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
					(void) tcgetattr(0, &tt);
				    	(void) ioctl(0, TIOCGWINSZ, 
						(char *)&win);
					return;
				}
				(void) close(master);
			}
		}
	}
	fprintf(stderr, _("Out of pty's\n"));
	fail();
#endif /* not HAVE_LIBUTIL */
}

void
getslave() {
#ifndef HAVE_LIBUTIL
	line[strlen("/dev/")] = 't';
	slave = open(line, O_RDWR);
	if (slave < 0) {
		perror(line);
		fail();
	}
	(void) tcsetattr(slave, TCSAFLUSH, &tt);
	(void) ioctl(slave, TIOCSWINSZ, (char *)&win);
#endif
	(void) setsid();
	(void) ioctl(slave, TIOCSCTTY, 0);
}
