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

#include "c.h"

static inline int write_all(int fd, const void *buf, size_t count)
{
	while (count) {
		ssize_t tmp;

		errno = 0;
		tmp = write(fd, buf, count);
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

static inline int fwrite_all(const void *ptr, size_t size,
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

static inline ssize_t read_all(int fd, char *buf, size_t count)
{
	ssize_t ret;
	ssize_t c = 0;
	int tries = 0;

	memset(buf, 0, count);
	while (count > 0) {
		ret = read(fd, buf, count);
		if (ret <= 0) {
			if (ret < 0 && (errno == EAGAIN || errno == EINTR) && (tries++ < 5)) {
				xusleep(250000);
				continue;
			}
			return c ? c : -1;
		}
		if (ret > 0)
			tries = 0;
		count -= ret;
		buf += ret;
		c += ret;
	}
	return c;
}

#endif /* UTIL_LINUX_ALL_IO_H */
