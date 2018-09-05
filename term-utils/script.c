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
#include <inttypes.h>

#include "closestream.h"
#include "nls.h"
#include "c.h"
#include "ttyutils.h"
#include "all-io.h"
#include "monotonic.h"
#include "timeutils.h"
#include "strutils.h"

#include "debug.h"

static UL_DEBUG_DEFINE_MASK(script);
UL_DEBUG_DEFINE_MASKNAMES(script) = UL_DEBUG_EMPTY_MASKNAMES;

#define SCRIPT_DEBUG_INIT	(1 << 1)
#define SCRIPT_DEBUG_POLL	(1 << 2)
#define SCRIPT_DEBUG_SIGNAL	(1 << 3)
#define SCRIPT_DEBUG_IO		(1 << 4)
#define SCRIPT_DEBUG_MISC	(1 << 5)
#define SCRIPT_DEBUG_ALL	0xFFFF

#define DBG(m, x)       __UL_DBG(script, SCRIPT_DEBUG_, m, x)
#define ON_DBG(m, x)    __UL_DBG_CALL(script, SCRIPT_DEBUG_, m, x)

#if defined(HAVE_LIBUTIL) && defined(HAVE_PTY_H)
# include <pty.h>
#endif

#ifdef HAVE_LIBUTEMPTER
# include <utempter.h>
#endif

#define DEFAULT_TYPESCRIPT_FILENAME "typescript"

struct script_control {
	char *shell;		/* shell to be executed */
	char *command;		/* command to be executed */
	char *fname;		/* output file path */
	FILE *typescriptfp;	/* output file pointer */
	char *tname;		/* timing file path */
	FILE *timingfp;		/* timing file pointer */
	uint64_t outsz;         /* current output file size */
	uint64_t maxsz;		/* maximum output file size */
	struct timeval oldtime;	/* previous write or command start time */
	int master;		/* pseudoterminal master file descriptor */
	int slave;		/* pseudoterminal slave file descriptor */
	int poll_timeout;	/* poll() timeout, used in end of execution */
	pid_t child;		/* child pid */
	int childstatus;	/* child process exit value */
	struct termios attrs;	/* slave terminal runtime attributes */
	struct winsize win;	/* terminal window size */
#if !HAVE_LIBUTIL || !HAVE_PTY_H
	char *line;		/* terminal line */
#endif
	unsigned int
	 append:1,		/* append output */
	 rc_wanted:1,		/* return child exit value */
	 flush:1,		/* flush after each write */
	 quiet:1,		/* suppress most output */
	 timing:1,		/* include timing file */
	 force:1,		/* write output to links */
	 isterm:1,		/* is child process running as terminal */
	 die:1;			/* terminate program */

	sigset_t sigset;	/* catch SIGCHLD and SIGWINCH with signalfd() */
	sigset_t sigorg;	/* original signal mask */
	int sigfd;		/* file descriptor for signalfd() */
};

static void script_init_debug(void)
{
	__UL_INIT_DEBUG_FROM_ENV(script, SCRIPT_DEBUG_, 0, SCRIPT_DEBUG);
}

/*
 * For tests we want to be able to control time output
 */
#ifdef TEST_SCRIPT
static inline time_t script_time(time_t *t)
{
	const char *str = getenv("SCRIPT_TEST_SECOND_SINCE_EPOCH");
	int64_t sec;

	if (!str || sscanf(str, "%"SCNi64, &sec) != 1)
		return time(t);
	if (t)
		*t = (time_t)sec;
	return (time_t)sec;
}
#else	/* !TEST_SCRIPT */
# define script_time(x) time(x)
#endif

static void __attribute__((__noreturn__)) usage(void)
{
	FILE *out = stdout;
	fputs(USAGE_HEADER, out);
	fprintf(out, _(" %s [options] [file]\n"), program_invocation_short_name);

	fputs(USAGE_SEPARATOR, out);
	fputs(_("Make a typescript of a terminal session.\n"), out);

	fputs(USAGE_OPTIONS, out);
	fputs(_(" -a, --append                  append the output\n"
		" -c, --command <command>       run command rather than interactive shell\n"
		" -e, --return                  return exit code of the child process\n"
		" -f, --flush                   run flush after each write\n"
		"     --force                   use output file even when it is a link\n"
		" -o, --output-limit <size>     terminate if output files exceed size\n"
		" -q, --quiet                   be quiet\n"
		" -t[<file>], --timing[=<file>] output timing data to stderr or to FILE\n"
		), out);
	printf(USAGE_HELP_OPTIONS(31));

	printf(USAGE_MAN_TAIL("script(1)"));
	exit(EXIT_SUCCESS);
}

