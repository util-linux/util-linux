/*
 * test_lsfd - make various file descriptors
 *
 * Written by Masatake YAMATO <yamato@redhat.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include "c.h"
#include "nls.h"
#include "xalloc.h"

#define _U_ __attribute__((__unused__))

static void __attribute__((__noreturn__)) usage(FILE *out, int status)
{
	fputs(USAGE_HEADER, out);
	fprintf(out, _(" %s [options] FACTORY FD... [PARAM=VAL...]\n"), program_invocation_short_name);

	fputs(USAGE_OPTIONS, out);
	fputs(_(" -l, --list                    list available file descriptor factories and exit\n"), out);
	fputs(_(" -I, --parameters <factory>    list parameters the factory takes\n"), out);
	fputs(_(" -r, --comm <name>             rename self\n"), out);
	fputs(_(" -q, --quiet                   don't print pid(s)\n"), out);
	fputs(_(" -c, --dont-pause              don't pause after making fd(s)\n"), out);

	fputs(USAGE_SEPARATOR, out);
	fputs(_("Examples:\n"), out);
	fprintf(out, _("Using 3, open /etc/group:\n\n	$ %s ro-regular-file 3 file=/etc/group\n\n"),
		program_invocation_short_name);
	fprintf(out, _("Using 3 and 4, make a pipe:\n\n	$ %s pipe-no-fork 3 4\n\n"),
		program_invocation_short_name);

	exit(status);
}

union value {
	const char *string;
	long integer;
};

enum ptype {
	PTYPE_STRING,
	PTYPE_INTEGER,
};

struct ptype_class {
	const char *name;

	/* Covert to a string representation.
	 * A caller must free the returned value with free(3) after using. */
	char *(*sprint)(const union value *value);

	/* Convert from a string. If ARG is NULL, use DEFV instead.
	 * A caller must free the returned value with the free method
	 * after using. */
	union value (*read)(const char *arg, const union value *defv);

	/* Free the value returned from the read method. */
	void (*free)(union value value);
};

#define ARG_STRING(A) (A.v.string)
#define ARG_INTEGER(A) (A.v.integer)
struct arg {
	union value v;
	void (*free)(union value value);
};

struct parameter {
	const char *name;
	const enum ptype type;
	const char *desc;
	union value defv;	/* Default value */
};

static char *string_sprint(const union value *value)
{
	return xstrdup(value->string);
}

static union value string_read(const char *arg, const union value *defv)
{
	return (union value){ .string = xstrdup(arg?: defv->string) };
}

static void string_free(union value value)
{
	free((void *)value.string);
}

static char *integer_sprint(const union value *value)
{
	char *str = NULL;
	xasprintf(&str, "%ld", value->integer);
	return str;
}

static union value integer_read(const char *arg, const union value *defv)
{
	char *ep;
	union value r;

	if (!arg)
		return *defv;

	errno = 0;
	r.integer = strtol(arg, &ep, 10);
	if (errno)
		err(EXIT_FAILURE, _("fail to make a number from %s"), arg);
	else if (*ep != '\0')
		errx(EXIT_FAILURE, _("garbage at the end of number: %s"), arg);
	return r;
}

static void integer_free(union value value _U_)
{
	/* Do nothing */
}


struct ptype_class ptype_classes [] = {
	[PTYPE_STRING] = {
		.name = "string",
		.sprint = string_sprint,
		.read   = string_read,
		.free   = string_free,
	},
	[PTYPE_INTEGER] = {
		.name = "integer",
		.sprint = integer_sprint,
		.read   = integer_read,
		.free   = integer_free,
	},
};

static struct arg decode_arg(const char *pname,
			     const struct parameter *parameters,
			     int argc, char **argv)
{
	char *v = NULL;
	size_t len = strlen(pname);
	const struct parameter *p = NULL;
	struct arg arg;

	while (parameters->name) {
		if (strcmp(pname, parameters->name) == 0) {
			p = parameters;
			break;
		}
		parameters++;
	}
	if (p == NULL)
		errx(EXIT_FAILURE, _("no such parameter: %s"), pname);

	for (int i = 0; i < argc; i++) {
		if (strncmp(pname, argv[i], len) == 0) {
			v = argv[i] + len;
			if (*v == '=') {
				v++;
				break;
			} else if (*v == '\0')
				errx(EXIT_FAILURE,
				     _("no value given for \"%s\" parameter"),
				     pname);
			else
				v = NULL;
		}
	}
	arg.v = ptype_classes [p->type].read (v, &p->defv);
	arg.free = ptype_classes [p->type].free;
	return arg;
}

