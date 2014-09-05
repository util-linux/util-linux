/*
 * Copyright (C) 1995  Andries E. Brouwer (aeb@cwi.nl)
 * Copyright (C) 2014 Karel Zak <kzak@redhat.com>
 *
 * This program is free software. You can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation: either Version 1
 * or (at your option) any later version.
 *
 * A.V. Le Blanc (LeBlanc@mcc.ac.uk) wrote Linux fdisk 1992-1994,
 * patched by various people (faith@cs.unc.edu, martin@cs.unc.edu,
 * leisner@sdsp.mc.xerox.com, esr@snark.thyrsus.com, aeb@cwi.nl)
 * 1993-1995, with version numbers (as far as I have seen) 0.93 - 2.0e.
 * This program had (head,sector,cylinder) as basic unit, and was
 * (therefore) broken in several ways for the use on larger disks -
 * for example, my last patch (from 2.0d to 2.0e) was required
 * to allow a partition to cross cylinder 8064, and to write an
 * extended partition past the 4GB mark.
 *
 * Karel Zak wrote new sfdisk based on libfdisk from util-linux
 * in 2014.
 */

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <sys/stat.h>
#include <assert.h>

#include "c.h"
#include "xalloc.h"
#include "nls.h"
#include "debug.h"
#include "strutils.h"
#include "closestream.h"
#include "colors.h"
#include "blkdev.h"

#include "libfdisk.h"
#include "fdisk-list.h"

/*
 * sfdisk debug stuff (see fdisk.h and include/debug.h)
 */
UL_DEBUG_DEFINE_MASK(sfdisk);
UL_DEBUG_DEFINE_MASKANEMS(sfdisk) = UL_DEBUG_EMPTY_MASKNAMES;

#define SFDISKPROG_DEBUG_INIT	(1 << 1)
#define SFDISKPROG_DEBUG_PARSE	(1 << 2)
#define SFDISKPROG_DEBUG_MISC	(1 << 3)
#define SFDISKPROG_DEBUG_ALL	0xFFFF

#define DBG(m, x)       __UL_DBG(sfdisk, SFDISKPROG_DEBUG_, m, x)
#define ON_DBG(m, x)    __UL_DBG_CALL(sfdisk, SFDISKPROG_DEBUG_, m, x)

enum {
	ACT_FDISK = 0,		/* default */

	ACT_ACTIVATE,
	ACT_CHANGE_ID,
	ACT_DUMP,
	ACT_LIST,
	ACT_LIST_TYPES,
	ACT_SHOW_SIZE,
	ACT_VERIFY
};

struct sfdisk {
	int		act;		/* action */
	size_t		partno;		/* partition number <1..N> */

	struct fdisk_context	*cxt;	/* libfdisk context */
};


static void sfdiskprog_init_debug(void)
{
	__UL_INIT_DEBUG(sfdisk, SFDISKPROG_DEBUG_, 0, SFDISK_DEBUG);
}

static int ask_callback(struct fdisk_context *cxt, struct fdisk_ask *ask,
		    void *data __attribute__((__unused__)))
{
	assert(cxt);
	assert(ask);

	switch(fdisk_ask_get_type(ask)) {
	case FDISK_ASKTYPE_INFO:
		fputs(fdisk_ask_print_get_mesg(ask), stdout);
		fputc('\n', stdout);
		break;
	case FDISK_ASKTYPE_WARNX:
		color_scheme_fenable("warn", UL_COLOR_RED, stderr);
		fputs(fdisk_ask_print_get_mesg(ask), stderr);
		color_fdisable(stderr);
		fputc('\n', stderr);
		break;
	case FDISK_ASKTYPE_WARN:
		color_scheme_fenable("warn", UL_COLOR_RED, stderr);
		fputs(fdisk_ask_print_get_mesg(ask), stderr);
		errno = fdisk_ask_print_get_errno(ask);
		fprintf(stderr, ": %m\n");
		color_fdisable(stderr);
		break;
	default:
		break;
	}
	return 0;
}

static void sfdisk_init(struct sfdisk *sf)
{
	fdisk_init_debug(0);
	sfdiskprog_init_debug();

	colors_init(UL_COLORMODE_UNDEF, "sfdisk");

	sf->cxt = fdisk_new_context();
	if (!sf->cxt)
		err(EXIT_FAILURE, _("failed to allocate libfdisk context"));
	fdisk_set_ask(sf->cxt, ask_callback, NULL);
}

static int sfdisk_deinit(struct sfdisk *sf)
{
	int rc;

	assert(sf);
	assert(sf->cxt);

	fdisk_unref_context(sf->cxt);
	memset(sf, 0, sizeof(*sf));

	return rc;
}

static int command_list_partitions(struct sfdisk *sf, int argc, char **argv)
{
	fdisk_enable_listonly(sf->cxt, 1);

	if (argc) {
		int i, ct = 0;

		for (i = 0; i < argc; i++) {
			if (ct)
				fputs("\n\n", stdout);
			if (print_device_pt(sf->cxt, argv[i], 0) == 0)
				ct++;
		}
	} else
		print_all_devices_pt(sf->cxt);

	return 0;
}

