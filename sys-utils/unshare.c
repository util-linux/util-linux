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
#include <poll.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/eventfd.h>
#include <sys/wait.h>
#include <sys/mount.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/prctl.h>
#include <grp.h>

/* we only need some defines missing in sys/mount.h, no libmount linkage */
#include <libmount.h>

#include "nls.h"
#include "c.h"
#include "caputils.h"
#include "closestream.h"
#include "namespace.h"
#include "pidfd-utils.h"
#include "exec_shell.h"
#include "xalloc.h"
#include "pathnames.h"
#include "all-io.h"
#include "signames.h"
#include "strutils.h"
#include "pwdutils.h"

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
	{ .type = CLONE_NEWPID,   .name = "ns/pid_for_children" },
	{ .type = CLONE_NEWNS,    .name = "ns/mnt"  },
	{ .type = CLONE_NEWTIME,  .name = "ns/time_for_children" },
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
		err(EXIT_FAILURE, _("stat of %s failed"), path);
	return st.st_ino;
}

static void settime(time_t offset, clockid_t clk_id)
{
	char buf[sizeof(stringify_value(ULONG_MAX)) * 3];
	int fd, len;

	len = snprintf(buf, sizeof(buf), "%d %" PRId64 " 0", clk_id, (int64_t) offset);

	fd = open("/proc/self/timens_offsets", O_WRONLY);
	if (fd < 0)
		err(EXIT_FAILURE, _("failed to open /proc/self/timens_offsets"));

	if (write(fd, buf, len) != len)
		err(EXIT_FAILURE, _("failed to write to /proc/self/timens_offsets"));

	close(fd);
}

/**
 * waitchild() - Wait for a process to exit successfully
 * @pid: PID of the process to wait for
 *
 * Wait for a process to exit successfully. If it exits with a non-zero return
 * code, then exit() with the same status.
 */
static void waitchild(int pid)
{
	int rc, status;

	do {
		rc = waitpid(pid, &status, 0);
		if (rc < 0) {
			if (errno == EINTR)
				continue;
			err(EXIT_FAILURE, _("waitpid failed"));
		}
		if (WIFEXITED(status) &&
		    WEXITSTATUS(status) != EXIT_SUCCESS)
			exit(WEXITSTATUS(status));
	} while (rc < 0);
}

/**
 * sync_with_child() - Tell our child we're ready and wait for it to exit
 * @pid: The pid of our child
 * @fd: A file descriptor created with eventfd()
 *
 * This tells a child created with fork_and_wait() that we are ready for it to
 * continue. Once we have done that, wait for our child to exit.
 */
static void sync_with_child(pid_t pid, int fd)
{
	uint64_t ch = PIPE_SYNC_BYTE;

	write_all(fd, &ch, sizeof(ch));
	close(fd);

	waitchild(pid);
}

/**
 * fork_and_wait() - Fork and wait to be sync'd with
 * @fd - A file descriptor created with eventfd() which should be passed to
 *       sync_with_child()
 *
 * This creates an eventfd and forks. The parent process returns immediately,
 * but the child waits for a %PIPE_SYNC_BYTE on the eventfd before returning.
 * This allows the parent to perform some tasks before the child starts its
 * work. The parent should call sync_with_child() once it is ready for the
 * child to continue.
 *
 * Return: The pid from fork()
 */
static pid_t fork_and_wait(int *fd)
{
	pid_t pid;
	uint64_t ch;

	*fd = eventfd(0, 0);
	if (*fd < 0)
		err(EXIT_FAILURE, _("eventfd failed"));

	pid = fork();
	if (pid < 0)
		err(EXIT_FAILURE, _("fork failed"));

	if (!pid) {
		/* wait for the our parent to tell us to continue */
		if (read_all(*fd, (char *)&ch, sizeof(ch)) != sizeof(ch) ||
		    ch != PIPE_SYNC_BYTE)
			err(EXIT_FAILURE, _("failed to read eventfd"));
		close(*fd);
	}

	return pid;
}