static void typescript_message_start(const struct script_control *ctl, time_t *tvec)
{
	char buf[FORMAT_TIMESTAMP_MAX];
	int cols = 0, lines = 0;
	const char *tty = NULL, *term = NULL;

	if (!ctl->typescriptfp)
		return;

	strtime_iso(tvec, ISO_TIMESTAMP, buf, sizeof(buf));

	fprintf(ctl->typescriptfp, _("Script started on %s ["), buf);

	if (ctl->isterm) {
		get_terminal_dimension(&cols, &lines);
		get_terminal_name(&tty, NULL, NULL);
		get_terminal_type(&term);

		if (term)
			fprintf(ctl->typescriptfp, "TERM=\"%s\" ", term);
		if (tty)
			fprintf(ctl->typescriptfp, "TTY=\"%s\" ", tty);

		fprintf(ctl->typescriptfp, "COLUMNS=\"%d\" LINES=\"%d\"", cols, lines);
	} else
		fprintf(ctl->typescriptfp, _("<not executed on terminal>"));

	fputs("]\n", ctl->typescriptfp);
}

static void typescript_message_done(const struct script_control *ctl, int status, const char *msg)
{
	char buf[FORMAT_TIMESTAMP_MAX];
	time_t tvec;

	if (!ctl->typescriptfp)
		return;

	tvec = script_time((time_t *)NULL);

	strtime_iso(&tvec, ISO_TIMESTAMP, buf, sizeof(buf));

	if (msg)
		fprintf(ctl->typescriptfp, _("\nScript done on %s [<%s>]\n"), buf, msg);
	else
		fprintf(ctl->typescriptfp, _("\nScript done on %s [COMMAND_EXIT_CODE=\"%d\"]\n"), buf, status);
}

static void die_if_link(const struct script_control *ctl)
{
	struct stat s;

	if (ctl->force)
		return;
	if (lstat(ctl->fname, &s) == 0 && (S_ISLNK(s.st_mode) || s.st_nlink > 1))
		errx(EXIT_FAILURE,
		     _("output file `%s' is a link\n"
		       "Use --force if you really want to use it.\n"
		       "Program not started."), ctl->fname);
}

static void restore_tty(struct script_control *ctl, int mode)
{
	struct termios rtt;

	if (!ctl->isterm)
		return;

	rtt = ctl->attrs;
	tcsetattr(STDIN_FILENO, mode, &rtt);
}

static void enable_rawmode_tty(struct script_control *ctl)
{
	struct termios rtt;

	if (!ctl->isterm)
		return;

	rtt = ctl->attrs;
	cfmakeraw(&rtt);
	rtt.c_lflag &= ~ECHO;
	tcsetattr(STDIN_FILENO, TCSANOW, &rtt);
}

static void __attribute__((__noreturn__)) done_log(struct script_control *ctl, const char *log_msg)
{
	int childstatus;

	DBG(MISC, ul_debug("done!"));

	restore_tty(ctl, TCSADRAIN);

	if (WIFSIGNALED(ctl->childstatus))
		childstatus = WTERMSIG(ctl->childstatus) + 0x80;
	else
		childstatus = WEXITSTATUS(ctl->childstatus);

	if (ctl->typescriptfp) {
		typescript_message_done(ctl, childstatus, log_msg);
		if (!ctl->quiet)
			printf(_("Script done, file is %s\n"), ctl->fname);
	}
#ifdef HAVE_LIBUTEMPTER
	if (ctl->master >= 0)
		utempter_remove_record(ctl->master);
#endif
	kill(ctl->child, SIGTERM);	/* make sure we don't create orphans */

	if (ctl->timingfp && close_stream(ctl->timingfp) != 0)
		err(EXIT_FAILURE, "write failed: %s", ctl->tname);
	if (ctl->typescriptfp && close_stream(ctl->typescriptfp) != 0)
		err(EXIT_FAILURE, "write failed: %s", ctl->fname);

	exit(ctl->rc_wanted ? childstatus : EXIT_SUCCESS);
}

static void __attribute__((__noreturn__)) done(struct script_control *ctl)
{
	done_log(ctl, NULL);
}

static void __attribute__((__noreturn__)) fail(struct script_control *ctl)
{
	DBG(MISC, ul_debug("fail!"));
	kill(0, SIGTERM);
	done(ctl);
}

