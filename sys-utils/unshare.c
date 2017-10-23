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
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/prctl.h>

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
#include "signames.h"

/* synchronize parent and child by pipe */
#define PIPE_SYNC_BYTE	0x06

/* 'private' is kernel default */
#define UNSHARE_PROPAGATION_DEFAULT	(MS_REC | MS_PRIVATE)

/* /proc namespace files and mountpoints for binds */
static struct namespace_file {
	int		type;		/* CLONE_NEW* */
	const char	*name;		/* ns/<type> */
	const char	*target;	/* user specified target for bind mount */
} namespace_files[] = {
	{ .type = CLONE_NEWUSER,  .name = "ns/user" },
	{ .type = CLONE_NEWCGROUP,.name = "ns/cgroup" },
	{ .type = CLONE_NEWIPC,   .name = "ns/ipc"  },
	{ .type = CLONE_NEWUTS,   .name = "ns/uts"  },
	{ .type = CLONE_NEWNET,   .name = "ns/net"  },
	{ .type = CLONE_NEWPID,   .name = "ns/pid"  },
	{ .type = CLONE_NEWNS,    .name = "ns/mnt"  },
	{ .name = NULL }
};

static int npersists;	/* number of persistent namespaces */


enum {
	SETGROUPS_NONE = -1,
	SETGROUPS_DENY = 0,
	SETGROUPS_ALLOW = 1,
};

static const char *setgroups_strings[] =
{
	[SETGROUPS_DENY] = "deny",
	[SETGROUPS_ALLOW] = "allow"
};

static int setgroups_str2id(const char *str)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(setgroups_strings); i++)
		if (strcmp(str, setgroups_strings[i]) == 0)
			return i;

	errx(EXIT_FAILURE, _("unsupported --setgroups argument '%s'"), str);
}

static void setgroups_control(int action)
{
	const char *file = _PATH_PROC_SETGROUPS;
	const char *cmd;
	int fd;

	if (action < 0 || (size_t) action >= ARRAY_SIZE(setgroups_strings))
		return;
	cmd = setgroups_strings[action];

	fd = open(file, O_WRONLY);
	if (fd < 0) {
		if (errno == ENOENT)
			return;
		err(EXIT_FAILURE, _("cannot open %s"), file);
	}

	if (write_all(fd, cmd, strlen(cmd)))
		err(EXIT_FAILURE, _("write failed %s"), file);
	close(fd);
}

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

static unsigned long parse_propagation(const char *str)
{
	size_t i;
	static const struct prop_opts {
		const char *name;
		unsigned long flag;
	} opts[] = {
		{ "slave",	MS_REC | MS_SLAVE },
		{ "private",	MS_REC | MS_PRIVATE },
		{ "shared",     MS_REC | MS_SHARED },
		{ "unchanged",        0 }
	};

	for (i = 0; i < ARRAY_SIZE(opts); i++) {
		if (strcmp(opts[i].name, str) == 0)
			return opts[i].flag;
	}

	errx(EXIT_FAILURE, _("unsupported propagation mode: %s"), str);
}

static void set_propagation(unsigned long flags)
{
	if (flags == 0)
		return;

	if (mount("none", "/", NULL, flags, NULL) != 0)
		err(EXIT_FAILURE, _("cannot change root filesystem propagation"));
}


static int set_ns_target(int type, const char *path)
{
	struct namespace_file *ns;

	for (ns = namespace_files; ns->name; ns++) {
		if (ns->type != type)
			continue;
		ns->target = path;
		npersists++;
		return 0;
	}

	return -EINVAL;
}

static int bind_ns_files(pid_t pid)
{
	struct namespace_file *ns;
	char src[PATH_MAX];

	for (ns = namespace_files; ns->name; ns++) {
		if (!ns->target)
			continue;

		snprintf(src, sizeof(src), "/proc/%u/%s", (unsigned) pid, ns->name);

		if (mount(src, ns->target, NULL, MS_BIND, NULL) != 0)
			err(EXIT_FAILURE, _("mount %s on %s failed"), src, ns->target);
	}

	return 0;
}

static ino_t get_mnt_ino(pid_t pid)
{
	struct stat st;
	char path[PATH_MAX];

	snprintf(path, sizeof(path), "/proc/%u/ns/mnt", (unsigned) pid);

	if (stat(path, &st) != 0)
		err(EXIT_FAILURE, _("cannot stat %s"), path);
	return st.st_ino;
}