static void free_arg(struct arg *arg)
{
	arg->free(arg->v);
}

struct fdesc {
	int fd;
	void (*close)(int, void *);
	void *data;
};

struct factory {
	const char *name;	/* [-a-zA-Z0-9_]+ */
	const char *desc;
	bool priv;		/* the root privilege is needed to make fd(s) */
#define MAX_N 3
	int  N;			/* the number of fds this factory makes */
	bool fork;		/* whether this factory make a child process or not */
	void (*make)(const struct factory *, struct fdesc[], pid_t *, int, char **);
	const struct parameter * params;
};

static void close_fdesc(int fd, void *data _U_)
{
	close(fd);
}

static void open_ro_regular_file(const struct factory *factory, struct fdesc fdescs[], pid_t * child _U_,
				 int argc, char ** argv)
{
	struct arg file = decode_arg("file", factory->params, argc, argv);
	struct arg offset = decode_arg("offset", factory->params, argc, argv);

	int fd = open(ARG_STRING(file), O_RDONLY);
	if (fd < 0)
		err(EXIT_FAILURE, "failed to open: %s", ARG_STRING(file));
	free_arg(&file);

	if (ARG_INTEGER(offset) != 0) {
		if (lseek(fd, (off_t)ARG_INTEGER(offset), SEEK_CUR) < 0) {
			int e = errno;
			close(fd);
			errno = e;
			err(EXIT_FAILURE, "failed to seek 0 -> %ld", ARG_INTEGER(offset));
		}
	}
	free_arg(&offset);

	if (fd != fdescs[0].fd) {
		if (dup2(fd, fdescs[0].fd) < 0) {
			int e = errno;
			close(fd);
			errno = e;
			err(EXIT_FAILURE, "failed to dup %d -> %d", fd, fdescs[0].fd);
		}
		close(fd);
	}

	fdescs[0] = (struct fdesc){
		.fd    = fdescs[0].fd,
		.close = close_fdesc,
		.data  = NULL
	};
}

static void make_pipe(const struct factory *factory, struct fdesc fdescs[], pid_t * child _U_,
		      int argc, char ** argv)
{
	int pd[2];
	int nonblock_flags[2] = {0, 0};
	struct arg nonblock = decode_arg("nonblock", factory->params, argc, argv);
	if (strlen(ARG_STRING(nonblock)) != 2) {
		errx(EXIT_FAILURE, "string value for %s has unexpected length: %s",
		     "nonblock", ARG_STRING(nonblock));
	}

	for (int i = 0; i < 2; i++) {
		if (ARG_STRING(nonblock)[i] == '-')
			continue;
		if ((i == 0 && ARG_STRING(nonblock)[i] == 'r')
		    || (i == 1 && ARG_STRING(nonblock)[i] == 'w'))
			nonblock_flags[i] = 1;
		else
			errx(EXIT_FAILURE, "unexpected value %c for the %s fd of %s",
			     ARG_STRING(nonblock)[i],
			     (i == 0)? "read": "write",
			     "nonblock");
	}
	free_arg(&nonblock);

	if (pipe(pd) < 0)
		err(EXIT_FAILURE, "failed to make pipe");

	for (int i = 0; i < 2; i++) {
		if (nonblock_flags[i]) {
			int flags = fcntl(pd[i], F_GETFL);
			if (fcntl(pd[i], F_SETFL, flags|O_NONBLOCK) < 0) {
				int e = errno;
				close(pd[0]);
				close(pd[1]);
				errno = e;
				errx(EXIT_FAILURE, "failed to set NONBLOCK flag to the %s fd",
				     (i == 0)? "read": "write");
			}
		}
	}

	for (int i = 0; i < 2; i++) {
		if (pd[i] != fdescs[i].fd) {
			if (dup2(pd[i], fdescs[i].fd) < 0) {
				int e = errno;
				close(pd[0]);
				close(pd[1]);
				errno = e;
				err(EXIT_FAILURE, "failed to dup %d -> %d",
				    pd[i], fdescs[i].fd);
			}
			close(pd[i]);
		}
		fdescs[i] = (struct fdesc){
			.fd    = fdescs[i].fd,
			.close = close_fdesc,
			.data  = NULL
		};
	}
}

