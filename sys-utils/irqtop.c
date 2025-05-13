/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * irqtop.c - utility to display kernel interrupt information.
 *
 * Copyright (C) 2019 zhenwei pi <pizhenwei@bytedance.com>
 * Copyright (C) 2020 Karel Zak <kzak@redhat.com>
 * Copyright (C) 2024 Robin Jarry <robin@jarry.cc>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */
#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <locale.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/signalfd.h>
#include <sys/time.h>
#include <sys/timerfd.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

#ifdef HAVE_SLCURSES_H
# include <slcurses.h>
#elif defined(HAVE_SLANG_SLCURSES_H)
# include <slang/slcurses.h>
#elif defined(HAVE_NCURSESW_NCURSES_H) && defined(HAVE_WIDECHAR)
# include <ncursesw/ncurses.h>
#elif defined(HAVE_NCURSES_H)
# include <ncurses.h>
#elif defined(HAVE_NCURSES_NCURSES_H)
# include <ncurses/ncurses.h>
#endif

#include <libsmartcols.h>

#include "c.h"
#include "widechar.h"
#include "closestream.h"
#include "cpuset.h"
#include "monotonic.h"
#include "pathnames.h"
#include "strutils.h"
#include "timeutils.h"
#include "ttyutils.h"
#include "xalloc.h"

#include "irq-common.h"

#define MAX_EVENTS	3

enum irqtop_cpustat_mode {
	IRQTOP_CPUSTAT_AUTO,
	IRQTOP_CPUSTAT_ENABLE,
	IRQTOP_CPUSTAT_DISABLE,
};

/* top control struct */
struct irqtop_ctl {
	WINDOW	*win;
	int	cols;
	int	rows;
	char	*hostname;

	struct itimerspec timer;
	struct irq_stat	*prev_stat;
	uintmax_t threshold;
	size_t setsize;
	cpu_set_t *cpuset;

	enum irqtop_cpustat_mode cpustat_mode;
	int64_t	iter;
	bool	batch;
	bool	request_exit,
		softirq;
};

static inline int irqtop_printf(struct irqtop_ctl *ctl, const char *fmt, ...)
{
	int ret = 0;
	va_list args;

	if (!ctl)
		return -1;

	va_start(args, fmt);
	if (ctl->batch)
		ret = vprintf(fmt, args);
	else
		ret = vw_printw(ctl->win, fmt, args);
	va_end(args);

	if (!ctl->batch && ret == OK)
		wrefresh(ctl->win);

	return ret;
}

/* user's input parser */
static void parse_input(struct irqtop_ctl *ctl, struct irq_output *out, char c)
{
	switch (c) {
	case 'q':
	case 'Q':
		ctl->request_exit = 1;
		break;
	default:
		set_sort_func_by_key(out, c);
		break;
	}
}

