#ifndef UTIL_LINUX_WRITEALL_H
#define UTIL_LINUX_WRITEALL_H

#include <string.h>
#include <unistd.h>
#include <errno.h>

static inline int write_all(int fd, const void *buf, size_t count)
{
	while(count) {
		ssize_t tmp;

		errno = 0;
		tmp = write(fd, buf, count);
		if (tmp > 0) {
			count -= tmp;
			if (count)
				buf += tmp;
		} else if (errno != EINTR && errno != EAGAIN)
			return -1;
	}
	return 0;
}

#endif /* UTIL_LINUX_WRITEALL_H */
