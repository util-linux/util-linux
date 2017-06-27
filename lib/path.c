/*
 * Simple functions to access files. Paths can be globally prefixed to read
 * data from an alternative source (e.g. a /proc dump for regression tests).
 *
 * Copyright (C) 2008 Cai Qian <qcai@redhat.com>
 * Copyright (C) 2008-2012 Karel Zak <kzak@redhat.com>
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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <inttypes.h>
#include <errno.h>

#include "all-io.h"
#include "path.h"
#include "nls.h"
#include "c.h"

static size_t prefixlen;
static char pathbuf[PATH_MAX];

static const char *
path_vcreate(const char *path, va_list ap)
{
	int rc = vsnprintf(
		pathbuf + prefixlen, sizeof(pathbuf) - prefixlen, path, ap);

	if (rc < 0)
		return NULL;
	if ((size_t)rc >= sizeof(pathbuf)) {
		errno = ENAMETOOLONG;
		return NULL;
	}
	return pathbuf;
}

const char *
path_get(const char *path, ...)
{
	const char *p;
	va_list ap;

	va_start(ap, path);
	p = path_vcreate(path, ap);
	va_end(ap);

	return p;
}

static FILE *
path_vfopen(const char *mode, int exit_on_error, const char *path, va_list ap)
{
	FILE *f;
	const char *p = path_vcreate(path, ap);
	if (!p)
		goto err;

	f = fopen(p, mode);
	if (!f)
		goto err;

	return f;
err:
	if (exit_on_error)
		err(EXIT_FAILURE, _("cannot open %s"), p ? p : "path");
	return NULL;
}

static int
path_vopen(int flags, const char *path, va_list ap)
{
	int fd;
	const char *p = path_vcreate(path, ap);
	if (!p)
		goto err;

	fd = open(p, flags);
	if (fd == -1)
		goto err;

	return fd;
err:
	err(EXIT_FAILURE, _("cannot open %s"), p ? p : "path");
}

FILE *
path_fopen(const char *mode, int exit_on_error, const char *path, ...)
{
	FILE *fd;
	va_list ap;

	va_start(ap, path);
	fd = path_vfopen(mode, exit_on_error, path, ap);
	va_end(ap);

	return fd;
}

void
path_read_str(char *result, size_t len, const char *path, ...)
{
	FILE *fd;
	va_list ap;

	va_start(ap, path);
	fd = path_vfopen("r" UL_CLOEXECSTR, 1, path, ap);
	va_end(ap);

	if (!fgets(result, len, fd))
		err(EXIT_FAILURE, _("cannot read %s"), pathbuf);
	fclose(fd);

	len = strlen(result);
	if (result[len - 1] == '\n')
		result[len - 1] = '\0';
}

int
path_read_s32(const char *path, ...)
{
	FILE *fd;
	va_list ap;
	int result;

	va_start(ap, path);
	fd = path_vfopen("r" UL_CLOEXECSTR, 1, path, ap);
	va_end(ap);

	if (fscanf(fd, "%d", &result) != 1) {
		if (ferror(fd))
			err(EXIT_FAILURE, _("cannot read %s"), pathbuf);
		else
			errx(EXIT_FAILURE, _("parse error: %s"), pathbuf);
	}
	fclose(fd);
	return result;
}

uint64_t
path_read_u64(const char *path, ...)
{
	FILE *fd;
	va_list ap;
	uint64_t result;

	va_start(ap, path);
	fd = path_vfopen("r", 1, path, ap);
	va_end(ap);

	if (fscanf(fd, "%"SCNu64, &result) != 1) {
		if (ferror(fd))
			err(EXIT_FAILURE, _("cannot read %s"), pathbuf);
		else
			errx(EXIT_FAILURE, _("parse error: %s"), pathbuf);
	}
	fclose(fd);
	return result;
}

int
path_write_str(const char *str, const char *path, ...)
{
	int fd, result;
	va_list ap;

	va_start(ap, path);
	fd = path_vopen(O_WRONLY|O_CLOEXEC, path, ap);
	va_end(ap);
	result = write_all(fd, str, strlen(str));
	close(fd);
	return result;
}

int
path_exist(const char *path, ...)
{
	va_list ap;
	const char *p;

	va_start(ap, path);
	p = path_vcreate(path, ap);
	va_end(ap);

	return p && access(p, F_OK) == 0;
}

#ifdef HAVE_CPU_SET_T

static cpu_set_t *
path_cpuparse(int maxcpus, int islist, const char *path, va_list ap)
{
	FILE *fd;
	cpu_set_t *set;
	size_t setsize, len = maxcpus * 7;
	char buf[len];

	fd = path_vfopen("r" UL_CLOEXECSTR, 1, path, ap);

	if (!fgets(buf, len, fd))
		err(EXIT_FAILURE, _("cannot read %s"), pathbuf);
	fclose(fd);

	len = strlen(buf);
	if (buf[len - 1] == '\n')
		buf[len - 1] = '\0';

	set = cpuset_alloc(maxcpus, &setsize, NULL);
	if (!set)
		err(EXIT_FAILURE, _("failed to callocate cpu set"));

	if (islist) {
		if (cpulist_parse(buf, set, setsize, 0))
			errx(EXIT_FAILURE, _("failed to parse CPU list %s"), buf);
	} else {
		if (cpumask_parse(buf, set, setsize))
			errx(EXIT_FAILURE, _("failed to parse CPU mask %s"), buf);
	}
	return set;
}

cpu_set_t *
path_read_cpuset(int maxcpus, const char *path, ...)
{
	va_list ap;
	cpu_set_t *set;

	va_start(ap, path);
	set = path_cpuparse(maxcpus, 0, path, ap);
	va_end(ap);

	return set;
}

cpu_set_t *
path_read_cpulist(int maxcpus, const char *path, ...)
{
	va_list ap;
	cpu_set_t *set;

	va_start(ap, path);
	set = path_cpuparse(maxcpus, 1, path, ap);
	va_end(ap);

	return set;
}

int
path_set_prefix(const char *prefix)
{
	size_t len = strlen(prefix);

	if (len >= sizeof(pathbuf) - 1) {
		errno = ENAMETOOLONG;
		return -1;
	}
	prefixlen = len;
	strcpy(pathbuf, prefix);
	return 0;
}

#endif /* HAVE_CPU_SET_T */
