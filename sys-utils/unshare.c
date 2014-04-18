/*
 * unshare(1) - command-line interface for unshare(2)
 *
 * Copyright (C) 2009 Mikhail Gusarov <dottedmag@dottedmag.net>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <errno.h>
#include <getopt.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/mount.h>

/* we only need some defines missing in sys/mount.h, no libmount linkage */
#include <libmount.h>

#include "nls.h"
#include "c.h"
#include "closestream.h"
#include "namespace.h"
#include "exec_shell.h"
#include "xalloc.h"
#include "pathnames.h"
#include "all-io.h"

static void map_id(const char *file, uint32_t from, uint32_t to)
{
	char *buf;
	int fd;

	fd = open(file, O_WRONLY);
	if (fd < 0)
		 err(EXIT_FAILURE, _("cannot open %s"), file);

	xasprintf(&buf, "%u %u 1", from, to);
	if (write_all(fd, buf, strlen(buf)))
		err(EXIT_FAILURE, _("write failed %s"), file);
	free(buf);
	close(fd);
}

static void usage(int status)
{
	FILE *out = status == EXIT_SUCCESS ? stdout : stderr;

	fputs(USAGE_HEADER, out);
	fprintf(out,
	      _(" %s [options] <program> [args...]\n"),	program_invocation_short_name);

	fputs(USAGE_OPTIONS, out);
	fputs(_(" -m, --mount               unshare mounts namespace\n"), out);
	fputs(_(" -u, --uts                 unshare UTS namespace (hostname etc)\n"), out);
	fputs(_(" -i, --ipc                 unshare System V IPC namespace\n"), out);
	fputs(_(" -n, --net                 unshare network namespace\n"), out);
	fputs(_(" -p, --pid                 unshare pid namespace\n"), out);
	fputs(_(" -U, --user                unshare user namespace\n"), out);
	fputs(_(" -f, --fork                fork before launching <program>\n"), out);
	fputs(_("     --mount-proc[=<dir>]  mount proc filesystem first (implies --mount)\n"), out);
	fputs(_(" -r, --map-root-user       map current user to root (implies --user)\n"), out);

	fputs(USAGE_SEPARATOR, out);
	fputs(USAGE_HELP, out);
	fputs(USAGE_VERSION, out);
	fprintf(out, USAGE_MAN_TAIL("unshare(1)"));

	exit(status);
}

int main(int argc, char *argv[])
{
	enum {
		OPT_MOUNTPROC = CHAR_MAX + 1
	};
	static const struct option longopts[] = {
		{ "help", no_argument, 0, 'h' },
		{ "version", no_argument, 0, 'V'},
		{ "mount", no_argument, 0, 'm' },
		{ "uts", no_argument, 0, 'u' },
		{ "ipc", no_argument, 0, 'i' },
		{ "net", no_argument, 0, 'n' },
		{ "pid", no_argument, 0, 'p' },
		{ "user", no_argument, 0, 'U' },
		{ "fork", no_argument, 0, 'f' },
		{ "mount-proc", optional_argument, 0, OPT_MOUNTPROC },
		{ "map-root-user", no_argument, 0, 'r' },
		{ NULL, 0, 0, 0 }
	};

	int unshare_flags = 0;
	int c, forkit = 0, maproot = 0;
	const char *procmnt = NULL;
	uid_t real_euid = geteuid();
	gid_t real_egid = getegid();;

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	while ((c = getopt_long(argc, argv, "+fhVmuinpUr", longopts, NULL)) != -1) {
		switch (c) {
		case 'f':
			forkit = 1;
			break;
		case 'h':
			usage(EXIT_SUCCESS);
		case 'V':
			printf(UTIL_LINUX_VERSION);
			return EXIT_SUCCESS;
		case 'm':
			unshare_flags |= CLONE_NEWNS;
			break;
		case 'u':
			unshare_flags |= CLONE_NEWUTS;
			break;
		case 'i':
			unshare_flags |= CLONE_NEWIPC;
			break;
		case 'n':
			unshare_flags |= CLONE_NEWNET;
			break;
		case 'p':
			unshare_flags |= CLONE_NEWPID;
			break;
		case 'U':
			unshare_flags |= CLONE_NEWUSER;
			break;
		case OPT_MOUNTPROC:
			unshare_flags |= CLONE_NEWNS;
			procmnt = optarg ? optarg : "/proc";
			break;
		case 'r':
			unshare_flags |= CLONE_NEWUSER;
			maproot = 1;
			break;
		default:
			usage(EXIT_FAILURE);
		}
	}

	if (-1 == unshare(unshare_flags))
		err(EXIT_FAILURE, _("unshare failed"));

	if (forkit) {
		int status;
		pid_t pid = fork();

		switch(pid) {
		case -1:
			err(EXIT_FAILURE, _("fork failed"));
		case 0:	/* child */
			break;
		default: /* parent */
			if (waitpid(pid, &status, 0) == -1)
				err(EXIT_FAILURE, _("waitpid failed"));
			if (WIFEXITED(status))
				return WEXITSTATUS(status);
			else if (WIFSIGNALED(status))
				kill(getpid(), WTERMSIG(status));
			err(EXIT_FAILURE, _("child exit failed"));
		}
	}

	if (maproot) {
		map_id(_PATH_PROC_UIDMAP, 0, real_euid);
		map_id(_PATH_PROC_GIDMAP, 0, real_egid);
	}

	if (procmnt &&
	    (mount("none", procmnt, NULL, MS_PRIVATE|MS_REC, NULL) != 0 ||
	     mount("proc", procmnt, "proc", MS_NOSUID|MS_NOEXEC|MS_NODEV, NULL) != 0))
			err(EXIT_FAILURE, _("mount %s failed"), procmnt);

	if (optind < argc) {
		execvp(argv[optind], argv + optind);
		err(EXIT_FAILURE, _("failed to execute %s"), argv[optind]);
	}
	exec_shell();
}
