/*
 * irqtop.c - utility to display kernel interrupt information.
 *
 * zhenwei pi <pizhenwei@bytedance.com>
 *
 * Copyright (C) 2019 zhenwei pi
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
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

#ifdef HAVE_WIDECHAR
# include <wctype.h>
# include <wchar.h>
#endif

#include "c.h"
#include "closestream.h"
#include "monotonic.h"
#include "nls.h"
#include "pathnames.h"
#include "strutils.h"
#include "timeutils.h"
#include "ttyutils.h"
#include "xalloc.h"

#define DEF_SORT_FUNC		sort_count
#define IRQ_NAME_LEN		4
#define IRQ_DESC_LEN		64
#define IRQ_INFO_LEN		64
#define RESERVE_ROWS		(1 + 2 + 1)	/* summary + header + last row */
#define MAX_EVENTS		3

struct irq_info {
	char irq[IRQ_NAME_LEN + 1];	/* name of this irq */
	char desc[IRQ_DESC_LEN + 1];	/* description of this irq */
	unsigned long count;		/* count of this irq for all cpu(s) */
};

struct irq_stat {
	unsigned int nr_irq;		/* number of irq vector */
	unsigned int nr_irq_info;	/* number of irq info */
	struct irq_info *irq_info;	/* array of irq_info */
	long nr_online_cpu;		/* number of online cpu */
	long nr_active_cpu;		/* number of active cpu */
	unsigned long total_irq;	/* total irqs */
};

struct irqtop_ctl {
	WINDOW *win;
	int cols;
	int rows;
	int old_rows;
	struct itimerspec timer;
	struct timeval uptime_tv;
	int (*sort_func)(const struct irq_info *, const struct irq_info *);
	long smp_num_cpus;
	struct irq_stat *last_stat;
	char *hostname;
	unsigned int
		request_exit:1,
		run_once:1;
};

/*
 * irqinfo - parse the system's interrupts
 */
static struct irq_stat *get_irqinfo(struct irqtop_ctl *ctl)
{
	FILE *irqfile;
	char *buffer, *tmp;
	long bufferlen;
	struct irq_stat *stat;
	struct irq_info *curr;

	/* NAME + ':' + 11 bytes/cpu + IRQ_DESC_LEN */
	bufferlen = IRQ_NAME_LEN + 1 + ctl->smp_num_cpus * 11 + IRQ_DESC_LEN;
	buffer = xmalloc(bufferlen);
	stat = xcalloc(1, sizeof(*stat));

	stat->irq_info = xmalloc(sizeof(*stat->irq_info) * IRQ_INFO_LEN);
	stat->nr_irq_info = IRQ_INFO_LEN;

	irqfile = fopen(_PATH_PROC_INTERRUPTS, "r");
	if (!irqfile) {
		warn(_("cannot open %s"), _PATH_PROC_INTERRUPTS);
		goto free_stat;
	}

	/* read header firstly */
	if (!fgets(buffer, bufferlen, irqfile)) {
		warn(_("cannot read %s"), _PATH_PROC_INTERRUPTS);
		goto close_file;
	}

	stat->nr_online_cpu = ctl->smp_num_cpus;
	tmp = buffer;
	while ((tmp = strstr(tmp, "CPU")) != NULL) {
		tmp += 3;	/* skip this "CPU", find next */
		stat->nr_active_cpu++;
	}

	/* parse each line of _PATH_PROC_INTERRUPTS */
	while (fgets(buffer, bufferlen, irqfile)) {
		unsigned long count;
		int index, length;

		tmp = strchr(buffer, ':');
		if (!tmp)
			continue;

		length = strlen(buffer);
		if (length < IRQ_NAME_LEN + 1 || tmp - buffer > IRQ_NAME_LEN)
			continue;

		curr = stat->irq_info + stat->nr_irq++;
		memset(curr, 0, sizeof(*curr));
		memcpy(curr->irq, buffer, tmp - buffer);

		tmp += 1;
		for (index = 0; (index < stat->nr_active_cpu) && (tmp - buffer < length); index++) {
			sscanf(tmp, " %10lu", &count);
			curr->count += count;
			stat->total_irq += count;
			tmp += 11;
		}

		if (tmp - buffer < length) {
			/* strip all space before desc */
			while (*tmp == ' ')
				tmp++;
			strcpy(curr->desc, tmp);
		} else {
			/* no desc string at all, we have to set '\n' here */
			curr->desc[0] = '\n';
		}

		if (stat->nr_irq == stat->nr_irq_info) {
			stat->nr_irq_info *= 2;
			stat->irq_info = xrealloc(stat->irq_info,
						  sizeof(*stat->irq_info) * stat->nr_irq_info);
		}
	}
	fclose(irqfile);
	free(buffer);
	return stat;

 close_file:
	fclose(irqfile);
 free_stat:
	free(stat->irq_info);
	free(stat);
	free(buffer);
	return NULL;
}

