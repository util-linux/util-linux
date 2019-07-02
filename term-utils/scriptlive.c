/*
 * Copyright (C) 2019, Karel Zak <kzak@redhat.com>
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This file is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <limits.h>
#include <math.h>
#include <sys/select.h>
#include <unistd.h>
#include <getopt.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#include <paths.h>

#include "c.h"
#include "xalloc.h"
#include "closestream.h"
#include "nls.h"
#include "strutils.h"
#include "optutils.h"
#include "script-playutils.h"
#include "rpmatch.h"


#define SCRIPT_MIN_DELAY 0.0001		/* from original sripreplay.pl */

static void __attribute__((__noreturn__))
usage(void)
{
	FILE *out = stdout;
	fputs(USAGE_HEADER, out);
	fprintf(out,
	      _(" %s [options]\n"),
	      program_invocation_short_name);
	fprintf(out,
	      _(" %s [-t] timingfile [-I|-B] typescript\n"),
	      program_invocation_short_name);

	fputs(USAGE_SEPARATOR, out);
	fputs(_("Execute terminal typescript.\n"), out);

	fputs(USAGE_OPTIONS, out);
	fputs(_(" -t, --timing <file>     script timing log file\n"), out);
	fputs(_(" -I, --log-in <file>     script stdin log file\n"), out);
	fputs(_(" -B, --log-io <file>     script stdin and stdout log file\n"), out);

	fputs(USAGE_SEPARATOR, out);
	fputs(_(" -d, --divisor <num>     speed up or slow down execution with time divisor\n"), out);
	fputs(_(" -m, --maxdelay <num>    wait at most this many seconds between updates\n"), out);
	printf(USAGE_HELP_OPTIONS(25));

	printf(USAGE_MAN_TAIL("scriptlive(1)"));
	exit(EXIT_SUCCESS);
}

static double
getnum(const char *s)
{
	const double d = strtod_or_err(s, _("failed to parse number"));

	if (isnan(d)) {
		errno = EINVAL;
		err(EXIT_FAILURE, "%s: %s", _("failed to parse number"), s);
	}
	return d;
}

static void
delay_for(double delay)
{
#ifdef HAVE_NANOSLEEP
	struct timespec ts, remainder;
	ts.tv_sec = (time_t) delay;
	ts.tv_nsec = (delay - ts.tv_sec) * 1.0e9;

	DBG(TIMING, ul_debug("going to sleep for %fs", delay));

	while (-1 == nanosleep(&ts, &remainder)) {
		if (EINTR == errno)
			ts = remainder;
		else
			break;
	}
#else
	struct timeval tv;
	tv.tv_sec = (long) delay;
	tv.tv_usec = (delay - tv.tv_sec) * 1.0e6;
	select(0, NULL, NULL, NULL, &tv);
#endif
}

static int start_shell(const char *shell, pid_t *shell_pid, int *shell_fd)
{
	const char *shname;
	int fds[2];

	assert(shell_pid);
	assert(shell_fd);

	if (pipe(fds) < 0)
		err(EXIT_FAILURE, _("pipe failed"));

	*shell_pid  = fork();

	if (*shell_pid == -1)
		err(EXIT_FAILURE, _("fork failed"));
	if (*shell_pid != 0) {
		/* parent */
		*shell_fd = fds[1];
		close(fds[0]);
		return -errno;
	}

	/* child */
	shname = strrchr(shell, '/');
	if (shname)
		shname++;
	else
		shname = shell;

	dup2(fds[0], STDIN_FILENO);
	close(fds[0]);
	close(fds[1]);

	execl(shell, shname, "-i", NULL);
	errexec(shell);
}