static void close_dir(int fd, void *data)
{
	DIR *dp = data;
	if (dp)
		closedir(dp);
	else
		close_fdesc(fd, NULL);
}

static void open_directory(const struct factory *factory, struct fdesc fdescs[], pid_t * child _U_,
			   int argc, char ** argv)
{
	struct arg dir = decode_arg("dir", factory->params, argc, argv);
	struct arg dentries = decode_arg("dentries", factory->params, argc, argv);
	DIR *dp = NULL;

	int fd = open(ARG_STRING(dir), O_RDONLY|O_DIRECTORY);
	if (fd < 0)
		err(EXIT_FAILURE, "failed to open: %s", ARG_STRING(dir));
	free_arg(&dir);

	if (fd != fdescs[0].fd) {
		if (dup2(fd, fdescs[0].fd) < 0) {
			int e = errno;
			close(fd);
			errno = e;
			err(EXIT_FAILURE, "failed to dup %d -> %d", fd, fdescs[0].fd);
		}
		close(fd);
	}

	if (ARG_INTEGER(dentries) > 0) {
		dp = fdopendir(fdescs[0].fd);
		if (dp == NULL) {
			int e = errno;
			close(fdescs[0].fd);
			errno = e;
			err(EXIT_FAILURE, "failed to make DIR* from fd: %s", ARG_STRING(dir));
		}
		for (int i = 0; i < ARG_INTEGER(dentries); i++) {
			struct dirent *d = readdir(dp);
			if (!d) {
				int e = errno;
				closedir(dp);
				errno = e;
				err(EXIT_FAILURE, "failed in readdir(3)");
			}
		}
	}
	free_arg(&dentries);


	fdescs[0] = (struct fdesc){
		.fd    = fdescs[0].fd,
		.close = close_dir,
		.data  = dp
	};
}

static void open_rw_chrdev(const struct factory *factory, struct fdesc fdescs[], pid_t * child _U_,
			   int argc, char ** argv)
{
	struct arg chrdev = decode_arg("chrdev", factory->params, argc, argv);
	int fd = open(ARG_STRING(chrdev), O_RDWR);
	if (fd < 0)
		err(EXIT_FAILURE, "failed to open: %s", ARG_STRING(chrdev));
	free_arg(&chrdev);

	fdescs[0] = (struct fdesc){
		.fd    = fd,
		.close = close_fdesc,
		.data  = NULL
	};
}

static void make_socketpair(const struct factory *factory, struct fdesc fdescs[], pid_t * child _U_,
			    int argc, char ** argv)
{
	int sd[2];
	struct arg socktype = decode_arg("socktype", factory->params, argc, argv);
	int isocktype;
	if (strcmp(ARG_STRING(socktype), "STREAM") == 0)
		isocktype = SOCK_STREAM;
	else if (strcmp(ARG_STRING(socktype), "DGRAM") == 0)
		isocktype = SOCK_DGRAM;
	else if (strcmp(ARG_STRING(socktype), "SEQPACKET") == 0)
		isocktype = SOCK_SEQPACKET;
	else
		errx(EXIT_FAILURE,
		     "unknown socket type for socketpair(AF_UNIX,...): %s",
		     ARG_STRING(socktype));
	free_arg(&socktype);

	if (socketpair(AF_UNIX, isocktype, 0, sd) < 0)
		err(EXIT_FAILURE, "failed to make socket pair");

	for (int i = 0; i < 2; i++) {
		if (sd[i] != fdescs[i].fd) {
			if (dup2(sd[i], fdescs[i].fd) < 0) {
				int e = errno;
				close(sd[0]);
				close(sd[1]);
				errno = e;
				err(EXIT_FAILURE, "failed to dup %d -> %d",
				    sd[i], fdescs[i].fd);
			}
			close(sd[i]);
		}
		fdescs[i] = (struct fdesc){
			.fd    = fdescs[i].fd,
			.close = close_fdesc,
			.data  = NULL
		};
	}
}

static void open_with_opath(const struct factory *factory, struct fdesc fdescs[], pid_t * child _U_,
			    int argc, char ** argv)
{
	struct arg path = decode_arg("path", factory->params, argc, argv);
	int fd = open(ARG_STRING(path), O_PATH|O_NOFOLLOW);
	if (fd < 0)
		err(EXIT_FAILURE, "failed to open with O_PATH: %s", ARG_STRING(path));
	free_arg(&path);

	fdescs[0] = (struct fdesc){
		.fd    = fd,
		.close = close_fdesc,
		.data  = NULL
	};
}

