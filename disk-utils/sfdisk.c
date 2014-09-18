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
#define SFDISKPROG_DEBUG_ASK	(1 << 4)
#define SFDISKPROG_DEBUG_ALL	0xFFFF

#define DBG(m, x)       __UL_DBG(sfdisk, SFDISKPROG_DEBUG_, m, x)
#define ON_DBG(m, x)    __UL_DBG_CALL(sfdisk, SFDISKPROG_DEBUG_, m, x)

enum {
	ACT_FDISK = 1,
	ACT_ACTIVATE,
	ACT_CHANGE_ID,
	ACT_DUMP,
	ACT_LIST,
	ACT_LIST_TYPES,
	ACT_SHOW_SIZE,
	ACT_SHOW_GEOM,
	ACT_VERIFY,
	ACT_PARTTYPE,
};

struct sfdisk {
	int		act;		/* action */
	int		partno;		/* -N <partno>, default -1 */
	const char	*label;		/* --label <label> */

	struct fdisk_context	*cxt;	/* libfdisk context */

	unsigned int verify : 1,	/* call fdisk_verify_disklabel() */
		     quiet : 1;		/* suppres extra messages */
};


static void sfdiskprog_init_debug(void)
{
	__UL_INIT_DEBUG(sfdisk, SFDISKPROG_DEBUG_, 0, SFDISK_DEBUG);
}


static int get_user_reply(const char *prompt, char *buf, size_t bufsz)
{
	char *p;
	size_t sz;

	fputs(prompt, stdout);
	fflush(stdout);

	if (!fgets(buf, bufsz, stdin))
		return 1;

	for (p = buf; *p && !isgraph(*p); p++);	/* get first non-blank */

	if (p > buf)
		memmove(buf, p, p - buf);	/* remove blank space */
	sz = strlen(buf);
	if (sz && *(buf + sz - 1) == '\n')
		*(buf + sz - 1) = '\0';

	DBG(ASK, ul_debug("user's reply: >>>%s<<<", buf));
	return 0;
}

static int ask_callback(struct fdisk_context *cxt,
			struct fdisk_ask *ask,
			void *data)
{
	struct sfdisk *sf = (struct sfdisk *) data;
	int rc = 0;

	assert(cxt);
	assert(ask);

	switch(fdisk_ask_get_type(ask)) {
	case FDISK_ASKTYPE_INFO:
		if (sf->quiet)
			break;
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
	case FDISK_ASKTYPE_YESNO:
	{
		char buf[BUFSIZ];
		fputc('\n', stdout);
		do {
			int x;
			fputs(fdisk_ask_get_query(ask), stdout);
			rc = get_user_reply(_(" [Y]es/[N]o: "), buf, sizeof(buf));
			if (rc)
				break;
			x = rpmatch(buf);
			if (x == 1 || x == 0) {
				fdisk_ask_yesno_set_result(ask, x);
				break;
			}
		} while(1);
		DBG(ASK, ul_debug("yes-no ask: reply '%s' [rc=%d]", buf, rc));
		break;
	}
	default:
		break;
	}
	return rc;
}

static void sfdisk_init(struct sfdisk *sf)
{
	fdisk_init_debug(0);
	sfdiskprog_init_debug();

	colors_init(UL_COLORMODE_UNDEF, "sfdisk");

	sf->cxt = fdisk_new_context();
	if (!sf->cxt)
		err(EXIT_FAILURE, _("failed to allocate libfdisk context"));
	fdisk_set_ask(sf->cxt, ask_callback, (void *) sf);
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
			if (print_device_pt(sf->cxt, argv[i], 0, sf->verify) == 0)
				ct++;
		}
	} else
		print_all_devices_pt(sf->cxt, sf->verify);

	return 0;
}

/*
 * sfdisk --list-types
 */
static int command_list_types(struct sfdisk *sf)
{
	const struct fdisk_parttype *t;
	struct fdisk_label *lb;
	const char *name;
	size_t i = 0;
	int codes;

	assert(sf);
	assert(sf->cxt);

	name = sf->label ? sf->label : "dos";
	lb = fdisk_get_label(sf->cxt, name);
	if (!lb)
		errx(EXIT_FAILURE, _("unsupported label '%s'"), name);

	codes = fdisk_label_has_code_parttypes(lb);
	fputs(_("Id  Name\n\n"), stdout);

	while ((t = fdisk_label_get_parttype(lb, i++))) {
		if (codes)
			printf("%2x  %s\n", fdisk_parttype_get_code(t),
					   fdisk_parttype_get_name(t));
		else
			printf("%s  %s\n", fdisk_parttype_get_string(t),
					  fdisk_parttype_get_name(t));
	}

	return 0;
}