static void free_irqinfo(struct irq_stat *stat)
{
	if (stat)
		free(stat->irq_info);
	free(stat);
}

static int sort_name(const struct irq_info *a, const struct irq_info *b)
{
	return (strcmp(a->irq, b->irq) > 0) ? 1 : 0;
}

static int sort_count(const struct irq_info *a, const struct irq_info *b)
{
	return a->count < b->count;
}

static int sort_interrupts(const struct irq_info *a __attribute__((__unused__)),
			   const struct irq_info *b __attribute__((__unused__)))
{
	return 0;
}

static void sort_result(struct irqtop_ctl *ctl, struct irq_info *result, size_t nmemb)
{
	qsort(result, nmemb, sizeof(*result), (int (*)(const void *, const void *))ctl->sort_func);
}

static void __attribute__((__noreturn__)) usage(void)
{
	fputs(USAGE_HEADER, stdout);
	printf(_(" %s [options]\n"), program_invocation_short_name);

	fputs(USAGE_SEPARATOR, stdout);
	puts(_("Utility to display kernel interrupt information."));

	fputs(USAGE_OPTIONS, stdout);
	fputs(_(" -d, --delay <secs>  delay updates\n"), stdout);
	fputs(_(" -o, --once          only display average irq once, then exit\n"), stdout);
	fputs(_(" -s, --sort <char>   specify sort criteria by character (see below)\n"), stdout);
	fputs(USAGE_SEPARATOR, stdout);
	printf(USAGE_HELP_OPTIONS(21));
	fputs(_("\nThe following are valid sort criteria:\n"), stdout);
	fputs(_("  c:   sort by increase count of each interrupt\n"), stdout);
	fputs(_("  i:   sort by default interrupts from proc interrupt\n"), stdout);
	fputs(_("  n:   sort by name\n"), stdout);
	printf(USAGE_MAN_TAIL("irqtop(1)"));
	exit(EXIT_SUCCESS);
}

static void *set_sort_func(char key)
{
	switch (key) {
	case 'c':
		return (void *)sort_count;
	case 'i':
		return (void *)sort_interrupts;
	case 'n':
		return (void *)sort_name;
	default:
		return (void *)DEF_SORT_FUNC;
	}
}

static void parse_input(struct irqtop_ctl *ctl, char c)
{
	switch (c) {
	case 'c':
		ctl->sort_func = sort_count;
		break;
	case 'i':
		ctl->sort_func = sort_interrupts;
		break;
	case 'n':
		ctl->sort_func = sort_name;
		break;
	case 'q':
	case 'Q':
		ctl->request_exit = 1;
		break;
	}
}

static inline size_t choose_smaller(size_t a, size_t b)
{
	if (a < b)
		return a;
	return b;
}

static inline void print_line(struct irqtop_ctl *ctl, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	if (ctl->run_once)
		vprintf(fmt, args);
	else {
		vw_printw(ctl->win, fmt, args);
	}
	va_end(args);
}

static int update_screen(struct irqtop_ctl *ctl)
{
	struct irq_info *result, *curr;
	struct irq_stat *stat;
	size_t size;
	size_t index;
	time_t now;
	char timestr[64];

	stat = get_irqinfo(ctl);
	if (!stat) {
		ctl->request_exit = 1;
		return 1;
	}

	if (!ctl->run_once && ctl->old_rows != ctl->rows) {
		resizeterm(ctl->rows, ctl->cols);
		ctl->old_rows = ctl->rows;
	}

	move(0, 0);

	/* summary stat */
	now = time(NULL);
	strtime_iso(&now, ISO_TIMESTAMP_T, timestr, sizeof(timestr));
	print_line(ctl,
		   "irqtop - IRQ: %d TOTAL: %ld CPU: %ld ACTIVE CPU: %ld\n"
		   "HOST: %s TIME: %s\n",
		   stat->nr_irq, stat->total_irq, stat->nr_online_cpu, stat->nr_active_cpu,
		   ctl->hostname, timestr);

	/* header */
	attron(A_REVERSE);
	print_line(ctl, "%-80s\n", " IRQ        COUNT   DESC ");
	attroff(A_REVERSE);

	size = sizeof(*stat->irq_info) * stat->nr_irq;
	result = xmalloc(size);
	memcpy(result, stat->irq_info, size);
	if (!ctl->last_stat) {
		for (index = 0; index < stat->nr_irq; index++) {
			curr = result + index;
			curr->count /= ctl->uptime_tv.tv_sec;
		}
		ctl->last_stat = stat;
	} else {
		size_t i, j;

		for (i = 0; i < stat->nr_irq; i++) {
			struct irq_info *found = NULL;
			unsigned long diff = 0;

			curr = result + i;
			for (j = 0; j < ctl->last_stat->nr_irq; j++) {
				struct irq_info *prev = ctl->last_stat->irq_info + j;

				if (!strcmp(curr->irq, prev->irq))
					found = prev;
			}

			if (found && curr->count >= found->count)
				diff = curr->count - found->count;
			else
				diff = curr->count;

			curr->count = diff;
		}
		free_irqinfo(ctl->last_stat);

		ctl->last_stat = stat;
	}

	/* okay, sort and show the result */
	sort_result(ctl, result, stat->nr_irq);
	for (index = 0; index < choose_smaller(ctl->rows - RESERVE_ROWS, stat->nr_irq); index++) {
		curr = result + index;
		print_line(ctl, "%4s   %10ld   %s", curr->irq, curr->count, curr->desc);
	}
	free(result);

	return 0;
}

