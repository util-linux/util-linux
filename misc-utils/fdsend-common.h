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
 * fdsend/fdrecv common API - SOCKSPEC resolution, socket, SCM_RIGHTS.
 */
#ifndef UTIL_LINUX_FDSEND_COMMON_H
#define UTIL_LINUX_FDSEND_COMMON_H

#include <sys/types.h>

/* fdsend options. pid < 0 means current process. */
struct fdsend_opts {
	int blocking;       /* wait for socket / retry connect */
	int abstract;       /* sockspec is abstract Unix socket name (Linux) */
	pid_t pid;          /* process whose fd to send; < 0 = current */
	int use_pidfd_getfd; /* use pidfd_getfd(2) to obtain fd from pid */
};

/* Send fd to socket identified by sockspec. */
extern int fdsend_do_send(const char *sockspec, int fd, const struct fdsend_opts *opts);

/* Receive fd from socket identified by sockspec. On success sets *out_fd.
 * dup2 and exec are done by the caller (fdrecv.c).
 * abstract: if true, sockspec is an abstract Unix socket name (Linux). */
extern int fdrecv_do_recv(const char *sockspec, int *out_fd, int abstract);

#endif /* UTIL_LINUX_FDSEND_COMMON_H */