static int verify_device(struct sfdisk *sf, const char *devname)
{
	int rc = 1;

	fdisk_enable_listonly(sf->cxt, 1);

	if (fdisk_assign_device(sf->cxt, devname, 1)) {
		warn(_("cannot open: %s"), devname);
		return 1;
	}

	color_scheme_enable("header", UL_COLOR_BOLD);
	fdisk_info(sf->cxt, "%s:", devname);
	color_disable();

	if (!fdisk_has_label(sf->cxt))
		fdisk_info(sf->cxt, _("unrecognized partition table type"));
	else
		rc = fdisk_verify_disklabel(sf->cxt);

	fdisk_deassign_device(sf->cxt, 1);
	return rc;
}

/*
 * sfdisk --verify [<device ..]
 */
static int command_verify(struct sfdisk *sf, int argc, char **argv)
{
	int nfails = 0, ct = 0;

	if (argc) {
		int i;
		for (i = 0; i < argc; i++) {
			if (i)
				fdisk_info(sf->cxt, " ");
			if (verify_device(sf, argv[i]) < 0)
				nfails++;
		}
	} else {
		FILE *f = NULL;
		char *dev;

		while ((dev = next_proc_partition(&f))) {
			if (ct)
				fdisk_info(sf->cxt, " ");
			if (verify_device(sf, dev) < 0)
				nfails++;
			free(dev);
			ct++;
		}
	}

	return nfails;
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

static int print_geom(struct sfdisk *sf, const char *devname)
{
	fdisk_enable_listonly(sf->cxt, 1);

	if (fdisk_assign_device(sf->cxt, devname, 1)) {
		warn(_("cannot open: %s"), devname);
		return 1;
	}

	fdisk_info(sf->cxt, "%s: %ju cylinders, %ju heads, %ju sectors/track",
			devname,
			(uintmax_t) fdisk_get_geom_cylinders(sf->cxt),
			(uintmax_t) fdisk_get_geom_heads(sf->cxt),
			(uintmax_t) fdisk_get_geom_sectors(sf->cxt));

	fdisk_deassign_device(sf->cxt, 1);
	return 0;
}

/*
 * sfdisk --show-geometry [<device ..]
 */
static int command_show_geometry(struct sfdisk *sf, int argc, char **argv)
{
	int nfails = 0;

	if (argc) {
		int i;
		for (i = 0; i < argc; i++) {
			if (print_geom(sf, argv[i]) < 0)
				nfails++;
		}
	} else {
		FILE *f = NULL;
		char *dev;

		while ((dev = next_proc_partition(&f))) {
			if (print_geom(sf, dev) < 0)
				nfails++;
			free(dev);
		}
	}

	return nfails;
}

/*
 * sfdisk --activate <device> [<partno> ...]
 */
static int command_activate(struct sfdisk *sf, int argc, char **argv)
{
	int rc, nparts, i, listonly;
	struct fdisk_partition *pa = NULL;
	const char *devname = NULL;

	if (argc < 1)
		errx(EXIT_FAILURE, _("no disk device specified"));
	devname = argv[0];

	/*  --activate <device> */
	listonly = argc == 1;

	rc = fdisk_assign_device(sf->cxt, devname, listonly);
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
		if (listonly) {
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
				_("%s: partition %d: failed to toggle bootable flag"),
				devname, i + 1);
	}

	fdisk_unref_partition(pa);

	if (!listonly)
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
	fdisk_deassign_device(sf->cxt, 1);
	return 0;
}

/*
 * sfdisk --type <device> <partno> [<type>]
 */