static int event_loop(struct irqtop_ctl *ctl)
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
		err(EXIT_FAILURE, _("cannot not create timerfd"));
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
	if ((sfd = signalfd(-1, &sigmask, 0)) < 0)
		err(EXIT_FAILURE, _("cannot not create signalfd"));
	ev.events = EPOLLIN;
	ev.data.fd = sfd;
	if (epoll_ctl(efd, EPOLL_CTL_ADD, sfd, &ev) != 0)
		err(EXIT_FAILURE, _("epoll_ctl failed"));

	ev.events = EPOLLIN;
	ev.data.fd = STDIN_FILENO;
	if (epoll_ctl(efd, EPOLL_CTL_ADD, STDIN_FILENO, &ev) != 0)
		err(EXIT_FAILURE, _("epoll_ctl failed"));

	retval |= update_screen(ctl);
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
				if (siginfo.ssi_signo == SIGWINCH)
					get_terminal_dimension(&ctl->cols, &ctl->rows);
				else {
					ctl->request_exit = 1;
					break;
				}
			} else if (events[nr].data.fd == STDIN_FILENO) {
				char c;

				if (read(STDIN_FILENO, &c, 1) != 1)
					warn(_("read failed"));
				parse_input(ctl, c);
			} else
				abort();
			retval |= update_screen(ctl);
			refresh();
		}
	}
	return retval;
}

static void parse_args(struct irqtop_ctl *ctl, int argc, char **argv)
{
	static const struct option longopts[] = {
		{"delay", required_argument, NULL, 'd'},
		{"sort", required_argument, NULL, 's'},
		{"once", no_argument, NULL, 'o'},
		{"help", no_argument, NULL, 'h'},
		{"version", no_argument, NULL, 'V'},
		{NULL, 0, NULL, 0}
	};
	int o;

	while ((o = getopt_long(argc, argv, "d:os:hV", longopts, NULL)) != -1) {
		switch (o) {
		case 'd':
			{
				struct timeval delay;

				strtotimeval_or_err(optarg, &delay,
						    _("failed to parse delay argument"));
				TIMEVAL_TO_TIMESPEC(&delay, &ctl->timer.it_interval);
				ctl->timer.it_value = ctl->timer.it_interval;
			}
			break;
		case 's':
			ctl->sort_func = (int (*)(const struct irq_info *, const struct irq_info *))
					 set_sort_func(optarg[0]);
			break;
		case 'o':
			ctl->run_once = 1;
			ctl->request_exit = 1;
			break;
		case 'V':
			print_version(EXIT_SUCCESS);
		case 'h':
			usage();
		default:
			errtryhelp(EXIT_FAILURE);
		}
	}
}

int main(int argc, char **argv)
{
	int is_tty;
	int retval = EXIT_SUCCESS;
	struct termios saved_tty;

	struct irqtop_ctl ctl = {
		.timer.it_interval = {3, 0},
		.timer.it_value = {3, 0}
	};

	setlocale(LC_ALL, "");
	ctl.sort_func = DEF_SORT_FUNC;

	parse_args(&ctl, argc, argv);

	is_tty = isatty(STDIN_FILENO);
	if (is_tty && tcgetattr(STDIN_FILENO, &saved_tty) == -1)
		fputs(_("terminal setting retrieval"), stdout);

	ctl.old_rows = ctl.rows;
	get_terminal_dimension(&ctl.cols, &ctl.rows);
	if (!ctl.run_once) {
		ctl.win = initscr();
		get_terminal_dimension(&ctl.cols, &ctl.rows);
		resizeterm(ctl.rows, ctl.cols);
	}
	ctl.smp_num_cpus = sysconf(_SC_NPROCESSORS_ONLN);
	gettime_monotonic(&ctl.uptime_tv);
	ctl.hostname = xgethostname();

	if (ctl.run_once)
		retval = update_screen(&ctl);
	else
		event_loop(&ctl);

	free_irqinfo(ctl.last_stat);
	free(ctl.hostname);

	if (is_tty)
		tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved_tty);

	if (ctl.win) {
		delwin(ctl.win);
		endwin();
	}
	return retval;
}
