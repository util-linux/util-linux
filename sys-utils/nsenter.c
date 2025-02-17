/*
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; version 2.
 *
 * Copyright (C) 2012-2023 Eric Biederman <ebiederm@xmission.com>
 *
 * nsenter(1) - command-line interface for setns(2)
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
#include <sys/statfs.h>

#include <sys/ioctl.h>
#include <linux/sockios.h>
#ifdef HAVE_LINUX_NSFS_H
# include <linux/nsfs.h>
#endif
#ifndef NS_GET_USERNS
# define NS_GET_USERNS           _IO(0xb7, 0x1)
#endif

#ifdef HAVE_LIBSELINUX
# include <selinux/selinux.h>
#endif

#include "strutils.h"
#include "nls.h"
#include "c.h"
#include "closestream.h"
#include "namespace.h"
#include "exec_shell.h"
#include "optutils.h"
#include "xalloc.h"
#include "all-io.h"
#include "env.h"
#include "caputils.h"
#include "statfs_magic.h"
#include "pathnames.h"
#include "pidfd-utils.h"
#include "linux_version.h"

static struct namespace_file {
	int nstype;
	const char *name;
	int fd;
	bool enabled;
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
	fputs(_(" -N, --net-socket <fd>  enter socket's network namespace (use with --target)\n"), out);
	fputs(_(" -p, --pid[=<file>]     enter pid namespace\n"), out);
	fputs(_(" -C, --cgroup[=<file>]  enter cgroup namespace\n"), out);
	fputs(_(" -U, --user[=<file>]    enter user namespace\n"), out);
	fputs(_("     --user-parent      enter parent user namespace\n"), out);
	fputs(_(" -T, --time[=<file>]    enter time namespace\n"), out);
	fputs(_(" -S, --setuid[=<uid>]   set uid in entered namespace\n"), out);
	fputs(_(" -G, --setgid[=<gid>]   set gid in entered namespace\n"), out);
	fputs(_("     --preserve-credentials do not touch uids or gids\n"), out);
	fputs(_("     --keep-caps        retain capabilities granted in user namespaces\n"), out);
	fputs(_(" -r, --root[=<dir>]     set the root directory\n"), out);
	fputs(_(" -w, --wd[=<dir>]       set the working directory\n"), out);
	fputs(_(" -W, --wdns <dir>       set the working directory in namespace\n"), out);
	fputs(_(" -e, --env              inherit environment variables from target process\n"), out);
	fputs(_(" -F, --no-fork          do not fork before exec'ing <program>\n"), out);
	fputs(_(" -c, --join-cgroup      join the cgroup of the target process\n"), out);
#ifdef HAVE_LIBSELINUX
	fputs(_(" -Z, --follow-context   set SELinux context according to --target PID\n"), out);
#endif

	fputs(USAGE_SEPARATOR, out);
	fprintf(out, USAGE_HELP_OPTIONS(24));
	fprintf(out, USAGE_MAN_TAIL("nsenter(1)"));

	exit(EXIT_SUCCESS);
}

static pid_t namespace_target_pid = 0;
static int root_fd = -1;
static int wd_fd = -1;
static int env_fd = -1;
static int uid_gid_fd = -1;
static int cgroup_procs_fd = -1;

static inline struct namespace_file *__next_nsfile(struct namespace_file *n, int namespaces, bool enabled)
{
	if (!n)
		n = namespace_files;
	else if (n->nstype != 0)
		n++;

	for ( ; n && n->nstype; n++) {
		if (namespaces && !(n->nstype & namespaces))
			continue;
		if (enabled && !n->enabled)
			continue;
		return n;
	}

	return NULL;
}

#define next_nsfile(_n, _ns)		__next_nsfile(_n, _ns, 0)
#define next_enabled_nsfile(_n, _ns)	__next_nsfile(_n, _ns, 1)

#define get_nsfile(_ns)			__next_nsfile(NULL, _ns, 0)
#define get_enabled_nsfile(_ns)		__next_nsfile(NULL, _ns, 1)

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

static void enable_nsfile(struct namespace_file *n, const char *path)
{
	if (path)
		open_target_fd(&n->fd, n->name, path);
	n->enabled = true;
}

static void disable_nsfile(struct namespace_file *n)
{
	if (n->fd >= 0)
		close(n->fd);
	n->fd = -1;
	n->enabled = false;
}

/* Enable namespace; optionally open @path if not NULL. */
static void enable_namespace(int nstype, const char *path)
{
	struct namespace_file *nsfile = get_nsfile(nstype);

	if (nsfile)
		enable_nsfile(nsfile, path);
	else
		assert(nsfile);
}

