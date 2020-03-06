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

#include <libsmartcols.h>

#include "c.h"
#include "closestream.h"
#include "monotonic.h"
#include "nls.h"
#include "pathnames.h"
#include "strutils.h"
#include "timeutils.h"
#include "ttyutils.h"
#include "xalloc.h"

#define IRQ_INFO_LEN	64
#define MAX_EVENTS	3

struct colinfo {
	const char *name;
	double whint;
	int flags;
	const char *help;
	int json_type;
};

enum {
	COL_IRQ,
	COL_TOTAL,
	COL_DELTA,
	COL_NAME
};

static const struct colinfo infos[] = {
	[COL_IRQ]   = {"IRQ",   0.10, SCOLS_FL_RIGHT, N_("interrupts"),  SCOLS_JSON_STRING},
	[COL_TOTAL] = {"TOTAL", 0.10, SCOLS_FL_RIGHT, N_("total count"), SCOLS_JSON_NUMBER},
	[COL_DELTA] = {"DELTA", 0.10, SCOLS_FL_RIGHT, N_("delta count"), SCOLS_JSON_NUMBER},
	[COL_NAME]  = {"NAME",  0.70, SCOLS_FL_TRUNC, N_("name"),        SCOLS_JSON_STRING},
};

struct irq_info {
	char *irq;			/* short name of this irq */
	char *name;			/* descriptive name of this irq */
	unsigned long total;		/* total count since system start up */
	unsigned long delta;		/* delta count since previous update */
};

struct irq_stat {
	unsigned int nr_irq;		/* number of irq vector */
	unsigned int nr_irq_info;	/* number of irq info */
	struct irq_info *irq_info;	/* array of irq_info */
	long nr_active_cpu;		/* number of active cpu */
	unsigned long total_irq;	/* total irqs */
	unsigned long delta_irq;	/* delta irqs */
};

typedef int (sort_fp)(const struct irq_info *, const struct irq_info *);

#define DEF_SORT_FUNC	((sort_fp *)sort_total)

struct irq_output {
	int columns[ARRAY_SIZE(infos) * 2];
	size_t ncolumns;

	sort_fp *sort_func;

	unsigned int
		json:1,
		no_headings:1;
};

struct irqtop_ctl {
	int cols;
	int rows;
	struct itimerspec timer;
	struct irq_stat *prev_stat;
	char *hostname;

	unsigned int
		request_exit:1,
		run_once:1;

};

static int column_name_to_id(char const *const name, size_t const namesz)
{
	size_t i;

	assert(name);
	for (i = 0; i < ARRAY_SIZE(infos); i++) {
		const char *cn = infos[i].name;

		if (!strncasecmp(name, cn, namesz) && !*(cn + namesz))
			return i;
	}
	warnx(_("unknown column: %s"), name);
	return -1;
}

static inline int get_column_id(struct irq_output *out, size_t const num)
{
	assert(num < out->ncolumns);
	assert(out->columns[num] < (int)ARRAY_SIZE(infos));

	return out->columns[num];
}

static inline const struct colinfo *get_column_info(
			struct irq_output *out, unsigned num)
{
	return &infos[get_column_id(out, num)];
}

static struct libscols_table *new_scols_table(struct irq_output *out)
{
	size_t i;
	struct libscols_table *table;

	table = scols_new_table();
	if (!table) {
		warn(_("failed to initialize output table"));
		return NULL;
	}
	scols_table_enable_json(table, out->json);
	scols_table_enable_noheadings(table, out->no_headings);

	if (out->json)
		scols_table_set_name(table, _("interrupts"));

	for (i = 0; i < out->ncolumns; i++) {
		const struct colinfo *col = get_column_info(out, i);
		int flags = col->flags;
		struct libscols_column *cl;

		cl = scols_table_new_column(table, col->name, col->whint, flags);
		if (cl == NULL) {
			warnx(_("failed to initialize output column"));
			goto err;
		}
		if (out->json)
			scols_column_set_json_type(cl, col->json_type);
	}

	return table;
 err:
	scols_unref_table(table);
	return NULL;
}

static void add_scols_line(struct irq_output *out,
			   struct irq_info *stat,
			   struct libscols_table *table)
{
	size_t i;
	struct libscols_line *line;

	line = scols_table_new_line(table, NULL);
	if (!line) {
		warn(_("failed to add line to output"));
		return;
	}

