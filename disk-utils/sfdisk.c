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
	int		partno;		/* -N <partno>, default -1 */
	const char	*label;		/* --label <label> */

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

/*
 * sfdisk --list [<device ..]
 */
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

static int get_size(const char *dev, int silent, uintmax_t *sz)
{
	int fd, rc = 0;

	fd = open(dev, O_RDONLY);
	if (fd < 0) {
		if (!silent)
			warn(_("cannot open: %s"), dev);
		return -errno;
	}

	if (blkdev_get_sectors(fd, (unsigned long long *) sz) == -1) {
		if (!silent)
			warn(_("Cannot get size of %s"), dev);
		rc = -errno;
	}

	close(fd);
	return rc;
}

/*
 * sfdisk --show-size [<device ..]
 *
 * (silly, but just for backward compatibility)
 */
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

/*
 * sfdisk --activate <device> [<partno> ...]
 */
static int command_activate(struct sfdisk *sf, int argc, char **argv)
{
	int rc, nparts, i;
	struct fdisk_partition *pa = NULL;
	const char *devname = NULL;

	if (argc)
		devname = argv[0];
	else
		errx(EXIT_FAILURE, _("no disk device specified"));
	if (argc > 2)
		errx(EXIT_FAILURE, _("uneexpected arguments"));

	rc = fdisk_assign_device(sf->cxt, devname, 0);
	if (rc)
		err(EXIT_FAILURE, _("cannot open %s"), devname);

	if (!fdisk_is_label(sf->cxt, DOS))
		errx(EXIT_FAILURE, _("toggle boot flags is supported for MBR only"));

	nparts = fdisk_get_npartitions(sf->cxt);
	for (i = 0; i < nparts; i++) {
		char *data = NULL;

		/* note that fdisk_get_partition() reuses the @pa pointer, you
		 * don't have to (re)allocate it */
		if (fdisk_get_partition(sf->cxt, i, &pa) != 0)
			continue;

		/* sfdisk --activate  list bootable partitions */
		if (argc == 1) {
			if (!fdisk_partition_is_bootable(pa))
				continue;
			if (fdisk_partition_to_string(pa, sf->cxt,
						FDISK_FIELD_DEVICE, &data) == 0) {
				printf("%s\n", data);
				free(data);
			}

		/* deactivate all active partitions */
		} else if (fdisk_partition_is_bootable(pa))
			fdisk_partition_toggle_flag(sf->cxt, i, DOS_FLAG_ACTIVE);
	}

	/* sfdisk --activate <partno> [..] */
	for (i = 1; i < argc; i++) {
		int n = strtou32_or_err(argv[i], _("failed to parse partition number"));

		rc = fdisk_partition_toggle_flag(sf->cxt, n - 1, DOS_FLAG_ACTIVE);
		if (rc)
			errx(EXIT_FAILURE,
				_("%s: #%d: failed to toggle bootable flag"),
				devname, i + 1);
	}

	fdisk_unref_partition(pa);
	rc = fdisk_write_disklabel(sf->cxt);
	if (!rc)
		rc = fdisk_deassign_device(sf->cxt, 1);
	return rc;
}

/*
 * sfdisk --dump <device>
 */
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


static void sfdisk_print_partition(struct sfdisk *sf, int n)
{
	struct fdisk_partition *pa;
	char *data;

	assert(sf);

	if (fdisk_get_partition(sf->cxt, n, &pa) != 0)
		return;

	fdisk_partition_to_string(pa, sf->cxt, FDISK_FIELD_DEVICE, &data);
	printf("%12s : ", data);

	fdisk_partition_to_string(pa, sf->cxt, FDISK_FIELD_START, &data);
	printf("%12s ", data);

	fdisk_partition_to_string(pa, sf->cxt, FDISK_FIELD_END, &data);
	printf("%12s ", data);

	fdisk_partition_to_string(pa, sf->cxt, FDISK_FIELD_SIZE, &data);
	printf("(%s) ", data);

	fdisk_partition_to_string(pa, sf->cxt, FDISK_FIELD_TYPE, &data);
	printf("%s\n", data);

	fdisk_unref_partition(pa);
}

/*
 * sfdisk <device> [[-N] <partno>]
 *
 * Note that the option -N is there for backward compatibility only.
 */
