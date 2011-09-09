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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
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

#define _PATH_SYS_CPU		"/sys/devices/system/cpu"
#define _PATH_SYS_CPU_RESCAN	_PATH_SYS_CPU "/rescan"
#define _PATH_SYS_CPU_DISPATCH	_PATH_SYS_CPU "/dispatching"

static char pathbuf[PATH_MAX];

enum {
	CMD_CPU_ENABLE	= 0,
	CMD_CPU_DISABLE,
	CMD_CPU_CONFIGURE,
	CMD_CPU_DECONFIGURE,
	CMD_CPU_RESCAN,
	CMD_CPU_DISPATCH_HORIZONTAL,
	CMD_CPU_DISPATCH_VERTICAL,
};

static int path_open(mode_t mode, const char *path, ...)
{
	va_list ap;
	int fd;

	va_start(ap, path);
	vsnprintf(pathbuf, sizeof(pathbuf), path, ap);
	va_end(ap);
	fd = open(pathbuf, mode);
	if (fd == -1)
		err(EXIT_FAILURE, "error: cannot open %s", pathbuf);
	return fd;
}

static int path_exist(const char *path, ...)
{
	va_list ap;

	va_start(ap, path);
	vsnprintf(pathbuf, sizeof(pathbuf), path, ap);
	va_end(ap);
	return access(pathbuf, F_OK) == 0;
}

static int cpu_enable(cpu_set_t *cpu_set, size_t setsize, int enable)
{
	unsigned int cpu;
	int fd, rc;
	char c;

	for (cpu = 0; cpu < setsize; cpu++) {
		if (!CPU_ISSET(cpu, cpu_set))
			continue;
		if (!path_exist(_PATH_SYS_CPU "/cpu%d", cpu)) {
			printf(_("CPU %d does not exist\n"), cpu);
			continue;
		}
		if (!path_exist(_PATH_SYS_CPU "/cpu%d/online", cpu)) {
			printf(_("CPU %d is not hot pluggable\n"), cpu);
			continue;
		}
		fd = path_open(O_RDWR, _PATH_SYS_CPU "/cpu%d/online", cpu);
		if (read(fd, &c, 1) == -1)
			err(EXIT_FAILURE, "error: cannot read from %s", pathbuf);
		if ((c == '1') && (enable == 1)) {
			printf(_("CPU %d is already enabled\n"), cpu);
			continue;
		}
		if ((c == '0') && (enable == 0)) {
			printf(_("CPU %d is already disabled\n"), cpu);
			continue;
		}
		if (enable) {
			rc = write(fd, "1", 1);
			if (rc == -1)
				printf(_("CPU %d enable failed (%s)\n"), cpu,
					strerror(errno));
			else
				printf(_("CPU %d enabled\n"), cpu);
		} else {
			rc = write(fd, "0", 1);
			if (rc == -1)
				printf(_("CPU %d disable failed (%s)\n"), cpu,
					strerror(errno));
			else
				printf(_("CPU %d disabled\n"), cpu);
		}
		close(fd);
	}
	return EXIT_SUCCESS;
}

static int cpu_rescan(void)
{
	int fd;

	if (!path_exist(_PATH_SYS_CPU_RESCAN))
		errx(EXIT_FAILURE, _("This system does not support rescanning of CPUs"));
	fd = path_open(O_WRONLY, _PATH_SYS_CPU_RESCAN);
	if (write(fd, "1", 1) == -1)
		err(EXIT_FAILURE, _("Failed to trigger rescan of CPUs"));
	close(fd);
	return EXIT_SUCCESS;
}

static int cpu_set_dispatch(int mode)
{
	int fd;

	if (!path_exist(_PATH_SYS_CPU_DISPATCH))
		errx(EXIT_FAILURE, _("This system does not support setting "
				     "the dispatching mode of CPUs"));
	fd = path_open(O_WRONLY, _PATH_SYS_CPU_DISPATCH);
	if (mode == 0) {
		if (write(fd, "0", 1) == -1)
			err(EXIT_FAILURE, _("Failed to set horizontal dispatch mode"));
		printf(_("Succesfully set horizontal dispatching mode\n"));
	} else {
		if (write(fd, "1", 1) == -1)
			err(EXIT_FAILURE, _("Failed to set vertical dispatch mode"));
		printf(_("Succesfully set vertical dispatching mode\n"));
	}
	close(fd);
	return EXIT_SUCCESS;
}