	for (i = 0; i < out->ncolumns; i++) {
		char *str = NULL;

		switch (get_column_id(out, i)) {
		case COL_IRQ:
			xasprintf(&str, "%s", stat->irq);
			break;
		case COL_TOTAL:
			xasprintf(&str, "%ld", stat->total);
			break;
		case COL_DELTA:
			xasprintf(&str, "%ld", stat->delta);
			break;
		case COL_NAME:
			xasprintf(&str, "%s", stat->name);
			break;
		default:
			break;
		}

		if (str && scols_line_refer_data(line, i, str) != 0)
			err_oom();
	}

	/* FIXME */
	free(stat->irq);
	free(stat->name);
}

static char *remove_repeated_spaces(char *str)
{
	char *inp = str, *outp = str;
	uint8_t prev_space = 0;

	while (*inp) {
		if (isspace(*inp)) {
			if (!prev_space) {
				*outp++ = ' ';
				prev_space = 1;
			}
		} else {
			*outp++ = *inp;
			prev_space = 0;
		}
		++inp;
	}
	*outp = '\0';
	return str;
}

/*
 * irqinfo - parse the system's interrupts
 */
static struct irq_stat *get_irqinfo(void)
{
	FILE *irqfile;
	char *line = NULL, *tmp;
	size_t len = 0;
	struct irq_stat *stat;
	struct irq_info *curr;

	/* NAME + ':' + 11 bytes/cpu + IRQ_NAME_LEN */
	stat = xcalloc(1, sizeof(*stat));

	stat->irq_info = xmalloc(sizeof(*stat->irq_info) * IRQ_INFO_LEN);
	stat->nr_irq_info = IRQ_INFO_LEN;

	irqfile = fopen(_PATH_PROC_INTERRUPTS, "r");
	if (!irqfile) {
		warn(_("cannot open %s"), _PATH_PROC_INTERRUPTS);
		goto free_stat;
	}

	/* read header firstly */
	if (getline(&line, &len, irqfile) < 0) {
		warn(_("cannot read %s"), _PATH_PROC_INTERRUPTS);
		goto close_file;
	}

	tmp = line;
	while ((tmp = strstr(tmp, "CPU")) != NULL) {
		tmp += 3;	/* skip this "CPU", find next */
		stat->nr_active_cpu++;
	}

	/* parse each line of _PATH_PROC_INTERRUPTS */
	while (getline(&line, &len, irqfile) >= 0) {
		unsigned long count;
		int index, length;

		tmp = strchr(line, ':');
		if (!tmp)
			continue;

		length = strlen(line);

		curr = stat->irq_info + stat->nr_irq++;
		memset(curr, 0, sizeof(*curr));
		*tmp = '\0';
		curr->irq = xstrdup(line);
		ltrim_whitespace((unsigned char *)curr->irq);

		tmp += 1;
		for (index = 0; (index < stat->nr_active_cpu) && (tmp - line < length); index++) {
			sscanf(tmp, " %10lu", &count);
			curr->total += count;
			stat->total_irq += count;
			tmp += 11;
		}

		if (tmp - line < length) {
			/* strip all space before desc */
			while (isspace(*tmp))
				tmp++;
			tmp = remove_repeated_spaces(tmp);
			rtrim_whitespace((unsigned char *)tmp);
			curr->name = xstrdup(tmp);
		} else	/* no irq name string, we have to set '\0' here */
			curr->name = xstrdup("");

		if (stat->nr_irq == stat->nr_irq_info) {
			stat->nr_irq_info *= 2;
			stat->irq_info = xrealloc(stat->irq_info,
						  sizeof(*stat->irq_info) * stat->nr_irq_info);
		}
	}
	fclose(irqfile);
	free(line);
	return stat;

 close_file:
	fclose(irqfile);
 free_stat:
	free(stat->irq_info);
	free(stat);
	free(line);
	return NULL;
}

static void free_irqinfo(struct irq_stat *stat)
{
	if (stat)
		free(stat->irq_info);
	free(stat);
}

static int sort_name(const struct irq_info *a,
		     const struct irq_info *b)
{
	return (strcmp(a->name, b->name) > 0) ? 1 : 0;
}

static int sort_total(const struct irq_info *a,
		      const struct irq_info *b)
{
	return a->total < b->total;
}

static int sort_delta(const struct irq_info *a,
		      const struct irq_info *b)
{
	return a->delta < b->delta;
}

static int sort_interrupts(const struct irq_info *a,
			   const struct irq_info *b)
{
	return (strcmp(a->irq, b->irq) > 0) ? 1 : 0;
}

