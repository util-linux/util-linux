#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <sys/mount.h>

#include <linux/mount.h>

#ifdef HAVE_LIBREADLINE
# define _FUNCTION_DEF
# include <readline/readline.h>
# include <readline/history.h>
#endif
#include <libgen.h>

#include "c.h"
#include "nls.h"
#include "xalloc.h"
#include "strv.h"
#include "strutils.h"
#include "procutils.h"
#include "closestream.h"

#ifndef HAVE_FSOPEN
static int fsopen(const char *fsname, unsigned int flags)
{
	return syscall(SYS_fsopen, fsname, flags);
}
#endif

#ifndef HAVE_FSMOUNT
static int fsmount(int fd, unsigned int flags, unsigned int mount_attrs)
{
	return syscall(SYS_fsmount, fd, flags, mount_attrs);
}
#endif

#ifndef HAVE_FSCONFIG
static int fsconfig(int fd, unsigned int cmd, const char *key,
		    const void *value, int aux)
{
	return syscall(SYS_fsconfig, fd, cmd, key, value, aux);
}
#endif

#ifndef HAVE_MOVE_MOUNT
static int move_mount(int from_dirfd, const char *from_pathname,
		      int to_dirfd,   const char *to_pathname,
		      unsigned int flags)
{
	return syscall(SYS_move_mount, from_dirfd, from_pathname,
			to_dirfd, to_pathname, flags);
}
#endif

struct sh_named_fd {
	char *name;
	int  id;
};

struct sh_context {
	size_t nfds;
	struct sh_named_fd *fds;

	int     cfd;	/* default fsopen FD */
	int     mfd;	/* default mount FD */
};

struct sh_command {
	const char	*name;	/* command name as used on command line */

	int (*func)(struct sh_context *sh, int argc, char *argv[]);

	const char	*desc;	/* description */
	const char	*syno;	/* synopsis */

	unsigned int	refd : 1;	/* returns FD */
};

struct mask_name {
	char *name;
	unsigned int mask;
};

static int cmd_help(struct sh_context *sh, int argc, char *argv[]);
static int cmd_fds(struct sh_context *sh, int argc, char *argv[]);
static int cmd_fsopen(struct sh_context *sh, int argc, char *argv[]);
static int cmd_close(struct sh_context *sh, int argc, char *argv[]);
static int cmd_fsconfig(struct sh_context *sh, int argc, char *argv[]);
static int cmd_fsmount(struct sh_context *sh __attribute__((__unused__)), int argc, char *argv[]);
static int cmd_move(struct sh_context *sh, int argc, char *argv[]);

static const struct sh_command commands[] =
{
	{
		.name = "close",
		.func = cmd_close,
		.desc = N_("close file descritor"),
		.syno = N_("\tclose <fd>")
	},{
		.name = "fds",
		.func = cmd_fds,
		.desc = N_("list relevant file descritors")
	},{
		.name = "fsconfig",
		.func = cmd_fsconfig,
		.desc = N_("(re)configure or create filesystem"),
		.syno = N_("\tfsconfig [<fd>] <flag|string|binary|path|path-empty|fd|create|reconf> [<key> [<value>] [<aux>]]")
	},{
		.name = "fsopen",
		.func = cmd_fsopen,
		.desc = N_("creates filesystem context"),
		.syno = N_("\tfsopen <name> [CLOEXEC]"),
		.refd = 1,
	},{
		.name = "fsmount",
		.func = cmd_fsmount,
		.desc = N_("create mount file descritor"),
		.syno = N_("\tfsmount [<fd>] [CLOEXEC] [<ro,nosuid,nodev,noexec,atime,realatime,noatime,strictatime,nodiratime>]"),
		.refd = 1
	},{
		.name = "move_mount",
		.func = cmd_move,
		.desc = N_("move mount object around the filesystem topology"),
		.syno = N_("\tmove_mount <from-dirfd> <to-path> [<{F,T}_(SYMLINKS,AUTOMOUNT}>]\n"
			   "\tmove_mount <from-dirfd> <from-path>|\"\" <to-dirfd>|AT_FDCWD <to-path>|\"\" [<{F,T}_(SYMLINKS,AUTOMOUNT,EMPTY_PATH}>]")
	},{

		.name = "help",
		.func = cmd_help,
		.desc = N_("list commands and help"),
		.syno = N_("[<command>]")
	}
};

