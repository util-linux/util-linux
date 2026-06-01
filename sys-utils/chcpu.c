/*
 * chcpu - CPU configuration tool
 *
 * Copyright IBM Corp. 2011
 * Author(s): Heiko Carstens <heiko.carstens@de.ibm.com>,
 *
 * Copyright (C) 2026 Christian Goeschel Ndjomouo <cgoesc2@wgu.edu>
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
 * along with this program.  If not, see <https://gnu.org/licenses/>.
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

/* partial success, otherwise we return regular EXIT_{SUCCESS,FAILURE} */
#define CHCPU_EXIT_SOMEOK	64

#define _PATH_SYS_CPU		"/sys/devices/system/cpu"

struct chcpu_context {
	struct path_cxt *sys;		/* _PATH_SYS_CPU handler */

	cpu_set_t	*cpu_set,
			*online_cpus;

	size_t		setsize;
	int		maxcpus,
			ncpus;

	bool		isdump;		/* live system or sys dump ? */
};

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
static int cpu_enable(struct chcpu_context *ctx, int enable)
{
	int cpu;
	int online = -1, rc;
	int configured = -1;
	int fails = 0;

	for (cpu = 0; cpu < ctx->maxcpus; cpu++) {
		if (!CPU_ISSET_S(cpu, ctx->setsize, ctx->cpu_set))
			continue;
		if (ul_path_accessf(ctx->sys, F_OK, "cpu%d", cpu) != 0) {
			warnx(_("CPU %d does not exist"), cpu);
			fails++;
			continue;
		}
		if (ul_path_accessf(ctx->sys, F_OK, "cpu%d/online", cpu) != 0) {
			warnx(_("CPU %d is not hot pluggable"), cpu);
			fails++;
			continue;
		}

		if (ul_path_readf_s32(ctx->sys, &online, "cpu%d/online", cpu) != 0) {
			warnx(_("failed to read CPU %d state"), cpu);
			fails++;
			continue;
		}

		if (online == 1 && enable == 1) {
			printf(_("CPU %d is already enabled\n"), cpu);
			continue;
		} else if (online == 0 && enable == 0) {
			printf(_("CPU %d is already disabled\n"), cpu);
			continue;
		}

		if (ul_path_accessf(ctx->sys, F_OK, "cpu%d/configure", cpu) == 0)
			ul_path_readf_s32(ctx->sys, &configured, "cpu%d/configure", cpu);
		if (enable) {
			rc = ul_path_writef_string(ctx->sys, "1", "cpu%d/online", cpu);
			if (rc != 0 && configured == 0) {
				warn(_("CPU %d enable failed (CPU is deconfigured)"), cpu);
				fails++;
			} else if (rc != 0) {
				warn(_("CPU %d enable failed"), cpu);
				fails++;
			} else
				printf(_("CPU %d enabled\n"), cpu);
		} else {
			if (ctx->online_cpus
					&& CPU_COUNT_S(ctx->setsize, ctx->online_cpus) == 1) {
				warnx(_("CPU %d disable failed (last enabled CPU)"), cpu);
				fails++;
				continue;
			}
			rc = ul_path_writef_string(ctx->sys, "0", "cpu%d/online", cpu);
			if (rc != 0) {
				warn(_("CPU %d disable failed"), cpu);
				fails++;
			} else {
				printf(_("CPU %d disabled\n"), cpu);
				if (ctx->online_cpus)
					CPU_CLR_S(cpu, ctx->setsize, ctx->online_cpus);
			}
		}
	}

	return fails == 0 ? 0 : fails == ctx->ncpus ? -1 : 1;
}

/* returns:   0 = success
 *          < 0 = failure
 */
static int cpu_rescan(struct path_cxt *sys)
{
	if (ul_path_access(sys, F_OK, "rescan") != 0) {
		warnx(_("This system does not support rescanning of CPUs"));
		return -1;
	}
	if (ul_path_write_string(sys, "1", "rescan") != 0) {
		warn(_("Failed to trigger rescan of CPUs"));
		return -1;
	}
	printf(_("Triggered rescan of CPUs\n"));
	return 0;
}

/* returns:   0 = success
 *          < 0 = failure
 */
static int cpu_set_dispatch(struct path_cxt *sys, int mode)
{
	if (ul_path_access(sys, F_OK, "dispatching") != 0) {
		warnx(_("This system does not support setting "
			"the dispatching mode of CPUs"));
		return -1;
	}
	if (mode == 0) {
		if (ul_path_write_string(sys, "0", "dispatching") != 0) {
			warn(_("Failed to set horizontal dispatch mode"));
			return -1;
		}
		printf(_("Successfully set horizontal dispatching mode\n"));
	} else {
		if (ul_path_write_string(sys, "1", "dispatching") != 0) {
			warn(_("Failed to set vertical dispatch mode"));
			return -1;
		}
		printf(_("Successfully set vertical dispatching mode\n"));
	}
	return 0;
}

