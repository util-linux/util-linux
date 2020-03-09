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
#include <sys/stat.h>

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
	 * The user namespace comes either first or last: first if
	 * you're using it to increase your privilege and last if
	 * you're using it to decrease.  We enter the namespaces in
	 * two passes starting initially from offset 1 and then offset
	 * 0 if that fails.
	 */
	{ .nstype = CLONE_NEWUSER,  .name = "ns/user", .fd = -1 },
	{ .nstype = CLONE_NEWCGROUP,.name = "ns/cgroup", .fd = -1 },
	{ .nstype = CLONE_NEWIPC,   .name = "ns/ipc",  .fd = -1 },
	{ .nstype = CLONE_NEWUTS,   .name = "ns/uts",  .fd = -1 },
	{ .nstype = CLONE_NEWNET,   .name = "ns/net",  .fd = -1 },
	{ .nstype = CLONE_NEWPID,   .name = "ns/pid",  .fd = -1 },
	{ .nstype = CLONE_NEWNS,    .name = "ns/mnt",  .fd = -1 },
	{ .nstype = CLONE_NEWTIME,  .name = "ns/time", .fd = -1 },
	{ .nstype = 0, .name = NULL, .fd = -1 }
};

static void __attribute__((__noreturn__)) usage(void)
{
	FILE *out = stdout;

	fputs(USAGE_HEADER, out);
	fprintf(out, _(" %s [options] [<program> [<argument>...]]\n"),
		program_invocation_short_name);

	fputs(USAGE_SEPARATOR, out);
	fputs(_("Run a program with namespaces of other processes.\n"), out);

	fputs(USAGE_OPTIONS, out);
	fputs(_(" -a, --all              enter all namespaces\n"), out);
	fputs(_(" -t, --target <pid>     target process to get namespaces from\n"), out);
	fputs(_(" -m, --mount[=<file>]   enter mount namespace\n"), out);
	fputs(_(" -u, --uts[=<file>]     enter UTS namespace (hostname etc)\n"), out);
	fputs(_(" -i, --ipc[=<file>]     enter System V IPC namespace\n"), out);
	fputs(_(" -n, --net[=<file>]     enter network namespace\n"), out);
	fputs(_(" -p, --pid[=<file>]     enter pid namespace\n"), out);
	fputs(_(" -C, --cgroup[=<file>]  enter cgroup namespace\n"), out);
	fputs(_(" -U, --user[=<file>]    enter user namespace\n"), out);
	fputs(_(" -T, --time[=<file>]    enter time namespace\n"), out);
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
	printf(USAGE_HELP_OPTIONS(24));
	printf(USAGE_MAN_TAIL("nsenter(1)"));

	exit(EXIT_SUCCESS);
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

static int get_ns_ino(const char *path, ino_t *ino)
{
	struct stat st;

	if (stat(path, &st) != 0)
		return -errno;
	*ino = st.st_ino;
	return 0;
}

static int is_same_namespace(pid_t a, pid_t b, const char *type)
{
	char path[PATH_MAX];
	ino_t a_ino = 0, b_ino = 0;

	snprintf(path, sizeof(path), "/proc/%u/%s", a, type);
	if (get_ns_ino(path, &a_ino) != 0)
		err(EXIT_FAILURE, _("stat of %s failed"), path);

	snprintf(path, sizeof(path), "/proc/%u/%s", b, type);
	if (get_ns_ino(path, &b_ino) != 0)
		err(EXIT_FAILURE, _("stat of %s failed"), path);

	return a_ino == b_ino;
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
		{ "all", no_argument, NULL, 'a' },
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
		{ "time", optional_argument, NULL, 'T' },
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
	int c, pass, namespaces = 0, setgroups_nerrs = 0, preserve_cred = 0;
	bool do_rd = false, do_wd = false, force_uid = false, force_gid = false;
	bool do_all = false;
	int do_fork = -1; /* unknown yet */
	uid_t uid = 0;
	gid_t gid = 0;
#ifdef HAVE_LIBSELINUX
	bool selinux = 0;
#endif

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	close_stdout_atexit();

	while ((c =
		getopt_long(argc, argv, "+ahVt:m::u::i::n::p::C::U::T::S:G:r::w::FZ",
			    longopts, NULL)) != -1) {
		switch (c) {
		case 'a':
			do_all = true;
			break;
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
		case 'T':
			if (optarg)
				open_namespace_fd(CLONE_NEWTIME, optarg);
			else
				namespaces |= CLONE_NEWTIME;
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
		case 'h':
			usage();
		case 'V':
			print_version(EXIT_SUCCESS);
		default:
			errtryhelp(EXIT_FAILURE);
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

	if (do_all) {
		if (!namespace_target_pid)
			errx(EXIT_FAILURE, _("no target PID specified for --all"));
		for (nsfile = namespace_files; nsfile->nstype; nsfile++) {
			if (nsfile->fd >= 0)
				continue;	/* namespace already specified */

			/* It is not permitted to use setns(2) to reenter the caller's
			 * current user namespace; see setns(2) man page for more details.
			 */
			if (nsfile->nstype & CLONE_NEWUSER
			    && is_same_namespace(getpid(), namespace_target_pid, nsfile->name))
				continue;

			namespaces |= nsfile->nstype;
		}
	}

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
	 * Now that we know which namespaces we want to enter, enter
	 * them.  Do this in two passes, not entering the user
	 * namespace on the first pass.  So if we're deprivileging the
	 * container we'll enter the user namespace last and if we're
	 * privileging it then we enter the user namespace first
	 * (because the initial setns will fail).
	 */
	for (pass = 0; pass < 2; pass ++) {
		for (nsfile = namespace_files + 1 - pass; nsfile->nstype; nsfile++) {
			if (nsfile->fd < 0)
				continue;
			if (nsfile->nstype == CLONE_NEWPID && do_fork == -1)
				do_fork = 1;
			if (setns(nsfile->fd, nsfile->nstype)) {
				if (pass != 0)
					err(EXIT_FAILURE,
					    _("reassociate to namespace '%s' failed"),
					    nsfile->name);
				else
					continue;
			}

			close(nsfile->fd);
			nsfile->fd = -1;
		}
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
		errexec(argv[optind]);
	}
	exec_shell();
}