static const struct sh_command *lookup_command(const char *name)
{
	size_t i;

	if (!name || !*name)
		return NULL;

	for (i = 0; i < ARRAY_SIZE(commands); i++) {
		if (strcmp(commands[i].name, name) == 0)
			return &commands[i];
	}

	return NULL;
}

static struct sh_named_fd *__get_named_fd(struct sh_context *sh, const char *name, int fd)
{
	size_t i;
	struct sh_named_fd *n = NULL;

	for (i = 0; i < sh->nfds; i++) {
		if (name) {
			if (strcmp(sh->fds[i].name, name) == 0)
				n = &sh->fds[i];
		} else if (fd >= 0) {
			if (sh->fds[i].id == fd)
				n = &sh->fds[i];
		}
		if (n)
			break;
	}

	return n;
}

static int get_named_fd(struct sh_context *sh, const char *name)
{
	struct sh_named_fd *n = __get_named_fd(sh, name, -1);

	return n ? n->id : -EINVAL;
}

static int set_named_fd(struct sh_context *sh, const char *name, int fd)
{
	struct sh_named_fd *n = __get_named_fd(sh, name, -1);

	if (n) {
		if (n->id >= 0)
			close(n->id);
	} else {
		sh->fds = xrealloc(sh->fds, sh->nfds + 1);
		n = &sh->fds[sh->nfds++];
		n->name = xstrdup(name);
	}

	n->id = fd;
	return 0;
}

static int remove_named_fd_by_id(struct sh_context *sh, int fd)
{
	size_t i;

	for (i = 0; i < sh->nfds; i++) {
		if (sh->fds[i].id == fd)
			break;
	}
	if (i >= sh->nfds)
		return -EINVAL;

	free(sh->fds[i].name);
	if (i + 1 < sh->nfds)
		memmove(sh->fds + i, sh->fds + i + 1, sh->nfds - i - 1);
	sh->nfds--;
	return 0;
}

static void cleanup(struct sh_context *sh)
{
	size_t i;

	for (i = 0; i < sh->nfds; i++) {
		free(sh->fds[i].name);
		close(sh->fds[i].id);
	}

	free(sh->fds);
	free(sh);
	return;
}

static const char *get_string(int argc, char **argv, int *idx)
{
	char *str;

	if (*idx >= argc)
		return NULL;

	str = argv[*idx];

	if (strcmp(str, "\"\"") == 0)
		return "";
	else if (*str == '"') {
		int sz = strlen(str) - 1;
		if (sz > 0 && *(str + sz) == '"') {
			*(str + sz) = '\0';
			str++;
		}
	}
	(*idx)++;

	return str;
}

static int execute_command(struct sh_context *sh,
			   const struct sh_command *cmd,
			   int argc, char **argv)
{
	return cmd->func(sh, argc, argv);
}

static int string_to_mask(const char *str, unsigned int *res,
		          const struct mask_name *names, size_t nnames)
{
	const char *p = str;

	while (p && *p) {
		size_t i, sz;
		char *end = strchr(p, ',');

		sz = end ? (size_t) (end - p) : strlen(p);

		for (i = 0; i < nnames; i++) {
			const struct mask_name *x = &names[i];

			if (strncmp(x->name, p, sz) == 0) {
				*res |= x->mask;
				break;
			}
		}
		if (i == nnames) {
			warnx(_("unsupported mask name %s"), p);
			return -EINVAL;
		}
		p = end;
		if (p)
			p++;
	}

	return 0;
}

static int get_user_reply(const char *prompt, char *buf, size_t bufsz)
{
	size_t sz;

#ifdef HAVE_LIBREADLINE
	if (isatty(STDIN_FILENO)) {
		char *p = readline(prompt);
		if (!p)
			return 1;
		xstrncpy(buf, p, bufsz);
		free(p);
	} else
#endif
	{
		fputs(prompt, stdout);
		fflush(stdout);

		if (!fgets(buf, bufsz, stdin))
			return 1;
	}

	sz = ltrim_whitespace((unsigned char *) buf);
	if (sz && *(buf + sz - 1) == '\n')
		*(buf + sz - 1) = '\0';

	return 0;
}

