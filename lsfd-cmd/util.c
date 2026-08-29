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
#include "fileutils.h"

int call_with_foreign_fd_via_pidfd(int pidfd, int target_fd,
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

int lsfd_stat(const char *path, struct stat *sb)
{
#if HAVE_UL_SAFE_STAT
	unsigned int retmask = 0;
	int rc = ul_safe_stat(path, sb, 0, UL_STATX_BASIC, &retmask);

	if (rc == 0) {
		if ((retmask & UL_STATX_BASIC) == UL_STATX_BASIC)
			return 0;
		/* Missing basic attributes; treat as unsupported non-blocking stat. */
		errno = EOPNOTSUPP;
	}

	switch (errno) {
	case ENOSYS:
	case EOPNOTSUPP:
		break;	/* Fall back to stat() only if unsupported. */
	default:
		return -1;
	}
#endif
	return stat(path, sb);
}

#if HAVE_UL_SAFE_STATX
static int check_statx_result(int rc, const struct statx *stx, struct stat *sb,
			      unsigned int mask)
{
	if (rc == 0) {
		if ((stx->stx_mask & mask) == mask) {
			ul_statx_to_stat(stx, sb);
			return 0;
		}
		errno = EOPNOTSUPP;
	} else if (rc < 0 && rc != -1)
		errno = -rc;

	switch (errno) {
	case ENOSYS:
	case EOPNOTSUPP:
		return 1;
	default:
		return -1;
	}
}
#endif

int lsfd_path_stat(struct path_cxt *pc, struct stat *sb, int flags, const char *path)
{
#if HAVE_UL_SAFE_STATX
	struct statx stx;
	int rc = ul_path_statx(pc, &stx, flags | AT_NO_AUTOMOUNT | AT_STATX_DONT_SYNC,
			       STATX_BASIC_STATS, path);
	rc = check_statx_result(rc, &stx, sb, UL_STATX_BASIC);
	if (rc <= 0)
		return rc;
#endif
	return ul_path_stat(pc, sb, flags, path);
}

int lsfd_path_statf(struct path_cxt *pc, struct stat *sb, int flags, const char *path, ...)
{
#if HAVE_UL_SAFE_STATX
	struct statx stx;
	va_list ap;
	int rc;

	va_start(ap, path);
	rc = ul_path_vstatxf(pc, &stx, flags | AT_NO_AUTOMOUNT | AT_STATX_DONT_SYNC,
			     STATX_BASIC_STATS, path, ap);
	va_end(ap);

	rc = check_statx_result(rc, &stx, sb, UL_STATX_BASIC);
	if (rc <= 0)
		return rc;
#endif
	{
		va_list ap;
		int rc;

		va_start(ap, path);
		rc = ul_path_vstatf(pc, sb, flags, path, ap);
		va_end(ap);
		if (rc < 0) {
			if (rc != -1)
				errno = -rc;
			return -1;
		}
		return 0;
	}
}

int lsfd_fstat(int fd, struct stat *sb)
{
#if HAVE_UL_SAFE_STATX
	struct statx stx;
	int rc;

	/* AT_NO_AUTOMOUNT is not needed here because statx() with
	 * AT_EMPTY_PATH on an already-open file descriptor operates
	 * directly on the open file without pathname resolution. */
	rc = statx(fd, "", AT_EMPTY_PATH | AT_STATX_DONT_SYNC, STATX_BASIC_STATS, &stx);
	rc = check_statx_result(rc, &stx, sb, UL_STATX_BASIC);
	if (rc <= 0)
		return rc;
#endif
	return fstat(fd, sb);
}