static pid_t bind_ns_files_from_child(int *fd)
{
	pid_t child, ppid = getpid();
	ino_t ino = get_mnt_ino(ppid);

	child = fork_and_wait(fd);
	if (child)
		return child;

	if (get_mnt_ino(ppid) == ino)
		exit(EXIT_FAILURE);
	bind_ns_files(ppid);
	exit(EXIT_SUCCESS);
}

static uid_t get_user(const char *s, const char *err)
{
	struct passwd *pw;
	char *buf = NULL;
	uid_t ret;

	pw = xgetpwnam(s, &buf);
	if (pw) {
		ret = pw->pw_uid;
		free(pw);
		free(buf);
	} else {
		ret = strtoul_or_err(s, err);
	}

	return ret;
}

static gid_t get_group(const char *s, const char *err)
{
	struct group *gr;
	char *buf = NULL;
	gid_t ret;

	gr = xgetgrnam(s, &buf);
	if (gr) {
		ret = gr->gr_gid;
		free(gr);
		free(buf);
	} else {
		ret = strtoul_or_err(s, err);
	}

	return ret;
}

/**
 * struct map_range - A range of IDs to map
 * @outer: First ID mapped on the outside of the namespace
 * @inner: First ID mapped on the inside of the namespace
 * @count: Length of the inside and outside ranges
 *
 * A range of uids/gids to map using new[gu]idmap.
 */
struct map_range {
	unsigned int outer;
	unsigned int inner;
	unsigned int count;
};

#define UID_BUFSIZ  sizeof(stringify_value(ULONG_MAX))

/**
 * get_map_range() - Parse a mapping range from a string
 * @s: A string of the format inner:outer:count or outer,inner,count
 *
 * Parse a string of the form inner:outer:count or outer,inner,count into
 * a new mapping range.
 *
 * Return: A new &struct map_range
 */
static struct map_range *get_map_range(const char *s)
{
	int end;
	struct map_range *ret;

	ret = xmalloc(sizeof(*ret));

	if (sscanf(s, "%u:%u:%u%n", &ret->inner, &ret->outer, &ret->count,
		   &end) >= 3 && !s[end])
		return ret; /* inner:outer:count */

	if (sscanf(s, "%u,%u,%u%n", &ret->outer, &ret->inner, &ret->count,
		   &end) >= 3 && !s[end])
		return ret; /* outer,inner,count */

	errx(EXIT_FAILURE, _("invalid mapping '%s'"), s);
}

/**
 * read_subid_range() - Look up a user's sub[gu]id range
 * @filename: The file to look up the range from. This should be either
 *            ``/etc/subuid`` or ``/etc/subgid``.
 * @uid: The uid of the user whose range we should look up.
 *
 * This finds the first subid range matching @uid in @filename.
 */
static struct map_range *read_subid_range(char *filename, uid_t uid)
{
	char *line = NULL, *pwbuf;
	FILE *idmap;
	size_t n = 0;
	struct passwd *pw;
	struct map_range *map;

	map = xmalloc(sizeof(*map));
	map->inner = -1;

	pw = xgetpwuid(uid, &pwbuf);
	if (!pw)
		errx(EXIT_FAILURE, _("you (user %d) don't exist."), uid);

	idmap = fopen(filename, "r");
	if (!idmap)
		err(EXIT_FAILURE, _("could not open '%s'"), filename);

