/*
 * taskset.c - command-line utility for setting and retrieving
 *             a task's CPU affinity
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, v2, as
 * published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Copyright (C) 2004 Robert Love
 * Copyright (C) 2010 Karel Zak <kzak@redhat.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <sched.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>
#include <err.h>

#include "cpuset.h"
#include "nls.h"

static void __attribute__((__noreturn__)) usage(FILE *out)
{
	fprintf(out,
		_("Usage: %s [options] [mask | cpu-list] [pid|cmd [args...]]\n\n"),
		program_invocation_short_name);

	fprintf(out, _(
		"Options:\n"
		" -p, --pid               operate on existing given pid\n"
		" -c, --cpu-list          display and specify cpus in list format\n"
		" -h, --help              display this help\n"
		" -V, --version           output version information\n\n"));

	fprintf(out, _(
		"The default behavior is to run a new command:\n"
		"    %1$s 03 sshd -b 1024\n"
		"You can retrieve the mask of an existing task:\n"
		"    %1$s -p 700\n"
		"Or set it:\n"
		"    %1$s -p 03 700\n"
		"List format uses a comma-separated list instead of a mask:\n"
		"    %1$s -pc 0,3,7-11 700\n"
		"Ranges in list format can take a stride argument:\n"
		"    e.g. 0-31:2 is equivalent to mask 0x55555555\n"),
		program_invocation_short_name);

	fprintf(out, _("\nFor more information see taskset(1).\n"));

	exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}

int main(int argc, char *argv[])
{
	cpu_set_t *new_set, *cur_set;
	pid_t pid = 0;
	int opt, c_opt = 0, rc;
	char *buf;
        unsigned int ncpus;
	size_t new_setsize, cur_setsize, cur_nbits, buflen;

	struct option longopts[] = {
		{ "pid",	0, NULL, 'p' },
		{ "cpu-list",	0, NULL, 'c' },
		{ "help",	0, NULL, 'h' },
		{ "version",	0, NULL, 'V' },
		{ NULL,		0, NULL, 0 }
	};

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

	while ((opt = getopt_long(argc, argv, "+pchV", longopts, NULL)) != -1) {
		switch (opt) {
		case 'p':
			pid = atoi(argv[argc - 1]);
			break;
		case 'c':
			c_opt = 1;
			break;
		case 'V':
			printf("taskset (%s)\n", PACKAGE_STRING);
			return EXIT_SUCCESS;
		case 'h':
			usage(stdout);
			break;
		default:
			usage(stderr);
			break;
		}
	}

	if ((!pid && argc - optind < 2)
			|| (pid && (argc - optind < 1 || argc - optind > 2)))
		usage(stderr);

	ncpus = get_max_number_of_cpus();
	if (ncpus <= 0)
		errx(EXIT_FAILURE, _("cannot determine NR_CPUS; aborting"));

	/*
	 * cur_set is always used for the sched_getaffinity call
	 * On the sched_getaffinity the kernel demands a user mask of
	 * at least the size of its own cpumask_t.
	 */
	cur_set = cpuset_alloc(ncpus, &cur_setsize, &cur_nbits);
	if (!cur_set)
		err(EXIT_FAILURE, _("cpuset_alloc failed"));

	buflen = 7 * cur_nbits;
	buf = malloc(buflen);
	if (!buf)
		err(EXIT_FAILURE, _("malloc failed"));

	/*
	 * new_set is always used for the sched_setaffinity call
	 * On the sched_setaffinity the kernel will zero-fill its
	 * cpumask_t if the user's mask is shorter.
	 */
	new_set = cpuset_alloc(ncpus, &new_setsize, NULL);
	if (!new_set)
		err(EXIT_FAILURE, _("cpuset_alloc failed"));

	if (pid) {
		if (sched_getaffinity(pid, cur_setsize, cur_set) < 0)
			err(EXIT_FAILURE, _("failed to get pid %d's affinity"), pid);

		if (c_opt)
			printf(_("pid %d's current affinity list: %s\n"), pid,
				cpulist_create(buf, buflen, cur_set, cur_setsize));
		else
			printf(_("pid %d's current affinity mask: %s\n"), pid,
				cpumask_create(buf, buflen, cur_set, cur_setsize));

		if (argc - optind == 1)
			return EXIT_SUCCESS;
	}

	rc = c_opt ? cpulist_parse(argv[optind], new_set, new_setsize) :
		     cpumask_parse(argv[optind], new_set, new_setsize);

	if (rc)
		errx(EXIT_FAILURE, _("failed to parse %s %s"),
				c_opt ? _("CPU list") : _("CPU mask"),
				argv[optind]);

	if (sched_setaffinity(pid, new_setsize, new_set) < 0)
		err(EXIT_FAILURE, _("failed to set pid %d's affinity"), pid);

	if (sched_getaffinity(pid, cur_setsize, cur_set) < 0)
		err(EXIT_FAILURE, _("failed to get pid %d's affinity"), pid);

	if (pid) {
		if (c_opt)
			printf(_("pid %d's new affinity list: %s\n"), pid,
				cpulist_create(buf, buflen, cur_set, cur_setsize));
		else
			printf(_("pid %d's new affinity mask: %s\n"), pid,
				cpumask_create(buf, buflen, cur_set, cur_setsize));
	}

	free(buf);
	cpuset_free(cur_set);
	cpuset_free(new_set);

	if (!pid) {
		argv += optind + 1;
		execvp(argv[0], argv);
		err(EXIT_FAILURE, _("executing %s failed"), argv[0]);
	}

	return EXIT_SUCCESS;
}