static int rdonly_open(const char *dev, int silent)
{
	int fd = open(dev, O_RDONLY);
	if (fd < 0) {
		if (!silent)
			warn(_("cannot open: %s"), dev);
		return -errno;
	}
	return fd;
}

static int get_size(const char *dev, int silent, uintmax_t *sz)
{
	int fd, rc = 0;

	fd = rdonly_open(dev, silent);
	if (fd < 0)
		return -errno;

	if (blkdev_get_sectors(fd, (unsigned long long *) sz) == -1) {
		if (!silent)
			warn(_("Cannot get size of %s"), dev);
		rc = -errno;
	}

	close(fd);
	return rc;
}

static int command_show_size(struct sfdisk *sf __attribute__((__unused__)),
			     int argc, char **argv)
{
	uintmax_t sz;

	if (argc) {
		int i;
		for (i = 0; i < argc; i++) {
			if (get_size(argv[i], 0, &sz) == 0)
				printf("%ju\n", sz / 2);
		}
	} else {
		FILE *f = NULL;
		uintmax_t total = 0;
		char *dev;

		while ((dev = next_proc_partition(&f))) {
			if (get_size(dev, 1, &sz) == 0) {
				printf("%s: %9ju\n", dev, sz / 2);
				total += sz / 2;
			}
			free(dev);
		}
		if (total)
			printf(_("total: %ju blocks\n"), total);
	}

	return 0;
}

static int command_dump(struct sfdisk *sf, int argc, char **argv)
{
	const char *devname = NULL;
	struct fdisk_script *dp;
	int rc;

	if (argc)
		devname = argv[0];
	if (!devname)
		errx(EXIT_FAILURE, _("no disk device specified"));

	rc = fdisk_assign_device(sf->cxt, devname, 1);
	if (rc)
		err(EXIT_FAILURE, _("cannot open %s"), devname);

	dp = fdisk_new_script(sf->cxt);
	if (!dp)
		err(EXIT_FAILURE, _("failed to allocate dump struct"));

	rc = fdisk_script_read_context(dp, NULL);
	if (rc)
		err(EXIT_FAILURE, _("failed to dump partition table"));

	fdisk_script_write_file(dp, stdout);

	fdisk_unref_script(dp);
	return 0;
}

/* default command */
static int command_fdisk(struct sfdisk *sf, int argc, char **argv)
{
	int rc;
	const char *devname = NULL;

	if (argc)
		devname = argv[0];
	if (argc > 1)
		sf->partno = strtou32_or_err(argv[1],
				_("failed to parse partition number"));
	if (!devname)
		errx(EXIT_FAILURE, _("no disk device specified"));

	rc = fdisk_assign_device(sf->cxt, devname, 0);
	if (rc)
		err(EXIT_FAILURE, _("cannot open %s"), devname);

	fdisk_deassign_device(sf->cxt, 1);
	return rc;
}

static void __attribute__ ((__noreturn__)) usage(FILE *out)
{
	fputs(USAGE_HEADER, out);

	fprintf(out,
	      _(" %1$s [options] <disk>\n"
		" %1$s [options] --list <disk> [...]\n"),
	      program_invocation_short_name);

	fputs(USAGE_OPTIONS, out);
	fputs(_(" -d, --dump           dump partition table (suitable for later input)\n"), out);
	fputs(_(" -l, --list           list partitions of each device\n"), out);

	fputs(USAGE_SEPARATOR, out);
	fputs(USAGE_HELP, out);
	fputs(USAGE_VERSION, out);

	fprintf(out, USAGE_MAN_TAIL("sfdisk(8)"));
	exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}



int main(int argc, char *argv[])
{
	struct sfdisk _sf = { .partno = 0 }, *sf  = &_sf;
	int rc = -EINVAL, c;

	static const struct option longopts[] = {
		{ "list",    no_argument,       NULL, 'l' },
		{ "dump",    no_argument,	NULL, 'd' },
		{ "help",    no_argument,       NULL, 'h' },
		{ "show-size", no_argument,	NULL, 's' },
		{ "version", no_argument,       NULL, 'v' },
		{ NULL, 0, 0, 0 },
	};

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	while ((c = getopt_long(argc, argv, "dhlsv", longopts, NULL)) != -1) {
		switch(c) {
		case 'h':
			usage(stdout);
			break;
		case 'l':
			sf->act = ACT_LIST;
			break;
		case 'd':
			sf->act = ACT_DUMP;
			break;
		case 's':
			sf->act = ACT_SHOW_SIZE;
			break;
		case 'v':
			printf(_("%s from %s\n"), program_invocation_short_name,
						  PACKAGE_STRING);
			return EXIT_SUCCESS;
		}
	}

	sfdisk_init(sf);

	switch (sf->act) {
	case ACT_LIST:
		rc = command_list_partitions(sf, argc - optind, argv + optind);
		break;

	case ACT_FDISK:
		rc = command_fdisk(sf, argc - optind, argv + optind);
		break;

	case ACT_DUMP:
		rc = command_dump(sf, argc - optind, argv + optind);
		break;

	case ACT_SHOW_SIZE:
		rc = command_show_size(sf, argc - optind, argv + optind);
		break;
	}

	sfdisk_deinit(sf);

	DBG(MISC, ul_debug("bye! [rc=%d]", rc));
	return rc == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