/* returns:   0 = success
 *          < 0 = failure
 *          > 0 = partial success
 */
static int cpu_configure(struct chcpu_context *ctx, int configure)
{
	int cpu;
	int rc, current = -1;
	int fails = 0;

	for (cpu = 0; cpu < ctx->maxcpus; cpu++) {
		if (!CPU_ISSET_S(cpu, ctx->setsize, ctx->cpu_set))
			continue;
		if (ul_path_accessf(ctx->sys, F_OK, "cpu%d", cpu) != 0) {
			warnx(_("CPU %d does not exist"), cpu);
			fails++;
			continue;
		}
		if (ul_path_accessf(ctx->sys, F_OK, "cpu%d/configure", cpu) != 0) {
			warnx(_("CPU %d is not configurable"), cpu);
			fails++;
			continue;
		}
		if (ul_path_readf_s32(ctx->sys, &current, "cpu%d/configure", cpu) != 0) {
			warnx(_("failed to read CPU %d configuration state"), cpu);
			fails++;
			continue;
		}

		if (current == 1 && configure == 1) {
			printf(_("CPU %d is already configured\n"), cpu);
			continue;
		}
		if (current == 0 && configure == 0) {
			printf(_("CPU %d is already deconfigured\n"), cpu);
			continue;
		}
		if (current == 1 && configure == 0 && ctx->online_cpus &&
				CPU_ISSET_S(cpu, ctx->setsize, ctx->online_cpus)) {
			warnx(_("CPU %d deconfigure failed (CPU is enabled)"), cpu);
			fails++;
			continue;
		}
		if (configure) {
			rc = ul_path_writef_string(ctx->sys, "1", "cpu%d/configure", cpu);
			if (rc != 0) {
				warn(_("CPU %d configure failed"), cpu);
				fails++;
			} else
				printf(_("CPU %d configured\n"), cpu);
		} else {
			rc = ul_path_writef_string(ctx->sys, "0", "cpu%d/configure", cpu);
			if (rc != 0) {
				warn(_("CPU %d deconfigure failed"), cpu);
				fails++;
			} else
				printf(_("CPU %d deconfigured\n"), cpu);
		}
	}

	return fails == 0 ? 0 : fails == ctx->ncpus ? -1 : 1;
}

static void cpu_parse(struct chcpu_context *ctx, char *cpu_string)
{
	int rc;

	rc = cpulist_parse(cpu_string, ctx->cpu_set, ctx->setsize, 1);
	if (rc == 0) {
		ctx->ncpus = CPU_COUNT_S(ctx->setsize, ctx->cpu_set);
		return;
	}
	if (rc == 2)
		errx(EXIT_FAILURE, _("invalid CPU number in CPU list: %s"), cpu_string);
	errx(EXIT_FAILURE, _("failed to parse CPU list: %s"), cpu_string);
}

static void read_cpulist(struct chcpu_context *ctx)
{
	if (ul_path_read_s32(ctx->sys, &ctx->maxcpus, "kernel_max") == 0)
		ctx->maxcpus += 1;
	else if (!ctx->isdump)
		ctx->maxcpus = get_max_number_of_cpus();

	if (ctx->maxcpus <= 0)
		ctx->maxcpus = 2048;

	ctx->setsize = CPU_ALLOC_SIZE(ctx->maxcpus);

	if (ul_path_access(ctx->sys, F_OK, "online") == 0) {
		ul_path_readf_cpulist(ctx->sys, &ctx->cpu_set, ctx->maxcpus, "online");
		ul_path_readf_cpulist(ctx->sys, &ctx->online_cpus, ctx->maxcpus, "online");
	} else {
		ctx->cpu_set = CPU_ALLOC(ctx->maxcpus);
	}

	if (!ctx->cpu_set)
		err(EXIT_FAILURE, _("cpuset_alloc failed"));
}

static void __attribute__((__noreturn__)) usage(void)
{
	FILE *out = stdout;
	fprintf(out, _(
		"\nUsage:\n"
		" %s [options]\n"), program_invocation_short_name);

	fputs(USAGE_SEPARATOR, out);
	fputs(_("Configure CPUs in a multi-processor system.\n"), out);

	fputs(USAGE_OPTIONS, out);
	fputs(_(" -e, --enable <cpu-list>       enable cpus\n"), out);
	fputs(_(" -d, --disable <cpu-list>      disable cpus\n"), out);
	fputs(_(" -c, --configure <cpu-list>    configure cpus\n"), out);
	fputs(_(" -g, --deconfigure <cpu-list>  deconfigure cpus\n"), out);
	fputs(_(" -p, --dispatch <mode>         set dispatching mode\n"), out);
	fputs(_(" -r, --rescan                  trigger rescan of cpus\n"), out);
	fputs(_(" -s, --sysroot <dir>           use the specified directory as system root\n"), out);
	fprintf(out, USAGE_HELP_OPTIONS(31));

	fprintf(out, USAGE_MAN_TAIL("chcpu(8)"));
	exit(EXIT_SUCCESS);
}

