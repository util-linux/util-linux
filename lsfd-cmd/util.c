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

#if defined(HAVE_STATX) && defined(HAVE_STRUCT_STATX)

static bool statx_available = false;

static void statx_to_stat(const struct statx *stx, struct stat *st)
{
	memset(st, 0, sizeof(*st));
	st->st_dev = makedev(stx->stx_dev_major, stx->stx_dev_minor);
	st->st_ino = stx->stx_ino;
	st->st_mode = stx->stx_mode;
	st->st_nlink = stx->stx_nlink;
	st->st_uid = stx->stx_uid;
	st->st_gid = stx->stx_gid;
	st->st_rdev = makedev(stx->stx_rdev_major, stx->stx_rdev_minor);
	st->st_size = stx->stx_size;
	st->st_blksize = stx->stx_blksize;
	st->st_blocks = stx->stx_blocks;
	st->st_atim.tv_sec = stx->stx_atime.tv_sec;
	st->st_atim.tv_nsec = stx->stx_atime.tv_nsec;
	st->st_mtim.tv_sec = stx->stx_mtime.tv_sec;
	st->st_mtim.tv_nsec = stx->stx_mtime.tv_nsec;
	st->st_ctim.tv_sec = stx->stx_ctime.tv_sec;
	st->st_ctim.tv_nsec = stx->stx_ctime.tv_nsec;
}

void lsfd_init_stat_system(void)
{
	struct statx stx;

	if (statx(AT_FDCWD, "/proc/self", 0, STATX_BASIC_STATS, &stx) == 0)
		statx_available = true;
}
#else
void lsfd_init_stat_system(void)
{
}
#endif

int lsfd_stat(const char *path, struct stat *sb)
{
#if defined(HAVE_STATX) && defined(HAVE_STRUCT_STATX)
	if (statx_available) {
		struct statx stx;
		int rc;

		rc = statx(AT_FDCWD, path, 0, STATX_BASIC_STATS, &stx);
		if (rc == 0)
			statx_to_stat(&stx, sb);
		return rc;
	}
#endif
	return stat(path, sb);
}

int lsfd_path_stat(struct path_cxt *pc, struct stat *sb, int flags, const char *path)
{
#if defined(HAVE_STATX) && defined(HAVE_STRUCT_STATX)
	if (statx_available) {
		struct statx stx;
		int rc;

		rc = ul_path_statx(pc, &stx, flags, STATX_BASIC_STATS, path);
		if (rc == 0)
			statx_to_stat(&stx, sb);
		return rc;
	}
#endif
	return ul_path_stat(pc, sb, flags, path);
}

int lsfd_path_statf(struct path_cxt *pc, struct stat *sb, int flags, const char *path, ...)
{
	char buf[PATH_MAX];
	va_list ap;
	int rc;

	va_start(ap, path);
	rc = vsnprintf(buf, sizeof(buf), path, ap);
	va_end(ap);

	if (rc < 0 || (size_t)rc >= sizeof(buf)) {
		errno = ENAMETOOLONG;
		return -errno;
	}

	return lsfd_path_stat(pc, sb, flags, buf);
}

int lsfd_fstat(int fd, struct stat *sb)
{
#if defined(HAVE_STATX) && defined(HAVE_STRUCT_STATX)
	if (statx_available) {
		struct statx stx;
		int rc;

		rc = statx(fd, "", AT_EMPTY_PATH, STATX_BASIC_STATS, &stx);
		if (rc == 0)
			statx_to_stat(&stx, sb);
		return rc;
	}
#endif
	return fstat(fd, sb);
}