static void disable_namespaces(int namespaces)
{
	struct namespace_file *n = NULL;

	while ((n = next_enabled_nsfile(n, namespaces)))
		disable_nsfile(n);
}

/* Returns mask of all enabled namespaces */
static int get_namespaces(void)
{
	struct namespace_file *n = NULL;
	int mask = 0;

	while ((n = next_enabled_nsfile(n, 0)))
		mask |= n->nstype;
	return mask;
}

static int get_namespaces_without_fd(void)
{
	struct namespace_file *n = NULL;
	int mask = 0;

	while ((n = next_enabled_nsfile(n, 0))) {
		if (n->fd < 0)
			mask |= n->nstype;
	}

	return mask;
}

/* Open /proc/#/ns/ files for enabled namespaces specified in @namespaces
 * if they have not been opened yet. */
static void open_namespaces(int namespaces)
{
	struct namespace_file *n = NULL;

	while ((n = next_enabled_nsfile(n, namespaces))) {
		if (n->fd < 0)
			open_target_fd(&n->fd, n->name, NULL);
	}
}

static int do_setns(int fd, int ns, const char *name, bool ignore_errors)
{
	int rc = setns(fd, ns);

	if (rc < 0 && !ignore_errors) {
		if (name)
			err(EXIT_FAILURE, _("reassociate to namespace '%s' failed"), name);
		else
			err(EXIT_FAILURE, _("reassociate to namespaces failed"));
	}
	return rc;
}

static void enter_namespaces(int pid_fd, int namespaces, bool ignore_errors)
{
	struct namespace_file *n =  NULL;

	if (pid_fd) {
		int ns = 0;
		while ((n = next_enabled_nsfile(n, namespaces))) {
			if (n->fd < 0)
				ns |= n->nstype;
		}
		if (ns && do_setns(pid_fd, ns, NULL, ignore_errors) == 0)
			disable_namespaces(ns);
	}

	n = NULL;
	while ((n = next_enabled_nsfile(n, namespaces))) {
		if (n->fd < 0)
			continue;
		if (do_setns(n->fd, n->nstype, n->name, ignore_errors) == 0)
			disable_nsfile(n);
	}
}

static void open_parent_user_ns_fd(int pid_fd)
{
	struct namespace_file *user = NULL;
	int fd = -1, parent_fd = -1;
	bool islocal = false;

	/* try user namespace if FD defined */
	user = get_nsfile(CLONE_NEWUSER);
	if (user->enabled)
		fd = user->fd;

	/* try pidfd to get FD */
	if (fd < 0 && pid_fd >= 0) {
		fd = ioctl(pid_fd, PIDFD_GET_USER_NAMESPACE, 0);
		if (fd >= 0)
			islocal = true;
	}

	/* try any enabled namespace */
	if (fd < 0) {
		struct namespace_file *n = get_enabled_nsfile(0);
		if (n)
			fd = n->fd;
	}

	/* try directly open the NS */
	if (fd < 0) {
		open_target_fd(&fd, "ns/user", NULL);
		islocal = true;
	}

	parent_fd = ioctl(fd, NS_GET_USERNS);
	if (parent_fd < 0)
		err(EXIT_FAILURE, _("failed to open parent namespace"));

	if (islocal)
		close(fd);
	if (user->fd > 0)
		close(user->fd);
	user->fd = parent_fd;
	user->enabled = true;
}


static void open_target_sk_netns(int pidfd, int sock_fd)
{
	struct namespace_file *nsfile;
	struct stat sb;
	int sk, nsfd;
	bool local_fd = false;

	nsfile = get_nsfile(CLONE_NEWNET);
	assert(nsfile->nstype);

	if (pidfd < 0) {
		pidfd = pidfd_open(namespace_target_pid, 0);
		if (pidfd < 0)
			err(EXIT_FAILURE, _("failed to pidfd_open() for %d"), namespace_target_pid);
		local_fd = true;
	}

	sk = pidfd_getfd(pidfd, sock_fd, 0);
	if (sk < 0)
		err(EXIT_FAILURE, _("pidfd_getfd(%d, %u)"), pidfd, sock_fd);

	if (fstat(sk, &sb) < 0)
		err(EXIT_FAILURE, _("fstat(%d)"), sk);

	nsfd = ioctl(sk, SIOCGSKNS);
	if (nsfd < 0)
		err(EXIT_FAILURE, _("ioctl(%d, SIOCGSKNS)"), sk);

	if (nsfile->fd >= 0)
		close(nsfile->fd);
	nsfile->fd = nsfd;
	nsfile->enabled = true;
	close(sk);

	if (local_fd)
		close(pidfd);
}

