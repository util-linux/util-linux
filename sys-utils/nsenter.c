/*
 * nsenter(1) - command-line interface for setns(2)
 *
 * Copyright (C) 2012-2013 Eric Biederman <ebiederm@xmission.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; version 2.
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

#include <sys/types.h>
#include <sys/wait.h>
#include <dirent.h>
#include <errno.h>
#include <getopt.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "strutils.h"
#include "nls.h"
#include "c.h"
#include "closestream.h"
#include "namespace.h"

static struct namespace_file{
	int nstype;
	const char *name;
	int fd;
} namespace_files[] = {
	/* Careful the order is significant in this array.
	 *
	 * The user namespace comes first, so that it is entered
	 * first.  This gives an unprivileged user the potential to
	 * enter the other namespaces.
	 */
	{ .nstype = CLONE_NEWUSER, .name = "ns/user", .fd = -1 },
	{ .nstype = CLONE_NEWIPC,  .name = "ns/ipc",  .fd = -1 },
	{ .nstype = CLONE_NEWUTS,  .name = "ns/uts",  .fd = -1 },
	{ .nstype = CLONE_NEWNET,  .name = "ns/net",  .fd = -1 },
	{ .nstype = CLONE_NEWPID,  .name = "ns/pid",  .fd = -1 },
	{ .nstype = CLONE_NEWNS,   .name = "ns/mnt",  .fd = -1 },
	{}
};

static void usage(int status)
{
	FILE *out = status == EXIT_SUCCESS ? stdout : stderr;

	fputs(USAGE_HEADER, out);
	fprintf(out, _(" %s [options] <program> [args...]\n"),
		program_invocation_short_name);

	fputs(USAGE_OPTIONS, out);
	fputs(_(" -t, --target <pid>     target process to get namespaces from\n"
		" -m, --mount [=<file>]  enter mount namespace\n"
		" -u, --uts   [=<file>]  enter UTS namespace (hostname etc)\n"
		" -i, --ipc   [=<file>]  enter System V IPC namespace\n"
		" -n, --net   [=<file>]  enter network namespace\n"
		" -p, --pid   [=<file>]  enter pid namespace\n"
		" -U, --user  [=<file>]  enter user namespace\n"
		" -e, --exec             don't fork before exec'ing <program>\n"
		" -r, --root  [=<dir>]   set the root directory\n"
		" -w, --wd    [=<dir>]   set the working directory\n"), out);
	fputs(USAGE_SEPARATOR, out);
	fputs(USAGE_HELP, out);
	fputs(USAGE_VERSION, out);
	fprintf(out, USAGE_MAN_TAIL("nsenter(1)"));

	exit(status);
}

static pid_t namespace_target_pid = 0;
static int root_fd = -1;
static int wd_fd = -1;

static void open_target_fd(int *fd, const char *type, const char *path)
{
	char pathbuf[PATH_MAX];

	if (!path && namespace_target_pid) {
		snprintf(pathbuf, sizeof(pathbuf), "/proc/%u/%s",
			namespace_target_pid, type);
		path = pathbuf;
	}
	if (!path)
		err(EXIT_FAILURE, _("No filename and no target pid supplied for %s"),
		    type);

	if (*fd >= 0)
		close(*fd);

	*fd = open(path, O_RDONLY);
	if (*fd < 0)
		err(EXIT_FAILURE, _("open of '%s' failed"), path);
}

static void open_namespace_fd(int nstype, const char *path)
{
	struct namespace_file *nsfile;

	for (nsfile = namespace_files; nsfile->nstype; nsfile++) {
		if (nstype != nsfile->nstype)
			continue;

		open_target_fd(&nsfile->fd, nsfile->name, path);
		return;
	}
	/* This should never happen */
	err(EXIT_FAILURE, "Unrecognized namespace type");
}

