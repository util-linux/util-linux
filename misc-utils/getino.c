/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Copyright (c) 2025 Christian Goeschel Ndjomouo <cgoesc2@wgu.edu>
 * Written by Christian Goeschel Ndjomouo <cgoesc2@wgu.edu>
 *
 * getino - get the unique pidfd or namespace inode number for a given PID
 */
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>

#include "c.h"
#include "closestream.h"
#include "nls.h"
#include "pidfd-utils.h"
#include "strutils.h"
#include "optutils.h"
#include "pidutils.h"

/* operation flags that determine for what to get the inode, i.e. pidfs, namespace... */
enum {
	GETINO_PIDFS = 1,
	GETINO_CGROUP_NAMESPACE,
	GETINO_IPC_NAMESPACE,
	GETINO_NET_NAMESPACE,
	GETINO_MNT_NAMESPACE,
	GETINO_PID_NAMESPACE,
	GETINO_TIME_NAMESPACE,
	GETINO_USER_NAMESPACE,
	GETINO_UTS_NAMESPACE,
};

#define IS_NAMESPACE_OP(op) ((op > GETINO_PIDFS && op <= GETINO_UTS_NAMESPACE) ? 1 : 0)
struct getino_context {
	int		op_flag;	/* controls for what to get the inode, i.e. GETINO_* */
	pid_t		pid;		/* PID provided on the command line */
	uint64_t	pidfd_ino;	/* pidfd inode provided on the command line (PID:inode) */
	unsigned int	pidfd_ioctl;	/* pidfs ioctl command for namespace fd */
	bool		print_pid;	/* print the pid and inode, colon-separated */
};

struct ns_desc {
	const char * const 	name;
	unsigned int		ioctl;
};

static struct ns_desc ns_info[] = {
	[GETINO_CGROUP_NAMESPACE] = { .name = "cgroup", .ioctl = PIDFD_GET_CGROUP_NAMESPACE },
	[GETINO_IPC_NAMESPACE] = { .name = "ipc", .ioctl = PIDFD_GET_IPC_NAMESPACE },
	[GETINO_NET_NAMESPACE] = { .name = "network", .ioctl = PIDFD_GET_NET_NAMESPACE },
	[GETINO_MNT_NAMESPACE] = { .name = "mount", .ioctl = PIDFD_GET_MNT_NAMESPACE },
	[GETINO_PID_NAMESPACE] = { .name = "pid", .ioctl = PIDFD_GET_PID_NAMESPACE },
	[GETINO_TIME_NAMESPACE] = { .name = "time", .ioctl = PIDFD_GET_TIME_NAMESPACE },
	[GETINO_USER_NAMESPACE] = { .name = "user", .ioctl = PIDFD_GET_USER_NAMESPACE },
	[GETINO_UTS_NAMESPACE] = { .name = "uts", .ioctl = PIDFD_GET_UTS_NAMESPACE },
};

static int get_pidfd_ns_ioctl(struct getino_context *ctx)
{
	if(!IS_NAMESPACE_OP(ctx->op_flag))
		return -1;

	return ns_info[ctx->op_flag].ioctl;
}

static int pidfd_get_nsfd_or_err(struct getino_context *ctx, int pidfd)
{
	int nsfd, pidfd_ioctl;

	pidfd_ioctl = get_pidfd_ns_ioctl(ctx);
	if (pidfd_ioctl < 0)
		errx(EXIT_FAILURE, _("no appropriate ioctl for the desired namespace"));

	nsfd = ioctl(pidfd, pidfd_ioctl, 0);
	if (nsfd < 0)
		err(EXIT_FAILURE, _("failed to determine %s namespace for process %d"),
			ns_info[ctx->op_flag].name, ctx->pid);
	return nsfd;
}

static void print_inode(struct getino_context *ctx)
{
	int pidfd, target_fd;
	uint64_t ino;

	pidfd = ul_get_valid_pidfd_or_err(ctx->pid, ctx->pidfd_ino);

	if (IS_NAMESPACE_OP(ctx->op_flag)) {
		target_fd = pidfd_get_nsfd_or_err(ctx, pidfd);
		close(pidfd);
	} else {
		target_fd = pidfd;
	}

	ino = pidfd_get_inode(target_fd);

	if (ctx->print_pid) {
		printf("%d:%"PRIu64"\n", ctx->pid, ino);
	} else {
		printf("%"PRIu64"\n", ino);
	}
	close(target_fd);
}

