/*
 * lsfd-sock-xinfo.c - read various information from files under /proc/net/
 *
 * Copyright (C) 2022 Red Hat, Inc. All rights reserved.
 * Written by Masatake YAMATO <yamato@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <arpa/inet.h>		/* inet_ntop */
#include <fcntl.h>		/* open(2) */
#include <inttypes.h>		/* SCNu16 */
#include <linux/net.h>		/* SS_* */
#include <linux/un.h>		/* UNIX_PATH_MAX */
#include <sched.h>		/* for setns(2) */
#include <search.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>		/* SOCK_* */

#include "xalloc.h"
#include "nls.h"
#include "libsmartcols.h"
#include "sysfs.h"
#include "bitops.h"

#include "lsfd.h"
#include "lsfd-sock.h"

static void load_xinfo_from_proc_unix(ino_t netns_inode);
static void load_xinfo_from_proc_raw(ino_t netns_inode);
static void load_xinfo_from_proc_tcp(ino_t netns_inode);
static void load_xinfo_from_proc_udp(ino_t netns_inode);

static int self_netns_fd = -1;
static struct stat self_netns_sb;

static void *xinfo_tree;	/* for tsearch/tfind */
static void *netns_tree;

static int netns_compare(const void *a, const void *b)
{
	if (*(ino_t *)a < *(ino_t *)b)
		return -1;
	else if (*(ino_t *)a > *(ino_t *)b)
		return 1;
	else
		return 0;
}

static bool is_sock_xinfo_loaded(ino_t netns)
{
	return tfind(&netns, &netns_tree, netns_compare)? true: false;
}

static void mark_sock_xinfo_loaded(ino_t ino)
{
	ino_t *netns = xmalloc(sizeof(ino));
	ino_t **tmp;

	*netns = ino;
	tmp = tsearch(netns, &netns_tree, netns_compare);
	if (tmp == NULL)
		errx(EXIT_FAILURE, _("failed to allocate memory"));
}

static void load_sock_xinfo_no_nsswitch(ino_t netns)
{
	load_xinfo_from_proc_unix(netns);
	load_xinfo_from_proc_tcp(netns);
	load_xinfo_from_proc_udp(netns);
	load_xinfo_from_proc_raw(netns);
}

static void load_sock_xinfo_with_fd(int fd, ino_t netns)
{
	if (setns(fd, CLONE_NEWNET) == 0) {
		load_sock_xinfo_no_nsswitch(netns);
		setns(self_netns_fd, CLONE_NEWNET);
	}
}

void load_sock_xinfo(struct path_cxt *pc, const char *name, ino_t netns)
{
	if (self_netns_fd == -1)
		return;

	if (!is_sock_xinfo_loaded(netns)) {
		int fd;

		mark_sock_xinfo_loaded(netns);
		fd = ul_path_open(pc, O_RDONLY, name);
		if (fd < 0)
			return;

		load_sock_xinfo_with_fd(fd, netns);
		close(fd);
	}
}

void initialize_sock_xinfos(void)
{
	struct path_cxt *pc;
	DIR *dir;
	struct dirent *d;

	self_netns_fd = open("/proc/self/ns/net", O_RDONLY);

	if (self_netns_fd < 0)
		load_sock_xinfo_no_nsswitch(0);
	else {
		if (fstat(self_netns_fd, &self_netns_sb) == 0) {
			mark_sock_xinfo_loaded(self_netns_sb.st_ino);
			load_sock_xinfo_no_nsswitch(self_netns_sb.st_ino);
		}
	}

	/* Load /proc/net/{unix,...} of the network namespace
	 * specified with netns files under /var/run/netns/.
	 *
	 * `ip netns' command pins a network namespace on
	 * /var/run/netns.
	 */
	pc = ul_new_path("/var/run/netns");
	if (!pc)
		err(EXIT_FAILURE, _("failed to alloc path context for /var/run/netns"));
	dir = ul_path_opendir(pc, NULL);
	if (dir == NULL) {
		ul_unref_path(pc);
		return;
	}
	while ((d = readdir(dir))) {
		struct stat sb;
		int fd;
		if (ul_path_stat(pc, &sb, 0, d->d_name) < 0)
			continue;
		if (is_sock_xinfo_loaded(sb.st_ino))
			continue;
		mark_sock_xinfo_loaded(sb.st_ino);
		fd = ul_path_open(pc, O_RDONLY, d->d_name);
		if (fd < 0)
			continue;
		load_sock_xinfo_with_fd(fd, sb.st_ino);
		close(fd);
	}
	closedir(dir);
	ul_unref_path(pc);
}

