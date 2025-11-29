/*
 * util.c - utilities used in lsfd
 *
 * Copyright (C) 2021-2026 Red Hat, Inc. All rights reserved.
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
#include "lsfd.h"		/* prototype decl for call_with_foreign_fd */
#include "pidfd-utils.h"

static int call_with_foreign_fd_via_pidfd(int pidfd, int target_fd,
					  int (*fn)(int, void*), void *data)
{
	int tfd, r;

	tfd = pidfd_getfd(pidfd, target_fd, 0);
	if (tfd < 0)
		return tfd;

	r = fn(tfd, data);

	close(tfd);
	return r;
}

int call_with_foreign_fd(pid_t target_pid, int target_fd,
			 int (*fn)(int, void*), void *data)
{
	int pidfd, r;

	pidfd = pidfd_open(target_pid, 0);
	if (pidfd < 0)
		return pidfd;

	r = call_with_foreign_fd_via_pidfd(pidfd, target_fd, fn, data);

	close(pidfd);
	return r;
}