	/*
	* Each line in sub[ug]idmap looks like
	* username:subuid:count
	* OR
	* uid:subuid:count
	*/
	while (getline(&line, &n, idmap) != -1) {
		char *rest, *s;

		rest = strchr(line, ':');
		if (!rest)
			continue;
		*rest = '\0';

		if (strcmp(line, pw->pw_name) &&
		    strtoul(line, NULL, 10) != pw->pw_uid)
			continue;

		s = rest + 1;
		rest = strchr(s, ':');
		if (!rest)
			continue;
		*rest = '\0';
		map->outer = strtoul_or_err(s, _("failed to parse subid map"));

		s = rest + 1;
		rest = strchr(s, '\n');
		if (rest)
			*rest = '\0';
		map->count = strtoul_or_err(s, _("failed to parse subid map"));

		fclose(idmap);
		free(pw);
		free(pwbuf);

		return map;
	}

	errx(EXIT_FAILURE, _("no line matching user \"%s\" in %s"),
	pw->pw_name, filename);
}

/**
 * map_ids() - Create a new uid/gid map
 * @idmapper: Either newuidmap or newgidmap
 * @ppid: Pid to set the map for
 * @outer: ID outside the namespace for a single map.
 * @inner: ID inside the namespace for a single map. May be -1 to only use @map.
 * @map: A range of IDs to map
 *
 * This creates a new uid/gid map for @ppid using @idmapper. The ID @outer in
 * the parent (our) namespace is mapped to the ID @inner in the child (@ppid's)
 * namespace. In addition, the range of IDs beginning at @map->outer is mapped
 * to the range of IDs beginning at @map->inner. The tricky bit is that we
 * cannot let these mappings overlap. We accomplish this by removing a "hole"
 * from @map, if @outer or @inner overlap it. This may result in one less than
 * @map->count IDs being mapped from @map. The unmapped IDs are always the
 * topmost IDs of the mapping (either in the parent or the child namespace).
 *
 * Most of the time, this function will be called with @map->outer as some
 * large ID, @map->inner as 0, and @map->count as a large number (at least
 * 1000, but less than @map->outer). Typically, there will be no conflict with
 * @outer. However, @inner may split the mapping for e.g. --map-current-user.
 *
 * This function always exec()s or errors out and does not return.
 */
static void __attribute__((__noreturn__))
map_ids(const char *idmapper, int ppid, unsigned int outer, unsigned int inner,
	struct map_range *map)
{
	/* idmapper + pid + 4 * map + NULL */
	char *argv[15];
	/* argv - idmapper - "1" - NULL */
	char args[12][UID_BUFSIZ];
	int i = 0, j = 0;
	struct map_range lo, mid, hi;
	unsigned int inner_offset, outer_offset;

	/* Some helper macros to reduce bookkeeping */
#define push_str(s) do { \
	argv[i++] = s; \
} while (0)
#define push_ul(x) do { \
	snprintf(args[j], sizeof(args[j]), "%u", x); \
	push_str(args[j++]); \
} while (0)

	push_str(xstrdup(idmapper));
	push_ul(ppid);
	if ((int)inner == -1) {
		/*
		 * If we don't have a "single" mapping, then we can just use map
		 * directly, starting inner IDs from zero for an auto mapping
		 */
		push_ul(map->inner + 1 ? map->inner : 0);
		push_ul(map->outer);
		push_ul(map->count);
		push_str(NULL);

		execvp(idmapper, argv);
		errexec(idmapper);
	}

	/*
	 * Start inner IDs from zero for an auto mapping; otherwise, if the two
	 * fixed mappings overlap, remove an ID from map
	 */
	if (map->inner + 1 == 0)
		map->inner = 0;
	else if ((outer >= map->outer && outer <= map->outer + map->count) ||
		 (inner >= map->inner && inner <= map->inner + map->count))
		map->count--;

	/* Determine where the splits between lo, mid, and hi will be */
	outer_offset = min(outer > map->outer ? outer - map->outer : 0,
			   map->count);
	inner_offset = min(inner > map->inner ? inner - map->inner : 0,
			   map->count);

	/*
	 * In the worst case, we need three mappings:
	 * From the bottom of map to either inner or outer
	 */
	lo.outer = map->outer;
	lo.inner = map->inner;
	lo.count = min(inner_offset, outer_offset);

	/* From the lower of inner or outer to the higher */
	mid.outer = lo.outer + lo.count;
	mid.outer += mid.outer == outer;
	mid.inner = lo.inner + lo.count;
	mid.inner += mid.inner == inner;
	mid.count = abs_diff(outer_offset, inner_offset);

	/* And from the higher of inner or outer to the end of the map */
	hi.outer = mid.outer + mid.count;
	hi.outer += hi.outer == outer;
	hi.inner = mid.inner + mid.count;
	hi.inner += hi.inner == inner;
	hi.count = map->count - lo.count - mid.count;

	push_ul(inner);
	push_ul(outer);
	push_str("1");
	/* new[gu]idmap doesn't like zero-length mappings, so skip them */
	if (lo.count) {
		push_ul(lo.inner);
		push_ul(lo.outer);
		push_ul(lo.count);
	}
	if (mid.count) {
		push_ul(mid.inner);
		push_ul(mid.outer);
		push_ul(mid.count);
	}
	if (hi.count) {
		push_ul(hi.inner);
		push_ul(hi.outer);
		push_ul(hi.count);
	}
	push_str(NULL);
	execvp(idmapper, argv);
	errexec(idmapper);
}