static void open_ro_blkdev(const struct factory *factory, struct fdesc fdescs[], pid_t * child _U_,
			    int argc, char ** argv)
{
	struct arg blkdev = decode_arg("blkdev", factory->params, argc, argv);
	int fd = open(ARG_STRING(blkdev), O_RDONLY);
	if (fd < 0)
		err(EXIT_FAILURE, "failed to open: %s", ARG_STRING(blkdev));
	free_arg(&blkdev);

	fdescs[0] = (struct fdesc){
		.fd    = fd,
		.close = close_fdesc,
		.data  = NULL,
	};
}

#define PARAM_END { .name = NULL, }
static const struct factory factories[] = {
	{
		.name = "ro-regular-file",
		.desc = "read-only regular file",
		.priv = false,
		.N    = 1,
		.fork = false,
		.make = open_ro_regular_file,
		.params = (struct parameter []) {
			{
				.name = "file",
				.type = PTYPE_STRING,
				.desc = "file to be opened",
				.defv.string = "/etc/passwd",
			},
			{
				.name = "offset",
				.type = PTYPE_INTEGER,
				.desc = "seek bytes after open with SEEK_CUR",
				.defv.integer = 0,
			},
			PARAM_END
		},
	},
	{
		.name = "pipe-no-fork",
		.desc = "making pair of fds with pipe(2)",
		.priv = false,
		.N    = 2,
		.fork = false,
		.make = make_pipe,
		.params = (struct parameter []) {
			{
				.name = "nonblock",
				.type = PTYPE_STRING,
				.desc = "set nonblock flag (\"--\", \"r-\", \"-w\", or \"rw\")",
				.defv.string = "--",
			},
			PARAM_END
		},
	},
	{
		.name = "directory",
		.desc = "directory",
		.priv = false,
		.N    = 1,
		.fork = false,
		.make = open_directory,
		.params = (struct parameter []) {
			{
				.name = "dir",
				.type = PTYPE_STRING,
				.desc = "directory to be opened",
				.defv.string = "/",
			},
			{
				.name = "dentries",
				.type = PTYPE_INTEGER,
				.desc = "read the number of dentries after open with readdir(3)",
				.defv.integer = 0,
			},
			PARAM_END
		},
	},
	{
		.name = "rw-character-device",
		.desc = "character device with O_RDWR flag",
		.priv = false,
		.N    = 1,
		.fork = false,
		.make = open_rw_chrdev,
		.params = (struct parameter []) {
			{
				.name = "chrdev",
				.type = PTYPE_STRING,
				.desc = "character device node to be opened",
				.defv.string = "/dev/zero",
			},
			PARAM_END
		},
	},
	{
		.name = "socketpair",
		.desc = "AF_UNIX socket pair created with socketpair(2)",
		.priv = false,
		.N    = 2,
		.fork = false,
		.make = make_socketpair,
		.params = (struct parameter []) {
			{
				.name = "socktype",
				.type = PTYPE_STRING,
				.desc = "STREAM, DGRAM, or SEQPACKET",
				.defv.string = "STREAM",
			},
			PARAM_END
		},
	},
	{
		.name = "symlink",
		.desc = "symbolic link itself opened with O_PATH",
		.priv = false,
		.N    = 1,
		.fork = false,
		.make = open_with_opath,
		.params = (struct parameter []) {
			{
				.name = "path",
				.type = PTYPE_STRING,
				.desc = "path to a symbolic link",
				.defv.string = "/dev/stdin",
			},
			PARAM_END
		},
	},
	{
		.name = "ro-block-device",
		.desc = "block device with O_RDONLY flag",
		.priv = true,
		.N = 1,
		.fork = false,
		.make = open_ro_blkdev,
		.params = (struct parameter []) {
			{
				.name = "blkdev",
				.type = PTYPE_STRING,
				.desc = "block device node to be opened",
				.defv.string = "/dev/nullb0",
			},
			PARAM_END
		},
	},
};

static int count_parameters(const struct factory *factory)
{

	const struct parameter *p = factory->params;
	if (!p)
		return 0;
	while (p->name)
		p++;
	return p - factory->params;
}

static void print_factory(const struct factory *factory)
{
	printf("%-20s %4s %5d %4s %6d %s\n",
	       factory->name,
	       factory->priv? "yes": "no",
	       factory->N,
	       factory->fork? "yes": "no",
	       count_parameters(factory),
	       factory->desc);
}

