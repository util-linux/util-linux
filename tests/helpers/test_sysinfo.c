/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Copyright (C) 2007 Karel Zak <kzak@redhat.com>
 *
 * This file is part of util-linux.
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This file is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <stdint.h>
#include <inttypes.h>
#include <errno.h>
#include <time.h>
#include <sys/ioctl.h>

#include "c.h"
#include "widechar.h"

#ifdef __linux__
# include <sys/mount.h>
# include "mount-api-utils.h"
#endif

#ifdef HAVE_LINUX_NSFS_H
# include <linux/nsfs.h>
# if defined(NS_GET_NSTYPE) && defined(NS_GET_OWNER_UID)
#  define USE_NS_GET_NSTYPE	1
# endif
# if defined(NS_GET_USERNS)
#  define USE_NS_GET_USERNS	1
# endif
#endif

#ifdef HAVE_FTS_OPEN
# include <fcntl.h>
# include <sys/stat.h>
# include <fts.h>
#endif

#include "xalloc.h"
#include "namespace.h"

typedef struct {
	const char	*name;
	int		(*fnc)(void);
} mntHlpfnc;

static int hlp_wordsize(void)
{
	printf("%zu\n", CHAR_BIT * sizeof(void*));
	return 0;
}

static int hlp_endianness(void)
{
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	printf("LE\n");
#else
	printf("BE\n");
#endif
	return 0;
}


static int hlp_pagesize(void)
{
	printf("%d\n", getpagesize());
	return 0;
}

static int hlp_int_max(void)
{
	printf("%d\n", INT_MAX);
	return 0;
}

static int hlp_uint_max(void)
{
	printf("%u\n", UINT_MAX);
	return 0;
}

static int hlp_long_max(void)
{
	printf("%ld\n", LONG_MAX);
	return 0;
}

static int hlp_ulong_max(void)
{
	printf("%lu\n", ULONG_MAX);
	return 0;
}

static int hlp_u64_max(void)
{
	printf("%" PRIu64 "\n", UINT64_MAX);
	return 0;
}

static int hlp_ulong_max32(void)
{
#if __WORDSIZE == 64
	printf("%lu\n", ULONG_MAX >> 32);
#else
	printf("%lu\n", ULONG_MAX);
#endif
	return 0;
}

static int hlp_wcsspn_ok(void)
{
	printf("%d\n", wcsspn(L"FOO", L"F") == 1);
	return 0;
}

static int hlp_enotty_ok(void)
{
	errno = 0;
	ioctl(STDOUT_FILENO, 0);

	printf("%d\n", errno != ENOSYS);
	return 0;
}

static int hlp_fsopen_ok(void)
{
#if defined(HAVE_FSOPEN) && defined(FSOPEN_CLOEXEC)
	errno = 0;
	fsopen(NULL, FSOPEN_CLOEXEC);
#else
	errno = ENOSYS;
#endif
	printf("%d\n", errno != ENOSYS);
	return 0;
}

static int hlp_statmount_ok(void)
{
	printf("%d\n",
#ifdef HAVE_STATMOUNT_API
		has_statmount()
#else
		0
#endif
	);
	return 0;
}

static int hlp_listmount_ok(void)
{
	printf("%d\n",
#ifdef HAVE_STATMOUNT_API
		has_listmount()
#else
		0
#endif
	);
	return 0;
}

static int hlp_sz_time(void)
{
	printf("%zu\n", sizeof(time_t));
	return 0;
}

static int hlp_get_nstype_ok(void)
{
#ifdef USE_NS_GET_NSTYPE
	int fd = open("/proc/self/ns/mnt", O_RDONLY);

	errno = 0;
	if (fd >= 0) {
		int errsv = 0;

		/* Check for actual usability */
		if (ioctl(fd, NS_GET_NSTYPE) != CLONE_NEWNS)
			errsv = ENOSYS;
		close(fd);
		errno = errsv;
	} else {
		/* Generic check for ENOSYS only */
		ioctl(STDOUT_FILENO, NS_GET_NSTYPE);
	}
#else
	errno = ENOSYS;
#endif
	printf("%d\n", errno != ENOSYS);
	return 0;
}

static int hlp_get_userns_ok(void)
{
#ifdef USE_NS_GET_USERNS
	errno = 0;
	ioctl(STDOUT_FILENO, NS_GET_USERNS);
#else
	errno = ENOSYS;
#endif
	printf("%d\n", errno != ENOSYS);
	return 0;
}