/**
 * map_ids_from_child() - Set up a new uid/gid map
 * @fd: The eventfd to wait on
 * @mapuser: The user to map the current user to (or -1)
 * @usermap: The range of UIDs to map (or %NULL)
 * @mapgroup: The group to map the current group to (or -1)
 * @groupmap: The range of GIDs to map (or %NULL)
 *
 * fork_and_wait() for our parent to call sync_with_child() on @fd. Upon
 * recieving the go-ahead, use newuidmap and newgidmap to set the uid/gid map
 * for our parent's PID.
 *
 * Return: The pid of the child.
 */
static pid_t map_ids_from_child(int *fd, uid_t mapuser,
				struct map_range *usermap, gid_t mapgroup,
				struct map_range *groupmap)
{
	pid_t child, pid = 0;
	pid_t ppid = getpid();

	child = fork_and_wait(fd);
	if (child)
		return child;

	/* Avoid forking more than we need to */
	if (usermap && groupmap) {
		pid = fork();
		if (pid < 0)
			err(EXIT_FAILURE, _("fork failed"));
		if (pid)
			waitchild(pid);
	}

	if (!pid && usermap)
		map_ids("newuidmap", ppid, geteuid(), mapuser, usermap);
	if (groupmap)
		map_ids("newgidmap", ppid, getegid(), mapgroup, groupmap);
	exit(EXIT_SUCCESS);
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
	fputs(_(" -T, --time[=<file>]       unshare time namespace\n"), out);
	fputs(USAGE_SEPARATOR, out);
	fputs(_(" -f, --fork                fork before launching <program>\n"), out);
	fputs(_(" --map-user=<uid>|<name>   map current user to uid (implies --user)\n"), out);
	fputs(_(" --map-group=<gid>|<name>  map current group to gid (implies --user)\n"), out);
	fputs(_(" -r, --map-root-user       map current user to root (implies --user)\n"), out);
	fputs(_(" -c, --map-current-user    map current user to itself (implies --user)\n"), out);
	fputs(_(" --map-auto                map users and groups automatically (implies --user)\n"), out);
	fputs(_(" --map-users=<inneruid>:<outeruid>:<count>\n"
		"                           map count users from outeruid to inneruid (implies --user)\n"), out);
	fputs(_(" --map-groups=<innergid>:<outergid>:<count>\n"
		"                           map count groups from outergid to innergid (implies --user)\n"), out);
	fputs(USAGE_SEPARATOR, out);
	fputs(_(" --kill-child[=<signame>]  when dying, kill the forked child (implies --fork)\n"
		"                             defaults to SIGKILL\n"), out);
	fputs(_(" --mount-proc[=<dir>]      mount proc filesystem first (implies --mount)\n"), out);
	fputs(_(" --propagation slave|shared|private|unchanged\n"
	        "                           modify mount propagation in mount namespace\n"), out);
	fputs(_(" --setgroups allow|deny    control the setgroups syscall in user namespaces\n"), out);
	fputs(_(" --keep-caps               retain capabilities granted in user namespaces\n"), out);
	fputs(USAGE_SEPARATOR, out);
	fputs(_(" -R, --root=<dir>          run the command with root directory set to <dir>\n"), out);
	fputs(_(" -w, --wd=<dir>            change working directory to <dir>\n"), out);
	fputs(_(" -S, --setuid <uid>        set uid in entered namespace\n"), out);
	fputs(_(" -G, --setgid <gid>        set gid in entered namespace\n"), out);
	fputs(_(" --monotonic <offset>      set clock monotonic offset (seconds) in time namespaces\n"), out);
	fputs(_(" --boottime <offset>       set clock boottime offset (seconds) in time namespaces\n"), out);

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
		OPT_KILLCHILD,
		OPT_KEEPCAPS,
		OPT_MONOTONIC,
		OPT_BOOTTIME,
		OPT_MAPUSER,
		OPT_MAPUSERS,
		OPT_MAPGROUP,
		OPT_MAPGROUPS,
		OPT_MAPAUTO,
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
		{ "time",          optional_argument, NULL, 'T'             },