static void list_factories(void)
{
	printf("%-20s PRIV COUNT FORK NPARAM DESCRIPTION\n", "FACTORY");
	for (size_t i = 0; i < ARRAY_SIZE(factories); i++)
		print_factory(factories + i);
}

static const struct factory *find_factory(const char *name)
{
	for (size_t i = 0; i < ARRAY_SIZE(factories); i++)
		if (strcmp(factories[i].name, name) == 0)
			return factories + i;
	return NULL;
}

static void list_parameters(const char *factory_name)
{
	const struct factory *factory = find_factory(factory_name);
	const char *fmt = "%-15s %-8s %15s %s\n";

	if (!factory)
		errx(EXIT_FAILURE, _("no such factory: %s"), factory_name);

	if (!factory->params)
		return;

	printf(fmt, "PARAMETER", "TYPE", "DEFAULT_VALUE", "DESCRIPTION");
	for (const struct parameter *p = factory->params; p->name != NULL; p++) {
		char *defv = ptype_classes[p->type].sprint(&p->defv);
		printf(fmt, p->name, ptype_classes[p->type].name, defv, p->desc);
		free(defv);
	}
}

static void rename_self(const char *comm)
{
	if (prctl(PR_SET_NAME, (unsigned long)comm, 0, 0, 0) < 0)
		err(EXIT_FAILURE, _("failed to rename self via prctl: %s"), comm);
}

static void do_nothing(int signum _U_)
{
}

int main(int argc, char **argv)
{
	int c;
	pid_t pid[2];
	const struct factory *factory;
	struct fdesc fdescs[MAX_N];
	bool quiet = false;
	bool cont  = false;

	pid[0] = getpid();
	pid[1] = -1;

	static const struct option longopts[] = {
		{ "list",	no_argument, NULL, 'l' },
		{ "parameters", required_argument, NULL, 'I' },
		{ "comm",       required_argument, NULL, 'r' },
		{ "quiet",	no_argument, NULL, 'q' },
		{ "dont-puase", no_argument, NULL, 'c' },
		{ "help",	no_argument, NULL, 'h' },
	};

	while ((c = getopt_long(argc, argv, "lhqcI:r:", longopts, NULL)) != -1) {
		switch (c) {
		case 'h':
			usage(stdout, EXIT_SUCCESS);
		case 'l':
			list_factories();
			exit(EXIT_SUCCESS);
		case 'I':
			list_parameters(optarg);
			exit(EXIT_SUCCESS);
		case 'q':
			quiet = true;
			break;
		case 'c':
			cont = true;
			break;
		case 'r':
			rename_self(optarg);
			break;
		default:
			usage(stderr, EXIT_FAILURE);
		}
	}


	if (optind == argc)
		errx(EXIT_FAILURE, _("no file descriptor specification given"));

	factory = find_factory(argv[optind]);
	if (!factory)
		errx(EXIT_FAILURE, _("no such factory: %s"), argv[optind]);
	assert(factory->N < MAX_N);
	optind++;

	if ((optind + factory->N) > argc)
		errx(EXIT_FAILURE, _("not enough file descriptors given for %s"),
		     factory->name);
	for (int i = 0; i < factory->N; i++) {
		char *str = argv[optind + i];
		long fd;
		char *ep;

		errno = 0;
		fd = strtol(str, &ep, 10);
		if (errno)
			err(EXIT_FAILURE, "failed to convert fd number: %s", str);
		if (ep == str)
			errx(EXIT_FAILURE, "failed to convert fd number: %s", str);
		if (*ep != '\0')
			errx(EXIT_FAILURE, _("garbage at the end of number: %s"), str);
		if (fd < 0)
			errx(EXIT_FAILURE, "fd number should not be negative: %s", str);
		if (fd < 3)
			errx(EXIT_FAILURE, "fd 0, 1, 2 are reserved: %s", str);
		fdescs[i].fd = fd;
	}
	optind += factory->N;

	factory->make(factory, fdescs, pid + 1, argc - optind, argv + optind);

	signal(SIGCONT, do_nothing);

	if (!quiet) {
		printf("%d", pid[0]);
		if (pid[1] != -1)
			printf(" %d", pid[1]);
		putchar('\n');
		fflush(stdout);
	}

	if (!cont)
		pause();

	for (int i = 0; i < factory->N; i++)
		fdescs[i].close(fdescs[i].fd, fdescs[i].data);

	exit(EXIT_SUCCESS);
}