static int cmd_fds(struct sh_context *sh,
		int argc __attribute__((__unused__)),
		char *argv[] __attribute__((__unused__)))
{
	char info[PATH_MAX];
	int fd;
	struct proc_fds *fds = proc_open_fds(getpid());

	if (!fds) {
		warn(_("faild to open /proc/self/fd/ directory"));
		return 0;
	}

	while (proc_next_fd(fds, &fd, info, sizeof(info)) == 0) {
		char *type;
		struct sh_named_fd *n;

		if (!*info)
			continue;
		if (startswith(info, "anon_inode:")) {
			type = info + 12;
			strrem(type, ']');
		} else if (isatty(fd)) {
			continue;
		} else if (*info == '/')
			type = "path";

		n = __get_named_fd(sh, NULL, fd);
		if (n)
			printf(" %d : %s [$%s]\n", fd, type, n->name);
		else
			printf(" %d : %s\n", fd, type);

		/* TODO: use fsinfo() to get more details about the FD */
	}

	proc_close_fds(fds);
	return 0;
}

/* TODO:
 *
 * cmd_pathopen() for FSCONFIG_SET_PATH with dir fd
 */

/* fsopen <fsname> [CLOEXEC] */
static int cmd_fsopen(struct sh_context *sh, int argc, char *argv[])
{
	char *fsname;
	int flags = 0, fd;

	if (argc < 2) {
		warnx(_("no filesystem name specified"));
		return -EINVAL;
	}
	fsname = argv[1];

	if (argc == 3) {
		if (strcmp("CLOEXEC", argv[2]) == 0)
			flags |=  FSOPEN_CLOEXEC;
		else {
			warnx(_("unknown argument '%s'"), argv[2]);
			return -EINVAL;
		}
	}

	fd = fsopen(fsname, flags);
	if (fd < 0)
		warn(_("cannot open %s filesystem"), fsname);
	else {
		if (sh->cfd < 0)		/* default */
			sh->cfd = fd;

		printf(_("new FD [fscontext]: %d\n"), fd);
	}
	return fd;
}


/* returns FD specified in argv[] element addressed by idx, on success idx is incremented,
 * otherwise default df is returned
 */
static int get_command_fd(struct sh_context *sh, int argc, char *argv[], int *idx, int dflt_fd)
{
	int fd;

	if (argc > *idx && isdigit_string(argv[*idx])) {
		char *end = NULL;
		char *str = argv[*idx];

		if (!str || !*str) {
			warnx(_("no file descritor specified"));
			return -EINVAL;
		}

		errno = 0;
		fd = (int) strtol(str, &end, 10);
		if (errno || str == end || (end && *end)) {
			warn(_("cannot use '%s' as file descriptor"), str);
			return -EINVAL;
		}
		if (fd < 0 || fd == STDIN_FILENO || fd == STDOUT_FILENO || fd == STDERR_FILENO) {
			warnx(_("invalid file descriptor"));
			return -EINVAL;
		}
		(*idx)++;

	} else if (argc > *idx && *argv[*idx] == '$') {
		fd = get_named_fd(sh, argv[*idx] + 1);
		(*idx)++;
	} else
		fd = dflt_fd;

	if (fd < 0) {
		warnx(_("no FD avalable"));
		return -EINVAL;
	}

	return fd;
}

/* close <FD> */
static int cmd_close(struct sh_context *sh, int argc, char *argv[])
{
	int idx = 1, fd, rc;

	fd = get_command_fd(sh, argc, argv, &idx, -1);
	if (fd < 0)
		return fd;

	rc = close(fd);
	if (rc != 0)
		warn(_("cannot close %d"), fd);
	else
		printf(_(" %d closed\n"), fd);

	remove_named_fd_by_id(sh, fd);

	return rc;
}



static const char *fsconfig_command_names[] =
{
	[FSCONFIG_SET_FLAG]	   = "flag",		/* Set parameter, supplying no value */
	[FSCONFIG_SET_STRING]	   = "string",		/* Set parameter, supplying a string value */
	[FSCONFIG_SET_BINARY]	   = "binary",		/* Set parameter, supplying a binary blob value */
	[FSCONFIG_SET_PATH]	   = "path",		/* Set parameter, supplying an object by path */
	[FSCONFIG_SET_PATH_EMPTY]  = "path-empty",	/* Set parameter, supplying an object by (empty) path */
	[FSCONFIG_SET_FD]          = "fd",		/* Set parameter, supplying an object by fd */
	[FSCONFIG_CMD_CREATE]	   = "create",		/* Invoke superblock creation */
	[FSCONFIG_CMD_RECONFIGURE] = "reconfigure"	/* Invoke superblock reconfiguration */
};