static int command_parttype(struct sfdisk *sf, int argc, char **argv)
{
	int rc;
	size_t partno, n;
	struct fdisk_label *lb = NULL;
	struct fdisk_parttype *type = NULL;
	const char *devname = NULL, *typestr = NULL;

	if (!argc)
		errx(EXIT_FAILURE, _("no disk device specified"));
	devname = argv[0];

	if (argc < 2)
		errx(EXIT_FAILURE, _("no partition number specified"));
	partno = strtou32_or_err(argv[1], _("failed to parse partition number"));

	if (argc == 3)
		typestr = argv[2];
	else if (argc > 3)
		errx(EXIT_FAILURE, _("uneexpected arguments"));

	/* read-only when a new <type> undefined */
	rc = fdisk_assign_device(sf->cxt, devname, !typestr);
	if (rc)
		err(EXIT_FAILURE, _("cannot open %s"), devname);

	lb = fdisk_get_label(sf->cxt, NULL);
	if (!lb)
		errx(EXIT_FAILURE, _("%s: not found partition table."), devname);

	n = fdisk_get_npartitions(sf->cxt);
	if (partno > n)
		errx(EXIT_FAILURE, _("%s: partition %zu: partition table contains %zu "
				     "partitions only."), devname, partno, n);
	if (!fdisk_is_partition_used(sf->cxt, partno - 1))
		errx(EXIT_FAILURE, _("%s: partition %zu: partition unnused"),
				devname, partno);

	/* print partition type */
	if (!typestr) {
		const struct fdisk_parttype *t = NULL;
		struct fdisk_partition *pa = NULL;

		if (fdisk_get_partition(sf->cxt, partno - 1, &pa) == 0)
			t = fdisk_partition_get_type(pa);
		if (!t)
			errx(EXIT_FAILURE, _("%s: partition %zu: failed to get partition type"),
						devname, partno);

		if (fdisk_label_has_code_parttypes(lb))
			printf("%2x\n", fdisk_parttype_get_code(t));
		else
			printf("%s\n", fdisk_parttype_get_string(t));

		fdisk_unref_partition(pa);
		fdisk_deassign_device(sf->cxt, 1);
		return 0;
	}

	/* parse <type> and apply yo PT */
	type = fdisk_label_parse_parttype(lb, typestr);
	if (!type || fdisk_parttype_is_unknown(type))
		errx(EXIT_FAILURE, _("failed to parse %s partition type '%s'"),
				fdisk_label_get_name(lb), typestr);

	else if (fdisk_set_partition_type(sf->cxt, partno - 1, type) != 0)
		errx(EXIT_FAILURE, _("%s: partition %zu: failed to set partition type"),
						devname, partno);

	fdisk_free_parttype(type);
	if (!rc)
		rc = fdisk_write_disklabel(sf->cxt);
	if (!rc)
		rc = fdisk_deassign_device(sf->cxt, 1);
	return rc;
}