static void free_sock_xinfo(void *node)
{
	struct sock_xinfo *xinfo = node;
	if (xinfo->class->free)
		xinfo->class->free(xinfo);
	free(node);
}

void finalize_sock_xinfos(void)
{
	if (self_netns_fd != -1)
		close(self_netns_fd);
	tdestroy(netns_tree, free);
	tdestroy(xinfo_tree, free_sock_xinfo);
}

static int xinfo_compare(const void *a, const void *b)
{
	if (((struct sock_xinfo *)a)->inode < ((struct sock_xinfo *)b)->inode)
		return -1;
	if (((struct sock_xinfo *)a)->inode > ((struct sock_xinfo *)b)->inode)
		return 1;
	return 0;
}

static void add_sock_info(struct sock_xinfo *xinfo)
{
	struct sock_xinfo **tmp = tsearch(xinfo, &xinfo_tree, xinfo_compare);

	if (tmp == NULL)
		errx(EXIT_FAILURE, _("failed to allocate memory"));
}

struct sock_xinfo *get_sock_xinfo(ino_t netns_inode)
{
	struct sock_xinfo **xinfo = tfind(&netns_inode, &xinfo_tree, xinfo_compare);

	if (xinfo)
		return *xinfo;
	return NULL;
}

bool is_nsfs_dev(dev_t dev)
{
	return dev == self_netns_sb.st_dev;
}

static const char *sock_decode_type(uint16_t type)
{
	switch (type) {
	case SOCK_STREAM:
		return "stream";
	case SOCK_DGRAM:
		return "dgram";
	case SOCK_RAW:
		return "raw";
	case SOCK_RDM:
		return "rdm";
	case SOCK_SEQPACKET:
		return "seqpacket";
	case SOCK_DCCP:
		return "dccp";
	case SOCK_PACKET:
		return "packet";
	default:
		return "unknown";
	}
}

/*
 * Protocol specific code
 */

/*
 * UNIX
 */
struct unix_xinfo {
	struct sock_xinfo sock;
	int acceptcon;	/* flags */
	uint16_t type;
	uint8_t  st;
	char path[
		  UNIX_PATH_MAX
		  + 1		/* for @ */
		  + 1		/* \0? */
		  ];
};

static const char *unix_decode_state(uint8_t st)
{
	switch (st) {
	case SS_FREE:
		return "free";
	case SS_UNCONNECTED:
		return "unconnected";
	case SS_CONNECTING:
		return "connecting";
	case SS_CONNECTED:
		return "connected";
	case SS_DISCONNECTING:
		return "disconnecting";
	default:
		return "unknown";
	}
}

static char *unix_get_name(struct sock_xinfo *sock_xinfo,
			   struct sock *sock)
{
	struct unix_xinfo *ux = (struct unix_xinfo *)sock_xinfo;
	const char *state = unix_decode_state(ux->st);
	char *str = NULL;

	if (sock->protoname && (strcmp(sock->protoname, "UNIX-STREAM") == 0))
		xasprintf(&str, "state=%s%s%s",
			  (ux->acceptcon)? "listen": state,
			  *(ux->path)? " path=": "",
			  *(ux->path)? ux->path: "");
	else
		xasprintf(&str, "state=%s%s%s type=%s",
			  (ux->acceptcon)? "listen": state,
			  *(ux->path)? " path=": "",
			  *(ux->path)? ux->path: "",
			  sock_decode_type(ux->type));
	return str;
}

static char *unix_get_type(struct sock_xinfo *sock_xinfo,
			   struct sock *sock __attribute__((__unused__)))
{
	const char *str;
	struct unix_xinfo *ux = (struct unix_xinfo *)sock_xinfo;

	str = sock_decode_type(ux->type);
	return strdup(str);
}

static char *unix_get_state(struct sock_xinfo *sock_xinfo,
			    struct sock *sock __attribute__((__unused__)))
{
	const char *str;
	struct unix_xinfo *ux = (struct unix_xinfo *)sock_xinfo;

