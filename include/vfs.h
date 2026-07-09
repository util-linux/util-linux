/*
 * No copyright is claimed.  This code is in the public domain; do with
 * it what you wish.
 */
#ifndef UTIL_LINUX_VFS_H
#define UTIL_LINUX_VFS_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <string.h>

#ifndef UL_VFS_OPS_DEFINED
#define UL_VFS_OPS_DEFINED

struct ul_vfs_ops {
	size_t size;

	ssize_t (*vfs_read)(int fd, void *buf, size_t count);
	ssize_t (*vfs_write)(int fd, const void *buf, size_t count);
	int     (*vfs_open)(const char *pathname, int flags, mode_t mode);
	int     (*vfs_close)(int fd);
	off_t   (*vfs_lseek)(int fd, off_t offset, int whence);
	int     (*vfs_fsync)(int fd);

	FILE   *(*vfs_fopen)(const char *pathname, const char *mode);
};

#endif /* UL_VFS_OPS_DEFINED */

static inline void ul_vfs_init(struct ul_vfs_ops *dst,
			       const struct ul_vfs_ops *src)
{
	memset(dst, 0, sizeof(*dst));
	if (src && src->size > 0) {
		size_t sz = src->size < sizeof(*dst) ? src->size : sizeof(*dst);
		memcpy(dst, src, sz);
	}
	dst->size = sizeof(*dst);
}

static inline struct ul_vfs_ops *ul_vfs_copy(const struct ul_vfs_ops *src)
{
	struct ul_vfs_ops *dst;

	if (!src)
		return NULL;
	dst = malloc(sizeof(*dst));
	if (!dst)
		return NULL;
	memcpy(dst, src, sizeof(*dst));
	return dst;
}

static inline ssize_t ul_vfs_read(const struct ul_vfs_ops *vfs,
				  int fd, void *buf, size_t count)
{
	if (vfs && vfs->vfs_read)
		return vfs->vfs_read(fd, buf, count);
	return read(fd, buf, count);
}

static inline ssize_t ul_vfs_write(const struct ul_vfs_ops *vfs,
				   int fd, const void *buf, size_t count)
{
	if (vfs && vfs->vfs_write)
		return vfs->vfs_write(fd, buf, count);
	return write(fd, buf, count);
}

static inline int ul_vfs_open(const struct ul_vfs_ops *vfs,
			      const char *pathname, int flags, mode_t mode)
{
	if (vfs && vfs->vfs_open)
		return vfs->vfs_open(pathname, flags, mode);
	return open(pathname, flags, mode);
}

static inline int ul_vfs_close(const struct ul_vfs_ops *vfs, int fd)
{
	if (vfs && vfs->vfs_close)
		return vfs->vfs_close(fd);
	return close(fd);
}

static inline off_t ul_vfs_lseek(const struct ul_vfs_ops *vfs,
				 int fd, off_t offset, int whence)
{
	if (vfs && vfs->vfs_lseek)
		return vfs->vfs_lseek(fd, offset, whence);
	return lseek(fd, offset, whence);
}

static inline int ul_vfs_fsync(const struct ul_vfs_ops *vfs, int fd)
{
	if (vfs && vfs->vfs_fsync)
		return vfs->vfs_fsync(fd);
	return fsync(fd);
}

static inline int ul_mode_to_flags(const char *mode)
{
	int flags = 0;
	const char *p;

	for (p = mode; p && *p; p++) {
		if (*p == 'r' && *(p + 1) == '+')
			flags |= O_RDWR;
		else if (*p == 'r')
			flags |= O_RDONLY;

		else if (*p == 'w' && *(p + 1) == '+')
			flags |= O_RDWR | O_TRUNC;
		else if (*p == 'w')
			flags |= O_WRONLY | O_TRUNC;

		else if (*p == 'a' && *(p + 1) == '+')
			flags |= O_RDWR | O_APPEND;
		else if (*p == 'a')
			flags |= O_WRONLY | O_APPEND;
#ifdef O_CLOEXEC
		else if (*p == 'e')
			flags |= O_CLOEXEC;
#endif
	}

	return flags;
}

#ifdef __GLIBC__

struct ul_vfs_cookie {
	const struct ul_vfs_ops *vfs;
	int fd;
};

static inline ssize_t ul_vfs_cookie_read(void *cookie, char *buf, size_t count)
{
	struct ul_vfs_cookie *ck = (struct ul_vfs_cookie *) cookie;
	return ul_vfs_read(ck->vfs, ck->fd, buf, count);
}

static inline ssize_t ul_vfs_cookie_write(void *cookie, const char *buf, size_t count)
{
	struct ul_vfs_cookie *ck = (struct ul_vfs_cookie *) cookie;
	return ul_vfs_write(ck->vfs, ck->fd, buf, count);
}

static inline int ul_vfs_cookie_seek(void *cookie, off64_t *pos, int whence)
{
	struct ul_vfs_cookie *ck = (struct ul_vfs_cookie *) cookie;
	off_t rc = ul_vfs_lseek(ck->vfs, ck->fd, (off_t) *pos, whence);

	if (rc == (off_t) -1)
		return -1;
	*pos = (off64_t) rc;
	return 0;
}

static inline int ul_vfs_cookie_close(void *cookie)
{
	struct ul_vfs_cookie *ck = (struct ul_vfs_cookie *) cookie;
	int rc = ul_vfs_close(ck->vfs, ck->fd);

	free(ck);
	return rc;
}

static inline FILE *ul_vfs_fdopen(const struct ul_vfs_ops *vfs,
				  int fd, const char *mode)
{
	struct ul_vfs_cookie *ck;
	cookie_io_functions_t io_funcs = {
		.read  = ul_vfs_cookie_read,
		.write = ul_vfs_cookie_write,
		.seek  = ul_vfs_cookie_seek,
		.close = ul_vfs_cookie_close,
	};

	if (!vfs)
		return fdopen(fd, mode);

	ck = malloc(sizeof(*ck));
	if (!ck)
		return NULL;
	ck->vfs = vfs;
	ck->fd = fd;

	return fopencookie(ck, mode, io_funcs);
}

#else /* !__GLIBC__ */

static inline FILE *ul_vfs_fdopen(
		const struct ul_vfs_ops *vfs __attribute__((__unused__)),
		int fd, const char *mode)
{
	return fdopen(fd, mode);
}

#endif /* __GLIBC__ */

static inline FILE *ul_vfs_fopen(const struct ul_vfs_ops *vfs,
				 const char *path, const char *mode)
{
	if (vfs && vfs->vfs_fopen)
		return vfs->vfs_fopen(path, mode);
#ifdef __GLIBC__
	if (vfs && vfs->vfs_open) {
		int flags = ul_mode_to_flags(mode);
		int fd = ul_vfs_open(vfs, path, flags, 0);

		if (fd < 0)
			return NULL;
		return ul_vfs_fdopen(vfs, fd, mode);
	}
#endif
	return fopen(path, mode);
}

#endif /* UTIL_LINUX_VFS_H */