static int hlp_hostname(void)
{
	char * h = xgethostname();
	printf("%s\n", h);
	free(h);
	return 0;
}

static int hlp_fts(void)
{
#ifdef HAVE_FTS_OPEN
	char template[NAME_MAX];
	char *paths[] = { NULL, NULL };
	char path[PATH_MAX];
	FTSENT *node;
	FTS *fts;
	int r;
	int files = 0, dirs = 0;

	snprintf(template, sizeof(template), "%s/fts_checkXXXXXX", getenv("TMPDIR") ?: "/tmp");

	paths[0] = mkdtemp(template);
	if (paths[0] == NULL)
		return 0;

	r = mkdir(template, 0755);
	if (r < 0 && errno != EEXIST)
		return 0;
	dirs++;

	snprintf(path, sizeof(path), "%s/subdir", template);
	r = mkdir(path, 0755);
	if (r < 0 && errno != EEXIST)
		return 0;
	dirs++;

	snprintf(path, sizeof(path), "%s/file1.txt", template);
	r = creat(path, 0644);
	if (r < 0)
		return 0;
	close(r);
	files++;

	snprintf(path, sizeof(path), "%s/file2.txt", template);
	r = creat(path, 0644);
	if (r < 0)
		return 0;
	close(r);
	files++;

	snprintf(path, sizeof(path), "%s/subdir/file3.txt", template);
	r = creat(path, 0644);
	if (r < 0)
		return 0;
	close(r);
	files++;

	snprintf(path, sizeof(path), "%s/subdir/file4.txt", template);
	r = creat(path, 0644);
	if (r < 0)
		return 0;
	close(r);
	files++;

	fts = fts_open(paths, FTS_NOCHDIR | FTS_PHYSICAL, NULL);
	if (fts == NULL)
		return 0;

	while ((node = fts_read(fts)) != NULL) {
		switch (node->fts_info) {
		case FTS_F:
			files--;
			break;
		case FTS_D:
			dirs--;
			break;
		case FTS_ERR:
			return 0;
		default:
			break;
		}
	}

	if (fts_close(fts) < 0 || files != 0 || dirs != 0)
		return 0;

	puts("FTS");
#endif
	return 0;
}

static const mntHlpfnc hlps[] =
{
	{ "WORDSIZE",	hlp_wordsize	},
	{ "pagesize",	hlp_pagesize	},
	{ "INT_MAX",	hlp_int_max	},
	{ "UINT_MAX",   hlp_uint_max	},
	{ "LONG_MAX",   hlp_long_max	},
	{ "ULONG_MAX",  hlp_ulong_max	},
	{ "ULONG_MAX32",hlp_ulong_max32	},
	{ "UINT64_MAX", hlp_u64_max     },
	{ "byte-order", hlp_endianness  },
	{ "wcsspn-ok",  hlp_wcsspn_ok   },
	{ "enotty-ok",  hlp_enotty_ok   },
	{ "fsopen-ok",  hlp_fsopen_ok   },
	{ "statmount-ok", hlp_statmount_ok },
	{ "listmount-ok", hlp_listmount_ok },
	{ "sz(time_t)", hlp_sz_time     },
	{ "ns-gettype-ok", hlp_get_nstype_ok },
	{ "ns-getuserns-ok", hlp_get_userns_ok },
	{ "hostname", hlp_hostname, },
	{ "fts", hlp_fts, },
	{ NULL, NULL }
};

int main(int argc, char **argv)
{
	int re = 0;
	const mntHlpfnc *fn;

	if (argc == 1) {
		for (fn = hlps; fn->name; fn++) {
			printf("%15s: ", fn->name);
			re += fn->fnc();
		}
	} else {
		int i;

		if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
			printf("%s <option>\n", argv[0]);
			fputs("options:\n", stdout);
			for (fn = hlps; fn->name; fn++)
				printf("\t%s\n", fn->name);
			exit(EXIT_SUCCESS);
		}

		for (i=1; i < argc; i++) {
			for (fn = hlps; fn->name; fn++) {
				if (strcmp(fn->name, argv[i]) == 0)
					re += fn->fnc();
			}
		}
	}

	exit(re ? EXIT_FAILURE : EXIT_SUCCESS);
}