	if (ux->acceptcon)
		return strdup("listen");

	str = unix_decode_state(ux->st);
	return strdup(str);
}

static bool unix_get_listening(struct sock_xinfo *sock_xinfo,
			       struct sock *sock __attribute__((__unused__)))
{
	struct unix_xinfo *ux = (struct unix_xinfo *)sock_xinfo;

	return ux->acceptcon;
}

static bool unix_fill_column(struct proc *proc __attribute__((__unused__)),
			     struct sock_xinfo *sock_xinfo,
			     struct sock *sock __attribute__((__unused__)),
			     struct libscols_line *ln __attribute__((__unused__)),
			     int column_id,
			     size_t column_index __attribute__((__unused__)),
			     char **str)
{
	struct unix_xinfo *ux = (struct unix_xinfo *)sock_xinfo;

	switch (column_id) {
	case COL_UNIX_PATH:
		if (*ux->path) {
			*str = strdup(ux->path);
			return true;
		}
		break;
	}

	return false;
}

static const struct sock_xinfo_class unix_xinfo_class = {
	.get_name = unix_get_name,
	.get_type = unix_get_type,
	.get_state = unix_get_state,
	.get_listening = unix_get_listening,
	.fill_column = unix_fill_column,
	.free = NULL,
};

/* #define UNIX_LINE_LEN 54 + 21 + UNIX_LINE_LEN + 1 */
#define UNIX_LINE_LEN 256
static void load_xinfo_from_proc_unix(ino_t netns_inode)
{
	char line[UNIX_LINE_LEN];
	FILE *unix_fp;

	unix_fp = fopen("/proc/net/unix", "r");
	if (!unix_fp)
		return;

	if (fgets(line, sizeof(line), unix_fp) == NULL)
		goto out;
	if (!(line[0] == 'N' && line[1] == 'u' && line[2] == 'm'))
		/* Unexpected line */
		goto out;

	while (fgets(line, sizeof(line), unix_fp)) {
		uint64_t flags;
		uint32_t type;
		unsigned int st;
		unsigned long inode;
		struct unix_xinfo *ux;
		char path[ sizeof_member(struct unix_xinfo, path) ] = { 0 };


		if (sscanf(line, "%*x: %*x %*x %" SCNx64 " %x %x %lu %s",
			   &flags, &type, &st, &inode, path) < 4)
			continue;

		if (inode == 0)
			continue;

		ux = xcalloc(1, sizeof(struct unix_xinfo));
		ux->sock.class = &unix_xinfo_class;
		ux->sock.inode = (ino_t)inode;
		ux->sock.netns_inode = netns_inode;

		ux->acceptcon = !!flags;
		ux->type = type;
		ux->st = st;
		xstrncpy(ux->path, path, sizeof(ux->path));

		add_sock_info((struct sock_xinfo *)ux);
	}

 out:
	fclose(unix_fp);
}

/*
 * AF_INET
 */
struct inet_xinfo {
	struct sock_xinfo sock;
	struct in_addr local_addr;
	struct in_addr remote_addr;
};

static uint32_t kernel32_to_cpu(enum sysfs_byteorder byteorder, uint32_t v)
{
	if (byteorder == SYSFS_BYTEORDER_LITTLE)
		return le32_to_cpu(v);
	else
		return be32_to_cpu(v);
}

/*
 * L4 abstract-layer for protocols stacked on IP and IP6.
 */
enum l4_state {
	/*
	 * Taken from linux/include/net/tcp_states.h.
	 * (GPL-2.0-or-later)
	 *
	 * UDP and RAW sockets also uses the contents in Linux.
	 */
	TCP_ESTABLISHED = 1,
	TCP_SYN_SENT,
	TCP_SYN_RECV,
	TCP_FIN_WAIT1,
	TCP_FIN_WAIT2,
	TCP_TIME_WAIT,
	TCP_CLOSE,
	TCP_CLOSE_WAIT,
	TCP_LAST_ACK,
	TCP_LISTEN,
	TCP_CLOSING,
	TCP_NEW_SYN_RECV,

	TCP_MAX_STATES	/* Leave at the end! */
};

