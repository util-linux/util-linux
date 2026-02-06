/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Copyright (C) 2026 WanBingjiang <wanbingjiang@webray.com.cn>
 *
 * fdsend/fdrecv common - SOCKSPEC resolution, socket, SCM_RIGHTS.
 */
#include "fdsend-common.h"
#include "c.h"
#include "fileutils.h"
#include "pathnames.h"
#include "pidfd-utils.h"

#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/inotify.h>

/* For fdrecv: cleanup socket file when interrupted in accept(). */
static volatile sig_atomic_t fdrecv_got_signal;

static void fdrecv_sig_handler(int sig)
{
	(void)sig;
	fdrecv_got_signal = 1;
}

static void fdrecv_setup_cleanup_signals(void)
{
	struct sigaction sa = { .sa_handler = fdrecv_sig_handler };
	fdrecv_got_signal = 0;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGHUP, &sa, NULL);
}

static int sockpath_from_spec(const char *spec, char *path, size_t size)
{
	char dir[PATH_MAX];
	uid_t uid;
	int r;

	if (!spec || !path || size == 0) {
		errno = EINVAL;
		return -1;
	}

	if (spec[0] == '/') {
		/* SOCKPATH: use as-is */
		if (strlen(spec) >= size) {
			errno = ENAMETOOLONG;
			return -1;
		}
		strncpy(path, spec, size);
		return 0;
	}

	/* SOCKNAME: must not contain '/' */
	if (strchr(spec, '/') != NULL) {
		errno = EINVAL;
		return -1;
	}

	uid = getuid();
	if (uid == 0)
		snprintf(dir, sizeof(dir), "%s", _PATH_FDSEND_RUN);
	else
		snprintf(dir, sizeof(dir), "%s/%u/fdsend", _PATH_FDSEND_RUN_USER, (unsigned) uid);

	/* Ensure the directory exists */
	r = ul_mkdir_p(dir, 0755);
	if (r != 0) {
		errno = -r;
		return -1;
	}

	if (snprintf(path, size, "%s/%s", dir, spec) >= (int) size) {
		errno = ENAMETOOLONG;
		return -1;
	}
	return 0;
}

/*
 * If blocking: wait for socket file to appear (receiver started first).
 * Returns 0 when socket exists or blocking is 0; -1 on error.
 */
static int fdsend_wait_for_socket(const char *sockpath, int blocking)
{
	char dir[PATH_MAX];
	char *base;
	int inotify_fd = -1;
	int wd = -1;
	struct pollfd pfd;
#define INOTIFY_BUF_LEN (sizeof(struct inotify_event) + NAME_MAX + 1)
	char buf[INOTIFY_BUF_LEN];
	int ret = -1;
	int poll_timeout_ms = 2000;

	if (!blocking)
		return 0;

	/* return if the socket exists */
	if (access(sockpath, F_OK) == 0)
		return 0;

	if (strlen(sockpath) >= sizeof(dir)) {
		errno = ENAMETOOLONG;
		return -1;
	}
	strncpy(dir, sockpath, sizeof(dir));
	base = strrchr(dir, '/');
	if (base) {
		*base = '\0';
		base++;
	} else {
		dir[0] = '.';
		dir[1] = '\0';
		base = dir;
	}

	inotify_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
	if (inotify_fd < 0)
		inotify_fd = inotify_init();
	if (inotify_fd < 0)
		return -1;

	wd = inotify_add_watch(inotify_fd, dir[0] == '.' ? "." : dir, IN_CREATE);
	if (wd < 0)
		goto out;

	for (;;) {
		ssize_t n;
		char *p;
		int pr;

		pfd.fd = inotify_fd;
		pfd.events = POLLIN;
		pr = poll(&pfd, 1, poll_timeout_ms);
		if (pr < 0) {
			if (errno == EINTR)
				continue;
			goto out;
		} else if (pr == 0) {
			/* Timeout: re-check access() in case fdrecv created socket between access() and poll(). */
			if (access(sockpath, F_OK) == 0) {
				ret = 0;
				goto out;
			}
			/* no more timeout */
			poll_timeout_ms = -1;
			continue;
		}

		n = read(inotify_fd, buf, sizeof(buf));
		if (n < 0) {
			if (errno == EINTR || errno == EAGAIN)
				continue;
			goto out;
		}
		for (p = buf; p < buf + n; ) {
			struct inotify_event *ev = (struct inotify_event *)p;
			if (ev->mask & IN_CREATE && ev->len > 0 &&
			    strcmp(ev->name, base) == 0) {
				ret = 0;
				goto out;
			}
			p += sizeof(struct inotify_event) + ev->len;
		}
	}
out:
	if (wd >= 0)
		inotify_rm_watch(inotify_fd, wd);
	close(inotify_fd);
	return ret;
}

