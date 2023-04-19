/*
 * test_mkfds - make various file descriptors
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

#include <arpa/inet.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <linux/netlink.h>
#include <linux/sockios.h>  /* SIOCGSKNS */
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sched.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/user.h>
#include <unistd.h>

#include "c.h"
#include "xalloc.h"

#define EXIT_ENOSYS 17
#define EXIT_EPERM  18
#define EXIT_ENOPROTOOPT 19
#define EXIT_EPROTONOSUPPORT 20
#define EXIT_EACCESS 21

#define _U_ __attribute__((__unused__))

static int pidfd_open(pid_t pid, unsigned int flags);

static void __attribute__((__noreturn__)) usage(FILE *out, int status)
{
	fputs("\nUsage:\n", out);
	fprintf(out, " %s [options] FACTORY FD... [PARAM=VAL...]\n", program_invocation_short_name);

	fputs("\nOptions:\n", out);
	fputs(" -l, --list                    list available file descriptor factories and exit\n", out);
	fputs(" -I, --parameters <factory>    list parameters the factory takes\n", out);
	fputs(" -r, --comm <name>             rename self\n", out);
	fputs(" -q, --quiet                   don't print pid(s)\n", out);
	fputs(" -c, --dont-pause              don't pause after making fd(s)\n", out);

	fputs("\n", out);
	fputs("Examples:\n", out);
	fprintf(out, "Using 3, open /etc/group:\n\n	$ %s ro-regular-file 3 file=/etc/group\n\n",
		program_invocation_short_name);
	fprintf(out, "Using 3 and 4, make a pipe:\n\n	$ %s pipe-no-fork 3 4\n\n",
		program_invocation_short_name);

	exit(status);
}

union value {
	const char *string;
	long integer;
	unsigned long uinteger;
	bool boolean;
};

enum ptype {
	PTYPE_STRING,
	PTYPE_INTEGER,
	PTYPE_UINTEGER,
	PTYPE_BOOLEAN,
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
#define ARG_UINTEGER(A) (A.v.uinteger)
#define ARG_BOOLEAN(A) (A.v.boolean)
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
		err(EXIT_FAILURE, "fail to make a number from %s", arg);
	else if (*ep != '\0')
		errx(EXIT_FAILURE, "garbage at the end of number: %s", arg);
	return r;
}

static void integer_free(union value value _U_)
{
	/* Do nothing */
}

static char *uinteger_sprint(const union value *value)
{
	char *str = NULL;
	xasprintf(&str, "%lu", value->uinteger);
	return str;
}

static union value uinteger_read(const char *arg, const union value *defv)
{
	char *ep;
	union value r;

	if (!arg)
		return *defv;

	errno = 0;
	r.uinteger = strtoul(arg, &ep, 10);
	if (errno)
		err(EXIT_FAILURE, "fail to make a number from %s", arg);
	else if (*ep != '\0')
		errx(EXIT_FAILURE, "garbage at the end of number: %s", arg);
	return r;
}

static void uinteger_free(union value value _U_)
{
	/* Do nothing */
}

static char *boolean_sprint(const union value *value)
{
	return xstrdup(value->boolean? "true": "false");
}

static union value boolean_read(const char *arg, const union value *defv)
{
	union value r;

	if (!arg)
		return *defv;

	if (strcasecmp(arg, "true") == 0
	    || strcmp(arg, "1") == 0
	    || strcasecmp(arg, "yes") == 0
	    || strcasecmp(arg, "y") == 0)
		r.boolean = true;
	else
		r.boolean = false;
	return r;
}

