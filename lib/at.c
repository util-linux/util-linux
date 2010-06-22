/*
 * Portable xxxat() functions.
 *
 * Copyright (C) 2010 Karel Zak <kzak@redhat.com>
 */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "at.h"

int fstat_at(int dir, const char *dirname, const char *filename,
				struct stat *st, int nofollow)
{
#ifdef HAVE_FSTATAT
	return fstatat(dir, filename, st,
			nofollow ? AT_SYMLINK_NOFOLLOW : 0);
#else
	char path[PATH_MAX];
	int len;

	len = snprintf(path, sizeof(path), "%s/%s", dirname, filename);
	if (len < 0 || len + 1 > sizeof(path))
		return -1;

	return nofollow ? lstat(path, st) : stat(path, st);
#endif
}

int open_at(int dir, const char *dirname, const char *filename, int flags)
{
#ifdef HAVE_FSTATAT
	return openat(dir, filename, flags);
#else
	char path[PATH_MAX];
	int len;

	len = snprintf(path, sizeof(path), "%s/%s", dirname, filename);
	if (len < 0 || len + 1 > sizeof(path))
		return -1;

	return open(path, flags);
#endif
}

FILE *fopen_at(int dir, const char *dirname, const char *filename, int flags,
			const char *mode)
{
	int fd = open_at(dir, dirname, filename, flags);

	if (fd < 0)
		return NULL;

	return fdopen(fd, mode);
}

#ifdef TEST_PROGRAM
#include <err.h>
#include <errno.h>
#include <sys/types.h>
#include <dirent.h>
#include <string.h>

int main(int argc, char *argv[])
{
	DIR *dir;
	struct dirent *d;
	char *dirname;

	if (argc != 2) {
		fprintf(stderr, "usage: %s <directory>\n", argv[0]);
		exit(EXIT_FAILURE);
	}
	dirname = argv[1];

	dir = opendir(dirname);
	if (!dir)
		err(EXIT_FAILURE, "%s: open failed", dirname);

	while ((d = readdir(dir))) {
		struct stat st;
		FILE *f;

		printf("%32s ", d->d_name);

		if (fstat_at(dirfd(dir), dirname, d->d_name, &st, 0) == 0)
			printf("%16jd bytes ", st.st_size);
		else
			printf("%16s bytes ", "???");

		f = fopen_at(dirfd(dir), dirname, d->d_name, O_RDONLY, "r");
		printf("   %s\n", f ? "OK" : strerror(errno));
		if (f)
			fclose(f);
	}
	closedir(dir);
	return EXIT_SUCCESS;
}
#endif
