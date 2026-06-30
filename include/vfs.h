/*
 * No copyright is claimed.  This code is in the public domain; do with
 * it what you wish.
 */
#ifndef UTIL_LINUX_VFS_H
#define UTIL_LINUX_VFS_H

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

#endif /* UTIL_LINUX_VFS_H */