static void bind_ns_files_from_child(pid_t *child, int fds[2])
{
	char ch;
	pid_t ppid = getpid();
	ino_t ino = get_mnt_ino(ppid);

	if (pipe(fds) < 0)
		err(EXIT_FAILURE, _("pipe failed"));

	*child = fork();

	switch (*child) {
	case -1:
		err(EXIT_FAILURE, _("fork failed"));

	case 0:	/* child */
		close(fds[1]);
		fds[1] = -1;

		/* wait for parent */
		if (read_all(fds[0], &ch, 1) != 1 && ch != PIPE_SYNC_BYTE)
			err(EXIT_FAILURE, _("failed to read pipe"));
		if (get_mnt_ino(ppid) == ino)
			exit(EXIT_FAILURE);
		bind_ns_files(ppid);
		exit(EXIT_SUCCESS);
		break;

	default: /* parent */
		close(fds[0]);
		fds[0] = -1;
		break;
	}
}

static void __attribute__((__noreturn__)) usage(void)
{
	FILE *out = stdout;

	fputs(USAGE_HEADER, out);
	fprintf(out, _(" %s [options] [<program> [<argument>...]]\n"),
		program_invocation_short_name);

	fputs(USAGE_SEPARATOR, out);
	fputs(_("Run a program with some namespaces unshared from the parent.\n"), out);

	fputs(USAGE_OPTIONS, out);
	fputs(_(" -m, --mount[=<file>]      unshare mounts namespace\n"), out);
	fputs(_(" -u, --uts[=<file>]        unshare UTS namespace (hostname etc)\n"), out);
	fputs(_(" -i, --ipc[=<file>]        unshare System V IPC namespace\n"), out);
	fputs(_(" -n, --net[=<file>]        unshare network namespace\n"), out);
	fputs(_(" -p, --pid[=<file>]        unshare pid namespace\n"), out);
	fputs(_(" -U, --user[=<file>]       unshare user namespace\n"), out);
	fputs(_(" -C, --cgroup[=<file>]     unshare cgroup namespace\n"), out);
	fputs(_(" -f, --fork                fork before launching <program>\n"), out);
	fputs(_("     --kill-child[=<signame>]  when dying, kill the forked child (implies --fork); defaults to SIGKILL\n"), out);
	fputs(_("     --mount-proc[=<dir>]  mount proc filesystem first (implies --mount)\n"), out);
	fputs(_(" -r, --map-root-user       map current user to root (implies --user)\n"), out);
	fputs(_("     --propagation slave|shared|private|unchanged\n"
	        "                           modify mount propagation in mount namespace\n"), out);
	fputs(_(" -s, --setgroups allow|deny  control the setgroups syscall in user namespaces\n"), out);

	fputs(USAGE_SEPARATOR, out);
	printf(USAGE_HELP_OPTIONS(27));
	printf(USAGE_MAN_TAIL("unshare(1)"));

	exit(EXIT_SUCCESS);
}