static const char *l4_decode_state(enum l4_state st)
{
	const char * table [] = {
		[TCP_ESTABLISHED] = "established",
		[TCP_SYN_SENT] = "syn-sent",
		[TCP_SYN_RECV] = "syn-recv",
		[TCP_FIN_WAIT1] = "fin-wait1",
		[TCP_FIN_WAIT2] = "fin-wait2",
		[TCP_TIME_WAIT] = "time-wait",
		[TCP_CLOSE] = "close",
		[TCP_CLOSE_WAIT] = "close-wait",
		[TCP_LAST_ACK] = "last-ack",
		[TCP_LISTEN] = "listen",
		[TCP_CLOSING] = "closing",
		[TCP_NEW_SYN_RECV] = "new-syn-recv",
	};

	if (st < TCP_MAX_STATES)
		return table[st];
	return "unknown";
}

struct l4_xinfo {
	struct inet_xinfo inet;
	enum l4_state st;
};

enum l4_side { L4_LOCAL, L4_REMOTE };

struct l4_xinfo_class {
	struct sock_xinfo_class sock;
	struct sock_xinfo *(*scan_line)(const struct sock_xinfo_class *,
					char *,
					ino_t,
					enum sysfs_byteorder);
	void * (*get_addr)(struct l4_xinfo *, enum l4_side);
	bool (*is_any_addr)(void *);
	int family;
};

#define l3_fill_column_handler(L3, SOCK_XINFO, COLUMN_ID, STR)	__extension__ \
	({								\
		struct l4_xinfo_class *class = (struct l4_xinfo_class *)SOCK_XINFO->class; \
		struct l4_xinfo *l4 = (struct l4_xinfo *)SOCK_XINFO;	\
		void *n = NULL;						\
		char s[BUFSIZ];						\
		bool r = false;						\
									\
		switch (COLUMN_ID) {					\
		case COL_##L3##_LADDR:					\
			n = class->get_addr(l4, L4_LOCAL);		\
			break;						\
		case COL_##L3##_RADDR:					\
			n = class->get_addr(l4, L4_REMOTE);		\
			break;						\
		default:						\
			break;						\
		}							\
									\
		if (n && inet_ntop(class->family, n, s, sizeof(s))) {	\
			*STR = strdup(s);				\
			r = true;					\
		}							\
		r;							\
	})

/*
 * TCP
 */
struct tcp_xinfo {
	struct l4_xinfo l4;
	uint16_t local_port;
	uint16_t remote_port;
};

static char *tcp_get_name(struct sock_xinfo *sock_xinfo,
			  struct sock *sock  __attribute__((__unused__)))
{
	char *str = NULL;
	struct tcp_xinfo *tcp = ((struct tcp_xinfo *)sock_xinfo);
	struct l4_xinfo *l4 = &tcp->l4;
	const char *st_str = l4_decode_state(l4->st);
	struct l4_xinfo_class *class = (struct l4_xinfo_class *)sock_xinfo->class;
	void *laddr = class->get_addr(l4, L4_LOCAL);
	void *raddr = class->get_addr(l4, L4_REMOTE);
	char local_s[BUFSIZ];
	char remote_s[BUFSIZ];

	if (!inet_ntop(class->family, laddr, local_s, sizeof(local_s)))
		xasprintf(&str, "state=%s", st_str);
	else if (l4->st == TCP_LISTEN
		 || !inet_ntop(class->family, raddr, remote_s, sizeof(remote_s)))
		xasprintf(&str, "state=%s laddr=%s:%"SCNu16,
			  st_str,
			  local_s, tcp->local_port);
	else
		xasprintf(&str, "state=%s laddr=%s:%"SCNu16" raddr=%s:%"SCNu16,
			  st_str,
			  local_s, tcp->local_port,
			  remote_s, tcp->remote_port);
	return str;
}

static char *tcp_get_type(struct sock_xinfo *sock_xinfo __attribute__((__unused__)),
			   struct sock *sock __attribute__((__unused__)))
{
	return strdup("stream");
}

static char *tcp_get_state(struct sock_xinfo *sock_xinfo,
			   struct sock *sock __attribute__((__unused__)))
{
	return strdup(l4_decode_state(((struct l4_xinfo *)sock_xinfo)->st));
}

static bool tcp_get_listening(struct sock_xinfo *sock_xinfo,
			      struct sock *sock __attribute__((__unused__)))
{
	return ((struct l4_xinfo *)sock_xinfo)->st == TCP_LISTEN;
}

