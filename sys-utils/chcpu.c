/*
 * chcpu - CPU configuration tool
 *
 * Copyright IBM Corp. 2011
 * Author(s): Heiko Carstens <heiko.carstens@de.ibm.com>,
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>
#include <unistd.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "cpuset.h"
#include "nls.h"
#include "xalloc.h"
#include "c.h"
#include "strutils.h"
#include "bitops.h"
#include "path.h"
#include "closestream.h"
#include "optutils.h"

#define EXCL_ERROR "--{configure,deconfigure,disable,dispatch,enable}"

/* partial success, otherwise we return regular EXIT_{SUCCESS,FAILURE} */
#define CHCPU_EXIT_SOMEOK	64

#define _PATH_SYS_CPU		"/sys/devices/system/cpu"
#define _PATH_SYS_CPU_ONLINE	_PATH_SYS_CPU "/online"
#define _PATH_SYS_CPU_RESCAN	_PATH_SYS_CPU "/rescan"
#define _PATH_SYS_CPU_DISPATCH	_PATH_SYS_CPU "/dispatching"

static cpu_set_t *onlinecpus;
static int maxcpus;

#define is_cpu_online(cpu) (CPU_ISSET_S((cpu), CPU_ALLOC_SIZE(maxcpus), onlinecpus))
#define num_online_cpus()  (CPU_COUNT_S(CPU_ALLOC_SIZE(maxcpus), onlinecpus))

enum {
	CMD_CPU_ENABLE	= 0,
	CMD_CPU_DISABLE,
	CMD_CPU_CONFIGURE,
	CMD_CPU_DECONFIGURE,
	CMD_CPU_RESCAN,
	CMD_CPU_DISPATCH_HORIZONTAL,
	CMD_CPU_DISPATCH_VERTICAL,
};

/* returns:   0 = success
 *          < 0 = failure
 *          > 0 = partial success
 */
static int cpu_enable(cpu_set_t *cpu_set, size_t setsize, int enable)
{
	unsigned int cpu;
	int online, rc;
	int configured = -1;
	size_t fails = 0;

	for (cpu = 0; cpu < setsize; cpu++) {
		if (!CPU_ISSET(cpu, cpu_set))
			continue;
		if (!path_exist(_PATH_SYS_CPU "/cpu%d", cpu)) {
			warnx(_("CPU %u does not exist"), cpu);
			fails++;
			continue;
		}
		if (!path_exist(_PATH_SYS_CPU "/cpu%d/online", cpu)) {
			warnx(_("CPU %u is not hot pluggable"), cpu);
			fails++;
			continue;
		}
		online = path_read_s32(_PATH_SYS_CPU "/cpu%d/online", cpu);
		if ((online == 1) && (enable == 1)) {
			printf(_("CPU %u is already enabled\n"), cpu);
			continue;
		}
		if ((online == 0) && (enable == 0)) {
			printf(_("CPU %u is already disabled\n"), cpu);
			continue;
		}
		if (path_exist(_PATH_SYS_CPU "/cpu%d/configure", cpu))
			configured = path_read_s32(_PATH_SYS_CPU "/cpu%d/configure", cpu);
		if (enable) {
			rc = path_write_str("1", _PATH_SYS_CPU "/cpu%d/online", cpu);
			if ((rc == -1) && (configured == 0)) {
				warn(_("CPU %u enable failed (CPU is deconfigured)"), cpu);
				fails++;
			} else if (rc == -1) {
				warn(_("CPU %u enable failed"), cpu);
				fails++;
			} else
				printf(_("CPU %u enabled\n"), cpu);
		} else {
			if (onlinecpus && num_online_cpus() == 1) {
				warnx(_("CPU %u disable failed (last enabled CPU)"), cpu);
				fails++;
				continue;
			}
			rc = path_write_str("0", _PATH_SYS_CPU "/cpu%d/online", cpu);
			if (rc == -1) {
				warn(_("CPU %u disable failed"), cpu);
				fails++;
			} else {
				printf(_("CPU %u disabled\n"), cpu);
				if (onlinecpus)
					CPU_CLR(cpu, onlinecpus);
			}
		}
	}

	return fails == 0 ? 0 : fails == setsize ? -1 : 1;
}