static int get_ns_ino(const char *path, ino_t *ino)
{
	struct stat st;

	if (stat(path, &st) != 0)
		return -errno;
	*ino = st.st_ino;
	return 0;
}

static void open_cgroup_procs(void)
{
	char *buf = NULL, *path = NULL, *p;
	int cgroup_fd = 0;
	char fdpath[PATH_MAX];

	open_target_fd(&cgroup_fd, "cgroup", optarg);

	if (read_all_alloc(cgroup_fd, &buf) < 1)
		err(EXIT_FAILURE, _("failed to get cgroup path"));

	p = strtok(buf, "\n");
	if (p)
		path = strrchr(p, ':');
	if (!path)
		err(EXIT_FAILURE, _("failed to get cgroup path"));
	path++;

	snprintf(fdpath, sizeof(fdpath), _PATH_SYS_CGROUP "/%s/cgroup.procs", path);

	if ((cgroup_procs_fd = open(fdpath, O_WRONLY | O_APPEND)) < 0)
		err(EXIT_FAILURE, _("failed to open cgroup.procs"));

	free(buf);
}

static int is_cgroup2(void)
{
	struct statfs fs_stat;
	int rc;

	rc = statfs(_PATH_SYS_CGROUP, &fs_stat);
	if (rc)
		err(EXIT_FAILURE, _("statfs %s failed"), _PATH_SYS_CGROUP);
	return F_TYPE_EQUAL(fs_stat.f_type, STATFS_CGROUP2_MAGIC);
}

static void join_into_cgroup(void)
{
	pid_t pid;
	char buf[ sizeof(stringify_value(UINT32_MAX)) ];
	int len;

	pid = getpid();
	len = snprintf(buf, sizeof(buf), "%zu", (size_t) pid);
	if (write_all(cgroup_procs_fd, buf, len))
		err(EXIT_FAILURE, _("write cgroup.procs failed"));
}

static int is_usable_namespace(pid_t target, const struct namespace_file *nsfile)
{
	char path[PATH_MAX];
	ino_t my_ino = 0;
	int rc;

	/* Check NS accessibility */
	snprintf(path, sizeof(path), "/proc/%u/%s", getpid(), nsfile->name);
	rc = get_ns_ino(path, &my_ino);
	if (rc == -ENOENT)
		return false; /* Unsupported NS */

	/* It is not permitted to use setns(2) to reenter the caller's
	 * current user namespace; see setns(2) man page for more details.
	 */
	if (nsfile->nstype & CLONE_NEWUSER) {
		ino_t target_ino = 0;

		snprintf(path, sizeof(path), "/proc/%u/%s", target, nsfile->name);
		if (get_ns_ino(path, &target_ino) != 0)
			err(EXIT_FAILURE, _("stat of %s failed"), path);

		if (my_ino == target_ino)
			return false;
	}

	return true; /* All pass */
}

