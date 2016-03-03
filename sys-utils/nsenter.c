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

#include <dirent.h>
#include <errno.h>
#include <getopt.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <grp.h>

#ifdef HAVE_LIBSELINUX
# include <selinux/selinux.h>
#endif

#include "strutils.h"
#include "nls.h"
#include "c.h"
#include "closestream.h"
#include "namespace.h"
#include "exec_shell.h"

static struct namespace_file {
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
	{ .nstype = CLONE_NEWUSER,  .name = "ns/user", .fd = -1 },
	{ .nstype = CLONE_NEWCGROUP,.name = "ns/cgroup", .fd = -1 },
	{ .nstype = CLONE_NEWIPC,   .name = "ns/ipc",  .fd = -1 },
	{ .nstype = CLONE_NEWUTS,   .name = "ns/uts",  .fd = -1 },
	{ .nstype = CLONE_NEWNET,   .name = "ns/net",  .fd = -1 },
	{ .nstype = CLONE_NEWPID,   .name = "ns/pid",  .fd = -1 },
	{ .nstype = CLONE_NEWNS,    .name = "ns/mnt",  .fd = -1 },
	{ .nstype = 0, .name = NULL, .fd = -1 }
};

static void usage(int status)
{
	FILE *out = status == EXIT_SUCCESS ? stdout : stderr;

	fputs(USAGE_HEADER, out);
	fprintf(out, _(" %s [options] <program> [<argument>...]\n"),
		program_invocation_short_name);

	fputs(USAGE_SEPARATOR, out);
	fputs(_("Run a program with namespaces of other processes.\n"), out);

	fputs(USAGE_OPTIONS, out);
	fputs(_(" -t, --target <pid>     target process to get namespaces from\n"), out);
	fputs(_(" -m, --mount[=<file>]   enter mount namespace\n"), out);
	fputs(_(" -u, --uts[=<file>]     enter UTS namespace (hostname etc)\n"), out);
	fputs(_(" -i, --ipc[=<file>]     enter System V IPC namespace\n"), out);
	fputs(_(" -n, --net[=<file>]     enter network namespace\n"), out);
	fputs(_(" -p, --pid[=<file>]     enter pid namespace\n"), out);
	fputs(_(" -C, --cgroup[=<file>]  enter cgroup namespace\n"), out);
	fputs(_(" -U, --user[=<file>]    enter user namespace\n"), out);
	fputs(_(" -S, --setuid <uid>     set uid in entered namespace\n"), out);
	fputs(_(" -G, --setgid <gid>     set gid in entered namespace\n"), out);
	fputs(_("     --preserve-credentials do not touch uids or gids\n"), out);
	fputs(_(" -r, --root[=<dir>]     set the root directory\n"), out);
	fputs(_(" -w, --wd[=<dir>]       set the working directory\n"), out);
	fputs(_(" -F, --no-fork          do not fork before exec'ing <program>\n"), out);
#ifdef HAVE_LIBSELINUX
	fputs(_(" -Z, --follow-context   set SELinux context according to --target PID\n"), out);
#endif

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
		errx(EXIT_FAILURE,
		     _("neither filename nor target pid supplied for %s"),
		     type);

	if (*fd >= 0)
		close(*fd);

	*fd = open(path, O_RDONLY);
	if (*fd < 0)
		err(EXIT_FAILURE, _("cannot open %s"), path);
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
	assert(nsfile->nstype);
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
	} else if (WIFSIGNALED(status)) {
		kill(getpid(), WTERMSIG(status));
	}
	exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
	enum {
		OPT_PRESERVE_CRED = CHAR_MAX + 1
	};
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
		{ "cgroup", optional_argument, NULL, 'C' },
		{ "setuid", required_argument, NULL, 'S' },
		{ "setgid", required_argument, NULL, 'G' },
		{ "root", optional_argument, NULL, 'r' },
		{ "wd", optional_argument, NULL, 'w' },
		{ "no-fork", no_argument, NULL, 'F' },
		{ "preserve-credentials", no_argument, NULL, OPT_PRESERVE_CRED },
#ifdef HAVE_LIBSELINUX
		{ "follow-context", no_argument, NULL, 'Z' },
#endif
		{ NULL, 0, NULL, 0 }
	};

	struct namespace_file *nsfile;
	int c, namespaces = 0, setgroups_nerrs = 0, preserve_cred = 0;
	bool do_rd = false, do_wd = false, force_uid = false, force_gid = false;
	int do_fork = -1; /* unknown yet */
	uid_t uid = 0;
	gid_t gid = 0;
#ifdef HAVE_LIBSELINUX
	bool selinux = 0;