static int update_screen(struct irqtop_ctl *ctl, struct irq_output *out)
{
	struct libscols_table *table, *cpus = NULL;
	struct irq_stat *stat;
	time_t now = time(NULL);
	char timestr[64], *data, *data0, *p = NULL;
	char *input_file;

	/* make irqs table */
	if (ctl->softirq)
		input_file = xstrdup(_PATH_PROC_SOFTIRQS);
	else
		input_file = xstrdup(_PATH_PROC_INTERRUPTS);

	table = get_scols_table(input_file, out, ctl->prev_stat, &stat,
				ctl->softirq, ctl->threshold, ctl->setsize,
				ctl->cpuset);
	free(input_file);
	if (!table) {
		ctl->request_exit = 1;
		return 1;
	}
	scols_table_enable_maxout(table, 1);
	scols_table_enable_nowrap(table, 1);
	scols_table_reduce_termwidth(table, 1);

	/* make cpus table */
	if (ctl->cpustat_mode != IRQTOP_CPUSTAT_DISABLE) {
		cpus = get_scols_cpus_table(out, ctl->prev_stat, stat, ctl->setsize,
					    ctl->cpuset);
		scols_table_reduce_termwidth(cpus, 1);
		if (ctl->cpustat_mode == IRQTOP_CPUSTAT_AUTO)
			scols_table_enable_nowrap(cpus, 1);
	}

	strtime_iso(&now, ISO_TIMESTAMP, timestr, sizeof(timestr));
	if (!ctl->batch)
		move(0, 0);

	/* print header */
	irqtop_printf(ctl, _("irqtop | total: %ld delta: %ld | %s | %s\n\n"),
			   stat->total_irq, stat->delta_irq, ctl->hostname, timestr);


	/* print cpus table or not by -c option */
	if (cpus) {
		scols_print_table_to_string(cpus, &data);
		if (data && *data)
			irqtop_printf(ctl, "%s\n\n", data);
		free(data);
	}

	/* print irqs table */
	scols_print_table_to_string(table, &data0);
	data = data0;

	if (data && *data)
		p = strchr(data, '\n');
	if (p) {
		/* print header in reverse mode */
		*p = '\0';
		if (!ctl->batch)
			attron(A_REVERSE);
		irqtop_printf(ctl, "%s\n", data);
		if (!ctl->batch)
			attroff(A_REVERSE);
		data = p + 1;
	}

	if (data && *data)
		irqtop_printf(ctl, "%s\n\n", data);
	free(data0);

	/* clean up */
	scols_unref_table(table);
	if (ctl->prev_stat)
		free_irqstat(ctl->prev_stat);
	ctl->prev_stat = stat;

	if (ctl->iter > 0) {
		ctl->iter--;
		if (ctl->iter == 0)
			ctl->request_exit = 1;
	}
	return 0;
}

static int event_loop(struct irqtop_ctl *ctl, struct irq_output *out)
{
	int efd, sfd, tfd;
	sigset_t sigmask;
	struct signalfd_siginfo siginfo;
	struct epoll_event ev, events[MAX_EVENTS];
	long int nr;
	uint64_t unused;
	int retval = 0;

	efd = epoll_create1(0);

	if ((tfd = timerfd_create(CLOCK_MONOTONIC, 0)) < 0)
		err(EXIT_FAILURE, _("cannot create timerfd"));
	if (timerfd_settime(tfd, 0, &ctl->timer, NULL) != 0)
		err(EXIT_FAILURE, _("cannot set timerfd"));

	ev.events = EPOLLIN;
	ev.data.fd = tfd;
	if (epoll_ctl(efd, EPOLL_CTL_ADD, tfd, &ev) != 0)
		err(EXIT_FAILURE, _("epoll_ctl failed"));

	if (sigfillset(&sigmask) != 0)
		err(EXIT_FAILURE, _("sigfillset failed"));
	if (sigprocmask(SIG_BLOCK, &sigmask, NULL) != 0)
		err(EXIT_FAILURE, _("sigprocmask failed"));

	sigaddset(&sigmask, SIGWINCH);
	sigaddset(&sigmask, SIGTERM);
	sigaddset(&sigmask, SIGINT);
	sigaddset(&sigmask, SIGQUIT);

	if ((sfd = signalfd(-1, &sigmask, SFD_CLOEXEC)) < 0)
		err(EXIT_FAILURE, _("cannot create signalfd"));

	ev.events = EPOLLIN;
	ev.data.fd = sfd;
	if (epoll_ctl(efd, EPOLL_CTL_ADD, sfd, &ev) != 0)
		err(EXIT_FAILURE, _("epoll_ctl failed"));

	ev.events = EPOLLIN;
	ev.data.fd = STDIN_FILENO;
	if (epoll_ctl(efd, EPOLL_CTL_ADD, STDIN_FILENO, &ev) != 0)
		err(EXIT_FAILURE, _("epoll_ctl failed"));

	retval |= update_screen(ctl, out);
	if (!ctl->batch)
		refresh();

	while (!ctl->request_exit) {
		const ssize_t nr_events = epoll_wait(efd, events, MAX_EVENTS, -1);

		for (nr = 0; nr < nr_events; nr++) {
			if (events[nr].data.fd == tfd) {
				if (read(tfd, &unused, sizeof(unused)) < 0)
					warn(_("read failed"));
			} else if (events[nr].data.fd == sfd) {
				if (read(sfd, &siginfo, sizeof(siginfo)) < 0) {
					warn(_("read failed"));
					continue;
				}
				if (siginfo.ssi_signo == SIGWINCH) {
					if (!ctl->batch) {
						get_terminal_dimension(&ctl->cols, &ctl->rows);
#if HAVE_RESIZETERM
						resizeterm(ctl->rows, ctl->cols);
#endif
					}
				}
				else {
					ctl->request_exit = 1;
					break;
				}
			} else if (events[nr].data.fd == STDIN_FILENO) {
				char c;

				if (read(STDIN_FILENO, &c, 1) != 1)
					warn(_("read failed"));
				parse_input(ctl, out, c);
			} else
				abort();
			retval |= update_screen(ctl, out);
			if (!ctl->batch)
				refresh();
		}
	}
	return retval;
}