static void continue_as_child(void)
{
	pid_t child;
	int status;
	pid_t ret;

	/* Clear any inherited settings */
	signal(SIGCHLD, SIG_DFL);

	child = fork();
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
		OPT_PRESERVE_CRED = CHAR_MAX + 1,
		OPT_KEEPCAPS,
		OPT_USER_PARENT,
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
		{ "net-socket", required_argument, NULL, 'N' },
		{ "pid", optional_argument, NULL, 'p' },
		{ "user", optional_argument, NULL, 'U' },
		{ "cgroup", optional_argument, NULL, 'C' },
		{ "time", optional_argument, NULL, 'T' },
		{ "setuid", required_argument, NULL, 'S' },
		{ "setgid", required_argument, NULL, 'G' },
		{ "root", optional_argument, NULL, 'r' },
		{ "wd", optional_argument, NULL, 'w' },
		{ "wdns", optional_argument, NULL, 'W' },
		{ "env", no_argument, NULL, 'e' },
		{ "no-fork", no_argument, NULL, 'F' },
		{ "join-cgroup", no_argument, NULL, 'c'},
		{ "preserve-credentials", no_argument, NULL, OPT_PRESERVE_CRED },
		{ "keep-caps", no_argument, NULL, OPT_KEEPCAPS },
		{ "user-parent", no_argument, NULL, OPT_USER_PARENT},
#ifdef HAVE_LIBSELINUX
		{ "follow-context", no_argument, NULL, 'Z' },
#endif
		{ NULL, 0, NULL, 0 }
	};
	static const ul_excl_t excl[] = {       /* rows and cols in ASCII order */
		{ 'W', 'w' },
		{ 0 }
	};
	int excl_st[ARRAY_SIZE(excl)] = UL_EXCL_STATUS_INIT;

	int c, namespaces = 0, setgroups_nerrs = 0, preserve_cred = 0;
	bool do_rd = false, do_wd = false, do_uid = false, force_uid = false,
	     do_gid = false, force_gid = false, do_env = false, do_all = false,
	     do_join_cgroup = false, do_user_parent = false;
	int do_fork = -1; /* unknown yet */
	char *wdns = NULL;
	uid_t uid = 0;
	gid_t gid = 0;
	int keepcaps = 0;
	int sock_fd = -1;
	int pid_fd = -1;
#ifdef HAVE_LIBSELINUX
	bool selinux = 0;