int main(int argc, char *argv[])
{
	enum {
		OPT_MOUNTPROC = CHAR_MAX + 1,
		OPT_PROPAGATION,
		OPT_SETGROUPS,
		OPT_KILLCHILD
	};
	static const struct option longopts[] = {
		{ "help",          no_argument,       NULL, 'h'             },
		{ "version",       no_argument,       NULL, 'V'             },

		{ "mount",         optional_argument, NULL, 'm'             },
		{ "uts",           optional_argument, NULL, 'u'             },
		{ "ipc",           optional_argument, NULL, 'i'             },
		{ "net",           optional_argument, NULL, 'n'             },
		{ "pid",           optional_argument, NULL, 'p'             },
		{ "user",          optional_argument, NULL, 'U'             },
		{ "cgroup",        optional_argument, NULL, 'C'             },

		{ "fork",          no_argument,       NULL, 'f'             },
		{ "kill-child",    optional_argument, NULL, OPT_KILLCHILD   },
		{ "mount-proc",    optional_argument, NULL, OPT_MOUNTPROC   },
		{ "map-root-user", no_argument,       NULL, 'r'             },
		{ "propagation",   required_argument, NULL, OPT_PROPAGATION },
		{ "setgroups",     required_argument, NULL, OPT_SETGROUPS   },
		{ NULL, 0, NULL, 0 }
	};

	int setgrpcmd = SETGROUPS_NONE;
	int unshare_flags = 0;
	int c, forkit = 0, maproot = 0;
	int kill_child_signo = 0; /* 0 means --kill-child was not used */
	const char *procmnt = NULL;
	pid_t pid = 0;
	int fds[2];
	int status;
	unsigned long propagation = UNSHARE_PROPAGATION_DEFAULT;
	uid_t real_euid = geteuid();
	gid_t real_egid = getegid();

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	while ((c = getopt_long(argc, argv, "+fhVmuinpCUr", longopts, NULL)) != -1) {
		switch (c) {
		case 'f':
			forkit = 1;
			break;
		case 'h':
			usage();
		case 'V':
			printf(UTIL_LINUX_VERSION);
			return EXIT_SUCCESS;
		case 'm':
			unshare_flags |= CLONE_NEWNS;
			if (optarg)
				set_ns_target(CLONE_NEWNS, optarg);
			break;
		case 'u':
			unshare_flags |= CLONE_NEWUTS;
			if (optarg)
				set_ns_target(CLONE_NEWUTS, optarg);
			break;
		case 'i':
			unshare_flags |= CLONE_NEWIPC;
			if (optarg)
				set_ns_target(CLONE_NEWIPC, optarg);
			break;
		case 'n':
			unshare_flags |= CLONE_NEWNET;
			if (optarg)
				set_ns_target(CLONE_NEWNET, optarg);
			break;
		case 'p':
			unshare_flags |= CLONE_NEWPID;
			if (optarg)
				set_ns_target(CLONE_NEWPID, optarg);
			break;
		case 'U':
			unshare_flags |= CLONE_NEWUSER;
			if (optarg)
				set_ns_target(CLONE_NEWUSER, optarg);
			break;
		case 'C':
			unshare_flags |= CLONE_NEWCGROUP;
			if (optarg)
				set_ns_target(CLONE_NEWCGROUP, optarg);
			break;
		case OPT_MOUNTPROC:
			unshare_flags |= CLONE_NEWNS;
			procmnt = optarg ? optarg : "/proc";
			break;
		case 'r':
			unshare_flags |= CLONE_NEWUSER;
			maproot = 1;
			break;
		case OPT_SETGROUPS:
			setgrpcmd = setgroups_str2id(optarg);
			break;
		case OPT_PROPAGATION:
			propagation = parse_propagation(optarg);
			break;
		case OPT_KILLCHILD:
			forkit = 1;
			if (optarg) {
				if ((kill_child_signo = signame_to_signum(optarg)) < 0)
					errx(EXIT_FAILURE, _("unknown signal: %s"),
					     optarg);
			} else {
				kill_child_signo = SIGKILL;
			}
			break;
		default:
			errtryhelp(EXIT_FAILURE);
		}
	}

	if (npersists && (unshare_flags & CLONE_NEWNS))
		bind_ns_files_from_child(&pid, fds);

	if (-1 == unshare(unshare_flags))
		err(EXIT_FAILURE, _("unshare failed"));

	if (npersists) {
		if (pid && (unshare_flags & CLONE_NEWNS)) {
			int rc;
			char ch = PIPE_SYNC_BYTE;

			/* signal child we are ready */
			write_all(fds[1], &ch, 1);
			close(fds[1]);
			fds[1] = -1;

			/* wait for bind_ns_files_from_child() */
			do {
				rc = waitpid(pid, &status, 0);
				if (rc < 0) {
					if (errno == EINTR)
						continue;
					err(EXIT_FAILURE, _("waitpid failed"));
				}
				if (WIFEXITED(status) &&
				    WEXITSTATUS(status) != EXIT_SUCCESS)
					return WEXITSTATUS(status);
			} while (rc < 0);
		} else
			/* simple way, just bind */
			bind_ns_files(getpid());
	}

	if (forkit) {
		pid = fork();

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

	if (kill_child_signo != 0)
		if (prctl(PR_SET_PDEATHSIG, kill_child_signo) < 0)
			err(EXIT_FAILURE, "prctl failed");

	if (maproot) {
		if (setgrpcmd == SETGROUPS_ALLOW)
			errx(EXIT_FAILURE, _("options --setgroups=allow and "
					"--map-root-user are mutually exclusive"));

		/* since Linux 3.19 unprivileged writing of /proc/self/gid_map
		 * has s been disabled unless /proc/self/setgroups is written
		 * first to permanently disable the ability to call setgroups
		 * in that user namespace. */
		setgroups_control(SETGROUPS_DENY);
		map_id(_PATH_PROC_UIDMAP, 0, real_euid);
		map_id(_PATH_PROC_GIDMAP, 0, real_egid);

	} else if (setgrpcmd != SETGROUPS_NONE)
		setgroups_control(setgrpcmd);

	if ((unshare_flags & CLONE_NEWNS) && propagation)
		set_propagation(propagation);

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