static int cpu_rescan(void)
{
	if (!path_exist(_PATH_SYS_CPU_RESCAN))
		errx(EXIT_FAILURE, _("This system does not support rescanning of CPUs"));
	if (path_write_str("1", _PATH_SYS_CPU_RESCAN) == -1)
		err(EXIT_FAILURE, _("Failed to trigger rescan of CPUs"));
	printf(_("Triggered rescan of CPUs\n"));
	return 0;
}

static int cpu_set_dispatch(int mode)
{
	if (!path_exist(_PATH_SYS_CPU_DISPATCH))
		errx(EXIT_FAILURE, _("This system does not support setting "
				     "the dispatching mode of CPUs"));
	if (mode == 0) {
		if (path_write_str("0", _PATH_SYS_CPU_DISPATCH) == -1)
			err(EXIT_FAILURE, _("Failed to set horizontal dispatch mode"));
		printf(_("Successfully set horizontal dispatching mode\n"));
	} else {
		if (path_write_str("1", _PATH_SYS_CPU_DISPATCH) == -1)
			err(EXIT_FAILURE, _("Failed to set vertical dispatch mode"));
		printf(_("Successfully set vertical dispatching mode\n"));
	}
	return 0;
}

/* returns:   0 = success
 *          < 0 = failure
 *          > 0 = partial success
 */
static int cpu_configure(cpu_set_t *cpu_set, size_t setsize, int configure)
{
	unsigned int cpu;
	int rc, current;
	size_t fails = 0;

	for (cpu = 0; cpu < setsize; cpu++) {
		if (!CPU_ISSET(cpu, cpu_set))
			continue;
		if (!path_exist(_PATH_SYS_CPU "/cpu%d", cpu)) {
			warnx(_("CPU %u does not exist"), cpu);
			fails++;
			continue;
		}
		if (!path_exist(_PATH_SYS_CPU "/cpu%d/configure", cpu)) {
			warnx(_("CPU %u is not configurable"), cpu);
			fails++;
			continue;
		}
		current = path_read_s32(_PATH_SYS_CPU "/cpu%d/configure", cpu);
		if ((current == 1) && (configure == 1)) {
			printf(_("CPU %u is already configured\n"), cpu);
			continue;
		}
		if ((current == 0) && (configure == 0)) {
			printf(_("CPU %u is already deconfigured\n"), cpu);
			continue;
		}
		if ((current == 1) && (configure == 0) && onlinecpus &&
		    is_cpu_online(cpu)) {
			warnx(_("CPU %u deconfigure failed (CPU is enabled)"), cpu);
			fails++;
			continue;
		}
		if (configure) {
			rc = path_write_str("1", _PATH_SYS_CPU "/cpu%d/configure", cpu);
			if (rc == -1) {
				warn(_("CPU %u configure failed"), cpu);
				fails++;
			} else
				printf(_("CPU %u configured\n"), cpu);
		} else {
			rc = path_write_str("0", _PATH_SYS_CPU "/cpu%d/configure", cpu);
			if (rc == -1) {
				warn(_("CPU %u deconfigure failed"), cpu);
				fails++;
			} else
				printf(_("CPU %u deconfigured\n"), cpu);
		}
	}

	return fails == 0 ? 0 : fails == setsize ? -1 : 1;
}

static void cpu_parse(char *cpu_string, cpu_set_t *cpu_set, size_t setsize)
{
	int rc;

	rc = cpulist_parse(cpu_string, cpu_set, setsize, 1);
	if (rc == 0)
		return;
	if (rc == 2)
		errx(EXIT_FAILURE, _("invalid CPU number in CPU list: %s"), cpu_string);
	errx(EXIT_FAILURE, _("failed to parse CPU list: %s"), cpu_string);
}

static void __attribute__((__noreturn__)) usage(void)
{
	FILE *out = stdout;
	fprintf(out, _(
		"\nUsage:\n"
		" %s [options]\n"), program_invocation_short_name);

	fputs(USAGE_SEPARATOR, out);
	fputs(_("Configure CPUs in a multi-processor system.\n"), out);

	fputs(USAGE_OPTIONS, stdout);
	fputs(_(
		" -e, --enable <cpu-list>       enable cpus\n"
		" -d, --disable <cpu-list>      disable cpus\n"
		" -c, --configure <cpu-list>    configure cpus\n"
		" -g, --deconfigure <cpu-list>  deconfigure cpus\n"
		" -p, --dispatch <mode>         set dispatching mode\n"
		" -r, --rescan                  trigger rescan of cpus\n"
		), stdout);
	printf(USAGE_HELP_OPTIONS(31));

	printf(USAGE_MAN_TAIL("chcpu(8)"));
	exit(EXIT_SUCCESS);
}