/* fsconfig [FD] <command> [<key> [<value>] [<aux>]] */
static int cmd_fsconfig(struct sh_context *sh, int argc, char *argv[])
{
	size_t i;
	int fd;
	int idx = 1;		/* argv[] index */
	unsigned int cmd = 0;
	char *key = NULL;
	const char *value = NULL;
	int  aux = 0;
	const char *cmdname = NULL;
	int rc = 0;

	/* [<fd>] */
	fd = get_command_fd(sh, argc, argv, &idx, sh->cfd);
	if (fd < 0)
		return fd;

	/* <command> */
	if (idx >= argc) {
		warnx(_("<command> not specified"));
		return -EINVAL;
	}
	for (i = 0; i < ARRAY_SIZE(fsconfig_command_names); i++) {
		if (strcmp(fsconfig_command_names[i], argv[idx]) == 0) {
			cmd = (unsigned int) i;
			cmdname = argv[idx];
			break;
		}
	}
	if (!cmdname) {
		warnx(_("unsupported command '%s'"), argv[idx]);
		return -EINVAL;
	}
	idx++;

	/* <key> */
	switch (cmd) {
	case FSCONFIG_SET_FLAG:
	case FSCONFIG_SET_STRING:
	case FSCONFIG_SET_BINARY:
	case FSCONFIG_SET_PATH:
	case FSCONFIG_SET_PATH_EMPTY:
	case FSCONFIG_SET_FD:
		if (idx >= argc) {
			warnx(_("%s requires <key>"), cmdname);
			return -EINVAL;
		}
		key = argv[idx++];
		break;
	default:
		break;
	}

	/* <value> */
	switch (cmd) {
	case FSCONFIG_SET_STRING:
	case FSCONFIG_SET_BINARY:
	case FSCONFIG_SET_PATH:
	case FSCONFIG_SET_PATH_EMPTY:
		if (idx >= argc) {
			warnx(_("%s requires <value>"), cmdname);
			return -EINVAL;
		}
		value = get_string(argc, argv, &idx);
		break;
	default:
		break;
	}

	/* <aux> */
	switch (cmd) {
	case FSCONFIG_SET_PATH:
	case FSCONFIG_SET_BINARY:
	case FSCONFIG_SET_FD:
		if (idx < argc) {
			char *end, *p = argv[idx++];

			errno = 0;
			aux = (unsigned int) strtoul(p, &end, 10);
			if (errno || p == end || (end && *end)) {
				warn(_("cannot use '%s' as aux"), p);
				return -EINVAL;
			}
		} else if (cmd == FSCONFIG_SET_PATH) {
			aux = AT_FDCWD;
		} else {
			warnx(_("%s requires <aux>"), cmdname);
			return -EINVAL;
		}
		break;
	default:
		break;
	}

	if (argc > idx) {
		warnx(_("wrong number of arguments"));
		return -EINVAL;
	}

	rc = fsconfig(fd, cmd, key, value, aux);
	if (rc != 0)
		warn(_("fsconfig failed"));

	return rc;
}

/* Do not use MS_* (for example from libmount) for fsmount(). Some attributes
 * are fsmount() specified and some anothers have to be set by fsconfig().
 */
static const struct mask_name fsmount_attrs[] =
{
	{ "ro",			MOUNT_ATTR_RDONLY },
	{ "nosuid",		MOUNT_ATTR_NOSUID },
	{ "nodev",		MOUNT_ATTR_NODEV },
	{ "noexec",		MOUNT_ATTR_NOEXEC },
	{ "atime",		MOUNT_ATTR__ATIME },
	{ "realatime",		MOUNT_ATTR_RELATIME },
	{ "noatime",		MOUNT_ATTR_NOATIME },
	{ "strictatime",	MOUNT_ATTR_STRICTATIME },
	{ "nodiratime",		MOUNT_ATTR_NODIRATIME }
};

/* fsmount [FD] [CLOEXEC] [<attrs>] */
static int cmd_fsmount(struct sh_context *sh, int argc, char *argv[])
{
	int idx = 1, fd, rc;
	unsigned int flags = 0, attrs = 0;

	/* [FD] */
	fd = get_command_fd(sh, argc, argv, &idx, sh->cfd);
	if (fd < 0)
		return fd;

	/* <flags> */
	if (idx < argc && strcmp(argv[idx], "CLOEXEC") == 0) {
		flags = FSMOUNT_CLOEXEC;
		idx++;
	}

	/* <attrs> */
	if (idx < argc) {
		rc = string_to_mask(argv[idx], &attrs,
				fsmount_attrs, ARRAY_SIZE(fsmount_attrs));
		if (rc)
			return rc;
		idx++;
	}

	if (argc > idx) {
		warnx(_("wrong number of arguments"));
		return -EINVAL;
	}

	rc = fsmount(fd, flags, attrs);
	if (rc < 0)
		warn(_("fsmount failed"));
	else {
		if (sh->mfd < 0)
			sh->mfd = rc;
		printf(_("new FD [fsmount]: %d\n"), rc);
	}

	return rc;
}