static int command_fdisk(struct sfdisk *sf, int argc, char **argv)
{
	int rc = 0, partno = sf->partno, created = 0;
	struct fdisk_script *dp;
	struct fdisk_table *tb = NULL;
	const char *devname = NULL, *label;
	char buf[BUFSIZ];

	if (argc)
		devname = argv[0];
	if (partno < 0 && argc > 1)
		partno = strtou32_or_err(argv[1],
				_("failed to parse partition number"));
	if (!devname)
		errx(EXIT_FAILURE, _("no disk device specified"));

	dp = fdisk_new_script(sf->cxt);
	if (!dp)
		err(EXIT_FAILURE, _("failed to allocate script handler"));

	rc = fdisk_assign_device(sf->cxt, devname, 0);
	if (rc)
		err(EXIT_FAILURE, _("cannot open %s"), devname);

	list_disk_geometry(sf->cxt);
	printf(_("\nOld situation:"));
	list_disklabel(sf->cxt);

	if (sf->label)
		label = sf->label;
	else if (fdisk_has_label(sf->cxt))
		label = fdisk_label_get_name(fdisk_get_label(sf->cxt, NULL));
	else
		label = "dos";	/* just for backward compatibility */

	fdisk_script_set_header(dp, "label", label);

	printf(_("\nInput in the following format; absent fields get a default value.\n"
		 "<start> <size> <type [uuid, hex or E,S,L,X]> <bootable [-,*]>\n"
		 "Usually you only need to specify <start> and <size>\n"));

	if (!fdisk_has_label(sf->cxt))
		printf(_("\nsfdisk is going to create a new '%s' disk label.\n"
			 "Use 'label: <name>' before you define a first partition\n"
			 "to override the default.\n"), label);

	tb = fdisk_script_get_table(dp);

	do {
		size_t nparts = fdisk_table_get_nents(tb);
		char *partname;

		partname = fdisk_partname(devname, nparts + 1);
		if (!partname)
			err(EXIT_FAILURE, _("failed to allocate partition name"));
		printf("%s: ", partname);
		free(partname);

		rc = fdisk_script_read_line(dp, stdin, buf, sizeof(buf));
		if (rc) {
			buf[sizeof(buf) - 1] = '\0';

			if (strcmp(buf, "print") == 0)
				list_disklabel(sf->cxt);
			else if (strcmp(buf, "quit") == 0)
				break;
			else if (strcmp(buf, "abort") == 0) {
				rc = -1;
				break;
			} else
				fdisk_warnx(sf->cxt, _("unsupported command"));
			continue;
		}

		nparts = fdisk_table_get_nents(tb);
		if (nparts) {
			struct fdisk_partition *pa = fdisk_table_get_partition(tb, nparts - 1);

			if (!created) {		/* create a disklabel */
				rc = fdisk_apply_script_headers(sf->cxt, dp);
				created = !rc;
			}
			if (!rc)		/* cretate partition */
				rc = fdisk_add_partition(sf->cxt, pa);

			if (!rc)		/* sucess, print reult */
				sfdisk_print_partition(sf, nparts - 1);
			else if (pa)		/* error, drop partition from script */
				fdisk_table_remove_partition(tb, pa);
		} else
			printf(_("OK\n"));	/* probably added script header */
	} while (1);


	if (!rc) {
		printf(_("\nNew situation:"));
		list_disklabel(sf->cxt);
	}
	if (!rc)
		rc = fdisk_write_disklabel(sf->cxt);
	if (!rc)
		rc = fdisk_deassign_device(sf->cxt, 1);

	fdisk_unref_script(dp);
	return rc;
}

static void __attribute__ ((__noreturn__)) usage(FILE *out)
{
	fputs(USAGE_HEADER, out);

	fprintf(out,
	      _(" %1$s [options] --dump <device>\n"
		" %1$s [options] --list [<device> ...]\n"
		" %1$s [options] --activate <device> [<partno> ...]\n"),
	      program_invocation_short_name);

	fputs(USAGE_OPTIONS, out);
	fputs(_(" -a, --activate       mark MBR partitions as bootable\n"), out);
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
	int rc = -EINVAL, c;
	struct sfdisk _sf = {
		.act = 0,
		.partno = -1
	}, *sf = &_sf;

	static const struct option longopts[] = {
		{ "activate",no_argument,	NULL, 'a' },
		{ "list",    no_argument,       NULL, 'l' },
		{ "dump",    no_argument,	NULL, 'd' },
		{ "help",    no_argument,       NULL, 'h' },
		{ "show-size", no_argument,	NULL, 's' },
		{ "partno",  required_argument, NULL, 'N' },
		{ "label",   required_argument, NULL, 'X' },
		{ "version", no_argument,       NULL, 'v' },
		{ NULL, 0, 0, 0 },
	};

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	while ((c = getopt_long(argc, argv, "adhlN:svX:", longopts, NULL)) != -1) {
		switch(c) {
		case 'a':
			sf->act = ACT_ACTIVATE;
			break;
		case 'h':
			usage(stdout);
			break;
		case 'l':
			sf->act = ACT_LIST;
			break;
		case 'd':
			sf->act = ACT_DUMP;
			break;
		case 'N':
			sf->partno = strtou32_or_err(optarg, _("failed to parse partition number"));
			break;
		case 'X':
			sf->label = optarg;
			break;
		case 's':
			sf->act = ACT_SHOW_SIZE;
			break;
		case 'v':
			printf(_("%s from %s\n"), program_invocation_short_name,
						  PACKAGE_STRING);
			return EXIT_SUCCESS;
		default:
			usage(stderr);
		}
	}

	sfdisk_init(sf);

	switch (sf->act) {
	case ACT_ACTIVATE:
		rc = command_activate(sf, argc - optind, argv + optind);
		break;

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