static int cpu_configure(cpu_set_t *cpu_set, size_t setsize, int configure)
{
	unsigned int cpu;
	int fd, rc;
	char c;

	for (cpu = 0; cpu < setsize; cpu++) {
		if (!CPU_ISSET(cpu, cpu_set))
			continue;
		if (!path_exist(_PATH_SYS_CPU "/cpu%d", cpu)) {
			printf(_("CPU %d does not exist\n"), cpu);
			continue;
		}
		if (!path_exist(_PATH_SYS_CPU "/cpu%d/configure", cpu)) {
			printf(_("CPU %d is not configurable\n"), cpu);
			continue;
		}
		fd = path_open(O_RDWR, _PATH_SYS_CPU "/cpu%d/configure", cpu);
		if (read(fd, &c, 1) == -1)
			err(EXIT_FAILURE, "error: cannot read from %s", pathbuf);
		if ((c == '1') && (configure == 1)) {
			printf(_("CPU %d is already configured\n"), cpu);
			continue;
		}
		if ((c == '0') && (configure == 0)) {
			printf(_("CPU %d is already deconfigured\n"), cpu);
			continue;
		}
		if (configure) {
			rc = write(fd, "1", 1);
			if (rc == -1)
				printf(_("CPU %d configure failed (%s)\n"), cpu,
					strerror(errno));
			else
				printf(_("CPU %d configured\n"), cpu);
		} else {
			rc = write(fd, "0", 1);
			if (rc == -1)
				printf(_("CPU %d deconfigure failed (%s)\n"), cpu,
					strerror(errno));
			else
				printf(_("CPU %d deconfigured\n"), cpu);
		}
		close(fd);
	}
	return EXIT_SUCCESS;
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

static void __attribute__((__noreturn__)) usage(FILE *out)
{
	fprintf(out, _(
		"\nUsage:\n"
		" %s [options]\n"), program_invocation_short_name);

	puts(_(	"\nOptions:\n"
		"  -h, --help                    print this help\n"
		"  -e, --enable <cpu-list>       enable cpus\n"
		"  -d, --disable <cpu-list>      disable cpus\n"
		"  -c, --configure <cpu-list>    configure cpus\n"
		"  -g, --deconfigure <cpu-list>  deconfigure cpus\n"
		"  -p, --dispatch <mode>         set dispatching mode\n"
		"  -r, --rescan                  trigger rescan of cpus\n"
		"  -V, --version                 output version information and exit\n"));

	exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}

int main(int argc, char *argv[])
{
	cpu_set_t *cpu_set;
	unsigned int ncpus;
	size_t setsize;
	int cmd = -1;
	int c;

	static const struct option longopts[] = {
		{ "configure",	required_argument, 0, 'c' },
		{ "deconfigure",required_argument, 0, 'g' },
		{ "disable",	required_argument, 0, 'd' },
		{ "dispatch",	required_argument, 0, 'p' },
		{ "enable",	required_argument, 0, 'e' },
		{ "help",	no_argument,       0, 'h' },
		{ "rescan",	no_argument,       0, 'r' },
		{ "version",	no_argument,       0, 'V' },
		{ NULL,		0, 0, 0 }
	};

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

	ncpus = get_max_number_of_cpus();
	if (ncpus <= 0)
		errx(EXIT_FAILURE, _("cannot determine NR_CPUS; aborting"));
	setsize = CPU_ALLOC_SIZE(ncpus);
	cpu_set = CPU_ALLOC(ncpus);
	if (!cpu_set)
		err(EXIT_FAILURE, _("cpuset_alloc failed"));

	while ((c = getopt_long(argc, argv, "c:d:e:g:hp:rV", longopts, NULL)) != -1) {
		if (cmd != -1 && strchr("cdegpr", c))
			errx(EXIT_FAILURE,
			     _("configure, deconfigure, disable, dispatch, enable "
			       "and rescan are mutually exclusive"));
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
			usage(stdout);
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
			printf(_("%s from %s\n"), program_invocation_short_name,
			       PACKAGE_STRING);
			return EXIT_SUCCESS;
		default:
			usage(stderr);
		}
	}

	if ((argc == 1) || (argc != optind))
		usage(stderr);

	switch (cmd) {
	case CMD_CPU_ENABLE:
		return cpu_enable(cpu_set, ncpus, 1);
	case CMD_CPU_DISABLE:
		return cpu_enable(cpu_set, ncpus, 0);
	case CMD_CPU_CONFIGURE:
		return cpu_configure(cpu_set, ncpus, 1);
	case CMD_CPU_DECONFIGURE:
		return cpu_configure(cpu_set, ncpus, 0);
	case CMD_CPU_RESCAN:
		return cpu_rescan();
	case CMD_CPU_DISPATCH_HORIZONTAL:
		return cpu_set_dispatch(0);
	case CMD_CPU_DISPATCH_VERTICAL:
		return cpu_set_dispatch(1);
	}
	return EXIT_SUCCESS;
}