#endif

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	while ((c =
		getopt_long(argc, argv, "+hVt:m::u::i::n::p::C::U::S:G:r::w::FZ",
			    longopts, NULL)) != -1) {
		switch (c) {
		case 'h':
			usage(EXIT_SUCCESS);
		case 'V':
			printf(UTIL_LINUX_VERSION);
			return EXIT_SUCCESS;
		case 't':
			namespace_target_pid =
			    strtoul_or_err(optarg, _("failed to parse pid"));
			break;
		case 'm':
			if (optarg)
				open_namespace_fd(CLONE_NEWNS, optarg);
			else
				namespaces |= CLONE_NEWNS;
			break;
		case 'u':
			if (optarg)
				open_namespace_fd(CLONE_NEWUTS, optarg);
			else
				namespaces |= CLONE_NEWUTS;
			break;
		case 'i':
			if (optarg)
				open_namespace_fd(CLONE_NEWIPC, optarg);
			else
				namespaces |= CLONE_NEWIPC;
			break;
		case 'n':
			if (optarg)
				open_namespace_fd(CLONE_NEWNET, optarg);
			else
				namespaces |= CLONE_NEWNET;
			break;
		case 'p':
			if (optarg)
				open_namespace_fd(CLONE_NEWPID, optarg);
			else
				namespaces |= CLONE_NEWPID;
			break;
		case 'C':
			if (optarg)
				open_namespace_fd(CLONE_NEWCGROUP, optarg);
			else
				namespaces |= CLONE_NEWCGROUP;
			break;
		case 'U':
			if (optarg)
				open_namespace_fd(CLONE_NEWUSER, optarg);
			else
				namespaces |= CLONE_NEWUSER;
			break;
		case 'S':
			uid = strtoul_or_err(optarg, _("failed to parse uid"));
			force_uid = true;
			break;
		case 'G':
			gid = strtoul_or_err(optarg, _("failed to parse gid"));
			force_gid = true;
			break;
		case 'F':
			do_fork = 0;
			break;
		case 'r':
			if (optarg)
				open_target_fd(&root_fd, "root", optarg);
			else
				do_rd = true;
			break;
		case 'w':
			if (optarg)
				open_target_fd(&wd_fd, "cwd", optarg);
			else
				do_wd = true;
			break;
		case OPT_PRESERVE_CRED:
			preserve_cred = 1;
			break;
#ifdef HAVE_LIBSELINUX
		case 'Z':
			selinux = 1;
			break;
#endif
		default:
			usage(EXIT_FAILURE);
		}
	}

#ifdef HAVE_LIBSELINUX
	if (selinux && is_selinux_enabled() > 0) {
		char *scon = NULL;

		if (!namespace_target_pid)
			errx(EXIT_FAILURE, _("no target PID specified for --follow-context"));
		if (getpidcon(namespace_target_pid, &scon) < 0)
			errx(EXIT_FAILURE, _("failed to get %d SELinux context"),
					(int) namespace_target_pid);
		if (setexeccon(scon) < 0)
			errx(EXIT_FAILURE, _("failed to set exec context to '%s'"), scon);
		freecon(scon);
	}
#endif
	/*
	 * Open remaining namespace and directory descriptors.
	 */
	for (nsfile = namespace_files; nsfile->nstype; nsfile++)
		if (nsfile->nstype & namespaces)
			open_namespace_fd(nsfile->nstype, NULL);
	if (do_rd)
		open_target_fd(&root_fd, "root", NULL);
	if (do_wd)
		open_target_fd(&wd_fd, "cwd", NULL);

	/*
	 * Update namespaces variable to contain all requested namespaces
	 */
	for (nsfile = namespace_files; nsfile->nstype; nsfile++) {
		if (nsfile->fd < 0)
			continue;
		namespaces |= nsfile->nstype;
	}

	/* for user namespaces we always set UID and GID (default is 0)
	 * and clear root's groups if --preserve-credentials is no specified */
	if ((namespaces & CLONE_NEWUSER) && !preserve_cred) {
		force_uid = true, force_gid = true;

		/* We call setgroups() before and after we enter user namespace,
		 * let's complain only if both fail */
		if (setgroups(0, NULL) != 0)
			setgroups_nerrs++;
	}

	/*
	 * Now that we know which namespaces we want to enter, enter them.
	 */
	for (nsfile = namespace_files; nsfile->nstype; nsfile++) {
		if (nsfile->fd < 0)
			continue;
		if (nsfile->nstype == CLONE_NEWPID && do_fork == -1)
			do_fork = 1;
		if (setns(nsfile->fd, nsfile->nstype))
			err(EXIT_FAILURE,
			    _("reassociate to namespace '%s' failed"),
			    nsfile->name);
		close(nsfile->fd);
		nsfile->fd = -1;
	}

	/* Remember the current working directory if I'm not changing it */
	if (root_fd >= 0 && wd_fd < 0) {
		wd_fd = open(".", O_RDONLY);
		if (wd_fd < 0)
			err(EXIT_FAILURE,
			    _("cannot open current working directory"));
	}

	/* Change the root directory */
	if (root_fd >= 0) {
		if (fchdir(root_fd) < 0)
			err(EXIT_FAILURE,
			    _("change directory by root file descriptor failed"));

		if (chroot(".") < 0)
			err(EXIT_FAILURE, _("chroot failed"));

		close(root_fd);
		root_fd = -1;
	}

	/* Change the working directory */
	if (wd_fd >= 0) {
		if (fchdir(wd_fd) < 0)
			err(EXIT_FAILURE,
			    _("change directory by working directory file descriptor failed"));

		close(wd_fd);
		wd_fd = -1;
	}

	if (do_fork == 1)
		continue_as_child();

	if (force_uid || force_gid) {
		if (force_gid && setgroups(0, NULL) != 0 && setgroups_nerrs)	/* drop supplementary groups */
			err(EXIT_FAILURE, _("setgroups failed"));
		if (force_gid && setgid(gid) < 0)		/* change GID */
			err(EXIT_FAILURE, _("setgid failed"));
		if (force_uid && setuid(uid) < 0)		/* change UID */
			err(EXIT_FAILURE, _("setuid failed"));
	}

	if (optind < argc) {
		execvp(argv[optind], argv + optind);
		err(EXIT_FAILURE, _("failed to execute %s"), argv[optind]);
	}
	exec_shell();
}