		{ "fork",          no_argument,       NULL, 'f'             },
		{ "kill-child",    optional_argument, NULL, OPT_KILLCHILD   },
		{ "mount-proc",    optional_argument, NULL, OPT_MOUNTPROC   },
		{ "map-user",      required_argument, NULL, OPT_MAPUSER     },
		{ "map-users",     required_argument, NULL, OPT_MAPUSERS    },
		{ "map-group",     required_argument, NULL, OPT_MAPGROUP    },
		{ "map-groups",    required_argument, NULL, OPT_MAPGROUPS   },
		{ "map-root-user", no_argument,       NULL, 'r'             },
		{ "map-current-user", no_argument,    NULL, 'c'             },
		{ "map-auto",      no_argument,       NULL, OPT_MAPAUTO     },
		{ "propagation",   required_argument, NULL, OPT_PROPAGATION },
		{ "setgroups",     required_argument, NULL, OPT_SETGROUPS   },
		{ "keep-caps",     no_argument,       NULL, OPT_KEEPCAPS    },
		{ "setuid",	   required_argument, NULL, 'S'		    },
		{ "setgid",	   required_argument, NULL, 'G'		    },
		{ "root",	   required_argument, NULL, 'R'		    },
		{ "wd",		   required_argument, NULL, 'w'		    },
		{ "monotonic",     required_argument, NULL, OPT_MONOTONIC   },
		{ "boottime",      required_argument, NULL, OPT_BOOTTIME    },
		{ NULL, 0, NULL, 0 }
	};

	int setgrpcmd = SETGROUPS_NONE;
	int unshare_flags = 0;
	int c, forkit = 0;
	uid_t mapuser = -1;
	gid_t mapgroup = -1;
	struct map_range *usermap = NULL;
	struct map_range *groupmap = NULL;
	int kill_child_signo = 0; /* 0 means --kill-child was not used */
	const char *procmnt = NULL;
	const char *newroot = NULL;
	const char *newdir = NULL;
	pid_t pid_bind = 0, pid_idmap = 0;
	pid_t pid = 0;
#ifdef UL_HAVE_PIDFD
	int fd_parent_pid = -1;