/*
 * Receiver: socket/bind/listen/accept, recvmsg with SCM_RIGHTS.
 * On success sets *out_fd to the received fd and returns 0.
 */
static int fdrecv_accept_and_recv_fd(const char *sockpath, int *out_fd)
{
	int sock = -1;
	int conn = -1;
	struct sockaddr_un sun;
	size_t path_len;
	struct cmsghdr *cmsg;
	union {
		struct cmsghdr hdr;
		char buf[CMSG_SPACE(sizeof(int))];
	} cmsg_buf = { 0 };
	struct msghdr msg = { 0 };
	struct iovec iov;
	char dummy = ' ';

	if (!sockpath || !out_fd) {
		errno = EINVAL;
		return -1;
	}

	path_len = strlen(sockpath);
	if (path_len >= sizeof(sun.sun_path)) {
		errno = ENAMETOOLONG;
		return -1;
	}

	sock = socket(AF_UNIX, SOCK_STREAM, 0);
	if (sock < 0)
		return -1;

	memset(&sun, 0, sizeof(sun));
	sun.sun_family = AF_UNIX;
	memcpy(sun.sun_path, sockpath, path_len + 1);

	if (bind(sock, (struct sockaddr *)&sun, sizeof(sun)) != 0) {
		close(sock);
		return -1;
	}

	if (chmod(sockpath, 0600) != 0) {
		close(sock);
		unlink(sockpath);
		return -1;
	}

	if (listen(sock, 1) != 0) {
		close(sock);
		unlink(sockpath);
		return -1;
	}

	/* Register handler so we unlink the socket file when interrupted in accept(). */
	fdrecv_setup_cleanup_signals();

	while(true) {
		conn = accept(sock, NULL, NULL);
		if (conn >= 0)
			break;
		if (errno != EINTR) {
			close(sock);
			unlink(sockpath);
			return -1;
		}
		if (fdrecv_got_signal) {
			close(sock);
			unlink(sockpath);
			errno = EINTR;
			return -1;
		}
	}

	close(sock);
	unlink(sockpath);

	/* recvmsg: at least one byte in iov; control buffer for SCM_RIGHTS */
	iov.iov_base = &dummy;
	iov.iov_len = 1;
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = cmsg_buf.buf;
	msg.msg_controllen = sizeof(cmsg_buf.buf);

	if (recvmsg(conn, &msg, 0) <= 0) {
		close(conn);
		return -1;
	}

	cmsg = CMSG_FIRSTHDR(&msg);
	for (; cmsg != NULL; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
		if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS) {
			*out_fd = *(int *)CMSG_DATA(cmsg);
			close(conn);
			return 0;
		}
	}
	close(conn);
	errno = EINVAL;
	return -1;
}

/*
 * Get fd number @fd from process @pid by opening /proc/PID/fd/FD.
 * Returns the new fd on success, -1 on error.
 */