static const struct mask_name move_flags[] =
{
	{ "F_SYMLINKS",		MOVE_MOUNT_F_SYMLINKS },
	{ "F_AUTOMOUNTS",	MOVE_MOUNT_F_AUTOMOUNTS },
	{ "F_EMPTY_PATH",	MOVE_MOUNT_F_EMPTY_PATH },
	{ "T_SYMLINKS",		MOVE_MOUNT_T_SYMLINKS },
	{ "T_AUTOMOUNTS",	MOVE_MOUNT_T_AUTOMOUNTS },
	{ "T_EMPTY_PATH",	MOVE_MOUNT_T_EMPTY_PATH }
};

/* <from-FD> <to-path> [<flags>] */
static int cmd_move_simple(struct sh_context *sh, int argc, char *argv[])
{
	int idx = 1, rc = 0, fd_from = -1;
	const char *path_to = NULL;
	unsigned int flags = 0;

	if (argc < 3) {
		warnx(_("insufficiend number of arguments"));
		return EINVAL;
	}

	/* from-dirfd */
	if ((isdigit_string(argv[idx]) || *argv[idx] == '$'))
		fd_from = get_command_fd(sh, argc, argv, &idx, -1);
	else {
		warnx(_("%s: unsupported from-dirfd"), argv[idx]);
		return -EINVAL;
	}

	/* to-PATH */
	path_to = get_string(argc, argv, &idx);

	/* flags */
	if (idx < argc) {
		rc = string_to_mask(argv[idx], &flags,
				move_flags, ARRAY_SIZE(move_flags));
		idx++;
	}

	if (argc > idx)
		warnx(_("wrong number of arguments"));
	if (rc == 0) {
		rc = move_mount(fd_from, "", AT_FDCWD, path_to,
			flags|MOVE_MOUNT_F_EMPTY_PATH);
		if (rc < 0)
			warn(_("move_mount() failed"));
	}
	return rc;
}

/* <from-FD> <from-path> <to-FD> <to-path> [<flags>] */
static int cmd_move_full(struct sh_context *sh, int argc, char *argv[])
{
	int idx = 1, rc = 0, fd_from = -1, fd_to = -1;
	const char *path_from = NULL, *path_to = NULL;
	unsigned int flags = 0;

	if (argc < 5) {
		warnx(_("insufficiend number of arguments"));
		return EINVAL;
	}

	/* from-dirfd */
	if ((isdigit_string(argv[idx]) || *argv[idx] == '$'))
		fd_from = get_command_fd(sh, argc, argv, &idx, -1);
	else {
		warnx(_("%s: unsupported from-dirfd"), argv[idx]);
		return -EINVAL;
	}

	/* from-PATH */
	path_from = get_string(argc, argv, &idx);

	/* to-dirfd */
	if ((isdigit_string(argv[idx]) || *argv[idx] == '$'))
		fd_to = get_command_fd(sh, argc, argv, &idx, -1);
	else if (strcmp("AT_FDCWD", argv[idx]) == 0 ||
		 strcmp("CWD", argv[idx]) == 0) {
		fd_to = AT_FDCWD;
		idx++;
	} else {
		warnx(_("%s: unsupported to-dirfd"), argv[idx]);
		return -EINVAL;
	}

	/* to-PATH */
	path_to = get_string(argc, argv, &idx);

	/* flags */
	if (idx < argc) {
		rc = string_to_mask(argv[idx], &flags,
				move_flags, ARRAY_SIZE(move_flags));
		idx++;
	}

	if (argc > idx)
		warnx(_("wrong number of arguments"));
	if (rc == 0) {
		rc = move_mount(fd_from, path_from, fd_to, path_to, flags);
		if (rc < 0)
			warn(_("move_mount() failed"));
	}
	return rc;
}


/* move_mount */
static int cmd_move(struct sh_context *sh, int argc, char *argv[])
{
	if (argc > 4)
		return cmd_move_full(sh, argc, argv);

	return cmd_move_simple(sh, argc, argv);
}