static void wait_for_child(struct script_control *ctl, int wait)
{
	int status;
	pid_t pid;
	int options = wait ? 0 : WNOHANG;

	DBG(MISC, ul_debug("waiting for child"));

	while ((pid = wait3(&status, options, NULL)) > 0)
		if (pid == ctl->child)
			ctl->childstatus = status;
}

static void write_output(struct script_control *ctl, char *obuf,
			    ssize_t bytes)
{
	int timing_bytes = 0;

	DBG(IO, ul_debug(" writing output"));

	if (ctl->timing && ctl->timingfp) {
		struct timeval now, delta;

		DBG(IO, ul_debug("  writing timing info"));

		gettime_monotonic(&now);
		timersub(&now, &ctl->oldtime, &delta);
		timing_bytes = fprintf(ctl->timingfp, "%ld.%06ld %zd\n",
		        (long)delta.tv_sec, (long)delta.tv_usec, bytes);
		if (ctl->flush)
			fflush(ctl->timingfp);
		ctl->oldtime = now;
		if (timing_bytes < 0)
			timing_bytes = 0;
	}

	DBG(IO, ul_debug("  writing to script file"));

	if (fwrite_all(obuf, 1, bytes, ctl->typescriptfp)) {
		warn(_("cannot write script file"));
		fail(ctl);
	}
	if (ctl->flush)
		fflush(ctl->typescriptfp);

	DBG(IO, ul_debug("  writing to output"));

	if (write_all(STDOUT_FILENO, obuf, bytes)) {
		DBG(IO, ul_debug("  writing output *failed*"));
		warn(_("write failed"));
		fail(ctl);
	}

	if (ctl->maxsz != 0)
		ctl->outsz += bytes + timing_bytes;

	DBG(IO, ul_debug("  writing output *done*"));
}

static int write_to_shell(struct script_control *ctl,
			  char *buf, size_t bufsz)
{
	return write_all(ctl->master, buf, bufsz);
}

/*
 * The script(1) is usually faster than shell, so it's a good idea to wait until
 * the previous message has been already read by shell from slave before we
 * write to master. This is necessary especially for EOF situation when we can
 * send EOF to master before shell is fully initialized, to workaround this
 * problem we wait until slave is empty. For example:
 *
 *   echo "date" | script
 *
 * Unfortunately, the child (usually shell) can ignore stdin at all, so we
 * don't wait forever to avoid dead locks...
 *
 * Note that script is primarily designed for interactive sessions as it
 * maintains master+slave tty stuff within the session. Use pipe to write to
 * script(1) and assume non-interactive (tee-like) behavior is NOT well
 * supported.
 */
static void write_eof_to_shell(struct script_control *ctl)
{
	unsigned int tries = 0;
	struct pollfd fds[] = {
	           { .fd = ctl->slave, .events = POLLIN }
	};
	char c = DEF_EOF;

	DBG(IO, ul_debug(" waiting for empty slave"));
	while (poll(fds, 1, 10) == 1 && tries < 8) {
		DBG(IO, ul_debug("   slave is not empty"));
		xusleep(250000);
		tries++;
	}
	if (tries < 8)
		DBG(IO, ul_debug("   slave is empty now"));

	DBG(IO, ul_debug(" sending EOF to master"));
	write_to_shell(ctl, &c, sizeof(char));
}

static void handle_io(struct script_control *ctl, int fd, int *eof)
{
	char buf[BUFSIZ];
	ssize_t bytes;
	DBG(IO, ul_debug("%d FD active", fd));
	*eof = 0;

	/* read from active FD */
	bytes = read(fd, buf, sizeof(buf));
	if (bytes < 0) {
		if (errno == EAGAIN || errno == EINTR)
			return;
		fail(ctl);
	}

	if (bytes == 0) {
		*eof = 1;
		return;
	}

	/* from stdin (user) to command */
	if (fd == STDIN_FILENO) {
		DBG(IO, ul_debug(" stdin --> master %zd bytes", bytes));

		if (write_to_shell(ctl, buf, bytes)) {
			warn(_("write failed"));
			fail(ctl);
		}
		/* without sync write_output() will write both input &
		 * shell output that looks like double echoing */
		fdatasync(ctl->master);

	/* from command (master) to stdout and log */
	} else if (fd == ctl->master) {
		DBG(IO, ul_debug(" master --> stdout %zd bytes", bytes));
		write_output(ctl, buf, bytes);

		/* check output limit */
		if (ctl->maxsz != 0 && ctl->outsz >= ctl->maxsz) {
			if (!ctl->quiet)
				printf(_("Script terminated, max output file size %"PRIu64" exceeded.\n"), ctl->maxsz);
			DBG(IO, ul_debug("output size %"PRIu64", exceeded limit %"PRIu64, ctl->outsz, ctl->maxsz));
			done_log(ctl, _("max output size exceeded"));
		}
	}
}

