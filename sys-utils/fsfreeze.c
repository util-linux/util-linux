/*
 * fsfreeze.c -- Filesystem freeze/unfreeze IO for Linux
 *
 * Copyright (C) 2010 Hajime Taira <htaira@redhat.com>
 *                    Masatake Yamato <yamato@redhat.com>
 *
 * This program is free software.  You can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation: either version 1 or
 * (at your option) any later version.
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <linux/fs.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <getopt.h>

#include "c.h"
#include "blkdev.h"
#include "nls.h"
#include "closestream.h"
#include "optutils.h"

enum fs_operation {
	NOOP,
	FREEZE,
	UNFREEZE
};

static int freeze_f(int fd)
{
	return ioctl(fd, FIFREEZE, 0);
}

static int unfreeze_f(int fd)
{
	return ioctl(fd, FITHAW, 0);
}

static void __attribute__((__noreturn__)) usage(FILE *out)
{
	fprintf(out, USAGE_HEADER);
	fprintf(out,
	      _(" %s [options] <mountpoint>\n"), program_invocation_short_name);
	fputs(USAGE_OPTIONS, out);
	fputs(_(" -f, --freeze      freeze the filesystem\n"), out);
	fputs(_(" -u, --unfreeze    unfreeze the filesystem\n"), out);
	fprintf(out, USAGE_SEPARATOR);
	fprintf(out, USAGE_HELP);
	fprintf(out, USAGE_VERSION);
	fprintf(out, USAGE_MAN_TAIL("fsfreeze(8)"));

	exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}

int main(int argc, char **argv)
{
	int fd = -1, c;
	int action = NOOP, rc = EXIT_FAILURE;
	char *path;
	struct stat sb;

	static const struct option longopts[] = {
	    { "help",      0, 0, 'h' },
	    { "freeze",    0, 0, 'f' },
	    { "unfreeze",  0, 0, 'u' },
	    { "version",   0, 0, 'V' },
	    { NULL,        0, 0, 0 }
	};

	static const ul_excl_t excl[] = {       /* rows and cols in in ASCII order */
		{ 'f','u' },			/* freeze, unfreeze */
		{ 0 }
	};
	int excl_st[ARRAY_SIZE(excl)] = UL_EXCL_STATUS_INIT;

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	while ((c = getopt_long(argc, argv, "hfuV", longopts, NULL)) != -1) {

		err_exclusive_options(c, longopts, excl, excl_st);

		switch(c) {
		case 'h':
			usage(stdout);
			break;
		case 'f':
			action = FREEZE;
			break;
		case 'u':
			action = UNFREEZE;
			break;
		case 'V':
			printf(UTIL_LINUX_VERSION);
			exit(EXIT_SUCCESS);
		default:
			usage(stderr);
			break;
		}
	}

	if (action == NOOP)
		errx(EXIT_FAILURE, _("neither --freeze or --unfreeze specified"));
	if (optind == argc)
		errx(EXIT_FAILURE, _("no filename specified"));
	path = argv[optind++];

	if (optind != argc) {
		warnx(_("unexpected number of arguments"));
		usage(stderr);
	}

	fd = open(path, O_RDONLY);
	if (fd < 0)
		err(EXIT_FAILURE, _("cannot open %s"), path);

	if (fstat(fd, &sb) == -1) {
		warn(_("stat failed %s"), path);
		goto done;
	}

	if (!S_ISDIR(sb.st_mode)) {
		warnx(_("%s: is not a directory"), path);
		goto done;
	}

	switch (action) {
	case FREEZE:
		if (freeze_f(fd)) {
			warn(_("%s: freeze failed"), path);
			goto done;
		}
		break;
	case UNFREEZE:
		if (unfreeze_f(fd)) {
			warn(_("%s: unfreeze failed"), path);
			goto done;
		}
		break;
	default:
		abort();
	}

	rc = EXIT_SUCCESS;
done:
	if (fd >= 0)
		close(fd);
	return rc;
}