static void __attribute__((__noreturn__)) usage(void)
{
	fputs(USAGE_HEADER, stdout);
	printf(_(" %s [options]\n"), program_invocation_short_name);
	fputs(USAGE_SEPARATOR, stdout);

	puts(_("Display kernel interrupt information."));

	fputs(USAGE_OPTIONS, stdout);
	fputs(_(" -b, --batch            send tables to stdout, not to a static screen\n"), stdout);
	fputs(_(" -c, --cpu-stat <when>  whether to show the per-cpu stats (auto|never|always)\n"), stdout);
	fputs(_(" -C, --cpu-list <list>  show IRQs only for the specified cpus\n"), stdout);
	fputs(_(" -d, --delay <secs>     wait this number of seconds between updates\n"), stdout);
	fputs(_(" -J, --json             use JSON output format (implies --batch)\n"), stdout);
	fputs(_(" -n, --iter <number>    the maximum number of iterations\n"), stdout);
	fputs(_(" -o, --output <list>    which columns to show, and in which order\n"), stdout);
	fputs(_(" -s, --sort <column>    sort the table on this column\n"), stdout);
	fputs(_(" -S, --softirq          show softirqs instead of interrupts\n"), stdout);
	fputs(_(" -t, --threshold <num>  show only IRQs with counters above this number\n"), stdout);
	fputs(USAGE_SEPARATOR, stdout);
	fprintf(stdout, USAGE_HELP_OPTIONS(24));

	fputs(_("\nThe following interactive key commands are valid:\n"), stdout);
	fputs(_("  i      sort by IRQ\n"), stdout);
	fputs(_("  t      sort by TOTAL\n"), stdout);
	fputs(_("  d      sort by DELTA\n"), stdout);
	fputs(_("  n      sort by NAME\n"), stdout);
	fputs(_("  q      quit program\n"), stdout);

	fputs(USAGE_COLUMNS, stdout);
	irq_print_columns(stdout, 0);

	fprintf(stdout, USAGE_MAN_TAIL("irqtop(1)"));
	exit(EXIT_SUCCESS);
}