static void handle_signal(struct script_control *ctl, int fd)
{
	struct signalfd_siginfo info;
	ssize_t bytes;

	DBG(SIGNAL, ul_debug("signal FD %d active", fd));

	bytes = read(fd, &info, sizeof(info));
	if (bytes != sizeof(info)) {
		if (bytes < 0 && (errno == EAGAIN || errno == EINTR))
			return;
		fail(ctl);
	}

	switch (info.ssi_signo) {
	case SIGCHLD:
		DBG(SIGNAL, ul_debug(" get signal SIGCHLD [ssi_code=%d, ssi_status=%d]",
							info.ssi_code, info.ssi_status));
		if (info.ssi_code == CLD_EXITED
		    || info.ssi_code == CLD_KILLED
		    || info.ssi_code == CLD_DUMPED) {
			wait_for_child(ctl, 0);
			ctl->poll_timeout = 10;

		/* In case of ssi_code is CLD_TRAPPED, CLD_STOPPED, or CLD_CONTINUED */
		} else if (info.ssi_status == SIGSTOP && ctl->child) {
			DBG(SIGNAL, ul_debug(" child stop by SIGSTOP -- stop parent too"));
			kill(getpid(), SIGSTOP);
			DBG(SIGNAL, ul_debug(" resume"));
			kill(ctl->child, SIGCONT);
		}
		return;
	case SIGWINCH:
		DBG(SIGNAL, ul_debug(" get signal SIGWINCH"));
		if (ctl->isterm) {
			ioctl(STDIN_FILENO, TIOCGWINSZ, (char *)&ctl->win);
			ioctl(ctl->slave, TIOCSWINSZ, (char *)&ctl->win);
		}
		break;
	case SIGTERM:
		/* fallthrough */
	case SIGINT:
		/* fallthrough */
	case SIGQUIT:
		DBG(SIGNAL, ul_debug(" get signal SIG{TERM,INT,QUIT}"));
		fprintf(stderr, _("\nSession terminated.\n"));
		/* Child termination is going to generate SIGCHILD (see above) */
		kill(ctl->child, SIGTERM);
		return;
	default:
		abort();
	}
	DBG(SIGNAL, ul_debug("signal handle on FD %d done", fd));
}