#define l4_fill_column_handler(L4, SOCK_XINFO, COLUMN_ID, STR)	__extension__ \
	({								\
		struct l4_xinfo_class *class = (struct l4_xinfo_class *)SOCK_XINFO->class; \
		struct tcp_xinfo *tcp = (struct tcp_xinfo *)SOCK_XINFO; \
		struct l4_xinfo *l4 = &tcp->l4;				\
		void *n = NULL;						\
		bool has_laddr = false;					\
		unsigned short p;					\
		bool has_lport = false;					\
		char s[BUFSIZ];						\
		bool r = true;						\
									\
		switch (COLUMN_ID) {					\
		case COL_##L4##_LADDR:					\
			n = class->get_addr(l4, L4_LOCAL);		\
			has_laddr = true;				\
			p = tcp->local_port;				\
			/* FALL THROUGH */				\
		case COL_##L4##_RADDR:					\
			if (!has_laddr) {				\
				n = class->get_addr(l4, L4_REMOTE);	\
				p = tcp->remote_port;			\
			}						\
			if (n && inet_ntop(class->family, n, s, sizeof(s))) \
				xasprintf(STR, "%s:%"SCNu16, s, p);	\
			break;						\
		case COL_##L4##_LPORT:					\
			p = tcp->local_port;				\
			has_lport = true;				\
			/* FALL THROUGH */				\
		case COL_##L4##_RPORT:					\
			if (!has_lport)					\
				p = tcp->remote_port;			\
			xasprintf(STR, "%"SCNu16, p);			\
			break;						\
		default:						\
			r = false;					\
			break;						\
		}							\
		r;							\
	})

static struct sock_xinfo *tcp_xinfo_scan_line(const struct sock_xinfo_class *class,
					      char * line,
					      ino_t netns_inode,
					      enum sysfs_byteorder byteorder)
{
	unsigned long local_addr;
	unsigned long local_port;
	unsigned long remote_addr;
	unsigned long remote_port;
	unsigned long st;
	unsigned long long inode;
	struct tcp_xinfo *tcp;
	struct inet_xinfo *inet;
	struct sock_xinfo *sock;

	if (sscanf(line, "%*d: %lx:%lx %lx:%lx %lx %*x:%*x %*x:%*x %*x %*u %*u %lld",
		   &local_addr, &local_port, &remote_addr, &remote_port,
		   &st, &inode) != 6)
		return NULL;

	if (inode == 0)
		return NULL;

	tcp = xcalloc(1, sizeof(struct tcp_xinfo));
	inet = &tcp->l4.inet;
	sock = &inet->sock;
	sock->class = class;
	sock->inode = (ino_t)inode;
	sock->netns_inode = netns_inode;
	inet->local_addr.s_addr = kernel32_to_cpu(byteorder, local_addr);
	tcp->local_port = local_port;
	inet->remote_addr.s_addr = kernel32_to_cpu(byteorder, remote_addr);
	tcp->remote_port = remote_port;
	tcp->l4.st = st;

	return sock;
}

static void *tcp_xinfo_get_addr(struct l4_xinfo *l4, enum l4_side side)
{
	return (side == L4_LOCAL)
		? &l4->inet.local_addr
		: &l4->inet.remote_addr;
}

static bool tcp_xinfo_is_any_addr(void *addr)
{
	return ((struct in_addr *)addr)->s_addr == INADDR_ANY;
}

static bool tcp_fill_column(struct proc *proc __attribute__((__unused__)),
			    struct sock_xinfo *sock_xinfo,
			    struct sock *sock __attribute__((__unused__)),
			    struct libscols_line *ln __attribute__((__unused__)),
			    int column_id,
			    size_t column_index __attribute__((__unused__)),
			    char **str)
{
	return l3_fill_column_handler(INET, sock_xinfo, column_id, str)
		|| l4_fill_column_handler(TCP, sock_xinfo, column_id, str);
}

static const struct l4_xinfo_class tcp_xinfo_class = {
	.sock = {
		.get_name = tcp_get_name,
		.get_type = tcp_get_type,
		.get_state = tcp_get_state,
		.get_listening = tcp_get_listening,
		.fill_column = tcp_fill_column,
		.free = NULL,
	},
	.scan_line = tcp_xinfo_scan_line,
	.get_addr = tcp_xinfo_get_addr,
	.is_any_addr = tcp_xinfo_is_any_addr,
	.family = AF_INET,
};