#endif

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	close_stdout_atexit();

	while ((c =
		getopt_long(argc, argv, "+ahVt:m::u::i::n::N:p::C::U::T::S:G:r::w::W::ecFZ",
			    longopts, NULL)) != -1) {

		err_exclusive_options(c, longopts, excl, excl_st);

		switch (c) {
		case 'a':
			do_all = true;
			break;
		case 't':
			namespace_target_pid =
			    strtoul_or_err(optarg, _("failed to parse pid"));
			break;
		case 'm':
			enable_namespace(CLONE_NEWNS, optarg);
			break;
		case 'u':
			enable_namespace(CLONE_NEWUTS, optarg);
			break;
		case 'i':
			enable_namespace(CLONE_NEWIPC, optarg);
			break;
		case 'n':
			enable_namespace(CLONE_NEWNET, optarg);
			break;
		case 'N':
			sock_fd = str2num_or_err(optarg, 10, _("failed to parse file descriptor"),
						 0, INT_MAX);
			break;
		case 'p':
			enable_namespace(CLONE_NEWPID, optarg);
			break;
		case 'C':
			enable_namespace(CLONE_NEWCGROUP, optarg);
			break;
		case 'U':
			enable_namespace(CLONE_NEWUSER, optarg);
			break;
		case 'T':
			enable_namespace(CLONE_NEWTIME, optarg);
			break;
		case 'S':
			if (strcmp(optarg, "follow") == 0)
				do_uid = true;
			else
				uid = strtoul_or_err(optarg, _("failed to parse uid"));
			force_uid = true;
			break;
		case 'G':
			if (strcmp(optarg, "follow") == 0)
				do_gid = true;
			else
				gid = strtoul_or_err(optarg, _("failed to parse gid"));
			force_gid = true;
			break;
		case 'F':
			do_fork = 0;
			break;
		case 'c':
			do_join_cgroup = true;
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
		case 'W':
			wdns = optarg;
			break;
		case 'e':
			do_env = true;
			break;
		case OPT_PRESERVE_CRED:
			preserve_cred = 1;
			break;
		case OPT_KEEPCAPS:
			keepcaps = 1;
			break;
		case OPT_USER_PARENT:
			do_user_parent = true;
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
		struct namespace_file *n = NULL;
		while ((n = next_nsfile(n, 0))) {
			if (n->enabled || !is_usable_namespace(namespace_target_pid, n))
				continue;
			enable_nsfile(n, NULL);
		}
	}

	/*
	 * Open remaining namespace and directory descriptors.
	 */
	namespaces = get_namespaces_without_fd();
	if (namespaces || sock_fd >= 0 || do_user_parent) {
		if (!namespace_target_pid)
			errx(EXIT_FAILURE, _("no target PID specified"));

		/* The syscall setns() before Linux 5.7 does not support pidfd.
		 * For other cases such as sock_fd and user-parent, the global
		 * pidfd needs to be optional.
		 */
		if (get_linux_version() > KERNEL_VERSION(5, 7, 0))
			pid_fd = pidfd_open(namespace_target_pid, 0);
		if (pid_fd < 0 && namespaces)
			open_namespaces(namespaces);	/* fallback */
	}

	if (do_rd)
		open_target_fd(&root_fd, "root", NULL);
	if (do_wd)
		open_target_fd(&wd_fd, "cwd", NULL);
	if (do_env)
		open_target_fd(&env_fd, "environ", NULL);
	if (do_uid || do_gid)
		open_target_fd(&uid_gid_fd, "", NULL);
	if (do_join_cgroup) {
		if (!is_cgroup2())
			errx(EXIT_FAILURE, _("--join-cgroup is only supported in cgroup v2"));
		open_cgroup_procs();
	}

	/*
	 * Get parent userns from any available ns.
	 */
	if (do_user_parent)
		open_parent_user_ns_fd(pid_fd);

	if (sock_fd >= 0)
		open_target_sk_netns(pid_fd, sock_fd);

	/* All initialized, get final set of namespaces */
	namespaces = get_namespaces();
	if (!namespaces)
		errx(EXIT_FAILURE, _("no namespace specified"));

	if ((namespaces & CLONE_NEWPID) && do_fork == -1)
		do_fork = 1;

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
	 * Now that we know which namespaces we want to enter, enter them.  Do
	 * this in two passes, not entering the user namespace on the first
	 * pass.  So if we're deprivileging the container we'll enter the user
	 * namespace last and if we're privileging it then we enter the user
	 * namespace first (because the initial setns will fail).
	 */
	enter_namespaces(pid_fd, namespaces & ~CLONE_NEWUSER, 1);	/* ignore errors */

	namespaces = get_namespaces();
	if (namespaces)
		enter_namespaces(pid_fd, namespaces, 0);		/* report errors */

	if (pid_fd >= 0)
		close(pid_fd);

	/* Remember the current working directory if I'm not changing it */
	if (root_fd >= 0 && wd_fd < 0 && wdns == NULL) {
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
		if (chdir("/"))
			err(EXIT_FAILURE, _("cannot change directory to %s"), "/");

		close(root_fd);
		root_fd = -1;
	}

	/* working directory specified as in-namespace path */
	if (wdns) {
		wd_fd = open(wdns, O_RDONLY);
		if (wd_fd < 0)
			err(EXIT_FAILURE,
			    _("cannot open current working directory"));
	}

	/* Change the working directory */
	if (wd_fd >= 0) {
		if (fchdir(wd_fd) < 0)
			err(EXIT_FAILURE,
			    _("change directory by working directory file descriptor failed"));

		close(wd_fd);
		wd_fd = -1;
	}

	/* Pass environment variables of the target process to the spawned process */
	if (env_fd >= 0) {
		struct ul_env_list *ls;

		ls = env_list_from_fd(env_fd);
		if (!ls && errno)
			err(EXIT_FAILURE, _("failed to get environment variables"));
		clearenv();
		if (ls && env_list_setenv(ls, 0) < 0)
			err(EXIT_FAILURE, _("failed to set environment variables"));
		env_list_free(ls);
		close(env_fd);
	}

	// Join into the target cgroup
	if (cgroup_procs_fd >= 0)
		join_into_cgroup();

	if (uid_gid_fd >= 0) {
		struct stat st;

		if (fstat(uid_gid_fd, &st) > 0)
			err(EXIT_FAILURE, _("can not get process stat"));

		close(uid_gid_fd);
		uid_gid_fd = -1;

		if (do_uid)
			uid = st.st_uid;
		if (do_gid)
			gid = st.st_gid;
	}

	if (do_fork == 1)
		continue_as_child();

	if (force_uid || force_gid) {
		if (force_gid && setgroups(0, NULL) != 0 && setgroups_nerrs)	/* drop supplementary groups */
			err(EXIT_FAILURE, _("setgroups failed"));
		if (force_gid && setgid(gid) < 0)		/* change GID */
			err(EXIT_FAILURE, _("setgid() failed"));
		if (force_uid && setuid(uid) < 0)		/* change UID */
			err(EXIT_FAILURE, _("setuid() failed"));
	}

	if (keepcaps && (namespaces & CLONE_NEWUSER))
		cap_permitted_to_ambient();

	if (optind < argc) {
		execvp(argv[optind], argv + optind);
		errexec(argv[optind]);
	}
	exec_shell();
}