static void parse_args(	struct irqtop_ctl *ctl,
			struct irq_output *out,
			int argc,
			char **argv)
{
	const char *outarg = NULL;
	static const struct option longopts[] = {
		{"batch", no_argument, NULL, 'b'},
		{"cpu-stat", required_argument, NULL, 'c'},
		{"cpu-list", required_argument, NULL, 'C'},
		{"delay", required_argument, NULL, 'd'},
		{"iter", required_argument, NULL, 'n'},
		{"json", no_argument, NULL, 'J'},
		{"sort", required_argument, NULL, 's'},
		{"output", required_argument, NULL, 'o'},
		{"softirq", no_argument, NULL, 'S'},
		{"threshold", required_argument, NULL, 't'},
		{"help", no_argument, NULL, 'h'},
		{"version", no_argument, NULL, 'V'},
		{NULL, 0, NULL, 0}
	};
	int o;

	while ((o = getopt_long(argc, argv, "bc:C:d:Jn:o:s:St:hV", longopts, NULL)) != -1) {
		switch (o) {
		case 'b':
			ctl->batch = 1;
			break;
		case 'c':
			if (!strcmp(optarg, "auto"))
				ctl->cpustat_mode = IRQTOP_CPUSTAT_AUTO;
			else
				ctl->cpustat_mode = IRQTOP_CPUSTAT_DISABLE - ul_parse_switch(optarg,
							"always", "never", "enable", "disable",
							"on", "off", "yes", "no", "1", "0", NULL);
			break;
		case 'C':
			{
				int ncpus = get_max_number_of_cpus();
				if (ncpus <= 0)
					errx(EXIT_FAILURE, _("cannot determine NR_CPUS; aborting"));

				ctl->cpuset = cpuset_alloc(ncpus, &ctl->setsize, NULL);
				if (!ctl->cpuset)
					err(EXIT_FAILURE, _("cpuset_alloc failed"));

				if (cpulist_parse(optarg, ctl->cpuset, ctl->setsize, 0))
					errx(EXIT_FAILURE, _("failed to parse CPU list: %s"),
						optarg);
			}
			break;
		case 'd':
			{
				struct timeval delay;

				strtotimeval_or_err(optarg, &delay,
						    _("failed to parse delay argument"));
				TIMEVAL_TO_TIMESPEC(&delay, &ctl->timer.it_interval);
				ctl->timer.it_value = ctl->timer.it_interval;
			}
			break;
		case 'J':
			out->json = 1;
			ctl->batch = 1;
			break;
		case 'n':
			ctl->iter = str2num_or_err(optarg, 10,
					_("failed to parse iter argument"),
					0, INT_MAX);
			break;
		case 's':
			set_sort_func_by_name(out, optarg);
			break;
		case 'o':
			outarg = optarg;
			break;
		case 'S':
			ctl->softirq = 1;
			break;
		case 't':
			ctl->threshold = strtosize_or_err(optarg, _("error: --threshold"));
			break;
		case 'V':
			print_version(EXIT_SUCCESS);
		case 'h':
			usage();
		default:
			errtryhelp(EXIT_FAILURE);
		}
	}

	/* default */
	if (!out->ncolumns) {
		out->columns[out->ncolumns++] = COL_IRQ;
		out->columns[out->ncolumns++] = COL_TOTAL;
		out->columns[out->ncolumns++] = COL_DELTA;
		out->columns[out->ncolumns++] = COL_NAME;
	}

	/* add -o [+]<list> to putput */
	if (outarg && string_add_to_idarray(outarg, out->columns,
				ARRAY_SIZE(out->columns),
				&out->ncolumns,
				irq_column_name_to_id) < 0)
		exit(EXIT_FAILURE);
}

int main(int argc, char **argv)
{
	int is_tty = 0;
	struct termios saved_tty;
	struct irq_output out = {
		.ncolumns = 0
	};
	struct irqtop_ctl ctl = {
		.timer.it_interval = {3, 0},
		.timer.it_value = {3, 0},
		.iter = -1
	};

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	close_stdout_atexit();

	parse_args(&ctl, &out, argc, argv);

	if (!ctl.batch) {
		is_tty = isatty(STDIN_FILENO);
		if (is_tty && tcgetattr(STDIN_FILENO, &saved_tty) == -1)
			fputs(_("failed to get terminal attributes"), stdout);

		ctl.win = initscr();
		get_terminal_dimension(&ctl.cols, &ctl.rows);
#if HAVE_RESIZETERM
		resizeterm(ctl.rows, ctl.cols);
#endif
		curs_set(0);
	}

	scols_init_debug(0);

	ctl.hostname = xgethostname();
	event_loop(&ctl, &out);

	free_irqstat(ctl.prev_stat);
	free(ctl.hostname);
	cpuset_free(ctl.cpuset);

	if (!ctl.batch) {
		if (is_tty)
			tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved_tty);

		delwin(ctl.win);
		endwin();
	}

	return EXIT_SUCCESS;
}
