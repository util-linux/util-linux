#ifndef UTIL_LINUX_FILEUTILS
#define UTIL_LINUX_FILEUTILS

extern int xmkstemp(char **tmpname);

static inline FILE *xfmkstemp(char **tmpname)
{
	int fd;
	FILE *ret;
	fd = xmkstemp(tmpname);
	if (fd == -1) {
		return NULL;
	}
	if (!(ret = fdopen(fd, "w+"))) {
		close(fd);
		return NULL;
	}
	return ret;
}
#endif
