#ifndef UTIL_LINUX_FILEUTILS
#define UTIL_LINUX_FILEUTILS

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>

#include "c.h"

extern int xmkstemp(char **tmpname, char *dir);

static inline FILE *xfmkstemp(char **tmpname, char *dir)
{
	int fd;
	FILE *ret;

	fd = xmkstemp(tmpname, dir);
	if (fd == -1)
		return NULL;

	if (!(ret = fdopen(fd, "w+" UL_CLOEXECSTR))) {
		close(fd);
		return NULL;
	}
	return ret;
}

extern int get_fd_tabsize(void);

extern int mkdir_p(const char *path, mode_t mode);
extern char *stripoff_last_component(char *path);

#endif /* UTIL_LINUX_FILEUTILS */
