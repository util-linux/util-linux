/*
 * Copyright (C) 2019 zhenwei pi <pizhenwei@bytedance.com>
 *
 * This file may be redistributed under the terms of the GNU Public
 * License.
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <getopt.h>
#include <sys/ioctl.h>
#include <linux/fs.h>

#include "c.h"
#include "nls.h"
#include "closestream.h"

#ifdef FS_IOC_FIEMAP
#include <linux/fiemap.h>

static struct fiemap *read_fiemap(int fd)
{
	struct fiemap *fiemap;
	int extents_size;

	fiemap = (struct fiemap *)malloc(sizeof(struct fiemap));
	if (fiemap == NULL)
		err(EXIT_FAILURE, _("malloc"));

	memset(fiemap, 0, sizeof(struct fiemap));

	fiemap->fm_start = 0;
	fiemap->fm_length = ~0;
	fiemap->fm_flags = 0;
	fiemap->fm_extent_count = 0;
	fiemap->fm_mapped_extents = 0;

	/* count how many extents there are */
	if (ioctl(fd, FS_IOC_FIEMAP, fiemap) < 0)
		err(EXIT_FAILURE, _("fiemap ioctl() failed"));

	/* read in the extents */
	extents_size = sizeof(struct fiemap_extent) *
		(fiemap->fm_mapped_extents);

	/* resize fiemaps for all extents */
	fiemap = (struct fiemap *)realloc(fiemap, sizeof(struct fiemap) +
			extents_size);
	if (fiemap == NULL)
		err(EXIT_FAILURE, _("realloc for extents memory"));

	memset(fiemap->fm_extents, 0, extents_size);
	fiemap->fm_extent_count = fiemap->fm_mapped_extents;
	fiemap->fm_mapped_extents = 0;

	if (ioctl(fd, FS_IOC_FIEMAP, fiemap) < 0) {
		err(EXIT_FAILURE, _("ioctl() FS_IOC_FIEMAP failed"));
		return NULL;
	}

	return fiemap;
}

static void show_fiemap(struct fiemap *fiemap, char *filename)
{
	unsigned int i = 0;

	printf("File %s has %d extent(s):\n", filename,
			fiemap->fm_mapped_extents);
	printf("#\tLogical          Physical         Length           Flag\n");
	for (i = 0; i < fiemap->fm_mapped_extents; i++) {
		printf("%d:\t%-16.16llx %-16.16llx %-16.16llx %-4.4x\n", i,
				fiemap->fm_extents[i].fe_logical,
				fiemap->fm_extents[i].fe_physical,
				fiemap->fm_extents[i].fe_length,
				fiemap->fm_extents[i].fe_flags);
	}

	printf("\n");
}

static void __attribute__((__noreturn__)) usage(void)
{
	FILE *out = stdout;

	fputs(USAGE_HEADER, out);
	fprintf(out, _(" %s <file>...\n"), program_invocation_short_name);

	fputs(USAGE_SEPARATOR, out);
	exit(EXIT_SUCCESS);
}

int main(int argc, char **argv)
{
	int c;
	static const struct option longopts[] = {
		{ "version",    no_argument, NULL, 'V' },
		{ "help",       no_argument, NULL, 'h' },
		{ NULL, 0, NULL, 0 },
	};

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	while ((c = getopt_long (argc, argv, "Vh", longopts, NULL)) != -1) {
		switch (c) {
		case 'V':
			printf(UTIL_LINUX_VERSION);
			return EXIT_SUCCESS;

		case 'h':
			usage();

		default:
			errtryhelp(EXIT_FAILURE);
		}
	}

	if (optind == argc) {
		warnx(_("no file specified"));
		errtryhelp(EXIT_FAILURE);
	}

	for ( ; optind < argc; optind++) {
		int fd = 0;

		fd = open(argv[optind], O_RDONLY);
		if (fd < 0) {
			err(EXIT_FAILURE, _("open file failed"));
		} else {
			struct fiemap *fiemap = NULL;

			fiemap = read_fiemap(fd);
			if (fiemap != NULL) {
				show_fiemap(fiemap, argv[optind]);
				free(fiemap);
			}
			close(fd);
		}
	}

	return 0;
}
#else
int main(int argc, char **argv)
{
	err(EXIT_FAILURE, _("FS_IOC_FIEMAP not supported"));

	return 0;
}
#endif