static void sort_result(struct irq_output *out,
			struct irq_info *result,
			size_t nmemb)
{
	qsort(result, nmemb, sizeof(*result),
			(int (*)(const void *,
				 const void *))out->sort_func);
}

static sort_fp *set_sort_func(char key)
{
	switch (key) {
	case 'i':
		return sort_interrupts;
	case 't':
		return sort_total;
	case 'd':
		return sort_delta;
	case 'n':
		return sort_name;
	default:
		return DEF_SORT_FUNC;
	}
}

static struct libscols_table *get_scols_table(struct irq_output *out,
					      struct irq_stat *prev,
					      struct irq_stat **xstat)
{
	struct libscols_table *table;
	struct irq_info *result, *curr;
	struct irq_stat *stat;
	size_t size;
	size_t index;

	/* the stats */
	stat = get_irqinfo();
	if (!stat)
		return NULL;

	size = sizeof(*stat->irq_info) * stat->nr_irq;
	result = xmalloc(size);
	memcpy(result, stat->irq_info, size);

	if (prev) {
		stat->delta_irq = 0;
		for (index = 0; index < stat->nr_irq; index++) {
			result[index].delta = result[index].total
					- prev->irq_info[index].total;
			stat->delta_irq += result[index].delta;
		}
	}
	sort_result(out, result, stat->nr_irq);

	table = new_scols_table(out);
	if (!table)
		return NULL;

	for (index = 0; index < stat->nr_irq; index++) {
		curr = result + index;
		add_scols_line(out, curr, table);
	}

	free(result);

	if (xstat)
		*xstat = stat;
	else
		free_irqinfo(stat);

	return table;
}

static void parse_input(struct irqtop_ctl *ctl, struct irq_output *out, char c)
{
	switch (c) {
	case 'i':
		out->sort_func = sort_interrupts;
		break;
	case 't':
		out->sort_func = sort_total;
		break;
	case 'd':
		out->sort_func = sort_delta;
		break;
	case 'n':
		out->sort_func = sort_name;
		break;
	case 'q':
	case 'Q':
		ctl->request_exit = 1;
		break;
	}
}

static int print_irq_data(struct irq_output *out)
{
	struct libscols_table *table;

	table = get_scols_table(out, NULL, NULL);
	if (!table)
		return 1;

	scols_print_table(table);
	scols_unref_table(table);
	return 0;
}

static int update_screen(struct irqtop_ctl *ctl, struct irq_output *out, WINDOW *win)
{
	struct libscols_table *table;
	struct irq_stat *stat;
	time_t now = time(NULL);
	char timestr[64], *data;

	table = get_scols_table(out, ctl->prev_stat, &stat);
	if (!table) {
		ctl->request_exit = 1;
		return 1;
	}

	/* header in interactive mode */
	move(0, 0);
	strtime_iso(&now, ISO_TIMESTAMP, timestr, sizeof(timestr));
	wprintw(win, _("irqtop | total: %ld delta: %ld | %s | %s\n\n"),
			   stat->total_irq, stat->delta_irq, ctl->hostname, timestr);

	scols_print_table_to_string(table, &data);
	wprintw(win, "%s", data);
	free(data);

	/* clean up */
	scols_unref_table(table);
	if (ctl->prev_stat)
		free_irqinfo(ctl->prev_stat);
	ctl->prev_stat = stat;
	return 0;
}

static int event_loop(struct irqtop_ctl *ctl, struct irq_output *out, WINDOW *win)
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

	sigaddset(&sigmask, SIGWINCH);
	sigaddset(&sigmask, SIGTERM);
	sigaddset(&sigmask, SIGINT);
	sigaddset(&sigmask, SIGQUIT);

	if ((sfd = signalfd(-1, &sigmask, SFD_CLOEXEC)) < 0)
		err(EXIT_FAILURE, _("cannot not create signalfd"));

	ev.events = EPOLLIN;
	ev.data.fd = sfd;
	if (epoll_ctl(efd, EPOLL_CTL_ADD, sfd, &ev) != 0)
		err(EXIT_FAILURE, _("epoll_ctl failed"));

	ev.events = EPOLLIN;
	ev.data.fd = STDIN_FILENO;
	if (epoll_ctl(efd, EPOLL_CTL_ADD, STDIN_FILENO, &ev) != 0)
		err(EXIT_FAILURE, _("epoll_ctl failed"));

	retval |= update_screen(ctl, out, win);
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
					get_terminal_dimension(&ctl->cols, &ctl->rows);
					resizeterm(ctl->rows, ctl->cols);
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
			retval |= update_screen(ctl, out, win);
			refresh();
		}
	}
	return retval;
}