int main(int argc, char *argv[])
{
	cpu_set_t *cpu_set;
	size_t setsize;
	int cmd = -1;
	int c, rc;

	static const struct option longopts[] = {
		{ "configure",	required_argument, NULL, 'c' },
		{ "deconfigure",required_argument, NULL, 'g' },
		{ "disable",	required_argument, NULL, 'd' },
		{ "dispatch",	required_argument, NULL, 'p' },
		{ "enable",	required_argument, NULL, 'e' },
		{ "help",	no_argument,       NULL, 'h' },
		{ "rescan",	no_argument,       NULL, 'r' },
		{ "version",	no_argument,       NULL, 'V' },
		{ NULL,		0, NULL, 0 }
	};

	static const ul_excl_t excl[] = {       /* rows and cols in ASCII order */
		{ 'c','d','e','g','p' },
		{ 0 }
	};
	int excl_st[ARRAY_SIZE(excl)] = UL_EXCL_STATUS_INIT;

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	maxcpus = get_max_number_of_cpus();
	if (maxcpus < 1)
		errx(EXIT_FAILURE, _("cannot determine NR_CPUS; aborting"));
	if (path_exist(_PATH_SYS_CPU_ONLINE))
		onlinecpus = path_read_cpulist(maxcpus, _PATH_SYS_CPU_ONLINE);
	setsize = CPU_ALLOC_SIZE(maxcpus);
	cpu_set = CPU_ALLOC(maxcpus);
	if (!cpu_set)
		err(EXIT_FAILURE, _("cpuset_alloc failed"));

	while ((c = getopt_long(argc, argv, "c:d:e:g:hp:rV", longopts, NULL)) != -1) {

		err_exclusive_options(c, longopts, excl, excl_st);

		switch (c) {
		case 'c':
			cmd = CMD_CPU_CONFIGURE;
			cpu_parse(argv[optind - 1], cpu_set, setsize);
			break;
		case 'd':
			cmd = CMD_CPU_DISABLE;
			cpu_parse(argv[optind - 1], cpu_set, setsize);
			break;
		case 'e':
			cmd = CMD_CPU_ENABLE;
			cpu_parse(argv[optind - 1], cpu_set, setsize);
			break;
		case 'g':
			cmd = CMD_CPU_DECONFIGURE;
			cpu_parse(argv[optind - 1], cpu_set, setsize);
			break;
		case 'h':
			usage();
		case 'p':
			if (strcmp("horizontal", argv[optind - 1]) == 0)
				cmd = CMD_CPU_DISPATCH_HORIZONTAL;
			else if (strcmp("vertical", argv[optind - 1]) == 0)
				cmd = CMD_CPU_DISPATCH_VERTICAL;
			else
				errx(EXIT_FAILURE, _("unsupported argument: %s"),
				     argv[optind -1 ]);
			break;
		case 'r':
			cmd = CMD_CPU_RESCAN;
			break;
		case 'V':
			printf(UTIL_LINUX_VERSION);
			return EXIT_SUCCESS;
		default:
			errtryhelp(EXIT_FAILURE);
		}
	}

	if ((argc == 1) || (argc != optind)) {
		warnx(_("bad usage"));
		errtryhelp(EXIT_FAILURE);
	}

	switch (cmd) {
	case CMD_CPU_ENABLE:
		rc = cpu_enable(cpu_set, maxcpus, 1);
		break;
	case CMD_CPU_DISABLE:
		rc = cpu_enable(cpu_set, maxcpus, 0);
		break;
	case CMD_CPU_CONFIGURE:
		rc = cpu_configure(cpu_set, maxcpus, 1);
		break;
	case CMD_CPU_DECONFIGURE:
		rc = cpu_configure(cpu_set, maxcpus, 0);
		break;
	case CMD_CPU_RESCAN:
		rc = cpu_rescan();
		break;
	case CMD_CPU_DISPATCH_HORIZONTAL:
		rc = cpu_set_dispatch(0);
		break;
	case CMD_CPU_DISPATCH_VERTICAL:
		rc = cpu_set_dispatch(1);
		break;
	default:
		rc = -EINVAL;
		break;
	}

	return rc == 0 ? EXIT_SUCCESS :
	        rc < 0 ? EXIT_FAILURE : CHCPU_EXIT_SOMEOK;
}