static bool L4_verify_initial_line(const char *line)
{
	/* At least we expect two white spaces. */
	if (strncmp(line, "  ", 2) != 0)
		return false;
	line += 2;

	/* Skip white spaces. */
	line = skip_space(line);

	return strncmp(line, "sl", 2) == 0;
}

#define TCP_LINE_LEN 256
static void load_xinfo_from_proc_inet_L4(ino_t netns_inode, const char *proc_file,
					 const struct l4_xinfo_class *class)
{
	char line[TCP_LINE_LEN];
	FILE *tcp_fp;

	tcp_fp = fopen(proc_file, "r");
	if (!tcp_fp)
		return;

	if (fgets(line, sizeof(line), tcp_fp) == NULL)
		goto out;
	if (!L4_verify_initial_line(line))
		/* Unexpected line */
		goto out;

	enum sysfs_byteorder byteorder = sysfs_get_byteorder(NULL);

	while (fgets(line, sizeof(line), tcp_fp)) {
		struct sock_xinfo *sock = class->scan_line(&class->sock, line, netns_inode, byteorder);
		if (sock)
			add_sock_info(sock);
	}

 out:
	fclose(tcp_fp);
}

static void load_xinfo_from_proc_tcp(ino_t netns_inode)
{
	load_xinfo_from_proc_inet_L4(netns_inode,
				     "/proc/net/tcp",
				     &tcp_xinfo_class);
}

/*
 * UDP
 */
static char *udp_get_name(struct sock_xinfo *sock_xinfo,
			  struct sock *sock  __attribute__((__unused__)))
{
	char *str = NULL;
	struct tcp_xinfo *tcp = ((struct tcp_xinfo *)sock_xinfo);
	struct l4_xinfo *l4 = &tcp->l4;
	unsigned int st = l4->st;
	const char *st_str = l4_decode_state(st);
	struct l4_xinfo_class *class = (struct l4_xinfo_class *)sock_xinfo->class;
	void *laddr = class->get_addr(l4, L4_LOCAL);
	void *raddr = class->get_addr(l4, L4_REMOTE);
	char local_s[BUFSIZ];
	char remote_s[BUFSIZ];

	if (!inet_ntop(class->family, laddr, local_s, sizeof(local_s)))
		xasprintf(&str, "state=%s", st_str);
	else if ((class->is_any_addr(raddr) && tcp->remote_port == 0)
		 || !inet_ntop(class->family, raddr, remote_s, sizeof(remote_s)))
		xasprintf(&str, "state=%s laddr=%s:%"SCNu16,
			  st_str,
			  local_s, tcp->local_port);
	else
		xasprintf(&str, "state=%s laddr=%s:%"SCNu16" raddr=%s:%"SCNu16,
			  st_str,
			  local_s, tcp->local_port,
			  remote_s, tcp->remote_port);
	return str;
}

static char *udp_get_type(struct sock_xinfo *sock_xinfo __attribute__((__unused__)),
			  struct sock *sock __attribute__((__unused__)))
{
	return strdup("dgram");
}

static bool udp_fill_column(struct proc *proc __attribute__((__unused__)),
			    struct sock_xinfo *sock_xinfo,
			    struct sock *sock __attribute__((__unused__)),
			    struct libscols_line *ln __attribute__((__unused__)),
			    int column_id,
			    size_t column_index __attribute__((__unused__)),
			    char **str)
{
	return l3_fill_column_handler(INET, sock_xinfo, column_id, str)
		|| l4_fill_column_handler(UDP, sock_xinfo, column_id, str);
}

static const struct l4_xinfo_class udp_xinfo_class = {
	.sock = {
		.get_name = udp_get_name,
		.get_type = udp_get_type,
		.get_state = tcp_get_state,
		.get_listening = NULL,
		.fill_column = udp_fill_column,
		.free = NULL,
	},
	.scan_line = tcp_xinfo_scan_line,
	.get_addr = tcp_xinfo_get_addr,
	.is_any_addr = tcp_xinfo_is_any_addr,
	.family = AF_INET,
};

static void load_xinfo_from_proc_udp(ino_t netns_inode)
{
	load_xinfo_from_proc_inet_L4(netns_inode,
				     "/proc/net/udp",
				     &udp_xinfo_class);
}

