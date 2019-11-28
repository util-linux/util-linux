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

struct sh_fd {
	int	fd;
	const char *type;
	const char *name;	/* for fsopen() */
};

struct sh_context {
	int	cfd;		/* filesystem configuration (fsopen() FD) */
	int	mfd;		/* mount FD */
};

struct sh_command {
	const char	*name;	/* command name as used on command line */

	int (*func)(struct sh_context *sh, int argc, char *argv[]);

	const char	*desc;	/* description */
	const char	*syno;	/* synopsis */
};


static int cmd_help(struct sh_context *sh, int argc, char *argv[]);
static int cmd_fds(struct sh_context *sh, int argc, char *argv[]);
static int cmd_fsopen(struct sh_context *sh, int argc, char *argv[]);
static int cmd_close(struct sh_context *sh, int argc, char *argv[]);
static int cmd_fsconfig(struct sh_context *sh, int argc, char *argv[]);

static const struct sh_command commands[] =
{
	{ "close", cmd_close,
		N_("close file descritor"),
		N_("<fd>")
	},
	{ "fds", cmd_fds,
		N_("list relevant file descritors")
	},
	{ "fsconfig", cmd_fsconfig,
		N_("(re)configure or create filesystem"),
		N_("[fd] <flag|string|binary|path|path-empty|fd|create|reconf> [<key> [<value>] [<aux>]]")
	},
	{ "fsopen", cmd_fsopen,
		N_("creates filesystem context"),
		N_("<name> [CLOEXEC]")
	},
	{ "help", cmd_help,
		N_("list commands and help"),
		N_("[<command>]")
	}
};

static const struct sh_command *lookup_command(const char *name)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(commands); i++) {
		if (strcmp(commands[i].name, name) == 0)
			return &commands[i];
	}

	return NULL;
}

static int execute_command(struct sh_context *sh,
			   const struct sh_command *cmd,
			   int argc, char **argv)
{
	return cmd->func(sh, argc, argv);
}

static int get_user_reply(const char *prompt, char *buf, size_t bufsz)
{
	char *p;
	size_t sz;

#ifdef HAVE_LIBREADLINE
	if (isatty(STDIN_FILENO)) {
		p = readline(prompt);
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

	for (p = buf; *p && !isgraph(*p); p++);	/* get first non-blank */

	if (p > buf)
		memmove(buf, p, p - buf);	/* remove blank space */
	sz = strlen(buf);
	if (sz && *(buf + sz - 1) == '\n')
		*(buf + sz - 1) = '\0';
	return 0;
}

static int cmd_fds(struct sh_context *sh __attribute__((__unused__)),
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

		if (!*info || !startswith(info, "anon_inode:"))
			continue;
		type = info + 12;
		strrem(type, ']');
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
	return 0;
}


/* returns FD specified in argv[] element addressed by idx, on success idx is incremented,
 * otherwise default df is returned
 */
static int get_command_fd(int argc, char *argv[], int *idx, int dflt_fd)
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
	} else
		fd = dflt_fd;

	if (fd < 0) {
		warnx(_("no FD avalable"));
		return -EINVAL;
	}

	return fd;
}

/* close [FD] */
static int cmd_close(struct sh_context *sh __attribute__((__unused__)),
		     int argc, char *argv[])
{
	int idx = 1, fd, rc;

	if (argc > idx && !isdigit_string(argv[idx])) {
		warnx(_("cannot use '%s' as file descriptor"), argv[idx]);
		return -EINVAL;
	}

	fd = get_command_fd(argc, argv, &idx, sh->cfd);
	if (fd < 0)
		return fd;

	rc = close(fd);
	if (rc != 0)
		warn(_("cannot close %d"), fd);
	else
		printf(_(" %d closed\n"), fd);

	return rc;
}

struct fsconfig_cmd {
	unsigned int	cmd;
	const char	*name;
};

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
	char *value = NULL;
	int  aux = 0;
	const char *cmdname = NULL;
	int rc = 0;

	/* [<fd>] */
	fd = get_command_fd(argc, argv, &idx, sh->cfd);
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
		value = argv[idx++];
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
		printf(_("All non-mountsh commands will be processed by regular shell.\n\n"));
	} else {
		const char *name = argv[1];
		const struct sh_command *cmd = lookup_command(name);

		if (!cmd)
			warnx(_("%s: command not found"), name);

		printf("%s %s\n", cmd->name, cmd->syno ? : "");
		printf("  - %s\n", cmd->desc);
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
		int argc = 0;
		char **argv, *name;

		rc = get_user_reply(prompt, buf, sizeof(buf));
		if (rc)
			break;

		argv = strv_split(buf, " ");
		if (!argv)
			continue;
		argc = (int) strv_length(argv);

		name = argv[0];
		cmd = lookup_command(name);

		if (!cmd)
			ignore_result( system(buf) );
		else
			execute_command(sh, cmd, argc, argv);

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
	struct sh_context sh = { .cfd = -1 };

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

	fputc('\n', stdout);
	printf(_("Welcome to mountsh, use 'help' for more details.\n"));
	printf(_("This shell PID is %d.\n"), getpid());
	fputc('\n', stdout);

	rc = mainloop(&sh);
	return rc != 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