static int open_proc_pid_fd(pid_t pid, int fd)
{
	char proc_path[PATH_MAX];

	if (snprintf(proc_path, sizeof(proc_path), "/proc/%d/fd/%d",
		     (int)pid, fd) >= (int)sizeof(proc_path)) {
		errno = ENAMETOOLONG;
		return -1;
	}
	return open(proc_path, O_RDWR);
}

/*
 * Get fd number @fd from process @pid.
 * use_pidfd_getfd: if true use pidfd_getfd only; if false use open(/proc/PID/fd/FD) only.
 * Returns the new fd on success, -1 on error.
 */
static int fdsend_open_pid_fd(pid_t pid, int fd, int use_pidfd_getfd)
{
	int pidfd;
	int ret;

	if (!use_pidfd_getfd)
		return open_proc_pid_fd(pid, fd);

	pidfd = pidfd_open(pid, 0);
	if (pidfd < 0)
		return -1;

	ret = pidfd_getfd(pidfd, fd, 0);
	close(pidfd);
	return ret;
}

/*
 * Sender: resolve path; get fd (open /proc/PID/fd/FD or use current); connect and sendmsg SCM_RIGHTS.
 */
static int fdsend_connect_and_send_fd(const char *sockpath, int fd_to_send, int own_fd)
{
	int sock = -1;
	struct sockaddr_un sun;
	size_t path_len;
	union {
		struct cmsghdr hdr;
		char buf[CMSG_SPACE(sizeof(int))];
	} cmsg_buf = { 0 };
	struct msghdr msg = { 0 };
	struct cmsghdr *cmsg;
	struct iovec iov;
	char dummy = ' ';
	int ret = -1;

	if (!sockpath) {
		errno = EINVAL;
		return -1;
	}

	path_len = strlen(sockpath);
	if (path_len >= sizeof(sun.sun_path)) {
		errno = ENAMETOOLONG;
		return -1;
	}

	sock = socket(AF_UNIX, SOCK_STREAM, 0);
	if (sock < 0)
		goto out;

	memset(&sun, 0, sizeof(sun));
	sun.sun_family = AF_UNIX;
	memcpy(sun.sun_path, sockpath, path_len + 1);

	if (connect(sock, (struct sockaddr *)&sun, sizeof(sun)) != 0)
		goto out;

	iov.iov_base = &dummy;
	iov.iov_len = 1;
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = cmsg_buf.buf;
	msg.msg_controllen = sizeof(cmsg_buf.buf);

	cmsg = CMSG_FIRSTHDR(&msg);
	cmsg->cmsg_len = CMSG_LEN(sizeof(int));
	cmsg->cmsg_level = SOL_SOCKET;
	cmsg->cmsg_type = SCM_RIGHTS;
	*(int *)CMSG_DATA(cmsg) = fd_to_send;

	if (sendmsg(sock, &msg, 0) < 0)
		goto out;
	ret = 0;
out:
	close(sock);
	if (own_fd && fd_to_send >= 0)
		close(fd_to_send);
	return ret;
}

int fdsend_do_send(const char *sockspec, int fd, int blocking, pid_t pid, int use_pidfd_getfd)
{
	char path[PATH_MAX];
	int fd_to_send = fd;
	int own_fd = 0;

	if (sockpath_from_spec(sockspec, path, sizeof(path)) != 0)
		return -1;

	if (fdsend_wait_for_socket(path, blocking) != 0)
		return -1;

	if (pid >= 0) {
		fd_to_send = fdsend_open_pid_fd(pid, fd, use_pidfd_getfd);
		if (fd_to_send < 0)
			return -1;
		own_fd = 1;
	}

	return fdsend_connect_and_send_fd(path, fd_to_send, own_fd);
}

int fdrecv_do_recv(const char *sockspec, int *out_fd)
{
	char path[PATH_MAX];

	if (!out_fd) {
		errno = EINVAL;
		return -1;
	}

	if (sockpath_from_spec(sockspec, path, sizeof(path)) != 0)
		return -1;

	return fdrecv_accept_and_recv_fd(path, out_fd);
}
