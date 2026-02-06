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

/* Send fd to socket identified by sockspec. pid < 0 means current process.
 * use_pidfd_getfd: if true use pidfd_getfd; if false use open(/proc/PID/fd/FD). */
extern int fdsend_do_send(const char *sockspec, int fd, int blocking, pid_t pid, int use_pidfd_getfd);

/* Receive fd from socket identified by sockspec. On success sets *out_fd.
 * dup2 and exec are done by the caller (fdrecv.c). */
extern int fdrecv_do_recv(const char *sockspec, int *out_fd);

#endif /* UTIL_LINUX_FDSEND_COMMON_H */