int
main(int argc, char *argv[])
{
	struct replay_setup *setup = NULL;
	struct replay_step *step = NULL;
	const char *log_in = NULL,
		   *log_io = NULL,
		   *log_tm = NULL,
		   *shell;
	double divi = 1, maxdelay = 0;
	int diviopt = FALSE, maxdelayopt = FALSE, idx;
	int ch, rc;
	int shell_fd;
	pid_t shell_pid;
	struct termios attrs;

	static const struct option longopts[] = {
		{ "timing",	required_argument,	0, 't' },
		{ "log-in",     required_argument,      0, 'I'},
		{ "log-io",     required_argument,      0, 'B'},
		{ "divisor",	required_argument,	0, 'd' },
		{ "maxdelay",	required_argument,	0, 'm' },
		{ "version",	no_argument,		0, 'V' },
		{ "help",	no_argument,		0, 'h' },
		{ NULL,		0, 0, 0 }
	};
	static const ul_excl_t excl[] = {       /* rows and cols in ASCII order */
		{ 'B', 'I' },
		{ 0 }
	};
	int excl_st[ARRAY_SIZE(excl)] = UL_EXCL_STATUS_INIT;
	/* Because we use space as a separator, we can't afford to use any
	 * locale which tolerates a space in a number.  In any case, script.c
	 * sets the LC_NUMERIC locale to C, anyway.
	 */
	setlocale(LC_ALL, "");
	setlocale(LC_NUMERIC, "C");

	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	close_stdout_atexit();

	replay_init_debug();

	while ((ch = getopt_long(argc, argv, "B:I:t:d:m:Vh", longopts, NULL)) != -1) {

		err_exclusive_options(ch, longopts, excl, excl_st);

		switch(ch) {
		case 't':
			log_tm = optarg;
			break;
		case 'I':
			log_in = optarg;
			break;
		case 'B':
			log_io = optarg;
			break;
		case 'd':
			diviopt = TRUE;
			divi = getnum(optarg);
			break;
		case 'm':
			maxdelayopt = TRUE;
			maxdelay = getnum(optarg);
			break;
		case 'V':
			print_version(EXIT_SUCCESS);
		case 'h':
			usage();
		default:
			errtryhelp(EXIT_FAILURE);
		}
	}
	argc -= optind;
	argv += optind;
	idx = 0;

	if (!isatty(STDIN_FILENO))
		errx(EXIT_FAILURE, _("stdin is not terminal"));

	if (!log_tm && idx < argc)
		log_tm = argv[idx++];
	if (!log_in && !log_io && idx < argc)
		log_in = argv[idx++];

	if (!diviopt)
		divi = idx < argc ? getnum(argv[idx]) : 1;
	if (maxdelay < 0)
		maxdelay = 0;

	if (!log_tm)
		errx(EXIT_FAILURE, _("timing file not specified"));
	if (!(log_in || log_io))
		errx(EXIT_FAILURE, _("stdin typescript file not specified"));

	setup = replay_new_setup();

	if (replay_set_timing_file(setup, log_tm) != 0)
		err(EXIT_FAILURE, _("cannot open %s"), log_tm);

	if (log_in && replay_associate_log(setup, "I", log_in) != 0)
		err(EXIT_FAILURE, _("cannot open %s"), log_in);

	if (log_io && replay_associate_log(setup, "IO", log_io) != 0)
		err(EXIT_FAILURE, _("cannot open %s"), log_io);

	replay_set_default_type(setup, 'I');
	replay_set_crmode(setup, REPLAY_CRMODE_AUTO);

	shell = getenv("SHELL");
	if (shell == NULL)
		shell = _PATH_BSHELL;

	fprintf(stdout, _(">>> scriptlive: Starting your typescript execution by %s. <<<\n"), shell);

	tcgetattr(STDIN_FILENO, &attrs);
	start_shell(shell, &shell_pid, &shell_fd);

	do {
		double delay;

		rc = replay_get_next_step(setup, "I", &step);
		if (rc)
			break;

		delay = replay_step_get_delay(step);
		delay /= divi;

		if (maxdelayopt && delay > maxdelay)
			delay = maxdelay;
		if (delay > SCRIPT_MIN_DELAY)
			delay_for(delay);

		rc = replay_emit_step_data(setup, step, shell_fd);
	} while (rc == 0);

	kill(shell_pid, SIGTERM);
	waitpid(shell_pid, 0, 0);
	tcsetattr(STDIN_FILENO, TCSADRAIN, &attrs);

	if (step && rc < 0)
		err(EXIT_FAILURE, _("%s: log file error"), replay_step_get_filename(step));
	else if (rc < 0)
		err(EXIT_FAILURE, _("%s: line %d: timing file error"),
				replay_get_timing_file(setup),
				replay_get_timing_line(setup));


	fprintf(stdout, _(">>> scriptlive: Done. <<<\n"));

	exit(EXIT_SUCCESS);
}