static void __attribute__((__noreturn__)) usage(void)
{
	size_t i;

	fputs(USAGE_HEADER, stdout);
	printf(_(" %s [options]\n"), program_invocation_short_name);
	fputs(USAGE_SEPARATOR, stdout);

	puts(_("Interactive utility to display kernel interrupt information."));

	fputs(USAGE_OPTIONS, stdout);
	fputs(_(" -d, --delay <secs>   delay updates\n"), stdout);
	fputs(_("     --once           only display interrupts once, then exit\n"), stdout);
	fputs(_(" -J  --json           output json, implies displaying once\n"), stdout);
	fputs(_(" -o  --output <list>  define which output columns to use (see below)\n"), stdout);
	fputs(_(" -s, --sort <char>    specify sort criteria by character (see below)\n"), stdout);
	fputs(USAGE_SEPARATOR, stdout);
	printf(USAGE_HELP_OPTIONS(22));

	fputs(_("\nThe following interactive key commands are valid:\n"), stdout);
	fputs(_("  i      sort by IRQ\n"), stdout);
	fputs(_("  t      sort by TOTAL\n"), stdout);
	fputs(_("  d      sort by DELTA\n"), stdout);
	fputs(_("  n      sort by NAME\n"), stdout);
	fputs(_("  q Q    quit program\n"), stdout);

	fputs(USAGE_COLUMNS, stdout);
	for (i = 0; i < ARRAY_SIZE(infos); i++)
		fprintf(stdout, "  %-5s  %s\n", infos[i].name, _(infos[i].help));

	printf(USAGE_MAN_TAIL("irqtop(1)"));
	exit(EXIT_SUCCESS);
}

static void parse_args(	struct irqtop_ctl *ctl,
			struct irq_output *out,
			int argc,
			char **argv)
{
	const char *outarg = NULL;
	enum {
		ONCE_OPT = CHAR_MAX + 1,
	};
	static const struct option longopts[] = {
		{"delay", required_argument, NULL, 'd'},
		{"sort", required_argument, NULL, 's'},
		{"once", no_argument, NULL, ONCE_OPT },
		{"json", no_argument, NULL, 'J'},
		{"output", required_argument, NULL, 'o'},
		{"help", no_argument, NULL, 'h'},
		{"version", no_argument, NULL, 'V'},
		{NULL, 0, NULL, 0}
	};
	int o;

	while ((o = getopt_long(argc, argv, "d:o:s:hJV", longopts, NULL)) != -1) {
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
			out->sort_func = set_sort_func(optarg[0]);
			break;
		case ONCE_OPT:
			ctl->run_once = 1;
			ctl->request_exit = 1;
			break;
		case 'J':
			out->json = 1;
			ctl->run_once = 1;
			ctl->request_exit = 1;
			break;
		case 'o':
			outarg = optarg;
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
		if (!ctl->run_once)
			out->columns[out->ncolumns++] = COL_DELTA;
		out->columns[out->ncolumns++] = COL_NAME;
	}

	/* add -o [+]<list> to putput */
	if (outarg && string_add_to_idarray(outarg, out->columns,
				ARRAY_SIZE(out->columns),
				&out->ncolumns, column_name_to_id) < 0)
		exit(EXIT_FAILURE);
}

int main(int argc, char **argv)
{
	WINDOW *win = NULL;
	int is_tty = 0;
	int retval = EXIT_SUCCESS;
	struct termios saved_tty;
	struct irq_output out = { .ncolumns = 0, .sort_func = DEF_SORT_FUNC };
	struct irqtop_ctl ctl = {
		.timer.it_interval = {3, 0},
		.timer.it_value = {3, 0}
	};

	setlocale(LC_ALL, "");

	parse_args(&ctl, &out, argc, argv);

	if (ctl.run_once)
		retval = print_irq_data(&out);
	else {
		is_tty = isatty(STDIN_FILENO);
		if (is_tty && tcgetattr(STDIN_FILENO, &saved_tty) == -1)
			fputs(_("terminal setting retrieval"), stdout);

		win = initscr();
		get_terminal_dimension(&ctl.cols, &ctl.rows);
		resizeterm(ctl.rows, ctl.cols);
		curs_set(0);

		ctl.hostname = xgethostname();
		event_loop(&ctl, &out, win);

		free_irqinfo(ctl.prev_stat);
		free(ctl.hostname);

		if (is_tty)
			tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved_tty);
		delwin(win);
		endwin();
	}
	return retval;
}