static void continue_as_child(void)
{
	pid_t child = fork();
	int status;
	pid_t ret;

	if (child < 0)
		err(EXIT_FAILURE, _("fork failed"));

	/* Only the child returns */
	if (child == 0)
		return;

	for (;;) {
		ret = waitpid(child, &status, WUNTRACED);
		if ((ret == child) && (WIFSTOPPED(status))) {
			/* The child suspended so suspend us as well */
			kill(getpid(), SIGSTOP);
			kill(child, SIGCONT);
		} else {
			break;
		}
	}
	/* Return the child's exit code if possible */
	if (WIFEXITED(status)) {
		exit(WEXITSTATUS(status));
	}
	else if (WIFSIGNALED(status)) {
		kill(getpid(), WTERMSIG(status));
	}
	exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
	static const struct option longopts[] = {
		{ "help", no_argument, NULL, 'h' },
		{ "version", no_argument, NULL, 'V'},
		{ "target", required_argument, NULL, 't' },
		{ "mount", optional_argument, NULL, 'm' },
		{ "uts", optional_argument, NULL, 'u' },
		{ "ipc", optional_argument, NULL, 'i' },
		{ "net", optional_argument, NULL, 'n' },
		{ "pid", optional_argument, NULL, 'p' },
		{ "user", optional_argument, NULL, 'U' },
		{ "exec", no_argument, NULL, 'e' },
		{ "root", optional_argument, NULL, 'r' },
		{ "wd", optional_argument, NULL, 'w' },
		{ NULL, 0, NULL, 0 }
	};

	struct namespace_file *nsfile;
	int do_fork = 0;
	int c;

	setlocale(LC_MESSAGES, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	while((c = getopt_long(argc, argv, "hVt:m::u::i::n::p::U::er::w::", longopts, NULL)) != -1) {
		switch(c) {
		case 'h':
			usage(EXIT_SUCCESS);
		case 'V':
			printf(UTIL_LINUX_VERSION);
			return EXIT_SUCCESS;
		case 't':
			namespace_target_pid = strtoul_or_err(optarg, _("failed to parse pid"));
			break;
		case 'm':
			open_namespace_fd(CLONE_NEWNS, optarg);
			break;
		case 'u':
			open_namespace_fd(CLONE_NEWUTS, optarg);
			break;
		case 'i':
			open_namespace_fd(CLONE_NEWIPC, optarg);
			break;
		case 'n':
			open_namespace_fd(CLONE_NEWNET, optarg);
			break;
		case 'p':
			do_fork = 1;
			open_namespace_fd(CLONE_NEWPID, optarg);
			break;
		case 'U':
			open_namespace_fd(CLONE_NEWUSER, optarg);
			break;
		case 'e':
			do_fork = 0;
			break;
		case 'r':
			open_target_fd(&root_fd, "root", optarg);
			break;
		case 'w':
			open_target_fd(&wd_fd, "cwd", optarg);
			break;
		default:
			usage(EXIT_FAILURE);
		}
	}

	if(optind >= argc)
		usage(EXIT_FAILURE);

	/*
	 * Now that we know which namespaces we want to enter, enter them.
	 */
	for (nsfile = namespace_files; nsfile->nstype; nsfile++) {
		if (nsfile->fd < 0)
			continue;
		if (setns(nsfile->fd, nsfile->nstype))
			err(EXIT_FAILURE, _("setns of '%s' failed"),
			    nsfile->name);
		close(nsfile->fd);
		nsfile->fd = -1;
	}

	/* Remember the current working directory if I'm not changing it */
	if (root_fd >= 0 && wd_fd < 0) {
		wd_fd = open(".", O_RDONLY);
		if (wd_fd < 0)
			err(EXIT_FAILURE, _("open of . failed"));
	}

	/* Change the root directory */
	if (root_fd >= 0) {
		if (fchdir(root_fd) < 0)
			err(EXIT_FAILURE, _("fchdir to root_fd failed"));

		if (chroot(".") < 0)
			err(EXIT_FAILURE, _("chroot failed"));

		close(root_fd);
		root_fd = -1;
	}

	/* Change the working directory */
	if (wd_fd >= 0) {
		if (fchdir(wd_fd) < 0)
			err(EXIT_FAILURE, _("fchdir to wd_fd failed"));

		close(wd_fd);
		wd_fd = -1;
	}

	if (do_fork)
		continue_as_child();

	execvp(argv[optind], argv + optind);

	err(EXIT_FAILURE, _("exec %s failed"), argv[optind]);
}