int main(int argc, char *argv[])
{
	struct chcpu_context _ctx = { 0 }, *ctx = &_ctx;
	int cmd = -1;
	int c, rc;
	char *sysroot = NULL, *cpu_list_arg = NULL;

	static const struct option longopts[] = {
		{ "configure",	required_argument, NULL, 'c' },
		{ "deconfigure",required_argument, NULL, 'g' },
		{ "disable",	required_argument, NULL, 'd' },
		{ "dispatch",	required_argument, NULL, 'p' },
		{ "enable",	required_argument, NULL, 'e' },
		{ "help",	no_argument,       NULL, 'h' },
		{ "rescan",	no_argument,       NULL, 'r' },
		{ "sysroot",	required_argument, NULL, 's'},
		{ "version",	no_argument,       NULL, 'V' },
		{ NULL,		0, NULL, 0 }
	};

	static const ul_excl_t excl[] = {       /* rows and cols in ASCII order */
		{ 'c','d','e','g','p','r' },
		{ 0 }
	};
	int excl_st[ARRAY_SIZE(excl)] = UL_EXCL_STATUS_INIT;

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	close_stdout_atexit();

	while ((c = getopt_long(argc, argv, "c:d:e:g:hp:rs:V", longopts, NULL)) != -1) {

		err_exclusive_options(c, longopts, excl, excl_st);

		switch (c) {
		case 'c':
			cmd = CMD_CPU_CONFIGURE;
			cpu_list_arg = optarg;
			break;
		case 'd':
			cmd = CMD_CPU_DISABLE;
			cpu_list_arg = optarg;
			break;
		case 'e':
			cmd = CMD_CPU_ENABLE;
			cpu_list_arg = optarg;
			break;
		case 'g':
			cmd = CMD_CPU_DECONFIGURE;
			cpu_list_arg = optarg;
			break;
		case 'p':
			if (strcmp("horizontal", optarg) == 0)
				cmd = CMD_CPU_DISPATCH_HORIZONTAL;
			else if (strcmp("vertical", optarg) == 0)
				cmd = CMD_CPU_DISPATCH_VERTICAL;
			else
				errx(EXIT_FAILURE, _("unsupported argument: %s"),
					optarg);
			break;
		case 'r':
			cmd = CMD_CPU_RESCAN;
			break;
		case 's':
			sysroot = optarg;
			ctx->isdump = true;
			break;
		case 'h':
			usage();
		case 'V':
			print_version(EXIT_SUCCESS);
		default:
			errtryhelp(EXIT_FAILURE);
		}
	}

	if ((argc == 1) || (argc != optind)) {
		warnx(_("bad usage"));
		errtryhelp(EXIT_FAILURE);
	}

	ul_path_init_debug();
	ctx->sys = ul_new_path(_PATH_SYS_CPU);
	if (!ctx->sys)
		err(EXIT_FAILURE, _("failed to initialize sysfs handler"));
	if (sysroot && ul_path_set_prefix(ctx->sys, sysroot) != 0)
		err(EXIT_FAILURE, _("failed to set up different sysroot"));
	if (!ul_path_is_accessible(ctx->sys))
		err(EXIT_FAILURE, _("cannot open %s"), _PATH_SYS_CPU);

	read_cpulist(ctx);
	if (cpu_list_arg)
		cpu_parse(ctx, cpu_list_arg);

	switch (cmd) {
	case CMD_CPU_ENABLE:
		rc = cpu_enable(ctx, 1);
		break;
	case CMD_CPU_DISABLE:
		rc = cpu_enable(ctx, 0);
		break;
	case CMD_CPU_CONFIGURE:
		rc = cpu_configure(ctx, 1);
		break;
	case CMD_CPU_DECONFIGURE:
		rc = cpu_configure(ctx, 0);
		break;
	case CMD_CPU_RESCAN:
		rc = cpu_rescan(ctx->sys);
		break;
	case CMD_CPU_DISPATCH_HORIZONTAL:
		rc = cpu_set_dispatch(ctx->sys, 0);
		break;
	case CMD_CPU_DISPATCH_VERTICAL:
		rc = cpu_set_dispatch(ctx->sys, 1);
		break;
	default:
		rc = -EINVAL;
		break;
	}

	CPU_FREE(ctx->cpu_set);
	CPU_FREE(ctx->online_cpus);
	ul_unref_path(ctx->sys);

	return rc == 0 ? EXIT_SUCCESS :
		rc < 0 ? EXIT_FAILURE : CHCPU_EXIT_SOMEOK;
}