static void do_io(struct script_control *ctl)
{
	int ret, eof = 0;
	time_t tvec = script_time((time_t *)NULL);
	enum {
		POLLFD_SIGNAL = 0,
		POLLFD_MASTER,
		POLLFD_STDIN

	};
	struct pollfd pfd[] = {
		[POLLFD_SIGNAL] = { .fd = ctl->sigfd,   .events = POLLIN | POLLERR | POLLHUP },
		[POLLFD_MASTER] = { .fd = ctl->master,  .events = POLLIN | POLLERR | POLLHUP },
		[POLLFD_STDIN]	= { .fd = STDIN_FILENO, .events = POLLIN | POLLERR | POLLHUP }
	};


	if ((ctl->typescriptfp =
	     fopen(ctl->fname, ctl->append ? "a" UL_CLOEXECSTR : "w" UL_CLOEXECSTR)) == NULL) {

		restore_tty(ctl, TCSANOW);
		warn(_("cannot open %s"), ctl->fname);
		fail(ctl);
	}
	if (ctl->timing) {
		const char *tname = ctl->tname ? ctl->tname : "/dev/stderr";

		if (!(ctl->timingfp = fopen(tname, "w" UL_CLOEXECSTR))) {
			restore_tty(ctl, TCSANOW);
			warn(_("cannot open %s"), tname);
			fail(ctl);
		}
	}


	if (ctl->typescriptfp)
		typescript_message_start(ctl, &tvec);

	gettime_monotonic(&ctl->oldtime);

	while (!ctl->die) {
		size_t i;
		int errsv;

		DBG(POLL, ul_debug("calling poll()"));

		/* wait for input or signal */
		ret = poll(pfd, ARRAY_SIZE(pfd), ctl->poll_timeout);
		errsv = errno;
		DBG(POLL, ul_debug("poll() rc=%d", ret));

		if (ret < 0) {
			if (errsv == EAGAIN)
				continue;
			warn(_("poll failed"));
			fail(ctl);
		}
		if (ret == 0) {
			DBG(POLL, ul_debug("setting die=1"));
			ctl->die = 1;
			break;
		}

		for (i = 0; i < ARRAY_SIZE(pfd); i++) {
			if (pfd[i].revents == 0)
				continue;

			DBG(POLL, ul_debug(" active pfd[%s].fd=%d %s %s %s",
						i == POLLFD_STDIN  ? "stdin" :
						i == POLLFD_MASTER ? "master" :
						i == POLLFD_SIGNAL ? "signal" : "???",
						pfd[i].fd,
						pfd[i].revents & POLLIN  ? "POLLIN" : "",
						pfd[i].revents & POLLHUP ? "POLLHUP" : "",
						pfd[i].revents & POLLERR ? "POLLERR" : ""));
			switch (i) {
			case POLLFD_STDIN:
			case POLLFD_MASTER:
				/* data */
				if (pfd[i].revents & POLLIN)
					handle_io(ctl, pfd[i].fd, &eof);
				/* EOF maybe detected by two ways:
				 *	A) poll() return POLLHUP event after close()
				 *	B) read() returns 0 (no data) */
				if ((pfd[i].revents & POLLHUP) || eof) {
					DBG(POLL, ul_debug(" ignore FD"));
					pfd[i].fd = -1;
					if (i == POLLFD_STDIN) {
						write_eof_to_shell(ctl);
						DBG(POLL, ul_debug("  ignore STDIN"));
					}
				}
				continue;
			case POLLFD_SIGNAL:
				handle_signal(ctl, pfd[i].fd);
				break;
			}
		}
	}

	DBG(POLL, ul_debug("poll() done"));

	if (!ctl->die)
		wait_for_child(ctl, 1);

	done(ctl);
}

static void getslave(struct script_control *ctl)
{
#ifndef HAVE_LIBUTIL
	ctl->line[strlen("/dev/")] = 't';
	ctl->slave = open(ctl->line, O_RDWR | O_CLOEXEC);
	if (ctl->slave < 0) {
		warn(_("cannot open %s"), ctl->line);
		fail(ctl);
	}
	if (ctl->isterm) {
		tcsetattr(ctl->slave, TCSANOW, &ctl->attrs);
		ioctl(ctl->slave, TIOCSWINSZ, (char *)&ctl->win);
	}
#endif
	setsid();
	ioctl(ctl->slave, TIOCSCTTY, 0);
}

/* don't use DBG() stuff here otherwise it will be in  the typescript file */
static void __attribute__((__noreturn__)) do_shell(struct script_control *ctl)
{
	char *shname;

	getslave(ctl);

	/* close things irrelevant for this process */
	close(ctl->master);
	close(ctl->sigfd);

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

	sigprocmask(SIG_SETMASK, &ctl->sigorg, NULL);

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
		if (ctl->command)
			execl(ctl->shell, shname, "-c", ctl->command, NULL);
		else
			execl(ctl->shell, shname, "-i", NULL);
	} else {
		if (ctl->command)
			execlp(shname, "-c", ctl->command, NULL);
		else
			execlp(shname, "-i", NULL);
	}
	warn(_("failed to execute %s"), ctl->shell);
	fail(ctl);
}