static void __attribute__((__noreturn__)) usage(void)
{
	fputs(USAGE_HEADER, stdout);
	fprintf(stdout, _(" %s [options] PID[:inode]...\n"), program_invocation_short_name);

	fputs(USAGE_SEPARATOR, stdout);
	fputsln(_("Print the unique inode number of a process's file descriptor or namespace."), stdout);

	fputs(USAGE_OPTIONS, stdout);
	fputsln(_(" -p, --print-pid     enable PID:inode format printing"), stdout);
	fputsln(_("     --cgroupns      act on the cgroup namespace"), stdout);
	fputsln(_("     --ipcns         act on the ipc namespace"), stdout);
	fputsln(_("     --mntns         act on the mount namespace"), stdout);
	fputsln(_("     --netns         act on the net namespace"), stdout);
	fputsln(_("     --pidfs         act on the pidfs file descriptor (default)"), stdout);
	fputsln(_("     --pidns         act on the pid namespace"), stdout);
	fputsln(_("     --timens        act on the time namespace"), stdout);
	fputsln(_("     --userns        act on the user namespace"), stdout);
	fputsln(_("     --utsns         act on the uts namespace"), stdout);
	fputs(USAGE_SEPARATOR, stdout);
	fprintf(stdout, USAGE_HELP_OPTIONS(21)); /* char offset to align option descriptions */
	fprintf(stdout, USAGE_MAN_TAIL("getino(1)"));
	exit(EXIT_SUCCESS);
}

int main(int argc, char **argv)
{
	int c, rc = 0;
	struct getino_context ctx = {
		.print_pid = false,
		.op_flag = GETINO_PIDFS,
	};

	enum {
		OPT_PIDFS = CHAR_MAX + 1,
		OPT_CGROUPNS,
		OPT_IPCNS,
		OPT_NETNS,
		OPT_MNTNS,
		OPT_PIDNS,
		OPT_TIMENS,
		OPT_USERNS,
		OPT_UTSNS,
	};

	static const struct option longopts[] = {
		{ "pidfs",                  	no_argument,       NULL, OPT_PIDFS	},
		{ "cgroupns",                  	no_argument,       NULL, OPT_CGROUPNS	},
		{ "ipcns",                  	no_argument,       NULL, OPT_IPCNS      },
		{ "netns",                  	no_argument,       NULL, OPT_NETNS      },
		{ "mntns",                  	no_argument,       NULL, OPT_MNTNS      },
		{ "pidns",                  	no_argument,       NULL, OPT_PIDNS      },
		{ "timens",                  	no_argument,       NULL, OPT_TIMENS     },
		{ "userns",                  	no_argument,       NULL, OPT_USERNS     },
		{ "utsns",                  	no_argument,       NULL, OPT_UTSNS      },
		{ "print-pid",                  no_argument,       NULL, 'p'          	},
		{ "version",                    no_argument,       NULL, 'V'          	},
		{ "help",                       no_argument,       NULL, 'h'          	},
		{ NULL, 0, NULL, 0 }
	};

	static const ul_excl_t excl[] = {       /* rows and cols in ASCII order */
		{ OPT_PIDFS, OPT_CGROUPNS, OPT_IPCNS,
		  OPT_NETNS, OPT_MNTNS, OPT_PIDNS,
		  OPT_TIMENS, OPT_USERNS, OPT_UTSNS },
		{ 0 }
	};
	int excl_st[ARRAY_SIZE(excl)] = UL_EXCL_STATUS_INIT;

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	close_stdout_atexit();

	while ((c = getopt_long(argc, argv, "pVh", longopts, NULL)) != -1) {

		err_exclusive_options(c, longopts, excl, excl_st);

		switch (c) {
		case OPT_PIDFS:
			ctx.op_flag = GETINO_PIDFS;
			break;
		case OPT_CGROUPNS:
			ctx.op_flag = GETINO_CGROUP_NAMESPACE;
			break;
		case OPT_IPCNS:
			ctx.op_flag = GETINO_IPC_NAMESPACE;
			break;
		case OPT_NETNS:
			ctx.op_flag = GETINO_NET_NAMESPACE;
			break;
		case OPT_MNTNS:
			ctx.op_flag = GETINO_MNT_NAMESPACE;
			break;
		case OPT_PIDNS:
			ctx.op_flag = GETINO_PID_NAMESPACE;
			break;
		case OPT_TIMENS:
			ctx.op_flag = GETINO_TIME_NAMESPACE;
			break;
		case OPT_USERNS:
			ctx.op_flag = GETINO_USER_NAMESPACE;
			break;
		case OPT_UTSNS:
			ctx.op_flag = GETINO_UTS_NAMESPACE;
			break;
		case 'p':
      			ctx.print_pid = true;
      			break;
		case 'V':
			print_version(EXIT_SUCCESS);
		case 'h':
			usage();
		default:
			errtryhelp(EXIT_FAILURE);
		}
	}

	if (argc - optind < 1) {
		warnx(_("no process specified"));
		errtryhelp(EXIT_FAILURE);
	}

	argv += optind - 1;
	while (*++argv) {
		rc = ul_parse_pid_str(*argv, &ctx.pid, &ctx.pidfd_ino);
		if (rc)
			err(EXIT_FAILURE, _("invalid PID argument '%s'"), *argv);
		print_inode(&ctx);
	}

	return EXIT_SUCCESS;
}
