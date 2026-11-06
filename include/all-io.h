/*
 * No copyright is claimed.  This code is in the public domain; do with
 * it what you wish.
 *
 * Written by Karel Zak <kzak@redhat.com>
 *            Petr Uzel <petr.uzel@suse.cz>
 */

#ifndef UTIL_LINUX_ALL_IO_H
#define UTIL_LINUX_ALL_IO_H

#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#ifdef HAVE_SYS_SENDFILE_H
# include <sys/sendfile.h>
#endif

#include "c.h"
#include "timeutils.h"
#include "vfs.h"

static inline int __write_all(const struct ul_vfs_ops *vfs, int fd,
			      const void *buf, size_t count)
{
	while (count) {
		ssize_t tmp;

		errno = 0;
		tmp = ul_vfs_write(vfs, fd, buf, count);
		if (tmp > 0) {
			count -= tmp;
			if (count)
				buf = (const void *) ((const char *) buf + tmp);
		} else if (errno != EINTR && errno != EAGAIN)
			return -1;
		if (errno == EAGAIN)	/* Try later, *sigh* */
			xusleep(250000);
	}
	return 0;
}

#define ul_write_all(fd, buf, count)		__write_all(NULL, (fd), (buf), (count))
#define ul_vfs_write_all(vfs, fd, buf, count)	__write_all((vfs), (fd), (buf), (count))

static inline int ul_fwrite_all(const void *ptr, size_t size,
			     size_t nmemb, FILE *stream)
{
	while (nmemb) {
		size_t tmp;

		errno = 0;
		tmp = fwrite(ptr, size, nmemb, stream);
		if (tmp > 0) {
			nmemb -= tmp;
			if (nmemb)
				ptr = (const void *) ((const char *) ptr + (tmp * size));
		} else if (errno != EINTR && errno != EAGAIN)
			return -1;
		if (errno == EAGAIN)	/* Try later, *sigh* */
			xusleep(250000);
	}
	return 0;
}

static inline ssize_t __read_all(const struct ul_vfs_ops *vfs, int fd,
				 char *buf, size_t count)
{
	ssize_t ret;
	ssize_t c = 0;
	int tries = 0;

	memset(buf, 0, count);
	while (count > 0) {
		ret = ul_vfs_read(vfs, fd, buf, count);
		if (ret < 0) {
			if ((errno == EAGAIN || errno == EINTR) && (tries++ < 5)) {
				xusleep(250000);
				continue;
			}
			return c ? c : -1;
		}
		if (ret == 0)
			return c;
		tries = 0;
		count -= ret;
		buf += ret;
		c += ret;
	}
	return c;
}

#define ul_read_all(fd, buf, count)		__read_all(NULL, (fd), (buf), (count))
#define ul_vfs_read_all(vfs, fd, buf, count)	__read_all((vfs), (fd), (buf), (count))

/*
 * Reads all data from @fd to a newly allocated buffer and returns the number
 * of bytes read, or -1 on error.
 *
 * The caller gets a buffer only when there is something in it: *buf is set to
 * NULL for an empty read as well as on error. In other words, there is
 * nothing to free unless the return value is greater than zero.
 */
static inline ssize_t ul_read_all_alloc(int fd, char **buf)
{
	size_t size = 1024, c = 0;
	ssize_t ret;
	char *tmp;

	*buf = malloc(size);
	if (!*buf)
		return -1;

	while (1) {
		ret = ul_read_all(fd, *buf + c, size - c);
		if (ret < 0)
			goto fail;

		if (ret == 0)
			break;

		c += ret;
		if (c == size) {
			size *= 2;
			tmp = realloc(*buf, size);
			if (!tmp)
				goto fail;
			*buf = tmp;
		}
	}

	if (c == 0) {
		/* empty read; don't hand over a buffer to free */
		free(*buf);
		*buf = NULL;
	}
	return c;
fail:
	free(*buf);
	*buf = NULL;
	return -1;
}

static inline ssize_t ul_sendfile_all(int out __attribute__((__unused__)),
				   int in __attribute__((__unused__)),
				   off_t *off __attribute__((__unused__)),
				   size_t count __attribute__((__unused__)))
{
#if defined(HAVE_SENDFILE) && defined(__linux__)
	ssize_t ret;
	ssize_t c = 0;
	int tries = 0;
	while (count) {
		ret = sendfile(out, in, off, count);
		if (ret < 0) {
			if ((errno == EAGAIN || errno == EINTR) && (tries++ < 5)) {
				xusleep(250000);
				continue;
			}
			return c ? c : -1;
		}
		if (ret == 0)
			return c;
		tries = 0;
		count -= ret;
		c += ret;
	}
	return c;
#else
	errno = ENOSYS;
	return -1;
#endif
}
#endif /* UTIL_LINUX_ALL_IO_H */