static void getmaster(struct script_control *ctl)
{
#if defined(HAVE_LIBUTIL) && defined(HAVE_PTY_H)
	int rc;

	ctl->isterm = isatty(STDIN_FILENO);

	if (ctl->isterm) {
		if (tcgetattr(STDIN_FILENO, &ctl->attrs) != 0)
			err(EXIT_FAILURE, _("failed to get terminal attributes"));
		ioctl(STDIN_FILENO, TIOCGWINSZ, (char *)&ctl->win);
		rc = openpty(&ctl->master, &ctl->slave, NULL, &ctl->attrs, &ctl->win);
	} else
		rc = openpty(&ctl->master, &ctl->slave, NULL, NULL, NULL);

	if (rc < 0) {
		warn(_("openpty failed"));
		fail(ctl);
	}
#else
	char *pty, *bank, *cp;

	ctl->isterm = isatty(STDIN_FILENO);

	pty = &ctl->line[strlen("/dev/ptyp")];
	for (bank = "pqrs"; *bank; bank++) {
		ctl->line[strlen("/dev/pty")] = *bank;
		*pty = '0';
		if (access(ctl->line, F_OK) != 0)
			break;
		for (cp = "0123456789abcdef"; *cp; cp++) {
			*pty = *cp;
			ctl->master = open(ctl->line, O_RDWR | O_CLOEXEC);
			if (ctl->master >= 0) {
				char *tp = &ctl->line[strlen("/dev/")];
				int ok;

				/* verify slave side is usable */
				*tp = 't';
				ok = access(ctl->line, R_OK | W_OK) == 0;
				*tp = 'p';
				if (ok) {
					if (ctl->isterm) {
						tcgetattr(STDIN_FILENO, &ctl->attrs);
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

	DBG(IO, ul_debug("master fd: %d", ctl->master));
}

int main(int argc, char **argv)
{
	struct script_control ctl = {
#if !HAVE_LIBUTIL || !HAVE_PTY_H
		.line = "/dev/ptyXX",
#endif
		.master = -1,
		.poll_timeout = -1
	};
	int ch;

	enum { FORCE_OPTION = CHAR_MAX + 1 };

	static const struct option longopts[] = {
		{"append", no_argument, NULL, 'a'},
		{"command", required_argument, NULL, 'c'},
		{"return", no_argument, NULL, 'e'},
		{"flush", no_argument, NULL, 'f'},
		{"force", no_argument, NULL, FORCE_OPTION,},
		{"output-limit", required_argument, NULL, 'o'},
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

	script_init_debug();

	while ((ch = getopt_long(argc, argv, "ac:efo:qt::Vh", longopts, NULL)) != -1)
		switch (ch) {
		case 'a':
			ctl.append = 1;
			break;
		case 'c':
			ctl.command = optarg;
			break;
		case 'e':
			ctl.rc_wanted = 1;
			break;
		case 'f':
			ctl.flush = 1;
			break;
		case FORCE_OPTION:
			ctl.force = 1;
			break;
		case 'o':
			ctl.maxsz = strtosize_or_err(optarg, _("failed to parse output limit size"));
			break;
		case 'q':
			ctl.quiet = 1;
			break;
		case 't':
			if (optarg)
				ctl.tname = optarg;
			ctl.timing = 1;
			break;
		case 'V':
			printf(UTIL_LINUX_VERSION);
			exit(EXIT_SUCCESS);
			break;
		case 'h':
			usage();
			break;
		default:
			errtryhelp(EXIT_FAILURE);
		}
	argc -= optind;
	argv += optind;

	if (argc > 0)
		ctl.fname = argv[0];
	else {
		ctl.fname = DEFAULT_TYPESCRIPT_FILENAME;
		die_if_link(&ctl);
	}

	ctl.shell = getenv("SHELL");
	if (ctl.shell == NULL)
		ctl.shell = _PATH_BSHELL;

	getmaster(&ctl);
	if (!ctl.quiet)
		printf(_("Script started, file is %s\n"), ctl.fname);
	enable_rawmode_tty(&ctl);

#ifdef HAVE_LIBUTEMPTER
	utempter_add_record(ctl.master, NULL);
#endif
	/* setup signal handler */
	sigemptyset(&ctl.sigset);
	sigaddset(&ctl.sigset, SIGCHLD);
	sigaddset(&ctl.sigset, SIGWINCH);
	sigaddset(&ctl.sigset, SIGTERM);
	sigaddset(&ctl.sigset, SIGINT);
	sigaddset(&ctl.sigset, SIGQUIT);

	/* block signals used for signalfd() to prevent the signals being
	 * handled according to their default dispositions */
	sigprocmask(SIG_BLOCK, &ctl.sigset, &ctl.sigorg);

	if ((ctl.sigfd = signalfd(-1, &ctl.sigset, SFD_CLOEXEC)) < 0)
		err(EXIT_FAILURE, _("cannot set signal handler"));

	DBG(SIGNAL, ul_debug("signal fd=%d", ctl.sigfd));

	fflush(stdout);
	ctl.child = fork();

	switch (ctl.child) {
	case -1: /* error */
		warn(_("fork failed"));
		fail(&ctl);
		break;
	case 0: /* child */
		do_shell(&ctl);
		break;
	default: /* parent */
		do_io(&ctl);
		break;
	}

	/* should not happen, all used functions are non-return */
	return EXIT_FAILURE;
}