static int cmd_help(struct sh_context *sh __attribute__((__unused__)),
		    int argc, char *argv[])
{
	size_t i;

	if (argc < 2) {
		/* list all commands */
		printf(_("\nSupported commands:\n"));
		for (i = 0; i < ARRAY_SIZE(commands); i++)
			printf("  %-12s %s\n", commands[i].name, commands[i].desc);
		printf(_("\nUse \"help <command>\" for more details.\n"));
		printf(_("All non-mountsh commands will be processed by regular shell.\n"));
		fputc('\n', stdout);
		printf(_("The three ways how to use file descritors are supported:\n"));
		fputc('\n', stdout);
		printf(_(" 1) default file descritors; initialized by fsopen and fsmount\n"));
		printf("    >>> fsopen ext4\n");
		printf("    >>> fsconfig string source /dev/sda1\n");
		fputc('\n', stdout);
		printf(_(" 2) explicit file descritors:\n"));
		printf("    >>> fsopen ext4\n");
		printf("    new FD [fscontext]: 3\n");
		printf("    >>> fsconfig 3 flag ro\n");
		fputc('\n', stdout);
		printf(_(" 3) named file descritors:\n"));
		printf("    >>> abc = fsopen ext4\n");
		printf("    >>> fsconfig $abc flag ro\n");
		fputc('\n', stdout);
	} else {
		const char *name = argv[1];
		const struct sh_command *cmd = lookup_command(name);

		if (!cmd)
			warnx(_("%s: command not found"), name);

		fputs("Description:\n", stdout);
		printf("\t%s - %s\n", cmd->name, cmd->desc);
		if (cmd->syno) {
			fputs("Synopsis:\n", stdout);
			fputs(cmd->syno, stdout);
		}
		fputc('\n', stdout);
	}
	return 0;
}

static int mainloop(struct sh_context *sh)
{
	const char *prompt = getuid() == 0 ? ">>> # " : ">>> $ ";
	char buf[BUFSIZ];
	int rc = 0;	/* internal status */

	do {
		const struct sh_command *cmd = NULL;
		int argc, idx = 0;
		char **argv, *name, *varname = NULL;

		rc = get_user_reply(prompt, buf, sizeof(buf));
		if (rc)
			break;

		argv = strv_split(buf, " ");
		if (!argv)
			continue;
		argc = (int) strv_length(argv);
		if (!argc) {
			strv_free(argv);
			continue;
		}

		/* <var> = <command> */
		if (argc >= 3 && strcmp(argv[1], "=") == 0) {
			varname = argv[idx++];		/* variable name */
			idx++;				/* skip '=' */
		}

		/* <command> */
		name = argv[idx];
		cmd = lookup_command(name);

		if (varname && (!cmd || !cmd->refd))
			warnx(_("command does not support named file descriptors"));
		else if (!cmd)
			ignore_result( system(buf) );
		else {
			int res = execute_command(sh, cmd, argc - idx, argv + idx);

			if (varname && res >= 0)
				set_named_fd(sh, varname, res);
		}

		add_history(buf);
		strv_free(argv);
	} while (1);

	return rc;
}

static void __attribute__((__noreturn__)) usage(void)
{
	FILE *out = stdout;
	fputs(USAGE_HEADER, out);
	fprintf(out,
	      _(" %s [options]\n"), program_invocation_short_name);

	fputs(USAGE_SEPARATOR, out);
	fputs(_("Shell-like command to modify mount tree.\n"), out);

	fputs(USAGE_OPTIONS, out);
	fputs(USAGE_SEPARATOR, out);
	printf(USAGE_HELP_OPTIONS(22));

	printf(USAGE_MAN_TAIL("mountsh(8)"));
	exit(EXIT_SUCCESS);
}

int main(int argc, char *argv[])
{
	int rc, c;
	struct sh_context *sh;

	static const struct option longopts[] = {
		{ "help",    no_argument,       NULL, 'h' },
		{ "version", no_argument,       NULL, 'v' },
		{ NULL, 0, NULL, 0 },
	};

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	close_stdout_atexit();

	while ((c = getopt_long(argc, argv, "hV", longopts, NULL)) != -1) {

		switch(c) {
		case 'h':
			usage();
		case 'V':
			print_version(EXIT_SUCCESS);
		default:
			errtryhelp(EXIT_FAILURE);
		}
	}

	sh = xcalloc(1, sizeof(*sh));
	sh->mfd = -1;
	sh->cfd = -1;

	fputc('\n', stdout);
	printf(_("Welcome to mountsh, use 'help' for more details.\n"));
	printf(_("This shell PID is %d.\n"), getpid());
	fputc('\n', stdout);

	rc = mainloop(sh);

	cleanup(sh);
	return rc != 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