static void sfdisk_print_partition(struct sfdisk *sf, size_t n)
{
	struct fdisk_partition *pa = NULL;
	char *data;

	assert(sf);

	if (sf->quiet)
		return;
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

static void command_fdisk_help(void)
{
	fputs(_("\nHelp:\n"), stdout);

	fputc('\n', stdout);
	color_scheme_enable("help-title", UL_COLOR_BOLD);
	fputs(_(" Commands:\n"), stdout);
	color_disable();
	fputs(_("   write    write table to disk and exit\n"), stdout);
	fputs(_("   quit     show new situation and wait for user's feedbadk before write\n"), stdout);
	fputs(_("   abort    exit sfdisk shell\n"), stdout);
	fputs(_("   print    print partition table.\n"), stdout);
	fputs(_("   help     this help.\n"), stdout);

	fputc('\n', stdout);
	color_scheme_enable("help-title", UL_COLOR_BOLD);
	fputs(_(" Input format:\n"), stdout);
	color_disable();
	fputs(_("   <start> <size> <typy> <bootable>\n"), stdout);

	fputc('\n', stdout);
	fputs(_("   <start>  begin of the partition in sectors. The default is the first\n"
		"            free space.\n"), stdout);

	fputc('\n', stdout);
	fputs(_("   <size>   size of the partition in sectors if specified in format\n"
		"            <number>{K,M,G,T,P,E,Z,Y} then it's interpreted as size\n"
		"            in bytes. The default is all available space.\n"), stdout);

	fputc('\n', stdout);
	fputs(_("   <type>   partition type. The default is Linux data partition.\n"), stdout);
	fputs(_("            MBR: hex or L,S,E,X shortcuts.\n"), stdout);
	fputs(_("            GPT: uuid or L,S,H shortcuts.\n"), stdout);

	fputc('\n', stdout);
	fputs(_("   <bootable>  '*' to mark MBR partition as bootable. \n"), stdout);

	fputc('\n', stdout);
	color_scheme_enable("help-title", UL_COLOR_BOLD);
	fputs(_(" Example:\n"), stdout);
	color_disable();
	fputs(_("   , 4G     creates 4GiB partition on default start offset.\n"), stdout);
	fputc('\n', stdout);
}

enum {
	SFDISK_DONE_NONE = 0,
	SFDISK_DONE_EOF,
	SFDISK_DONE_ABORT,
	SFDISK_DONE_WRITE,
	SFDISK_DONE_ASK
};

/* returns: 0 on success, <0 on error, 1 successfully stop sfdisk */
static int loop_control_commands(struct sfdisk *sf,
				 struct fdisk_script *dp,
				 char *buf)
{
	const char *p = skip_blank(buf);
	int rc = SFDISK_DONE_NONE;

	if (strcmp(p, "print") == 0)
		list_disklabel(sf->cxt);
	else if (strcmp(p, "help") == 0)
		command_fdisk_help();
	else if (strcmp(p, "quit") == 0)
		rc = SFDISK_DONE_ASK;
	else if (strcmp(p, "write") == 0)
		rc = SFDISK_DONE_WRITE;
	else if (strcmp(p, "abort") == 0)
		rc = SFDISK_DONE_ABORT;
	else {
		if (isatty(STDIN_FILENO))
			fdisk_warnx(sf->cxt, _("unsupported command"));
		else {
			fdisk_warnx(sf->cxt, _("line %d: unsupported command"),
					fdisk_script_get_nlines(dp));
			rc = -EINVAL;
		}
	}
	return rc;
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
	size_t next_partno = (size_t) -1;

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

	/*
	 * Don't create a new disklabel when [-N] <partno> specified. In this
	 * case reuse already specified disklabel. Let's check that the disk
	 * really contains the partition.
	 */
	if (partno >= 0) {
		size_t n;
		if (!fdisk_has_label(sf->cxt))
			errx(EXIT_FAILURE, _("%s: cannot modify partition %d, "
					     "not found partition table."),
					devname, partno);
		n = fdisk_get_npartitions(sf->cxt);
		if ((size_t) partno > n)
			errx(EXIT_FAILURE, _("%s: cannot modify partition %d, "
					     "partition table contains %zu "
					     "partitions only."),
					devname, partno, n);
		created = 1;
		next_partno = partno;
	}

	if (!sf->quiet && isatty(STDIN_FILENO)) {
		color_scheme_enable("welcome", UL_COLOR_GREEN);
		fdisk_info(sf->cxt, _("\nWelcome to sfdisk (%s)."), PACKAGE_STRING);
		color_disable();
		fdisk_info(sf->cxt, _("Changes will remain in memory only, until you decide to write them.\n"
				  "Be careful before using the write command.\n"));
	}

	if (!sf->quiet) {
		list_disk_geometry(sf->cxt);
		if (fdisk_has_label(sf->cxt)) {
			fdisk_info(sf->cxt, _("\nOld situation:"));
			list_disklabel(sf->cxt);
		}
	}

	if (sf->label)
		label = sf->label;
	else if (fdisk_has_label(sf->cxt))
		label = fdisk_label_get_name(fdisk_get_label(sf->cxt, NULL));
	else
		label = "dos";	/* just for backward compatibility */

	fdisk_script_set_header(dp, "label", label);

	if (!sf->quiet && isatty(STDIN_FILENO)) {
		if (!fdisk_has_label(sf->cxt) && !sf->label)
			fdisk_info(sf->cxt,
				_("\nsfdisk is going to create a new '%s' disk label.\n"
				  "Use 'label: <name>' before you define a first partition\n"
				  "to override the default."), label);
		fdisk_info(sf->cxt, _("\nType 'help' to get more information.\n"));
	} else if (!sf->quiet)
		fputc('\n', stdout);

	tb = fdisk_script_get_table(dp);
	assert(tb);

	do {
		size_t nparts;

		DBG(PARSE, ul_debug("<---next-line--->"));
		if (next_partno == (size_t) -1)
			next_partno = fdisk_table_get_nents(tb);

		if (created) {
			char *partname = fdisk_partname(devname, next_partno + 1);
			if (!partname)
				err(EXIT_FAILURE, _("failed to allocate partition name"));
			printf("%s: ", partname);
			free(partname);
		} else
			printf(">>> ");

		rc = fdisk_script_read_line(dp, stdin, buf, sizeof(buf));
		if (rc < 0) {
			DBG(PARSE, ul_debug("script parsing failed, trying sfdisk specific commands"));
			buf[sizeof(buf) - 1] = '\0';
			rc = loop_control_commands(sf, dp, buf);
			if (rc)
				break;
			continue;
		} else if (rc == 1) {
			rc = SFDISK_DONE_EOF;
			break;
		}

		nparts = fdisk_table_get_nents(tb);
		if (nparts) {
			size_t cur_partno;
			struct fdisk_partition *pa = fdisk_table_get_partition(tb, nparts - 1);

			assert(pa);

			if (!fdisk_partition_get_start(pa) &&
			    !fdisk_partition_start_is_default(pa)) {
				fdisk_info(sf->cxt, _("Ignore partition %zu"), next_partno + 1);
				continue;
			}
			if (!created) {		/* create a new disklabel */
				rc = fdisk_apply_script_headers(sf->cxt, dp);
				created = !rc;
			}
			if (!rc && partno >= 0) {	/* -N <partno>, modify partition */
				rc = fdisk_set_partition(sf->cxt, partno, pa);
				if (rc == 0)
					rc = SFDISK_DONE_ASK;
				break;
			} else if (!rc)		/* add partition */
				rc = fdisk_add_partition(sf->cxt, pa, &cur_partno);

			if (!rc) {		/* success, print reult */
				sfdisk_print_partition(sf, cur_partno);
				next_partno = cur_partno + 1;
			} else if (pa)		/* error, drop partition from script */
				fdisk_table_remove_partition(tb, pa);
		} else
			fdisk_info(sf->cxt, _("Script header accepted."));
	} while (1);

	if (!sf->quiet && rc != SFDISK_DONE_ABORT) {
		fdisk_info(sf->cxt, _("\nNew situation:"));
		list_disklabel(sf->cxt);
	}

	switch (rc) {
	case SFDISK_DONE_ASK:
		if (isatty(STDIN_FILENO)) {
			int yes = 0;
			fdisk_ask_yesno(sf->cxt, _("Do you want to write this to disk?"), &yes);
			if (!yes) {
				printf(_("Leaving.\n"));
				rc = 0;
				break;
			}
		}
	case SFDISK_DONE_EOF:
	case SFDISK_DONE_WRITE:
		rc = fdisk_write_disklabel(sf->cxt);
		if (!rc) {
			fdisk_info(sf->cxt, _("\nThe partition table has been altered."));
			fdisk_reread_partition_table(sf->cxt);
			rc = fdisk_deassign_device(sf->cxt, 0);
		}
		break;
	case SFDISK_DONE_ABORT:
	default:				/* rc < 0 on error */
		fdisk_info(sf->cxt, _("Leaving.\n"));
		break;
	}

	fdisk_unref_script(dp);
	return rc;
}

static void __attribute__ ((__noreturn__)) usage(FILE *out)
{
	fputs(USAGE_HEADER, out);

	fprintf(out,
	      _(" %1$s [options] <dev> [[-N] <part>]\n"
		" %1$s [options] <command>\n"), program_invocation_short_name);

	fputs(_("\nCommands:\n"), out);
	fputs(_(" -a, --activate <dev> [<part> ...] list or set bootable MBR partitions\n"), out);
	fputs(_(" -c, --type <dev> <part> [<type>]  print or change partition type\n"), out);
	fputs(_(" -d, --dump <dev>                  dump partition table (usable for later input)\n"), out);
	fputs(_(" -g, --show-geometry [<dev> ...]   list geometry of all or specified devices\n"), out);
	fputs(_(" -l, --list [<dev> ...]            list partitions of each device\n"), out);
	fputs(_(" -s, --show-size [<dev> ...]       list sizes of all or specified devices\n"), out);
	fputs(_(" -T, --list-types                  print the recognized types (see -X)\n"), out);
	fputs(_(" -V, --verify                      test whether partitions seem correct\n"), out);


	fputs(USAGE_SEPARATOR, out);
	fputs(_(" <dev>                device (usually disk) path\n"), out);
	fputs(_(" <part>               partition number\n"), out);
	fputs(_(" <type>               partition type, GUID for GPT, hex for MBR\n"), out);

	fputs(USAGE_OPTIONS, out);
	fputs(_(" -N, --partno <num>   specify partition number\n"), out);
	fputs(_(" -X, --label <name>   specify label type (dos, gpt, ...)\n"), out);
	fputs(_(" -u, --unit S         deprecated, all input and output is in sectors only\n"), out);
	fputs(_(" -q, --quiet          suppress extra info messages\n"), out);

	fputs(USAGE_SEPARATOR, out);
	fputs(USAGE_HELP, out);
	fputs(USAGE_VERSION, out);

	fprintf(out, USAGE_MAN_TAIL("sfdisk(8)"));
	exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}



int main(int argc, char *argv[])
{
	int rc = -EINVAL, c, longidx = -1;
	struct sfdisk _sf = {
		.partno = -1
	}, *sf = &_sf;

	enum {
		OPT_CHANGE_ID = CHAR_MAX + 1,
		OPT_PRINT_ID,
		OPT_ID
	};

	static const struct option longopts[] = {
		{ "activate",no_argument,	NULL, 'a' },
		{ "dump",    no_argument,	NULL, 'd' },
		{ "help",    no_argument,       NULL, 'h' },
		{ "label",   required_argument, NULL, 'X' },
		{ "list",    no_argument,       NULL, 'l' },
		{ "list-types", no_argument,	NULL, 'T' },
		{ "partno",  required_argument, NULL, 'N' },
		{ "show-size", no_argument,	NULL, 's' },
		{ "show-geometry", no_argument, NULL, 'g' },
		{ "quiet",   no_argument,       NULL, 'q' },
		{ "verify",  no_argument,       NULL, 'V' },
		{ "version", no_argument,       NULL, 'v' },
		{ "unit",    required_argument, NULL, 'u' },		/* deprecated */

		{ "type",no_argument,           NULL, 'c' },		/* wanted */
		{ "change-id",no_argument,      NULL, OPT_CHANGE_ID },	/* deprecated */
		{ "id",      no_argument,       NULL, OPT_ID },		/* deprecated */
		{ "print-id",no_argument,       NULL, OPT_PRINT_ID },	/* deprecated */

		{ NULL, 0, 0, 0 },
	};

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	while ((c = getopt_long(argc, argv, "adhglN:qsTu:vVX:",
					longopts, &longidx)) != -1) {
		switch(c) {
		case 'a':
			sf->act = ACT_ACTIVATE;
			break;
		case OPT_CHANGE_ID:
		case OPT_PRINT_ID:
		case OPT_ID:
			warnx(_("%s is deprecated in favour of --type"),
				longopts[longidx].name);
			/* fallthrough */
		case 'c':
			sf->act = ACT_PARTTYPE;
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
		case 'g':
			sf->act = ACT_SHOW_GEOM;
			break;
		case 'N':
			sf->partno = strtou32_or_err(optarg, _("failed to parse partition number")) - 1;
			break;
		case 'q':
			sf->quiet = 1;
			break;
		case 'X':
			sf->label = optarg;
			break;
		case 's':
			sf->act = ACT_SHOW_SIZE;
			break;
		case 'T':
			sf->act = ACT_LIST_TYPES;
			break;
		case 'u':
			/* deprecated */
			warnx(_("--unit option is deprecated, only sectors are supported"));
			if (*optarg != 'S')
				errx(EXIT_FAILURE, _("unssupported unit '%c'"), *optarg);
			break;
		case 'v':
			printf(_("%s from %s\n"), program_invocation_short_name,
						  PACKAGE_STRING);
			return EXIT_SUCCESS;
		case 'V':
			sf->verify = 1;
			break;
		default:
			usage(stderr);
		}
	}

	sfdisk_init(sf);

	if (sf->verify && !sf->act)
		sf->act = ACT_VERIFY;	/* --verify make be used with --list too */
	else if (!sf->act)
		sf->act = ACT_FDISK;	/* default */

	switch (sf->act) {
	case ACT_ACTIVATE:
		rc = command_activate(sf, argc - optind, argv + optind);
		break;

	case ACT_LIST:
		rc = command_list_partitions(sf, argc - optind, argv + optind);
		break;

	case ACT_LIST_TYPES:
		rc = command_list_types(sf);
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

	case ACT_SHOW_GEOM:
		rc = command_show_geometry(sf, argc - optind, argv + optind);
		break;

	case ACT_VERIFY:
		rc = command_verify(sf, argc - optind, argv + optind);
		break;

	case ACT_PARTTYPE:
		rc = command_parttype(sf, argc - optind, argv + optind);
		break;
	}

	sfdisk_deinit(sf);

	DBG(MISC, ul_debug("bye! [rc=%d]", rc));
	return rc == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