#endif
	int fd_idmap, fd_bind = -1;
	sigset_t sigset, oldsigset;
	int status;
	unsigned long propagation = UNSHARE_PROPAGATION_DEFAULT;
	int force_uid = 0, force_gid = 0;
	uid_t uid = 0, real_euid = geteuid();
	gid_t gid = 0, real_egid = getegid();
	int keepcaps = 0;
	time_t monotonic = 0;
	time_t boottime = 0;
	int force_monotonic = 0;
	int force_boottime = 0;

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	close_stdout_atexit();

	while ((c = getopt_long(argc, argv, "+fhVmuinpCTUrR:w:S:G:c", longopts, NULL)) != -1) {
		switch (c) {
		case 'f':
			forkit = 1;
			break;
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
		case 'T':
			unshare_flags |= CLONE_NEWTIME;
			if (optarg)
				set_ns_target(CLONE_NEWTIME, optarg);
			break;
		case OPT_MOUNTPROC:
			unshare_flags |= CLONE_NEWNS;
			procmnt = optarg ? optarg : "/proc";
			break;
		case OPT_MAPUSER:
			unshare_flags |= CLONE_NEWUSER;
			mapuser = get_user(optarg, _("failed to parse uid"));
			break;
		case OPT_MAPGROUP:
			unshare_flags |= CLONE_NEWUSER;
			mapgroup = get_group(optarg, _("failed to parse gid"));
			break;
		case 'r':
			unshare_flags |= CLONE_NEWUSER;
			mapuser = 0;
			mapgroup = 0;
			break;
		case 'c':
			unshare_flags |= CLONE_NEWUSER;
			mapuser = real_euid;
			mapgroup = real_egid;
			break;
		case OPT_MAPUSERS:
			unshare_flags |= CLONE_NEWUSER;
			if (!strcmp(optarg, "auto"))
				usermap = read_subid_range(_PATH_SUBUID, real_euid);
			else
				usermap = get_map_range(optarg);
			break;
		case OPT_MAPGROUPS:
			unshare_flags |= CLONE_NEWUSER;
			if (!strcmp(optarg, "auto"))
				groupmap = read_subid_range(_PATH_SUBGID, real_euid);
			else
				groupmap = get_map_range(optarg);
			break;
		case OPT_MAPAUTO:
			unshare_flags |= CLONE_NEWUSER;
			usermap = read_subid_range(_PATH_SUBUID, real_euid);
			groupmap = read_subid_range(_PATH_SUBGID, real_euid);
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
                case OPT_KEEPCAPS:
			keepcaps = 1;
			cap_last_cap(); /* Force last cap to be cached before we fork. */
			break;
		case 'S':
			uid = strtoul_or_err(optarg, _("failed to parse uid"));
			force_uid = 1;
			break;
		case 'G':
			gid = strtoul_or_err(optarg, _("failed to parse gid"));
			force_gid = 1;
			break;
		case 'R':
			newroot = optarg;
			break;
		case 'w':
			newdir = optarg;
			break;
                case OPT_MONOTONIC:
			monotonic = strtoul_or_err(optarg, _("failed to parse monotonic offset"));
			force_monotonic = 1;
			break;
                case OPT_BOOTTIME:
			boottime = strtoul_or_err(optarg, _("failed to parse boottime offset"));
			force_boottime = 1;
			break;

		case 'h':
			usage();
		case 'V':
			print_version(EXIT_SUCCESS);
		default:
			errtryhelp(EXIT_FAILURE);
		}
	}

	if ((force_monotonic || force_boottime) && !(unshare_flags & CLONE_NEWTIME))
		errx(EXIT_FAILURE, _("options --monotonic and --boottime require "
			"unsharing of a time namespace (-T)"));

	/* clear any inherited settings */
	signal(SIGCHLD, SIG_DFL);

	if (npersists && (unshare_flags & CLONE_NEWNS))
		pid_bind = bind_ns_files_from_child(&fd_bind);

	if (usermap || groupmap)
		pid_idmap = map_ids_from_child(&fd_idmap, mapuser, usermap,
					       mapgroup, groupmap);

	if (-1 == unshare(unshare_flags))
		err(EXIT_FAILURE, _("unshare failed"));

	/* Tell child we've called unshare() */
	if (usermap || groupmap)
		sync_with_child(pid_idmap, fd_idmap);

	if (force_boottime)
		settime(boottime, CLOCK_BOOTTIME);

	if (force_monotonic)
		settime(monotonic, CLOCK_MONOTONIC);

	if (forkit) {
		if (sigemptyset(&sigset) != 0 ||
			sigaddset(&sigset, SIGINT) != 0 ||
			sigaddset(&sigset, SIGTERM) != 0 ||
			sigprocmask(SIG_BLOCK, &sigset, &oldsigset) != 0)
			err(EXIT_FAILURE, _("sigprocmask block failed"));
#ifdef UL_HAVE_PIDFD
		if (kill_child_signo != 0) {
			/* make a connection to the original process (parent) */
			fd_parent_pid = pidfd_open(getpid(), 0);
			if (0 > fd_parent_pid)
				err(EXIT_FAILURE, _("pidfd_open failed"));
		}
#endif
		/* force child forking before mountspace binding so
		 * pid_for_children is populated */
		pid = fork();

		switch(pid) {
		case -1:
			err(EXIT_FAILURE, _("fork failed"));
		case 0:	/* child */
			if (sigprocmask(SIG_SETMASK, &oldsigset, NULL))
				err(EXIT_FAILURE,
					_("sigprocmask restore failed"));
			if (npersists && (unshare_flags & CLONE_NEWNS))
				close(fd_bind);
			break;
		default: /* parent */
			break;
		}
	}

	if (npersists && (pid || !forkit)) {
		/* run in parent */
		if (pid_bind && (unshare_flags & CLONE_NEWNS))
			sync_with_child(pid_bind, fd_bind);
		else
			/* simple way, just bind */
			bind_ns_files(getpid());
	}

	if (pid) {
		if (waitpid(pid, &status, 0) == -1)
			err(EXIT_FAILURE, _("waitpid failed"));

		if (WIFEXITED(status))
			return WEXITSTATUS(status);
		if (WIFSIGNALED(status)) {

			/* Ensure the signal that terminated the child will
			 * also terminate the parent. */

			int termsig = WTERMSIG(status);

			if (signal(termsig, SIG_DFL) == SIG_ERR ||
				sigemptyset(&sigset) != 0 ||
				sigaddset(&sigset, termsig) != 0 ||
				sigprocmask(SIG_UNBLOCK, &sigset, NULL) != 0)
				err(EXIT_FAILURE,
					_("sigprocmask unblock failed"));

			kill(getpid(), termsig);
		}
		err(EXIT_FAILURE, _("child exit failed"));
	}

	if (kill_child_signo != 0) {
		if (prctl(PR_SET_PDEATHSIG, kill_child_signo) < 0)
			err(EXIT_FAILURE, "prctl failed");
#ifdef UL_HAVE_PIDFD
		/* Use poll() to check that there is still the original parent. */
		if (fd_parent_pid != -1) {
			struct pollfd pollfds[1] = {
				{ .fd = fd_parent_pid, .events = POLLIN	}
			};
			int nfds = poll(pollfds, 1, 0);

			if (0 > nfds)
				err(EXIT_FAILURE, "poll parent pidfd failed");

			/* If the child was re-parented before prctl(2) was called, the
			 * new parent will likely not be interested in the precise exit
			 * status of the orphan.
			 */
			if (nfds)
				exit(EXIT_FAILURE);

			close(fd_parent_pid);
			fd_parent_pid = -1;
		}
#endif
	}

        if (mapuser != (uid_t) -1 && !usermap)
		map_id(_PATH_PROC_UIDMAP, mapuser, real_euid);

        /* Since Linux 3.19 unprivileged writing of /proc/self/gid_map
         * has been disabled unless /proc/self/setgroups is written
         * first to permanently disable the ability to call setgroups
         * in that user namespace. */
	if (mapgroup != (gid_t) -1 && !groupmap) {
		if (setgrpcmd == SETGROUPS_ALLOW)
			errx(EXIT_FAILURE, _("options --setgroups=allow and "
					"--map-group are mutually exclusive"));
		setgroups_control(SETGROUPS_DENY);
		map_id(_PATH_PROC_GIDMAP, mapgroup, real_egid);
	}

	if (setgrpcmd != SETGROUPS_NONE)
		setgroups_control(setgrpcmd);

	if ((unshare_flags & CLONE_NEWNS) && propagation)
		set_propagation(propagation);

	if (newroot) {
		if (chroot(newroot) != 0)
			err(EXIT_FAILURE,
			    _("cannot change root directory to '%s'"), newroot);
		newdir = newdir ?: "/";
	}
	if (newdir && chdir(newdir))
		err(EXIT_FAILURE, _("cannot chdir to '%s'"), newdir);

	if (procmnt) {
		/* When not changing root and using the default propagation flags
		   then the recursive propagation change of root will
		   automatically change that of an existing proc mount. */
		if (!newroot && propagation != (MS_PRIVATE|MS_REC)) {
			int rc = mount("none", procmnt, NULL, MS_PRIVATE|MS_REC, NULL);

			/* Custom procmnt means that proc is very likely not mounted, causing EINVAL.
			   Ignoring the error in this specific instance is considered safe. */
			if(rc != 0 && errno != EINVAL)
				err(EXIT_FAILURE, _("cannot change %s filesystem propagation"), procmnt);
		}

		if (mount("proc", procmnt, "proc", MS_NOSUID|MS_NOEXEC|MS_NODEV, NULL) != 0)
			err(EXIT_FAILURE, _("mount %s failed"), procmnt);
	}

	if (force_gid) {
		if (setgroups(0, NULL) != 0)	/* drop supplementary groups */
			err(EXIT_FAILURE, _("setgroups failed"));
		if (setgid(gid) < 0)		/* change GID */
			err(EXIT_FAILURE, _("setgid failed"));
	}
	if (force_uid && setuid(uid) < 0)	/* change UID */
		err(EXIT_FAILURE, _("setuid failed"));

	/* We use capabilities system calls to propagate the permitted
	 * capabilities into the ambient set because we have already
	 * forked so are in async-signal-safe context. */
	if (keepcaps && (unshare_flags & CLONE_NEWUSER)) {
		struct __user_cap_header_struct header = {
			.version = _LINUX_CAPABILITY_VERSION_3,
			.pid = 0,
		};

		struct __user_cap_data_struct payload[_LINUX_CAPABILITY_U32S_3] = {{ 0 }};
		uint64_t effective, cap;

		if (capget(&header, payload) < 0)
			err(EXIT_FAILURE, _("capget failed"));

		/* In order the make capabilities ambient, we first need to ensure
		 * that they are all inheritable. */
		payload[0].inheritable = payload[0].permitted;
		payload[1].inheritable = payload[1].permitted;

		if (capset(&header, payload) < 0)
			err(EXIT_FAILURE, _("capset failed"));

		effective = ((uint64_t)payload[1].effective << 32) |  (uint64_t)payload[0].effective;

		for (cap = 0; cap < (sizeof(effective) * 8); cap++) {
			/* This is the same check as cap_valid(), but using
			 * the runtime value for the last valid cap. */
			if (cap > (uint64_t) cap_last_cap())
				continue;

			if ((effective & (1 << cap))
			    && prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_RAISE, cap, 0, 0) < 0)
					err(EXIT_FAILURE, _("prctl(PR_CAP_AMBIENT) failed"));
                }
        }

	if (optind < argc) {
		execvp(argv[optind], argv + optind);
		errexec(argv[optind]);
	}
	exec_shell();
}