/*
 * RAW
 */
struct raw_xinfo {
	struct l4_xinfo l4;
	uint16_t protocol;
};

static char *raw_get_name(struct sock_xinfo *sock_xinfo,
			  struct sock *sock  __attribute__((__unused__)))
{
	char *str = NULL;
	struct l4_xinfo_class *class = (struct l4_xinfo_class *)sock_xinfo->class;
	struct raw_xinfo *raw = ((struct raw_xinfo *)sock_xinfo);
	struct l4_xinfo *l4 = &raw->l4;
	const char *st_str = l4_decode_state(l4->st);
	void *laddr = class->get_addr(l4, L4_LOCAL);
	void *raddr = class->get_addr(l4, L4_REMOTE);
	char local_s[BUFSIZ];
	char remote_s[BUFSIZ];

	if (!inet_ntop(class->family, laddr, local_s, sizeof(local_s)))
		xasprintf(&str, "state=%s", st_str);
	else if (class->is_any_addr(raddr)
		 || !inet_ntop(class->family, raddr, remote_s, sizeof(remote_s)))
		xasprintf(&str, "state=%s protocol=%"SCNu16" laddr=%s",
			  st_str,
			  raw->protocol, local_s);
	else
		xasprintf(&str, "state=%s protocol=%"SCNu16" laddr=%s raddr=%s",
			  st_str,
			  raw->protocol, local_s, remote_s);
	return str;
}

static char *raw_get_type(struct sock_xinfo *sock_xinfo __attribute__((__unused__)),
			  struct sock *sock __attribute__((__unused__)))
{
	return strdup("raw");
}

static bool raw_fill_column(struct proc *proc __attribute__((__unused__)),
			    struct sock_xinfo *sock_xinfo,
			    struct sock *sock __attribute__((__unused__)),
			    struct libscols_line *ln __attribute__((__unused__)),
			    int column_id,
			    size_t column_index __attribute__((__unused__)),
			    char **str)
{
	if (l3_fill_column_handler(INET, sock_xinfo, column_id, str))
		return true;

	if (column_id == COL_RAW_PROTOCOL) {
		xasprintf(str, "%"SCNu16,
			  ((struct raw_xinfo *)sock_xinfo)->protocol);
		return true;
	}

	return false;
}

static struct sock_xinfo *raw_xinfo_scan_line(const struct sock_xinfo_class *class,
					      char * line,
					      ino_t netns_inode,
					      enum sysfs_byteorder byteorder)
{
	unsigned long local_addr;
	unsigned long protocol;
	unsigned long remote_addr;
	unsigned long st;
	unsigned long long inode;
	struct raw_xinfo *raw;
	struct inet_xinfo *inet;
	struct sock_xinfo *sock;

	if (sscanf(line, "%*d: %lx:%lx %lx:%*x %lx %*x:%*x %*x:%*x %*x %*u %*u %lld",
		   &local_addr, &protocol, &remote_addr,
		   &st, &inode) != 5)
		return NULL;

	if (inode == 0)
		return NULL;

	raw = xcalloc(1, sizeof(struct raw_xinfo));
	inet = &raw->l4.inet;
	sock = &inet->sock;
	sock->class = class;
	sock->inode = (ino_t)inode;
	sock->netns_inode = netns_inode;
	inet->local_addr.s_addr = kernel32_to_cpu(byteorder, local_addr);
	inet->remote_addr.s_addr = kernel32_to_cpu(byteorder, remote_addr);
	raw->protocol = protocol;
	raw->l4.st = st;

	return sock;
}

static const struct l4_xinfo_class raw_xinfo_class = {
	.sock = {
		.get_name = raw_get_name,
		.get_type = raw_get_type,
		.get_state = tcp_get_state,
		.get_listening = NULL,
		.fill_column = raw_fill_column,
		.free = NULL,
	},
	.scan_line = raw_xinfo_scan_line,
	.get_addr = tcp_xinfo_get_addr,
	.is_any_addr = tcp_xinfo_is_any_addr,
	.family = AF_INET,
};

static void load_xinfo_from_proc_raw(ino_t netns_inode)
{
	load_xinfo_from_proc_inet_L4(netns_inode,
				     "/proc/net/raw",
				     &raw_xinfo_class);
}