static void boolean_free(union value value _U_)
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
	[PTYPE_UINTEGER] = {
		.name = "uinteger",
		.sprint = uinteger_sprint,
		.read   = uinteger_read,
		.free   = uinteger_free,
	},
	[PTYPE_BOOLEAN] = {
		.name = "boolean",
		.sprint = boolean_sprint,
		.read   = boolean_read,
		.free   = boolean_free,
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
		errx(EXIT_FAILURE, "no such parameter: %s", pname);

	for (int i = 0; i < argc; i++) {
		if (strncmp(pname, argv[i], len) == 0) {
			v = argv[i] + len;
			if (*v == '=') {
				v++;
				break;
			} else if (*v == '\0')
				errx(EXIT_FAILURE,
				     "no value given for \"%s\" parameter",
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
#define MAX_N 5
	int  N;			/* the number of fds this factory makes */
	int  EX_N;		/* fds made optionally */
	void *(*make)(const struct factory *, struct fdesc[], int, char **);
	void (*free)(const struct factory *, void *);
	void (*report)(const struct factory *, void *, FILE *);
	const struct parameter * params;
};

static void close_fdesc(int fd, void *data _U_)
{
	close(fd);
}

static void *open_ro_regular_file(const struct factory *factory, struct fdesc fdescs[],
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

	return NULL;
}

static void *make_pipe(const struct factory *factory, struct fdesc fdescs[],
		       int argc, char ** argv)
{
	int pd[2];
	int nonblock_flags[2] = {0, 0};
	struct arg nonblock = decode_arg("nonblock", factory->params, argc, argv);
	if (strlen(ARG_STRING(nonblock)) != 2) {
		errx(EXIT_FAILURE, "string value for %s has unexpected length: %s",
		     "nonblock", ARG_STRING(nonblock));
	}

	/* Make extra pipe descriptors for making pipe objects connected
	 * with fds more than 2.
	 * See https://github.com/util-linux/util-linux/pull/1622
	 * about the background of the requirement. */
	struct arg rdup = decode_arg("rdup", factory->params, argc, argv);
	struct arg wdup = decode_arg("wdup", factory->params, argc, argv);
	int xpd[2];
	xpd [0] = ARG_INTEGER(rdup);
	xpd [1] = ARG_INTEGER(wdup);

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

	/* Make extra pipe descriptors. */
	for (int i = 0; i < 2; i++) {
		if (xpd[i] >= 0) {
			if (dup2(fdescs[i].fd, xpd[i]) < 0) {
				int e = errno;
				close(fdescs[0].fd);
				close(fdescs[1].fd);
				if (i > 0 && xpd[0] >= 0)
					close(xpd[0]);
				errno = e;
				err(EXIT_FAILURE, "failed to dup %d -> %d",
				    fdescs[i].fd, xpd[i]);
			}
			fdescs[i + 2] = (struct fdesc){
				.fd = xpd[i],
				.close = close_fdesc,
				.data = NULL
			};
		}
	}

	return NULL;
}

static void close_dir(int fd, void *data)
{
	DIR *dp = data;
	if (dp)
		closedir(dp);
	else
		close_fdesc(fd, NULL);
}

static void *open_directory(const struct factory *factory, struct fdesc fdescs[],
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

	return NULL;
}

static void *open_rw_chrdev(const struct factory *factory, struct fdesc fdescs[],
			    int argc, char ** argv)
{
	struct arg chrdev = decode_arg("chrdev", factory->params, argc, argv);
	int fd = open(ARG_STRING(chrdev), O_RDWR);
	if (fd < 0)
		err(EXIT_FAILURE, "failed to open: %s", ARG_STRING(chrdev));
	free_arg(&chrdev);

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

	return NULL;
}

static void *make_socketpair(const struct factory *factory, struct fdesc fdescs[],
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

	return NULL;
}

static void *open_with_opath(const struct factory *factory, struct fdesc fdescs[],
			     int argc, char ** argv)
{
	struct arg path = decode_arg("path", factory->params, argc, argv);
	int fd = open(ARG_STRING(path), O_PATH|O_NOFOLLOW);
	if (fd < 0)
		err(EXIT_FAILURE, "failed to open with O_PATH: %s", ARG_STRING(path));
	free_arg(&path);

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

	return NULL;
}

static void *open_ro_blkdev(const struct factory *factory, struct fdesc fdescs[],
			    int argc, char ** argv)
{
	struct arg blkdev = decode_arg("blkdev", factory->params, argc, argv);
	int fd = open(ARG_STRING(blkdev), O_RDONLY);
	if (fd < 0)
		err(EXIT_FAILURE, "failed to open: %s", ARG_STRING(blkdev));
	free_arg(&blkdev);

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
		.data  = NULL,
	};

	return NULL;
}

static int make_packet_socket(int socktype, const char *interface)
{
	int sd;
	struct sockaddr_ll addr;

	sd = socket(AF_PACKET, socktype, htons(ETH_P_ALL));
	if (sd < 0)
		err(EXIT_FAILURE, "failed to make a socket with AF_PACKET");

	if (interface == NULL)
		return sd;	/* Just making a socket */

	memset(&addr, 0, sizeof(struct sockaddr_ll));
	addr.sll_family = AF_PACKET;
	addr.sll_ifindex = if_nametoindex(interface);
	if (addr.sll_ifindex == 0) {
		int e = errno;
		close(sd);
		errno = e;
		err(EXIT_FAILURE,
		    "failed to get the interface index for %s", interface);
	}
	if (bind(sd, (struct sockaddr *)&addr, sizeof(struct sockaddr_ll)) < 0) {
		int e = errno;
		close(sd);
		errno = e;
		err(EXIT_FAILURE,
		    "failed to get the interface index for %s", interface);
	}

	return sd;
}

struct munmap_data {
	void *ptr;
	size_t len;
};

static void close_fdesc_after_munmap(int fd, void *data)
{
	struct munmap_data *munmap_data = data;
	munmap(munmap_data->ptr, munmap_data->len);
	free(data);
	close(fd);
}

static void *make_mmapped_packet_socket(const struct factory *factory, struct fdesc fdescs[],
					int argc, char ** argv)
{
	int sd;
	struct arg socktype = decode_arg("socktype", factory->params, argc, argv);
	struct arg interface = decode_arg("interface", factory->params, argc, argv);

	int isocktype;
	const char *sinterface;
	struct tpacket_req req;
	struct munmap_data *munmap_data;

	if (strcmp(ARG_STRING(socktype), "DGRAM") == 0)
		isocktype = SOCK_DGRAM;
	else if (strcmp(ARG_STRING(socktype), "RAW") == 0)
		isocktype = SOCK_RAW;
	else
		errx(EXIT_FAILURE,
		     "unknown socket type for socket(AF_PACKET,...): %s",
		     ARG_STRING(socktype));
	free_arg(&socktype);

	sinterface = ARG_STRING(interface);
	sd = make_packet_socket(isocktype, sinterface);
	free_arg(&interface);

	/* Specify the spec of ring buffers.
	 *
	 * ref.
	 * - linux/Documentation/networking/packet_mmap.rst
	 * - https://sites.google.com/site/packetmmap/home
	 */
	req.tp_block_size = getpagesize();
	req.tp_frame_size = getpagesize();
	req.tp_block_nr = 1;
	req.tp_frame_nr = 1;
	if (setsockopt(sd, SOL_PACKET, PACKET_TX_RING, (char *)&req, sizeof(req)) < 0) {
		int e = errno;
		close(sd);
		errno = e;
		err((errno == ENOPROTOOPT? EXIT_ENOPROTOOPT: EXIT_FAILURE),
		    "failed to specify a buffer spec to a packet socket");
	}

	munmap_data = xmalloc(sizeof (*munmap_data));
	munmap_data->len = (size_t) req.tp_block_size * req.tp_block_nr;
	munmap_data->ptr = mmap(NULL, munmap_data->len, PROT_WRITE, MAP_SHARED, sd, 0);
	if (munmap_data->ptr == MAP_FAILED) {
		int e = errno;
		close(sd);
		free(munmap_data);
		errno = e;
		err(EXIT_FAILURE, "failed to do mmap a packet socket");
	}

	if (sd != fdescs[0].fd) {
		if (dup2(sd, fdescs[0].fd) < 0) {
			int e = errno;
			close(sd);
			munmap(munmap_data->ptr, munmap_data->len);
			free(munmap_data);
			errno = e;
			err(EXIT_FAILURE, "failed to dup %d -> %d", sd, fdescs[0].fd);
		}
		close(sd);
	}

	fdescs[0] = (struct fdesc){
		.fd    = fdescs[0].fd,
		.close = close_fdesc_after_munmap,
		.data  = munmap_data,
	};

	return NULL;
}

static void *make_pidfd(const struct factory *factory, struct fdesc fdescs[],
			int argc, char ** argv)
{
	struct arg target_pid = decode_arg("target-pid", factory->params, argc, argv);
	pid_t pid = ARG_INTEGER(target_pid);

	int fd = pidfd_open(pid, 0);
	if (fd < 0)
		err((errno == ENOSYS? EXIT_ENOSYS: EXIT_FAILURE),
		    "failed in pidfd_open(%d)", (int)pid);
	free_arg(&target_pid);

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

	return NULL;
}

static void *make_inotify_fd(const struct factory *factory _U_, struct fdesc fdescs[],
			     int argc _U_, char ** argv _U_)
{
	int fd = inotify_init();
	if (fd < 0)
		err(EXIT_FAILURE, "failed in inotify_init()");

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

	return NULL;
}

static void close_unix_socket(int fd, void *data)
{
	char *path = data;
	close(fd);
	if (path) {
		unlink(path);
		free(path);
	}
}

static void *make_unix_stream_core(const struct factory *factory, struct fdesc fdescs[],
				   int argc, char ** argv, int type, const char *typestr)
{
	struct arg path = decode_arg("path", factory->params, argc, argv);
	const char *spath = ARG_STRING(path);

	struct arg backlog = decode_arg("backlog", factory->params, argc, argv);
	int ibacklog = ARG_INTEGER(path);

	struct arg abstract = decode_arg("abstract", factory->params, argc, argv);
	bool babstract = ARG_BOOLEAN(abstract);

	struct arg server_shutdown = decode_arg("server-shutdown", factory->params, argc, argv);
	int iserver_shutdown = ARG_INTEGER(server_shutdown);
	struct arg client_shutdown = decode_arg("client-shutdown", factory->params, argc, argv);
	int iclient_shutdown = ARG_INTEGER(client_shutdown);

	int ssd, csd, asd;	/* server, client, and accepted socket descriptors */
	struct sockaddr_un un;
	size_t un_len = sizeof(un);

	memset(&un, 0, sizeof(un));
	un.sun_family = AF_UNIX;
	if (babstract) {
		strncpy(un.sun_path + 1, spath, sizeof(un.sun_path) - 1 - 1);
		size_t pathlen = strlen(spath);
		if (sizeof(un.sun_path) - 1 > pathlen)
			un_len = sizeof(un) - sizeof(un.sun_path) + 1 + pathlen;
	} else
		strncpy(un.sun_path,     spath, sizeof(un.sun_path) - 1    );

	free_arg(&client_shutdown);
	free_arg(&server_shutdown);
	free_arg(&abstract);
	free_arg(&backlog);
	free_arg(&path);

	if (iserver_shutdown < 0 || iserver_shutdown > 3)
		errx(EXIT_FAILURE, "the server shudown specification in unexpected range");
	if (iclient_shutdown < 0 || iclient_shutdown > 3)
		errx(EXIT_FAILURE, "the client shudown specification in unexpected range");

	ssd = socket(AF_UNIX, type, 0);
	if (ssd < 0)
		err(EXIT_FAILURE,
		    "failed to make a socket with AF_UNIX + SOCK_%s (server side)", typestr);
	if (ssd != fdescs[0].fd) {
		if (dup2(ssd, fdescs[0].fd) < 0) {
			int e = errno;
			close(ssd);
			errno = e;
			err(EXIT_FAILURE, "failed to dup %d -> %d", ssd, fdescs[0].fd);
		}
		close(ssd);
		ssd = fdescs[0].fd;
	}

	fdescs[0] = (struct fdesc){
		.fd    = fdescs[0].fd,
		.close = close_unix_socket,
		.data  = NULL,
	};

	if (!babstract)
		unlink(un.sun_path);
	if (bind(ssd, (const struct sockaddr *)&un, un_len) < 0) {
		int e = errno;
		close(ssd);
		errno = e;
		err(EXIT_FAILURE, "failed to bind a socket for listening");
	}

	if (!babstract)
		fdescs[0].data = xstrdup(un.sun_path);
	if (listen(ssd, ibacklog) < 0) {
		int e = errno;
		close_unix_socket(ssd, fdescs[0].data);
		errno = e;
		err(EXIT_FAILURE, "failed to listen a socket");
	}

	csd = socket(AF_UNIX, type, 0);
	if (csd < 0)
		err(EXIT_FAILURE,
		    "failed to make a socket with AF_UNIX + SOCK_%s (client side)", typestr);
	if (csd != fdescs[1].fd) {
		if (dup2(csd, fdescs[1].fd) < 0) {
			int e = errno;
			close(csd);
			close_unix_socket(ssd, fdescs[0].data);
			errno = e;
			err(EXIT_FAILURE, "failed to dup %d -> %d", csd, fdescs[1].fd);
		}
		close(csd);
		csd = fdescs[1].fd;
	}

	fdescs[1] = (struct fdesc){
		.fd    = fdescs[1].fd,
		.close = close_fdesc,
		.data  = NULL,
	};

	if (connect(csd, (const struct sockaddr *)&un, un_len) < 0) {
		int e = errno;
		close_fdesc(csd, NULL);
		close_unix_socket(ssd, fdescs[0].data);
		errno = e;
		err(EXIT_FAILURE, "failed to connect a socket to the listening socket");
	}

	if (!babstract)
		unlink(un.sun_path);

	asd = accept(ssd, NULL, NULL);
	if (asd < 0) {
		int e = errno;
		close_fdesc(csd, NULL);
		close_unix_socket(ssd, fdescs[0].data);
		errno = e;
		err(EXIT_FAILURE, "failed to accept a socket from the listening socket");
	}
	if (asd != fdescs[2].fd) {
		if (dup2(asd, fdescs[2].fd) < 0) {
			int e = errno;
			close(asd);
			close_fdesc(csd, NULL);
			close_unix_socket(ssd, fdescs[0].data);
			errno = e;
			err(EXIT_FAILURE, "failed to dup %d -> %d", asd, fdescs[2].fd);
		}
		close(asd);
		asd = fdescs[2].fd;
	}

	if (iserver_shutdown & (1 << 0))
		shutdown(asd, SHUT_RD);
	if (iserver_shutdown & (1 << 1))
		shutdown(asd, SHUT_WR);
	if (iclient_shutdown & (1 << 0))
		shutdown(csd, SHUT_RD);
	if (iclient_shutdown & (1 << 1))
		shutdown(csd, SHUT_WR);

	return NULL;
}

static void *make_unix_stream(const struct factory *factory, struct fdesc fdescs[],
			      int argc, char ** argv)
{
	struct arg type = decode_arg("type", factory->params, argc, argv);
	const char *stype = ARG_STRING(type);

	int typesym;
	const char *typestr;

	if (strcmp(stype, "stream") == 0) {
		typesym = SOCK_STREAM;
		typestr = "STREAM";
	} else if (strcmp(stype, "seqpacket") == 0) {
		typesym = SOCK_SEQPACKET;
		typestr = "SEQPACKET";
	} else
		errx(EXIT_FAILURE, "unknown unix socket type: %s", stype);

	free_arg(&type);

	return make_unix_stream_core(factory, fdescs, argc, argv, typesym, typestr);
}

static void *make_unix_dgram(const struct factory *factory, struct fdesc fdescs[],
			     int argc, char ** argv)
{
	struct arg path = decode_arg("path", factory->params, argc, argv);
	const char *spath = ARG_STRING(path);

	struct arg abstract = decode_arg("abstract", factory->params, argc, argv);
	bool babstract = ARG_BOOLEAN(abstract);

	int ssd, csd;	/* server and client socket descriptors */

	struct sockaddr_un un;
	size_t un_len = sizeof(un);

	memset(&un, 0, sizeof(un));
	un.sun_family = AF_UNIX;
	if (babstract) {
		strncpy(un.sun_path + 1, spath, sizeof(un.sun_path) - 1 - 1);
		size_t pathlen = strlen(spath);
		if (sizeof(un.sun_path) - 1 > pathlen)
			un_len = sizeof(un) - sizeof(un.sun_path) + 1 + pathlen;
	} else
		strncpy(un.sun_path,     spath, sizeof(un.sun_path) - 1    );

	free_arg(&abstract);
	free_arg(&path);

	ssd = socket(AF_UNIX, SOCK_DGRAM, 0);
	if (ssd < 0)
		err(EXIT_FAILURE,
		    "failed to make a socket with AF_UNIX + SOCK_DGRAM (server side)");
	if (ssd != fdescs[0].fd) {
		if (dup2(ssd, fdescs[0].fd) < 0) {
			int e = errno;
			close(ssd);
			errno = e;
			err(EXIT_FAILURE, "failed to dup %d -> %d", ssd, fdescs[0].fd);
		}
		close(ssd);
		ssd = fdescs[0].fd;
	}

	fdescs[0] = (struct fdesc){
		.fd    = fdescs[0].fd,
		.close = close_unix_socket,
		.data  = NULL,
	};

	if (!babstract)
		unlink(un.sun_path);
	if (bind(ssd, (const struct sockaddr *)&un, un_len) < 0) {
		int e = errno;
		close(ssd);
		errno = e;
		err(EXIT_FAILURE, "failed to bind a socket for server");
	}

	if (!babstract)
		fdescs[0].data = xstrdup(un.sun_path);
	csd = socket(AF_UNIX, SOCK_DGRAM, 0);
	if (csd < 0)
		err(EXIT_FAILURE,
		    "failed to make a socket with AF_UNIX + SOCK_DGRAM (client side)");
	if (csd != fdescs[1].fd) {
		if (dup2(csd, fdescs[1].fd) < 0) {
			int e = errno;
			close(csd);
			close_unix_socket(ssd, fdescs[0].data);
			errno = e;
			err(EXIT_FAILURE, "failed to dup %d -> %d", csd, fdescs[1].fd);
		}
		close(csd);
		csd = fdescs[1].fd;
	}

	fdescs[1] = (struct fdesc){
		.fd    = fdescs[1].fd,
		.close = close_fdesc,
		.data  = NULL,
	};

	if (connect(csd, (const struct sockaddr *)&un, un_len) < 0) {
		int e = errno;
		close_fdesc(csd, NULL);
		close_unix_socket(ssd, fdescs[0].data);
		errno = e;
		err(EXIT_FAILURE, "failed to connect a socket to the server socket");
	}

	if (!babstract)
		unlink(un.sun_path);

	return NULL;
}

static void *make_unix_in_new_netns(const struct factory *factory, struct fdesc fdescs[],
				    int argc, char ** argv)
{
	struct arg type = decode_arg("type", factory->params, argc, argv);
	const char *stype = ARG_STRING(type);

	struct arg path = decode_arg("path", factory->params, argc, argv);
	const char *spath = ARG_STRING(path);

	struct arg abstract = decode_arg("abstract", factory->params, argc, argv);
	bool babstract = ARG_BOOLEAN(abstract);

	int typesym;
	const char *typestr;

	struct sockaddr_un un;
	size_t un_len = sizeof(un);

	int self_netns, tmp_netns, sd;

	if (strcmp(stype, "stream") == 0) {
		typesym = SOCK_STREAM;
		typestr = "STREAM";
	} else if (strcmp(stype, "seqpacket") == 0) {
		typesym = SOCK_SEQPACKET;
		typestr = "SEQPACKET";
	} else if (strcmp(stype, "dgram") == 0) {
		typesym = SOCK_DGRAM;
		typestr = "DGRAM";
	} else {
		free_arg(&abstract);
		free_arg(&path);
		free_arg(&type);
		errx(EXIT_FAILURE, "unknown unix socket type: %s", stype);
	}

	memset(&un, 0, sizeof(un));
	un.sun_family = AF_UNIX;
	if (babstract) {
		strncpy(un.sun_path + 1, spath, sizeof(un.sun_path) - 1 - 1);
		size_t pathlen = strlen(spath);
		if (sizeof(un.sun_path) - 1 > pathlen)
			un_len = sizeof(un) - sizeof(un.sun_path) + 1 + pathlen;
	} else
		strncpy(un.sun_path,     spath, sizeof(un.sun_path) - 1    );

	free_arg(&abstract);
	free_arg(&path);
	free_arg(&type);

	self_netns = open("/proc/self/ns/net", O_RDONLY);
	if (self_netns < 0)
		err(EXIT_FAILURE, "failed to open /proc/self/ns/net");
	if (self_netns != fdescs[0].fd) {
		if (dup2(self_netns, fdescs[0].fd) < 0) {
			int e = errno;
			close(self_netns);
			errno = e;
			err(EXIT_FAILURE, "failed to dup %d -> %d", self_netns, fdescs[0].fd);
		}
		close(self_netns);
		self_netns = fdescs[0].fd;
	}

	fdescs[0] = (struct fdesc){
		.fd    = fdescs[0].fd,
		.close = close_fdesc,
		.data  = NULL,
	};

	if (unshare(CLONE_NEWNET) < 0) {
		int e = errno;
		close_fdesc(self_netns, NULL);
		errno = e;
		err((errno == EPERM? EXIT_EPERM: EXIT_FAILURE),
		    "failed in unshare");
	}

	tmp_netns = open("/proc/self/ns/net", O_RDONLY);
	if (tmp_netns < 0) {
		int e = errno;
		close_fdesc(self_netns, NULL);
		errno = e;
		err(EXIT_FAILURE, "failed to open /proc/self/ns/net for the new netns");
	}
	if (tmp_netns != fdescs[1].fd) {
		if (dup2(tmp_netns, fdescs[1].fd) < 0) {
			int e = errno;
			close_fdesc(self_netns, NULL);
			close(tmp_netns);
			errno = e;
			err(EXIT_FAILURE, "failed to dup %d -> %d", tmp_netns, fdescs[1].fd);
		}
		close(tmp_netns);
		tmp_netns = fdescs[1].fd;
	}

	fdescs[1] = (struct fdesc){
		.fd    = fdescs[1].fd,
		.close = close_fdesc,
		.data  = NULL,
	};

	sd = socket(AF_UNIX, typesym, 0);
	if (sd < 0) {
		int e = errno;
		close_fdesc(self_netns, NULL);
		close_fdesc(tmp_netns, NULL);
		errno = e;
		err(EXIT_FAILURE,
		    "failed to make a socket with AF_UNIX + SOCK_%s",
		    typestr);
	}

	if (sd != fdescs[2].fd) {
		if (dup2(sd, fdescs[2].fd) < 0) {
			int e = errno;
			close_fdesc(self_netns, NULL);
			close_fdesc(tmp_netns, NULL);
			close(sd);
			errno = e;
			err(EXIT_FAILURE, "failed to dup %d -> %d", sd, fdescs[2].fd);
		}
		close(sd);
		sd = fdescs[2].fd;
	}

	fdescs[2] = (struct fdesc){
		.fd    = fdescs[2].fd,
		.close = close_unix_socket,
		.data  = NULL,
	};

	if (!babstract)
		unlink(un.sun_path);
	if (bind(sd, (const struct sockaddr *)&un, un_len) < 0) {
		int e = errno;
		close_fdesc(self_netns, NULL);
		close_fdesc(tmp_netns, NULL);
		close_unix_socket(sd, NULL);
		errno = e;
		err(EXIT_FAILURE, "failed to bind a socket");
	}

	if (!babstract)
		fdescs[2].data = xstrdup(un.sun_path);

	if (typesym != SOCK_DGRAM) {
		if (listen(sd, 1) < 0) {
			int e = errno;
			close_fdesc(self_netns, NULL);
			close_fdesc(tmp_netns, NULL);
			close_unix_socket(sd, fdescs[2].data);
			errno = e;
			err(EXIT_FAILURE, "failed to listen a socket");
		}
	}

	if (setns(self_netns, CLONE_NEWNET) < 0) {
		int e = errno;
		close_fdesc(self_netns, NULL);
		close_fdesc(tmp_netns, NULL);
		close_unix_socket(sd, fdescs[2].data);
		errno = e;
		err(EXIT_FAILURE, "failed to swich back to the original net namespace");
	}

	return NULL;
}

static void *make_tcp_common(const struct factory *factory, struct fdesc fdescs[],
			     int argc, char ** argv,
			     int family,
			     void (*init_addr)(struct sockaddr *, unsigned short),
			     size_t addr_size,
			     struct sockaddr * sin, struct sockaddr * cin)
{
	struct arg server_port = decode_arg("server-port", factory->params, argc, argv);
	unsigned short iserver_port = (unsigned short)ARG_INTEGER(server_port);
	struct arg client_port = decode_arg("client-port", factory->params, argc, argv);
	unsigned short iclient_port = (unsigned short)ARG_INTEGER(client_port);

	int ssd, csd, asd;

	const int y = 1;

	free_arg(&server_port);
	free_arg(&client_port);

	ssd = socket(family, SOCK_STREAM, 0);
	if (ssd < 0)
		err(EXIT_FAILURE,
		    "failed to make a tcp socket for listening");

	if (setsockopt(ssd, SOL_SOCKET,
		       SO_REUSEADDR, (const char *)&y, sizeof(y)) < 0) {
		int e = errno;
		close(ssd);
		errno = e;
		err(EXIT_FAILURE, "failed to setsockopt(SO_REUSEADDR)");
	}

	if (ssd != fdescs[0].fd) {
		if (dup2(ssd, fdescs[0].fd) < 0) {
			int e = errno;
			close(ssd);
			errno = e;
			err(EXIT_FAILURE, "failed to dup %d -> %d", ssd, fdescs[0].fd);
		}
		close(ssd);
		ssd = fdescs[0].fd;
	}

	init_addr(sin, iserver_port);
	if (bind(ssd, sin, addr_size) < 0) {
		int e = errno;
		close(ssd);
		errno = e;
		err(EXIT_FAILURE, "failed to bind a listening socket");
	}

	if (listen(ssd, 1) < 0) {
		int e = errno;
		close(ssd);
		errno = e;
		err(EXIT_FAILURE, "failed to listen a socket");
	}

	csd = socket(family, SOCK_STREAM, 0);
	if (csd < 0) {
		int e = errno;
		close(ssd);
		errno = e;
		err(EXIT_FAILURE,
		    "failed to make a tcp client socket");
	}

	if (setsockopt(csd, SOL_SOCKET,
		       SO_REUSEADDR, (const char *)&y, sizeof(y)) < 0) {
		int e = errno;
		close(ssd);
		close(csd);
		errno = e;
		err(EXIT_FAILURE, "failed to setsockopt(SO_REUSEADDR)");
	}

	if (csd != fdescs[1].fd) {
		if (dup2(csd, fdescs[1].fd) < 0) {
			int e = errno;
			close(ssd);
			close(csd);
			errno = e;
			err(EXIT_FAILURE, "failed to dup %d -> %d", csd, fdescs[1].fd);
		}
		close(csd);
		csd = fdescs[1].fd;
	}

	init_addr(cin, iclient_port);
	if (bind(csd, cin, addr_size) < 0) {
		int e = errno;
		close(ssd);
		close(csd);
		errno = e;
		err(EXIT_FAILURE, "failed to bind a client socket");
	}

	if (connect(csd, sin, addr_size) < 0) {
		int e = errno;
		close(ssd);
		close(csd);
		errno = e;
		err(EXIT_FAILURE, "failed to connect a client socket to the server socket");
	}

	asd = accept(ssd, NULL, NULL);
	if (asd < 0) {
		int e = errno;
		close(ssd);
		close(csd);
		errno = e;
		err(EXIT_FAILURE, "failed to accept a socket from the listening socket");
	}
	if (asd != fdescs[2].fd) {
		if (dup2(asd, fdescs[2].fd) < 0) {
			int e = errno;
			close(ssd);
			close(csd);
			errno = e;
			err(EXIT_FAILURE, "failed to dup %d -> %d", asd, fdescs[2].fd);
		}
		close(asd);
		asd = fdescs[2].fd;
	}

	fdescs[0] = (struct fdesc) {
		.fd    = fdescs[0].fd,
		.close = close_fdesc,
		.data  = NULL,
	};
	fdescs[1] = (struct fdesc) {
		.fd    = fdescs[1].fd,
		.close = close_fdesc,
		.data  = NULL,
	};
	fdescs[2] = (struct fdesc) {
		.fd    = fdescs[2].fd,
		.close = close_fdesc,
		.data  = NULL,
	};

	return NULL;
}

static void tcp_init_addr(struct sockaddr *addr, unsigned short port)
{
	struct sockaddr_in *in = (struct sockaddr_in *)addr;
	memset(in, 0, sizeof(*in));
	in->sin_family = AF_INET;
	in->sin_port = htons(port);
	in->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
}

static void *make_tcp(const struct factory *factory, struct fdesc fdescs[],
				    int argc, char ** argv)
{
	struct sockaddr_in sin, cin;
	return make_tcp_common(factory, fdescs, argc, argv,
			       AF_INET,
			       tcp_init_addr, sizeof(sin),
			       (struct sockaddr *)&sin, (struct sockaddr *)&cin);
}

static void *make_udp_common(const struct factory *factory, struct fdesc fdescs[],
			     int argc, char ** argv,
			     int family,
			     void (*init_addr)(struct sockaddr *, unsigned short),
			     size_t addr_size,
			     struct sockaddr * sin, struct sockaddr * cin)
{
	struct arg lite = decode_arg("lite", factory->params, argc, argv);
	bool blite = ARG_BOOLEAN(lite);

	struct arg server_port = decode_arg("server-port", factory->params, argc, argv);
	unsigned short iserver_port = (unsigned short)ARG_INTEGER(server_port);
	struct arg client_port = decode_arg("client-port", factory->params, argc, argv);
	unsigned short iclient_port = (unsigned short)ARG_INTEGER(client_port);

	struct arg server_do_bind = decode_arg("server-do-bind", factory->params, argc, argv);
	bool bserver_do_bind = ARG_BOOLEAN(server_do_bind);
	struct arg client_do_bind = decode_arg("client-do-bind", factory->params, argc, argv);
	bool bclient_do_bind = ARG_BOOLEAN(client_do_bind);
	struct arg client_do_connect = decode_arg("client-do-connect", factory->params, argc, argv);
	bool bclient_do_connect = ARG_BOOLEAN(client_do_connect);

	int ssd, csd;

	const int y = 1;

	free_arg(&client_do_connect);
	free_arg(&client_do_bind);
	free_arg(&server_do_bind);
	free_arg(&server_port);
	free_arg(&client_port);
	free_arg(&lite);

	ssd = socket(family, SOCK_DGRAM, blite? IPPROTO_UDPLITE: 0);
	if (ssd < 0)
		err(EXIT_FAILURE,
		    "failed to make a udp socket for server");

	if (setsockopt(ssd, SOL_SOCKET,
		       SO_REUSEADDR, (const char *)&y, sizeof(y)) < 0) {
		int e = errno;
		close(ssd);
		errno = e;
		err(EXIT_FAILURE, "failed to setsockopt(SO_REUSEADDR)");
	}

	if (ssd != fdescs[0].fd) {
		if (dup2(ssd, fdescs[0].fd) < 0) {
			int e = errno;
			close(ssd);
			errno = e;
			err(EXIT_FAILURE, "failed to dup %d -> %d", ssd, fdescs[0].fd);
		}
		close(ssd);
		ssd = fdescs[0].fd;
	}

	init_addr(sin, iserver_port);
	if (bserver_do_bind) {
		if (bind(ssd, sin, addr_size) < 0) {
			int e = errno;
			close(ssd);
			errno = e;
			err(EXIT_FAILURE, "failed to bind a server socket");
		}
	}

	csd = socket(family, SOCK_DGRAM, blite? IPPROTO_UDPLITE: 0);
	if (csd < 0) {
		int e = errno;
		close(ssd);
		errno = e;
		err(EXIT_FAILURE,
		    "failed to make a udp client socket");
	}

	if (setsockopt(csd, SOL_SOCKET,
		       SO_REUSEADDR, (const char *)&y, sizeof(y)) < 0) {
		int e = errno;
		close(ssd);
		close(csd);
		errno = e;
		err(EXIT_FAILURE, "failed to setsockopt(SO_REUSEADDR)");
	}

	if (csd != fdescs[1].fd) {
		if (dup2(csd, fdescs[1].fd) < 0) {
			int e = errno;
			close(ssd);
			close(csd);
			errno = e;
			err(EXIT_FAILURE, "failed to dup %d -> %d", csd, fdescs[1].fd);
		}
		close(csd);
		csd = fdescs[1].fd;
	}

	if (bclient_do_bind) {
		init_addr(cin, iclient_port);
		if (bind(csd, cin, addr_size) < 0) {
			int e = errno;
			close(ssd);
			close(csd);
			errno = e;
			err(EXIT_FAILURE, "failed to bind a client socket");
		}
	}

	if (bclient_do_connect) {
		if (connect(csd, sin, addr_size) < 0) {
			int e = errno;
			close(ssd);
			close(csd);
			errno = e;
			err(EXIT_FAILURE, "failed to connect a client socket to the server socket");
		}
	}

	fdescs[0] = (struct fdesc) {
		.fd    = fdescs[0].fd,
		.close = close_fdesc,
		.data  = NULL,
	};
	fdescs[1] = (struct fdesc) {
		.fd    = fdescs[1].fd,
		.close = close_fdesc,
		.data  = NULL,
	};

	return NULL;
}

static void *make_udp(const struct factory *factory, struct fdesc fdescs[],
				    int argc, char ** argv)
{
	struct sockaddr_in sin, cin;
	return make_udp_common(factory, fdescs, argc, argv,
			       AF_INET,
			       tcp_init_addr, sizeof(sin),
			       (struct sockaddr *)&sin, (struct sockaddr *)&cin);
}

static void *make_raw_common(const struct factory *factory, struct fdesc fdescs[],
			     int argc, char ** argv,
			     int family,
			     void (*init_addr)(struct sockaddr *, bool),
			     size_t addr_size,
			     struct sockaddr * sin)
{
	struct arg protocol = decode_arg("protocol", factory->params, argc, argv);
	int iprotocol = ARG_INTEGER(protocol);
	int ssd;

	free_arg(&protocol);

	ssd = socket(family, SOCK_RAW, iprotocol);
	if (ssd < 0)
		err(EXIT_FAILURE,
		    "failed to make a udp socket for server");

	if (ssd != fdescs[0].fd) {
		if (dup2(ssd, fdescs[0].fd) < 0) {
			int e = errno;
			close(ssd);
			errno = e;
			err(EXIT_FAILURE, "failed to dup %d -> %d", ssd, fdescs[0].fd);
		}
		close(ssd);
		ssd = fdescs[0].fd;
	}

	init_addr(sin, false);
	if (bind(ssd, sin, addr_size) < 0) {
		int e = errno;
		close(ssd);
		errno = e;
		err(EXIT_FAILURE, "failed in bind(2)");
	}

	init_addr(sin, true);
	if (connect(ssd, sin, addr_size) < 0) {
		int e = errno;
		close(ssd);
		errno = e;
		err(EXIT_FAILURE, "failed in connect(2)");
	}

	fdescs[0] = (struct fdesc) {
		.fd    = fdescs[0].fd,
		.close = close_fdesc,
		.data  = NULL,
	};

	return NULL;
}

static void raw_init_addr(struct sockaddr * addr, bool remote_addr)
{
	struct sockaddr_in *in = (struct sockaddr_in *)addr;
	memset(in, 0, sizeof(*in));
	in->sin_family = AF_INET;
	in->sin_addr.s_addr = htonl(INADDR_LOOPBACK + (remote_addr? 1: 0));
}

static void *make_raw(const struct factory *factory, struct fdesc fdescs[],
				    int argc, char ** argv)
{
	struct sockaddr_in sin;
	return make_raw_common(factory, fdescs, argc, argv,
			       AF_INET,
			       raw_init_addr, sizeof(sin),
			       (struct sockaddr *)&sin);
}

static void *make_ping_common(const struct factory *factory, struct fdesc fdescs[],
			      int argc, char ** argv,
			      int family, int protocol,
			      void (*init_addr)(struct sockaddr *, unsigned short),
			      size_t addr_size,
			      struct sockaddr *sin)
{
	struct arg connect_ = decode_arg("connect", factory->params, argc, argv);
	bool bconnect = ARG_BOOLEAN(connect_);

	struct arg bind_ = decode_arg("bind", factory->params, argc, argv);
	bool bbind = ARG_BOOLEAN(bind_);

	struct arg id = decode_arg("id", factory->params, argc, argv);
	unsigned short iid = (unsigned short)ARG_INTEGER(id);

	int sd;

	free_arg(&id);
	free_arg(&bind_);
	free_arg(&connect_);

	sd = socket(family, SOCK_DGRAM, protocol);
	if (sd < 0)
		err((errno == EACCES? EXIT_EACCESS: EXIT_FAILURE),
		    "failed to make an icmp socket");

	if (sd != fdescs[0].fd) {
		if (dup2(sd, fdescs[0].fd) < 0) {
			int e = errno;
			close(sd);
			errno = e;
			err(EXIT_FAILURE, "failed to dup %d -> %d", sd, fdescs[0].fd);
		}
		close(sd);
		sd = fdescs[0].fd;
	}

	if (bbind) {
		init_addr(sin, iid);
		if (bind(sd, sin, addr_size) < 0) {
			int e = errno;
			close(sd);
			errno = e;
			err((errno == EACCES? EXIT_EACCESS: EXIT_FAILURE),
			    "failed in bind(2)");
		}
	}

	if (bconnect) {
		init_addr(sin, 0);
		if (connect(sd, sin, addr_size) < 0) {
			int e = errno;
			close(sd);
			errno = e;
			err(EXIT_FAILURE, "failed in connect(2)");
		}
	}

	fdescs[0] = (struct fdesc) {
		.fd    = fdescs[0].fd,
		.close = close_fdesc,
		.data  = NULL,
	};

	return NULL;
}

static void ping_init_addr(struct sockaddr *addr, unsigned short id)
{
	struct sockaddr_in *in = (struct sockaddr_in *)addr;
	memset(in, 0, sizeof(*in));
	in->sin_family = AF_INET;
	in->sin_port = htons(id);
	in->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
}

static void *make_ping(const struct factory *factory, struct fdesc fdescs[],
		       int argc, char ** argv)
{
	struct sockaddr_in in;
	return make_ping_common(factory, fdescs, argc, argv,
				AF_INET, IPPROTO_ICMP,
				ping_init_addr,
				sizeof(in),
				(struct sockaddr *)&in);
}

static void tcp6_init_addr(struct sockaddr *addr, unsigned short port)
{
	struct sockaddr_in6 *in6 = (struct sockaddr_in6 *)addr;
	memset(in6, 0, sizeof(*in6));
	in6->sin6_family = AF_INET6;
	in6->sin6_flowinfo = 0;
	in6->sin6_port = htons(port);
	in6->sin6_addr = in6addr_loopback;
}

static void *make_tcp6(const struct factory *factory, struct fdesc fdescs[],
		       int argc, char ** argv)
{
	struct sockaddr_in6 sin, cin;
	return make_tcp_common(factory, fdescs, argc, argv,
			       AF_INET6,
			       tcp6_init_addr, sizeof(sin),
			       (struct sockaddr *)&sin, (struct sockaddr *)&cin);
}

static void *make_udp6(const struct factory *factory, struct fdesc fdescs[],
		       int argc, char ** argv)
{
	struct sockaddr_in6 sin, cin;
	return make_udp_common(factory, fdescs, argc, argv,
			       AF_INET6,
			       tcp6_init_addr, sizeof(sin),
			       (struct sockaddr *)&sin, (struct sockaddr *)&cin);
}

static void raw6_init_addr(struct sockaddr *addr, bool remote_addr)
{
	struct sockaddr_in6 *in6 = (struct sockaddr_in6 *)addr;
	memset(in6, 0, sizeof(*in6));
	in6->sin6_family = AF_INET6;
	in6->sin6_flowinfo = 0;

	if (remote_addr) {
		/* ::ffff:127.0.0.1 */
		in6->sin6_addr.s6_addr16[5] = 0xffff;
		in6->sin6_addr.s6_addr32[3] = htonl(INADDR_LOOPBACK);
	} else
		in6->sin6_addr = in6addr_loopback;
}

static void *make_raw6(const struct factory *factory, struct fdesc fdescs[],
		       int argc, char ** argv)
{
	struct sockaddr_in6 sin;
	return make_raw_common(factory, fdescs, argc, argv,
			       AF_INET6,
			       raw6_init_addr, sizeof(sin),
			       (struct sockaddr *)&sin);
}

static void ping6_init_addr(struct sockaddr *addr, unsigned short id)
{
	struct sockaddr_in6 *in6 = (struct sockaddr_in6 *)addr;
	memset(in6, 0, sizeof(*in6));
	in6->sin6_family = AF_INET6;
	in6->sin6_port = htons(id);
	in6->sin6_addr = in6addr_loopback;
}

static void *make_ping6(const struct factory *factory, struct fdesc fdescs[],
			int argc, char ** argv)
{
	struct sockaddr_in6 in6;
	return make_ping_common(factory, fdescs, argc, argv,
				AF_INET6, IPPROTO_ICMPV6,
				ping6_init_addr,
				sizeof(in6),
				(struct sockaddr *)&in6);
}

static void *make_netns(const struct factory *factory _U_, struct fdesc fdescs[],
			int argc _U_, char ** argv _U_)
{
	int sd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (sd < 0)
		err(EXIT_FAILURE, "failed in socket()");

	int ns = ioctl(sd, SIOCGSKNS);
	if (ns < 0)
		err((errno == ENOSYS? EXIT_ENOSYS: EXIT_FAILURE),
		    "failed in ioctl(SIOCGSKNS)");
	close(sd);

	if (ns != fdescs[0].fd) {
		if (dup2(ns, fdescs[0].fd) < 0) {
			int e = errno;
			close(ns);
			errno = e;
			err(EXIT_FAILURE, "failed to dup %d -> %d", ns, fdescs[0].fd);
		}
		close(ns);
	}

	fdescs[0] = (struct fdesc){
		.fd    = fdescs[0].fd,
		.close = close_fdesc,
		.data  = NULL
	};

	return NULL;
}

static void *make_netlink(const struct factory *factory, struct fdesc fdescs[],
			  int argc, char ** argv)
{
	struct arg protocol = decode_arg("protocol", factory->params, argc, argv);
	int iprotocol = ARG_INTEGER(protocol);
	struct arg groups = decode_arg("groups", factory->params, argc, argv);
	unsigned int ugroups = ARG_UINTEGER(groups);
	int sd;

	free_arg(&protocol);

	sd = socket(AF_NETLINK, SOCK_RAW, iprotocol);
	if (sd < 0)
		err((errno == EPROTONOSUPPORT)? EXIT_EPROTONOSUPPORT: EXIT_FAILURE,
		    "failed in socket()");

	if (sd != fdescs[0].fd) {
		if (dup2(sd, fdescs[0].fd) < 0) {
			int e = errno;
			close(sd);
			errno = e;
			err(EXIT_FAILURE, "failed to dup %d -> %d", sd, fdescs[0].fd);
		}
		close(sd);
	}

	struct sockaddr_nl nl;
	memset(&nl, 0, sizeof(nl));
	nl.nl_family = AF_NETLINK;
	nl.nl_groups = ugroups;
	if (bind(sd, (struct sockaddr*)&nl, sizeof(nl)) < 0) {
		int e = errno;
		close(sd);
		errno = e;
		err(EXIT_FAILURE, "failed in bind(2)");
	}

	fdescs[0] = (struct fdesc){
		.fd    = fdescs[0].fd,
		.close = close_fdesc,
		.data  = NULL
	};

	return NULL;
}

#define PARAM_END { .name = NULL, }
static const struct factory factories[] = {
	{
		.name = "ro-regular-file",
		.desc = "read-only regular file",
		.priv = false,
		.N    = 1,
		.EX_N = 0,
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
		.EX_N = 2,
		.make = make_pipe,
		.params = (struct parameter []) {
			{
				.name = "nonblock",
				.type = PTYPE_STRING,
				.desc = "set nonblock flag (\"--\", \"r-\", \"-w\", or \"rw\")",
				.defv.string = "--",
			},
			{
				.name = "rdup",
				.type = PTYPE_INTEGER,
				.desc = "file descriptor for duplicating the pipe input",
				.defv.integer = -1,
			},
			{
				.name = "wdup",
				.type = PTYPE_INTEGER,
				.desc = "file descriptor for duplicating the pipe output",
				.defv.integer = -1,
			},
			PARAM_END
		},
	},
	{
		.name = "directory",
		.desc = "directory",
		.priv = false,
		.N    = 1,
		.EX_N = 0,
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
		.EX_N = 0,
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
		.EX_N = 0,
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
		.EX_N = 0,
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
		.EX_N = 0,
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
	{
		.name = "mapped-packet-socket",
		.desc = "mmap'ed AF_PACKET socket",
		.priv = true,
		.N = 1,
		.EX_N = 0,
		.make = make_mmapped_packet_socket,
		.params = (struct parameter []) {
			{
				.name = "socktype",
				.type = PTYPE_STRING,
				.desc = "DGRAM or RAW",
				.defv.string = "RAW",
			},
			{
				.name = "interface",
				.type = PTYPE_STRING,
				.desc = "a name of network interface like eth0 or lo",
				.defv.string = "lo",
			},
			PARAM_END
		},
	},
	{
		.name = "pidfd",
		.desc = "pidfd returned from pidfd_open(2)",
		.priv = false,
		.N    = 1,
		.EX_N = 0,
		.make = make_pidfd,
		.params = (struct parameter []) {
			{
				.name = "target-pid",
				.type = PTYPE_INTEGER,
				.desc = "the pid of the target process",
				.defv.integer = 1,
			},
			PARAM_END
		},
	},
	{
		.name = "inotify",
		.desc = "inotify fd returned from inotify_init(2)",
		.priv = false,
		.N    = 1,
		.EX_N = 0,
		.make = make_inotify_fd,
		.params = (struct parameter []) {
			PARAM_END
		},
	},
	{
		.name = "unix-stream",
		.desc = "AF_UNIX+SOCK_STREAM sockets",
		.priv = false,
		.N    = 3,
		.EX_N = 0,
		.make = make_unix_stream,
		.params = (struct parameter []) {
			{
				.name = "path",
				.type = PTYPE_STRING,
				.desc = "path for listening-socket bound to",
				.defv.string = "/tmp/test_mkfds-unix-stream",
			},
			{
				.name = "backlog",
				.type = PTYPE_INTEGER,
				.desc = "backlog passed to listen(2)",
				.defv.integer = 5,
			},
			{
				.name = "abstract",
				.type = PTYPE_BOOLEAN,
				.desc = "use PATH as an abstract socket address",
				.defv.boolean = false,
			},
			{
				.name = "server-shutdown",
				.type = PTYPE_INTEGER,
				.desc = "shutdown the accepted socket; 1: R, 2: W, 3: RW",
				.defv.integer = 0,
			},
			{
				.name = "client-shutdown",
				.type = PTYPE_INTEGER,
				.desc = "shutdown the client socket; 1: R, 2: W, 3: RW",
				.defv.integer = 0,
			},
			{
				.name = "type",
				.type = PTYPE_STRING,
				.desc = "stream or seqpacket",
				.defv.string = "stream",
			},
			PARAM_END
		},
	},
	{
		.name = "unix-dgram",
		.desc = "AF_UNIX+SOCK_DGRAM sockets",
		.priv = false,
		.N    = 2,
		.EX_N = 0,
		.make = make_unix_dgram,
		.params = (struct parameter []) {
			{
				.name = "path",
				.type = PTYPE_STRING,
				.desc = "path for unix non-stream bound to",
				.defv.string = "/tmp/test_mkfds-unix-dgram",
			},
			{
				.name = "abstract",
				.type = PTYPE_BOOLEAN,
				.desc = "use PATH as an abstract socket address",
				.defv.boolean = false,
			},
			PARAM_END
		},
	},
	{
		.name = "unix-in-netns",
		.desc = "make a unix socket in a new network namespace",
		.priv = true,
		.N    = 3,
		.EX_N = 0,
		.make = make_unix_in_new_netns,
		.params = (struct parameter []) {
			{
				.name = "type",
				.type = PTYPE_STRING,
				.desc = "dgram, stream, or seqpacket",
				.defv.string = "stream",
			},
			{
				.name = "path",
				.type = PTYPE_STRING,
				.desc = "path for unix non-stream bound to",
				.defv.string = "/tmp/test_mkfds-unix-in-netns",
			},
			{
				.name = "abstract",
				.type = PTYPE_BOOLEAN,
				.desc = "use PATH as an abstract socket address",
				.defv.boolean = false,
			},
			PARAM_END
		},
	},
	{
		.name = "tcp",
		.desc = "AF_INET+SOCK_STREAM sockets",
		.priv = false,
		.N    = 3,
		.EX_N = 0,
		.make = make_tcp,
		.params = (struct parameter []) {
			{
				.name = "server-port",
				.type = PTYPE_INTEGER,
				.desc = "TCP port the server may listen",
				.defv.integer = 12345,
			},
			{
				.name = "client-port",
				.type = PTYPE_INTEGER,
				.desc = "TCP port the client may bind",
				.defv.integer = 23456,
			},
			PARAM_END
		}
	},
	{
		.name = "udp",
		.desc = "AF_INET+SOCK_DGRAM sockets",
		.priv = false,
		.N    = 2,
		.EX_N = 0,
		.make = make_udp,
		.params = (struct parameter []) {
			{
				.name = "lite",
				.type = PTYPE_BOOLEAN,
				.desc = "Use UDPLITE instead of UDP",
				.defv.boolean = false,
			},
			{
				.name = "server-port",
				.type = PTYPE_INTEGER,
				.desc = "UDP port the server may listen",
				.defv.integer = 12345,
			},
			{
				.name = "client-port",
				.type = PTYPE_INTEGER,
				.desc = "UDP port the client may bind",
				.defv.integer = 23456,
			},
			{
				.name = "server-do-bind",
				.type = PTYPE_BOOLEAN,
				.desc = "call bind with the server socket",
				.defv.boolean = true,
			},
			{
				.name = "client-do-bind",
				.type = PTYPE_BOOLEAN,
				.desc = "call bind with the client socket",
				.defv.boolean = true,
			},
			{
				.name = "client-do-connect",
				.type = PTYPE_BOOLEAN,
				.desc = "call connect with the client socket",
				.defv.boolean = true,
			},
			PARAM_END
		}
	},
	{
		.name = "raw",
		.desc = "AF_INET+SOCK_RAW sockets",
		.priv = true,
		.N    = 1,
		.EX_N = 0,
		.make = make_raw,
		.params = (struct parameter []) {
			{
				.name = "protocol",
				.type = PTYPE_INTEGER,
				.desc = "protocol passed to socket(AF_INET, SOCK_RAW, protocol)",
				.defv.integer = IPPROTO_IPIP,
			},
			PARAM_END
		}

	},
	{
		.name = "ping",
		.desc = "AF_INET+SOCK_DGRAM+IPPROTO_ICMP sockets",
		.priv = false,
		.N    = 1,
		.EX_N = 0,
		.make = make_ping,
		.params = (struct parameter []) {
			{
				.name = "connect",
				.type = PTYPE_BOOLEAN,
				.desc = "call connect(2) with the socket",
				.defv.boolean = true,
			},
			{
				.name = "bind",
				.type = PTYPE_BOOLEAN,
				.desc = "call bind(2) with the socket",
				.defv.boolean = true,
			},
			{
				.name = "id",
				.type = PTYPE_INTEGER,
				.desc = "ICMP echo request id",
				.defv.integer = 0,
			},
			PARAM_END
		}
	},
	{
		.name = "tcp6",
		.desc = "AF_INET6+SOCK_STREAM sockets",
		.priv = false,
		.N    = 3,
		.EX_N = 0,
		.make = make_tcp6,
		.params = (struct parameter []) {
			{
				.name = "server-port",
				.type = PTYPE_INTEGER,
				.desc = "TCP port the server may listen",
				.defv.integer = 12345,
			},
			{
				.name = "client-port",
				.type = PTYPE_INTEGER,
				.desc = "TCP port the client may bind",
				.defv.integer = 23456,
			},
			PARAM_END
		}
	},
	{
		.name = "udp6",
		.desc = "AF_INET6+SOCK_DGRAM sockets",
		.priv = false,
		.N    = 2,
		.EX_N = 0,
		.make = make_udp6,
		.params = (struct parameter []) {
			{
				.name = "lite",
				.type = PTYPE_BOOLEAN,
				.desc = "Use UDPLITE instead of UDP",
				.defv.boolean = false,
			},
			{
				.name = "server-port",
				.type = PTYPE_INTEGER,
				.desc = "UDP port the server may listen",
				.defv.integer = 12345,
			},
			{
				.name = "client-port",
				.type = PTYPE_INTEGER,
				.desc = "UDP port the client may bind",
				.defv.integer = 23456,
			},
			{
				.name = "server-do-bind",
				.type = PTYPE_BOOLEAN,
				.desc = "call bind with the server socket",
				.defv.boolean = true,
			},
			{
				.name = "client-do-bind",
				.type = PTYPE_BOOLEAN,
				.desc = "call bind with the client socket",
				.defv.boolean = true,
			},
			{
				.name = "client-do-connect",
				.type = PTYPE_BOOLEAN,
				.desc = "call connect with the client socket",
				.defv.boolean = true,
			},
			PARAM_END
		}
	},
	{
		.name = "raw6",
		.desc = "AF_INET6+SOCK_RAW sockets",
		.priv = true,
		.N    = 1,
		.EX_N = 0,
		.make = make_raw6,
		.params = (struct parameter []) {
			{
				.name = "protocol",
				.type = PTYPE_INTEGER,
				.desc = "protocol passed to socket(AF_INET6, SOCK_RAW, protocol)",
				.defv.integer = IPPROTO_IPIP,
			},
			PARAM_END
		}

	},
	{
		.name = "ping6",
		.desc = "AF_INET6+SOCK_DGRAM+IPPROTO_ICMPV6 sockets",
		.priv = false,
		.N    = 1,
		.EX_N = 0,
		.make = make_ping6,
		.params = (struct parameter []) {
			{
				.name = "connect",
				.type = PTYPE_BOOLEAN,
				.desc = "call connect(2) with the socket",
				.defv.boolean = true,
			},
			{
				.name = "bind",
				.type = PTYPE_BOOLEAN,
				.desc = "call bind(2) with the socket",
				.defv.boolean = true,
			},
			{
				.name = "id",
				.type = PTYPE_INTEGER,
				.desc = "ICMP echo request id",
				.defv.integer = 0,
			},
			PARAM_END
		}
	},
	{
		.name = "netns",
		.desc = "open a file specifying a netns",
		.priv = true,
		.N    = 1,
		.EX_N = 0,
		.make = make_netns,
		.params = (struct parameter []) {
			PARAM_END
		}
	},
	{
		.name = "netlink",
		.desc = "AF_NETLINK sockets",
		.priv = false,
		.N    = 1,
		.EX_N = 0,
		.make = make_netlink,
		.params = (struct parameter []) {
			{
				.name = "protocol",
				.type = PTYPE_INTEGER,
				.desc = "protocol passed to socket(AF_NETLINK, SOCK_RAW, protocol)",
				.defv.integer = NETLINK_USERSOCK,
			},
			{
				.name = "groups",
				.type = PTYPE_UINTEGER,
				.desc = "multicast groups of netlink communication (requires CAP_NET_ADMIN)",
				.defv.uinteger = 0,
			},
			PARAM_END
		}
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
	printf("%-20s %4s %5d %6d %s\n",
	       factory->name,
	       factory->priv? "yes": "no",
	       factory->N,
	       count_parameters(factory),
	       factory->desc);
}

static void list_factories(void)
{
	printf("%-20s PRIV COUNT NPARAM DESCRIPTION\n", "FACTORY");
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
		errx(EXIT_FAILURE, "no such factory: %s", factory_name);

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
		err(EXIT_FAILURE, "failed to rename self via prctl: %s", comm);
}

static void do_nothing(int signum _U_)
{
}

#ifdef __NR_pidfd_open

static int
pidfd_open(pid_t pid, unsigned int flags)
{
	return syscall(__NR_pidfd_open, pid, flags);
}
#else
static int
pidfd_open(pid_t pid _U_, unsigned int flags _U_)
{
	errno = ENOSYS;
	return -1;
}
#endif

static void wait_event(void)
{
	fd_set readfds;
	sigset_t sigset;
	int n = 0;

	FD_ZERO(&readfds);
	/* Monitor the standard input only when the process
	 * is in foreground. */
	if (tcgetpgrp(STDIN_FILENO) == getpgrp()) {
		n = 1;
		FD_SET(0, &readfds);
	}

	sigemptyset(&sigset);

	if (pselect(n, &readfds, NULL, NULL, NULL, &sigset) < 0
	    && errno != EINTR)
		errx(EXIT_FAILURE, "failed in pselect");
}

int main(int argc, char **argv)
{
	int c;
	const struct factory *factory;
	struct fdesc fdescs[MAX_N];
	bool quiet = false;
	bool cont  = false;
	void *data;

	static const struct option longopts[] = {
		{ "list",	no_argument, NULL, 'l' },
		{ "parameters", required_argument, NULL, 'I' },
		{ "comm",       required_argument, NULL, 'r' },
		{ "quiet",	no_argument, NULL, 'q' },
		{ "dont-puase", no_argument, NULL, 'c' },
		{ "help",	no_argument, NULL, 'h' },
		{ NULL, 0, NULL, 0 },
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
		errx(EXIT_FAILURE, "no file descriptor specification given");

	factory = find_factory(argv[optind]);
	if (!factory)
		errx(EXIT_FAILURE, "no such factory: %s", argv[optind]);
	assert(factory->N + factory->EX_N < MAX_N);
	optind++;

	if ((optind + factory->N) > argc)
		errx(EXIT_FAILURE, "not enough file descriptors given for %s",
		     factory->name);

	if (factory->priv && getuid() != 0)
		errx(EXIT_FAILURE, "%s factory requires root privilege", factory->name);

	for (int i = 0; i < MAX_N; i++) {
		fdescs[i].fd = -1;
		fdescs[i].close = NULL;
	}

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
			errx(EXIT_FAILURE, "garbage at the end of number: %s", str);
		if (fd < 0)
			errx(EXIT_FAILURE, "fd number should not be negative: %s", str);
		if (fd < 3)
			errx(EXIT_FAILURE, "fd 0, 1, 2 are reserved: %s", str);
		fdescs[i].fd = fd;
	}
	optind += factory->N;

	data = factory->make(factory, fdescs, argc - optind, argv + optind);

	signal(SIGCONT, do_nothing);

	if (!quiet) {
		printf("%d", getpid());
		putchar('\n');
		if (factory->report)
			factory->report(factory, data, stdout);
		fflush(stdout);
	}

	if (!cont)
		wait_event();

	for (int i = 0; i < factory->N + factory->EX_N; i++)
		if (fdescs[i].fd >= 0 && fdescs[i].close)
			fdescs[i].close(fdescs[i].fd, fdescs[i].data);

	if (factory->free)
		factory->free (factory, data);

	exit(EXIT_SUCCESS);
}
