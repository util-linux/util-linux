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
#include <libsmartcols.h>
#ifdef HAVE_LIBREADLINE
# include <readline/readline.h>
#endif

#include "c.h"
#include "xalloc.h"
#include "nls.h"
#include "debug.h"
#include "strutils.h"
#include "closestream.h"
#include "colors.h"
#include "blkdev.h"
#include "all-io.h"
#include "rpmatch.h"
#include "loopdev.h"

#include "libfdisk.h"
#include "fdisk-list.h"

/*
 * sfdisk debug stuff (see fdisk.h and include/debug.h)
 */
UL_DEBUG_DEFINE_MASK(sfdisk);
UL_DEBUG_DEFINE_MASKNAMES(sfdisk) = UL_DEBUG_EMPTY_MASKNAMES;

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
	ACT_LIST_FREE,
	ACT_LIST_TYPES,
	ACT_SHOW_SIZE,
	ACT_SHOW_GEOM,
	ACT_VERIFY,
	ACT_PARTTYPE,
	ACT_PARTUUID,
	ACT_PARTLABEL,
	ACT_PARTATTRS,
};

struct sfdisk {
	int		act;		/* ACT_* */
	int		partno;		/* -N <partno>, default -1 */
	const char	*label;		/* --label <label> */
	const char	*label_nested;	/* --label-nested <label> */
	const char	*backup_file;	/* -O <path> */
	char		*prompt;

	struct fdisk_context	*cxt;	/* libfdisk context */

	unsigned int verify : 1,	/* call fdisk_verify_disklabel() */
		     quiet  : 1,	/* suppres extra messages */
		     interactive : 1,	/* running on tty */
		     noreread : 1,	/* don't check device is in use */
		     force  : 1,	/* do also stupid things */
		     backup : 1,	/* backup sectors before write PT */
		     container : 1,	/* PT contains container (MBR extended) partitions */
		     append : 1,	/* don't create new PT, append partitions only */
		     json : 1,		/* JSON dump */
		     noact  : 1;	/* do not write to device */
};

#define SFDISK_PROMPT	">>> "

static void sfdiskprog_init_debug(void)
{
	__UL_INIT_DEBUG(sfdisk, SFDISKPROG_DEBUG_, 0, SFDISK_DEBUG);
}


static int get_user_reply(const char *prompt, char *buf, size_t bufsz)
{
	char *p;
	size_t sz;

#ifdef HAVE_LIBREADLINE
	if (isatty(STDIN_FILENO)) {
		p = readline(prompt);
		if (!p)
			return 1;
		memcpy(buf, p, bufsz);
		free(p);
	} else
#endif
	{
		fputs(prompt, stdout);
		fflush(stdout);

		if (!fgets(buf, bufsz, stdin))
			return 1;
	}

	for (p = buf; *p && !isgraph(*p); p++);	/* get first non-blank */

	if (p > buf)
		memmove(buf, p, p - buf);	/* remove blank space */
	sz = strlen(buf);
	if (sz && *(buf + sz - 1) == '\n')
		*(buf + sz - 1) = '\0';

	DBG(ASK, ul_debug("user's reply: >>>%s<<<", buf));
	return 0;
}

static int ask_callback(struct fdisk_context *cxt __attribute__((__unused__)),
			struct fdisk_ask *ask,
			void *data)
{
	struct sfdisk *sf = (struct sfdisk *) data;
	int rc = 0;

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
			if (x == RPMATCH_YES || x == RPMATCH_NO) {
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
	scols_init_debug(0);
	sfdiskprog_init_debug();

	sf->cxt = fdisk_new_context();
	if (!sf->cxt)
		err(EXIT_FAILURE, _("failed to allocate libfdisk context"));
	fdisk_set_ask(sf->cxt, ask_callback, (void *) sf);
	fdisk_enable_bootbits_protection(sf->cxt, 1);

	if (sf->label_nested) {
		struct fdisk_context *x = fdisk_new_nested_context(sf->cxt,
							sf->label_nested);
		if (!x)
			err(EXIT_FAILURE, _("failed to allocate nested libfdisk context"));
		/* the original context is available by fdisk_get_parent() */
		sf->cxt = x;
	}
}

static int sfdisk_deinit(struct sfdisk *sf)
{
	struct fdisk_context *parent;

	assert(sf);
	assert(sf->cxt);

	parent = fdisk_get_parent(sf->cxt);
	if (parent) {
		fdisk_unref_context(sf->cxt);
		sf->cxt = parent;
	}

	fdisk_unref_context(sf->cxt);
	free(sf->prompt);

	memset(sf, 0, sizeof(*sf));
	return 0;
}

static void backup_sectors(struct sfdisk *sf,
			   const char *tpl,
			   const char *name,
			   const char *devname,
			   uint64_t offset, size_t size)
{
	char *fname;
	int fd, devfd;

	devfd = fdisk_get_devfd(sf->cxt);
	assert(devfd >= 0);

	xasprintf(&fname, "%s0x%08jx.bak", tpl, offset);

	fd = open(fname, O_CREAT | O_WRONLY, S_IRUSR | S_IWUSR);
	if (fd < 0)
		goto fail;

	if (lseek(devfd, (off_t) offset, SEEK_SET) == (off_t) -1) {
		fdisk_warn(sf->cxt, _("cannot seek %s"), devname);
		goto fail;
	} else {
		unsigned char *buf = xmalloc(size);

		if (read_all(devfd, (char *) buf, size) != (ssize_t) size) {
			fdisk_warn(sf->cxt, _("cannot read %s"), devname);
			goto fail;
		}
		if (write_all(fd, buf, size) != 0) {
			fdisk_warn(sf->cxt, _("cannot write %s"), fname);
			goto fail;
		}
		free(buf);
	}

	fdisk_info(sf->cxt, _("%12s (offset %5ju, size %5ju): %s"),
			name, (uintmax_t) offset, (uintmax_t) size, fname);
	close(fd);
	free(fname);
	return;
fail:
	errx(EXIT_FAILURE, _("%s: failed to create a backup"), devname);
}

static void backup_partition_table(struct sfdisk *sf, const char *devname)
{
	const char *name;
	char *tpl;
	uint64_t offset = 0;
	size_t size = 0;
	int i = 0;

	assert(sf);

	if (!fdisk_has_label(sf->cxt))
		return;

	if (!sf->backup_file) {
		/* initialize default backup filename */
		const char *home = getenv ("HOME");
		if (!home)
			errx(EXIT_FAILURE, _("failed to create a signature backup, $HOME undefined"));
		xasprintf(&tpl, "%s/sfdisk-%s-", home, basename(devname));
	} else
		xasprintf(&tpl, "%s-%s-", sf->backup_file, basename(devname));

	color_scheme_enable("header", UL_COLOR_BOLD);
	fdisk_info(sf->cxt, _("Backup files:"));
	color_disable();

	while (fdisk_locate_disklabel(sf->cxt, i++, &name, &offset, &size) == 0 && size)
		backup_sectors(sf, tpl, name, devname, offset, size);

	if (!sf->quiet)
		fputc('\n', stdout);
	free(tpl);
}

static int write_changes(struct sfdisk *sf)
{
	int rc = 0;

	if (sf->noact)
		fdisk_info(sf->cxt, _("The partition table is unchanged (--no-act)."));
	else {
		rc = fdisk_write_disklabel(sf->cxt);
		if (!rc) {
			fdisk_info(sf->cxt, _("\nThe partition table has been altered."));
			fdisk_reread_partition_table(sf->cxt);
		}
	}
	if (!rc)
		rc = fdisk_deassign_device(sf->cxt, sf->noact);	/* no-sync when no-act */
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
 * sfdisk --list-free [<device ..]
 */
static int command_list_freespace(struct sfdisk *sf, int argc, char **argv)
{
	fdisk_enable_listonly(sf->cxt, 1);

	if (argc) {
		int i, ct = 0;

		for (i = 0; i < argc; i++) {
			if (ct)
				fputs("\n\n", stdout);
			if (print_device_freespace(sf->cxt, argv[i], 0) == 0)
				ct++;
		}
	} else
		print_all_devices_freespace(sf->cxt);

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
		warn(_("cannot open %s"), devname);
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
			warn(_("cannot open %s"), dev);
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
		warn(_("cannot open %s"), devname);
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

	if (!listonly && sf->backup)
		backup_partition_table(sf, devname);

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
			fdisk_toggle_partition_flag(sf->cxt, i, DOS_FLAG_ACTIVE);
	}

	/* sfdisk --activate <partno> [..] */
	for (i = 1; i < argc; i++) {
		int n = strtou32_or_err(argv[i], _("failed to parse partition number"));

		rc = fdisk_toggle_partition_flag(sf->cxt, n - 1, DOS_FLAG_ACTIVE);
		if (rc)
			errx(EXIT_FAILURE,
				_("%s: partition %d: failed to toggle bootable flag"),
				devname, i + 1);
	}

	fdisk_unref_partition(pa);
	if (listonly)
		rc = fdisk_deassign_device(sf->cxt, 1);
	else
		rc = write_changes(sf);
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

	rc = fdisk_assign_device(sf->cxt, devname, 1);	/* read-only */
	if (rc)
		err(EXIT_FAILURE, _("cannot open %s"), devname);

	dp = fdisk_new_script(sf->cxt);
	if (!dp)
		err(EXIT_FAILURE, _("failed to allocate dump struct"));

	rc = fdisk_script_read_context(dp, NULL);
	if (rc)
		err(EXIT_FAILURE, _("failed to dump partition table"));

	if (sf->json)
		fdisk_script_enable_json(dp, 1);
	fdisk_script_write_file(dp, stdout);

	fdisk_unref_script(dp);
	fdisk_deassign_device(sf->cxt, 1);		/* no-sync() */
	return 0;
}

static void assign_device_partition(struct sfdisk *sf,
				const char *devname,
				size_t partno,
				int rdonly)
{
	int rc;
	size_t n;
	struct fdisk_label *lb = NULL;

	assert(sf);
	assert(devname);

	/* read-only when a new <type> undefined */
	rc = fdisk_assign_device(sf->cxt, devname, rdonly);
	if (rc)
		err(EXIT_FAILURE, _("cannot open %s"), devname);

	lb = fdisk_get_label(sf->cxt, NULL);
	if (!lb)
		errx(EXIT_FAILURE, _("%s: no partition table found"), devname);

	n = fdisk_get_npartitions(sf->cxt);
	if (partno > n)
		errx(EXIT_FAILURE, _("%s: partition %zu: partition table contains "
				     "only %zu partitions"), devname, partno, n);
	if (!fdisk_is_partition_used(sf->cxt, partno - 1))
		errx(EXIT_FAILURE, _("%s: partition %zu: partition is unused"),
				devname, partno);
}

/*
 * sfdisk --part-type <device> <partno> [<type>]
 */
static int command_parttype(struct sfdisk *sf, int argc, char **argv)
{
	size_t partno;
	struct fdisk_parttype *type = NULL;
	struct fdisk_label *lb;
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
		errx(EXIT_FAILURE, _("unexpected arguments"));

	/* read-only when a new <type> undefined */
	assign_device_partition(sf, devname, partno, !typestr);

	lb = fdisk_get_label(sf->cxt, NULL);

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

	if (sf->backup)
		backup_partition_table(sf, devname);

	/* parse <type> and apply to PT */
	type = fdisk_label_parse_parttype(lb, typestr);
	if (!type)
		errx(EXIT_FAILURE, _("failed to parse %s partition type '%s'"),
				fdisk_label_get_name(lb), typestr);

	else if (fdisk_set_partition_type(sf->cxt, partno - 1, type) != 0)
		errx(EXIT_FAILURE, _("%s: partition %zu: failed to set partition type"),
						devname, partno);
	fdisk_unref_parttype(type);
	return write_changes(sf);
}

/*
 * sfdisk --part-uuid <device> <partno> [<uuid>]
 */
static int command_partuuid(struct sfdisk *sf, int argc, char **argv)
{
	size_t partno;
	struct fdisk_partition *pa = NULL;
	const char *devname = NULL, *uuid = NULL;

	if (!argc)
		errx(EXIT_FAILURE, _("no disk device specified"));
	devname = argv[0];

	if (argc < 2)
		errx(EXIT_FAILURE, _("no partition number specified"));
	partno = strtou32_or_err(argv[1], _("failed to parse partition number"));

	if (argc == 3)
		uuid = argv[2];
	else if (argc > 3)
		errx(EXIT_FAILURE, _("unexpected arguments"));

	/* read-only if uuid not given */
	assign_device_partition(sf, devname, partno, !uuid);

	/* print partition uuid */
	if (!uuid) {
		const char *str = NULL;

		if (fdisk_get_partition(sf->cxt, partno - 1, &pa) == 0)
			str = fdisk_partition_get_uuid(pa);
		if (!str)
			errx(EXIT_FAILURE, _("%s: partition %zu: failed to get partition UUID"),
						devname, partno);
		printf("%s\n", str);
		fdisk_unref_partition(pa);
		fdisk_deassign_device(sf->cxt, 1);
		return 0;
	}

	if (sf->backup)
		backup_partition_table(sf, devname);

	pa = fdisk_new_partition();
	if (!pa)
		err(EXIT_FAILURE, _("failed to allocate partition object"));

	if (fdisk_partition_set_uuid(pa, uuid) != 0 ||
	    fdisk_set_partition(sf->cxt, partno - 1, pa) != 0)
		errx(EXIT_FAILURE, _("%s: partition %zu: failed to set partition UUID"),
						devname, partno);
	fdisk_unref_partition(pa);
	return write_changes(sf);
}

/*
 * sfdisk --part-label <device> <partno> [<label>]
 */
static int command_partlabel(struct sfdisk *sf, int argc, char **argv)
{
	size_t partno;
	struct fdisk_partition *pa = NULL;
	const char *devname = NULL, *name = NULL;

	if (!argc)
		errx(EXIT_FAILURE, _("no disk device specified"));
	devname = argv[0];

	if (argc < 2)
		errx(EXIT_FAILURE, _("no partition number specified"));
	partno = strtou32_or_err(argv[1], _("failed to parse partition number"));

	if (argc == 3)
		name = argv[2];
	else if (argc > 3)
		errx(EXIT_FAILURE, _("unexpected arguments"));

	/* read-only if name not given */
	assign_device_partition(sf, devname, partno, !name);

	/* print partition name */
	if (!name) {
		const char *str = NULL;

		if (fdisk_get_partition(sf->cxt, partno - 1, &pa) == 0)
			str = fdisk_partition_get_name(pa);
		if (!str)
			errx(EXIT_FAILURE, _("%s: partition %zu: failed to get partition name"),
						devname, partno);
		printf("%s\n", str);
		fdisk_unref_partition(pa);
		fdisk_deassign_device(sf->cxt, 1);
		return 0;
	}

	if (sf->backup)
		backup_partition_table(sf, devname);

	pa = fdisk_new_partition();
	if (!pa)
		err(EXIT_FAILURE, _("failed to allocate partition object"));

	if (fdisk_partition_set_name(pa, name) != 0 ||
	    fdisk_set_partition(sf->cxt, partno - 1, pa) != 0)
		errx(EXIT_FAILURE, _("%s: partition %zu: failed to set partition name"),
				devname, partno);

	fdisk_unref_partition(pa);
	return write_changes(sf);
}

/*
 * sfdisk --part-attrs <device> <partno> [<attrs>]
 */
static int command_partattrs(struct sfdisk *sf, int argc, char **argv)
{
	size_t partno;
	struct fdisk_partition *pa = NULL;
	const char *devname = NULL, *attrs = NULL;

	if (!argc)
		errx(EXIT_FAILURE, _("no disk device specified"));
	devname = argv[0];

	if (argc < 2)
		errx(EXIT_FAILURE, _("no partition number specified"));
	partno = strtou32_or_err(argv[1], _("failed to parse partition number"));

	if (argc == 3)
		attrs = argv[2];
	else if (argc > 3)
		errx(EXIT_FAILURE, _("unexpected arguments"));

	/* read-only if name not given */
	assign_device_partition(sf, devname, partno, !attrs);

	/* print partition name */
	if (!attrs) {
		const char *str = NULL;

		if (fdisk_get_partition(sf->cxt, partno - 1, &pa) == 0)
			str = fdisk_partition_get_attrs(pa);
		if (str)
			printf("%s\n", str);
		fdisk_unref_partition(pa);
		fdisk_deassign_device(sf->cxt, 1);
		return 0;
	}

	if (sf->backup)
		backup_partition_table(sf, devname);

	pa = fdisk_new_partition();
	if (!pa)
		err(EXIT_FAILURE, _("failed to allocate partition object"));

	if (fdisk_partition_set_attrs(pa, attrs) != 0 ||
	    fdisk_set_partition(sf->cxt, partno - 1, pa) != 0)
		errx(EXIT_FAILURE, _("%s: partition %zu: failed to set partition attributes"),
				devname, partno);

	fdisk_unref_partition(pa);
	return write_changes(sf);
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
	fputs(_("   quit     show new situation and wait for user's feedback before write\n"), stdout);
	fputs(_("   abort    exit sfdisk shell\n"), stdout);
	fputs(_("   print    display the partition table\n"), stdout);
	fputs(_("   help     show this help text\n"), stdout);
	fputc('\n', stdout);
	fputs(_("   Ctrl-D   the same as 'quit'\n"), stdout);

	fputc('\n', stdout);
	color_scheme_enable("help-title", UL_COLOR_BOLD);
	fputs(_(" Input format:\n"), stdout);
	color_disable();
	fputs(_("   <start>, <size>, <type>, <bootable>\n"), stdout);

	fputc('\n', stdout);
	fputs(_("   <start>  Beginning of the partition in sectors, or bytes if\n"
		"            specified in the format <number>{K,M,G,T,P,E,Z,Y}.\n"
		"            The default is the first free space.\n"), stdout);

	fputc('\n', stdout);
	fputs(_("   <size>   Size of the partition in sectors, or bytes if\n"
		"            specified in the format <number>{K,M,G,T,P,E,Z,Y}.\n"
		"            The default is all available space.\n"), stdout);

	fputc('\n', stdout);
	fputs(_("   <type>   The partition type.  Default is a Linux data partition.\n"), stdout);
	fputs(_("            MBR: hex or L,S,E,X shortcuts.\n"), stdout);
	fputs(_("            GPT: UUID or L,S,H shortcuts.\n"), stdout);

	fputc('\n', stdout);
	fputs(_("   <bootable>  Use '*' to mark an MBR partition as bootable.\n"), stdout);

	fputc('\n', stdout);
	color_scheme_enable("help-title", UL_COLOR_BOLD);
	fputs(_(" Example:\n"), stdout);
	color_disable();
	fputs(_("   , 4G     Creates a 4GiB partition at default start offset.\n"), stdout);
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
		if (sf->interactive)
			fdisk_warnx(sf->cxt, _("unsupported command"));
		else {
			fdisk_warnx(sf->cxt, _("line %d: unsupported command"),
					fdisk_script_get_nlines(dp));
			rc = -EINVAL;
		}
	}
	return rc;
}

static int has_container(struct sfdisk *sf)
{
	size_t i, nparts;
	struct fdisk_partition *pa = NULL;

	if (sf->container)
		return sf->container;

	nparts = fdisk_get_npartitions(sf->cxt);
	for (i = 0; i < nparts; i++) {
		if (fdisk_get_partition(sf->cxt, i, &pa) != 0)
			continue;
		if (fdisk_partition_is_container(pa)) {
			sf->container = 1;
			break;
		}
	}

	fdisk_unref_partition(pa);
	return sf->container;
}

static size_t last_pt_partno(struct sfdisk *sf)
{
	size_t i, nparts, partno = 0;
	struct fdisk_partition *pa = NULL;


	nparts = fdisk_get_npartitions(sf->cxt);
	for (i = 0; i < nparts; i++) {
		size_t x;

		if (fdisk_get_partition(sf->cxt, i, &pa) != 0 ||
		    !fdisk_partition_is_used(pa))
			continue;
		x = fdisk_partition_get_partno(pa);
		if (x > partno)
			partno = x;
	}

	fdisk_unref_partition(pa);
	return partno;
}

static int is_device_used(struct sfdisk *sf)
{
#ifdef BLKRRPART
	struct stat st;
	int fd;

	assert(sf);
	assert(sf->cxt);

	fd = fdisk_get_devfd(sf->cxt);
	if (fd < 0)
		return 0;

	if (fstat(fd, &st) == 0 && S_ISBLK(st.st_mode)
	    && major(st.st_rdev) != LOOPDEV_MAJOR)
		return ioctl(fd, BLKRRPART) != 0;
#endif
	return 0;
}

#ifdef HAVE_LIBREADLINE
static char *sfdisk_fgets(struct fdisk_script *dp,
			  char *buf, size_t bufsz, FILE *f)
{
	struct sfdisk *sf = (struct sfdisk *) fdisk_script_get_userdata(dp);

	assert(dp);
	assert(buf);
	assert(bufsz > 2);

	if (sf->interactive) {
		char *p = readline(sf->prompt);
		size_t len;

		if (!p)
			return NULL;
		len = strlen(p);
		if (len > bufsz - 2)
			len = bufsz - 2;

		memcpy(buf, p, len);
		buf[len] = '\n';		/* append \n to be compatible with libc fgetc() */
		buf[len + 1] = '\0';
		free(p);
		fflush(stdout);
		return buf;
	}
	return fgets(buf, bufsz, f);
}
#endif

static int ignore_partition(struct fdisk_partition *pa)
{
	/* incomplete partition setting */
	if (!fdisk_partition_has_start(pa) && !fdisk_partition_start_is_default(pa))
		return 1;
	if (!fdisk_partition_has_size(pa) && !fdisk_partition_end_is_default(pa))
		return 1;

	/* probably dump from old sfdisk with start=0 size=0 */
	if (fdisk_partition_has_start(pa) && fdisk_partition_get_start(pa) == 0 &&
	    fdisk_partition_has_size(pa) && fdisk_partition_get_size(pa) == 0)
		return 1;

	return 0;
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

	rc = fdisk_assign_device(sf->cxt, devname, 0);
	if (rc)
		err(EXIT_FAILURE, _("cannot open %s"), devname);

	dp = fdisk_new_script(sf->cxt);
	if (!dp)
		err(EXIT_FAILURE, _("failed to allocate script handler"));
	fdisk_set_script(sf->cxt, dp);
#ifdef HAVE_LIBREADLINE
	fdisk_script_set_fgets(dp, sfdisk_fgets);
#endif
	fdisk_script_set_userdata(dp, (void *) sf);

	/*
	 * Don't create a new disklabel when [-N] <partno> specified. In this
	 * case reuse already specified disklabel. Let's check that the disk
	 * really contains the partition.
	 */
	if (partno >= 0) {
		size_t n;

		if (!fdisk_has_label(sf->cxt))
			errx(EXIT_FAILURE, _("%s: cannot modify partition %d: "
					     "no partition table was found"),
					devname, partno + 1);
		n = fdisk_get_npartitions(sf->cxt);
		if ((size_t) partno > n)
			errx(EXIT_FAILURE, _("%s: cannot modify partition %d: "
					     "partition table contains only %zu "
					     "partitions"),
					devname, partno + 1, n);

		if (!fdisk_is_partition_used(sf->cxt, partno))
			fdisk_warnx(sf->cxt, _("warning: %s: partition %d is not defined yet"),
					devname, partno + 1);
		created = 1;
		next_partno = partno;
	}

	if (sf->append) {
		created = 1;
		next_partno = last_pt_partno(sf) + 1;
	}

	if (!sf->quiet && sf->interactive) {
		color_scheme_enable("welcome", UL_COLOR_GREEN);
		fdisk_info(sf->cxt, _("\nWelcome to sfdisk (%s)."), PACKAGE_STRING);
		color_disable();
		fdisk_info(sf->cxt, _("Changes will remain in memory only, until you decide to write them.\n"
				  "Be careful before using the write command.\n"));
	}

	if (!sf->noact && !sf->noreread) {
		if (!sf->quiet)
			fputs(_("Checking that no-one is using this disk right now ..."), stdout);
		if (is_device_used(sf)) {
			if (!sf->quiet)
				fputs(_(" FAILED\n\n"), stdout);

			fdisk_warnx(sf->cxt, _(
			"This disk is currently in use - repartitioning is probably a bad idea.\n"
			"Umount all file systems, and swapoff all swap partitions on this disk.\n"
			"Use the --no-reread flag to suppress this check.\n"));

			if (!sf->force)
				errx(EXIT_FAILURE, _("Use the --force flag to overrule all checks."));
		} else if (!sf->quiet)
			fputs(_(" OK\n\n"), stdout);
	}

	if (sf->backup)
		backup_partition_table(sf, devname);

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

	if (!sf->quiet && sf->interactive) {
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

		if (created
		    && partno < 0
		    && next_partno == fdisk_get_npartitions(sf->cxt)
		    && !has_container(sf)) {
			fdisk_info(sf->cxt, _("All partitions used."));
			rc = SFDISK_DONE_ASK;
			break;
		}

		if (created) {
			char *partname = fdisk_partname(devname, next_partno + 1);
			if (!partname)
				err(EXIT_FAILURE, _("failed to allocate partition name"));
			if (!sf->prompt || !startswith(sf->prompt, partname)) {
				free(sf->prompt);
				xasprintf(&sf->prompt,"%s: ", partname);
			}
			free(partname);
		} else if (!sf->prompt || !startswith(sf->prompt, SFDISK_PROMPT)) {
			free(sf->prompt);
			sf->prompt = xstrdup(SFDISK_PROMPT);
		}

#ifndef HAVE_LIBREADLINE
		if (sf->prompt)
			fputs(sf->prompt, stdout);
#else
		if (!sf->interactive && sf->prompt)
			fputs(sf->prompt, stdout);
#endif
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

			if (ignore_partition(pa)) {
				fdisk_info(sf->cxt, _("Ignoring partition."));
				next_partno++;
				continue;
			}
			if (!created) {		/* create a new disklabel */
				rc = fdisk_apply_script_headers(sf->cxt, dp);
				created = !rc;
				if (rc)
					fdisk_warnx(sf->cxt, _(
					  "Failed to apply script headers, "
					  "disk label not created."));
			}
			if (!rc && partno >= 0) {	/* -N <partno>, modify partition */
				rc = fdisk_set_partition(sf->cxt, partno, pa);
				rc = rc == 0 ? SFDISK_DONE_ASK : SFDISK_DONE_ABORT;
				break;
			} else if (!rc) {		/* add partition */
				rc = fdisk_add_partition(sf->cxt, pa, &cur_partno);
				if (rc) {
					errno = -rc;
					fdisk_warn(sf->cxt, _("Failed to add partition"));
				}
			}

			if (!rc) {		/* success, print reult */
				if (sf->interactive)
					sfdisk_print_partition(sf, cur_partno);
				next_partno = cur_partno + 1;
			} else if (pa)		/* error, drop partition from script */
				fdisk_table_remove_partition(tb, pa);
		} else
			fdisk_info(sf->cxt, _("Script header accepted."));

		if (rc && !sf->interactive) {
			rc =  SFDISK_DONE_ABORT;
			break;
		}
	} while (1);

	if (!sf->quiet && rc != SFDISK_DONE_ABORT) {
		fdisk_info(sf->cxt, _("\nNew situation:"));
		list_disklabel(sf->cxt);
	}

	switch (rc) {
	case SFDISK_DONE_ASK:
	case SFDISK_DONE_EOF:
		if (sf->interactive) {
			int yes = 0;
			fdisk_ask_yesno(sf->cxt, _("Do you want to write this to disk?"), &yes);
			if (!yes) {
				fdisk_info(sf->cxt, _("Leaving."));
				rc = 0;
				break;
			}
		}
	case SFDISK_DONE_WRITE:
		rc = write_changes(sf);
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

	fputs(USAGE_SEPARATOR, out);
	fputs(_("Display or manipulate a disk partition table.\n"), out);

	fputs(_("\nCommands:\n"), out);
	fputs(_(" -A, --activate <dev> [<part> ...] list or set bootable MBR partitions\n"), out);
	fputs(_(" -d, --dump <dev>                  dump partition table (usable for later input)\n"), out);
	fputs(_(" -J, --json <dev>                  dump partition table in JSON format\n"), out);
	fputs(_(" -g, --show-geometry [<dev> ...]   list geometry of all or specified devices\n"), out);
	fputs(_(" -l, --list [<dev> ...]            list partitions of each device\n"), out);
	fputs(_(" -F, --list-free [<dev> ...]       list unpartitions free areas of each device\n"), out);
	fputs(_(" -s, --show-size [<dev> ...]       list sizes of all or specified devices\n"), out);
	fputs(_(" -T, --list-types                  print the recognized types (see -X)\n"), out);
	fputs(_(" -V, --verify [<dev> ...]          test whether partitions seem correct\n"), out);

	fputs(USAGE_SEPARATOR, out);
	fputs(_(" --part-label <dev> <part> [<str>] print or change partition label\n"), out);
	fputs(_(" --part-type <dev> <part> [<type>] print or change partition type\n"), out);
	fputs(_(" --part-uuid <dev> <part> [<uuid>] print or change partition uuid\n"), out);
	fputs(_(" --part-attrs <dev> <part> [<str>] print or change partition attributes\n"), out);

	fputs(USAGE_SEPARATOR, out);
	fputs(_(" <dev>                     device (usually disk) path\n"), out);
	fputs(_(" <part>                    partition number\n"), out);
	fputs(_(" <type>                    partition type, GUID for GPT, hex for MBR\n"), out);

	fputs(USAGE_OPTIONS, out);
	fputs(_(" -a, --append              append partitions to existing partition table\n"), out);
	fputs(_(" -b, --backup              backup partition table sectors (see -O)\n"), out);
	fputs(_("     --bytes               print SIZE in bytes rather than in human readable format\n"), out);
	fputs(_(" -f, --force               disable all consistency checking\n"), out);
	fputs(_("     --color[=<when>]      colorize output (auto, always or never)\n"), out);
	fprintf(out,
	        "                             %s\n", USAGE_COLORS_DEFAULT);
	fputs(_(" -N, --partno <num>        specify partition number\n"), out);
	fputs(_(" -n, --no-act              do everything except write to device\n"), out);
	fputs(_("     --no-reread           do not check whether the device is in use\n"), out);
	fputs(_(" -O, --backup-file <path>  override default backup file name\n"), out);
	fputs(_(" -o, --output <list>       output columns\n"), out);
	fputs(_(" -q, --quiet               suppress extra info messages\n"), out);
	fputs(_(" -X, --label <name>        specify label type (dos, gpt, ...)\n"), out);
	fputs(_(" -Y, --label-nested <name> specify nested label type (dos, bsd)\n"), out);
	fputs(USAGE_SEPARATOR, out);
	fputs(_(" -L, --Linux               deprecated, only for backward compatibility\n"), out);
	fputs(_(" -u, --unit S              deprecated, only sector unit is supported\n"), out);

	fputs(USAGE_SEPARATOR, out);
	fputs(USAGE_HELP, out);
	fputs(_(" -v, --version  output version information and exit\n"), out);

	list_available_columns(out);

	fprintf(out, USAGE_MAN_TAIL("sfdisk(8)"));
	exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}


int main(int argc, char *argv[])
{
	const char *outarg = NULL;
	int rc = -EINVAL, c, longidx = -1, bytes = 0;
	int colormode = UL_COLORMODE_UNDEF;
	struct sfdisk _sf = {
		.partno = -1,
		.interactive = isatty(STDIN_FILENO) ? 1 : 0,
	}, *sf = &_sf;

	enum {
		OPT_CHANGE_ID = CHAR_MAX + 1,
		OPT_PRINT_ID,
		OPT_ID,
		OPT_NOREREAD,
		OPT_PARTUUID,
		OPT_PARTLABEL,
		OPT_PARTTYPE,
		OPT_PARTATTRS,
		OPT_BYTES,
		OPT_COLOR
	};

	static const struct option longopts[] = {
		{ "activate",no_argument,	NULL, 'A' },
		{ "append",  no_argument,       NULL, 'a' },
		{ "backup",  no_argument,       NULL, 'b' },
		{ "backup-file", required_argument, NULL, 'O' },
		{ "bytes",   no_argument,	NULL, OPT_BYTES },
		{ "color",   optional_argument, NULL, OPT_COLOR },
		{ "dump",    no_argument,	NULL, 'd' },
		{ "help",    no_argument,       NULL, 'h' },
		{ "force",   no_argument,       NULL, 'f' },
		{ "json",    no_argument,	NULL, 'J' },
		{ "label",   required_argument, NULL, 'X' },
		{ "label-nested", required_argument, NULL, 'Y' },
		{ "list",    no_argument,       NULL, 'l' },
		{ "list-free", no_argument,     NULL, 'F' },
		{ "list-types", no_argument,	NULL, 'T' },
		{ "no-act",  no_argument,       NULL, 'n' },
		{ "no-reread", no_argument,     NULL, OPT_NOREREAD },
		{ "output",  required_argument, NULL, 'o' },
		{ "partno",  required_argument, NULL, 'N' },
		{ "show-size", no_argument,	NULL, 's' },
		{ "show-geometry", no_argument, NULL, 'g' },
		{ "quiet",   no_argument,       NULL, 'q' },
		{ "verify",  no_argument,       NULL, 'V' },
		{ "version", no_argument,       NULL, 'v' },

		{ "part-uuid",  no_argument,    NULL, OPT_PARTUUID },
		{ "part-label", no_argument,    NULL, OPT_PARTLABEL },
		{ "part-type",  no_argument,    NULL, OPT_PARTTYPE },
		{ "part-attrs", no_argument,    NULL, OPT_PARTATTRS },

		{ "unit",    required_argument, NULL, 'u' },
		{ "Linux",   no_argument,       NULL, 'L' },		/* deprecated */

		{ "change-id",no_argument,      NULL, OPT_CHANGE_ID },	/* deprecated */
		{ "id",      no_argument,       NULL, 'c' },		/* deprecated */
		{ "print-id",no_argument,       NULL, OPT_PRINT_ID },	/* deprecated */

		{ NULL, 0, 0, 0 },
	};

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	while ((c = getopt_long(argc, argv, "aAbcdfFghJlLo:O:nN:qsTu:vVX:Y:",
					longopts, &longidx)) != -1) {
		switch(c) {
		case 'A':
			sf->act = ACT_ACTIVATE;
			break;
		case 'a':
			sf->append = 1;
			break;
		case 'b':
			sf->backup = 1;
			break;
		case OPT_CHANGE_ID:
		case OPT_PRINT_ID:
		case OPT_ID:
			warnx(_("%s is deprecated in favour of --part-type"),
				longopts[longidx].name);
			sf->act = ACT_PARTTYPE;
			break;
		case 'c':
			warnx(_("--id is deprecated in favour of --part-type"));
			sf->act = ACT_PARTTYPE;
			break;
		case 'J':
			sf->json = 1;
			/* fallthrough */
		case 'd':
			sf->act = ACT_DUMP;
			break;
		case 'F':
			sf->act = ACT_LIST_FREE;
			break;
		case 'f':
			sf->force = 1;
			break;
		case 'g':
			sf->act = ACT_SHOW_GEOM;
			break;
		case 'h':
			usage(stdout);
			break;
		case 'l':
			sf->act = ACT_LIST;
			break;
		case 'L':
			warnx(_("--Linux option is unnecessary and deprecated"));
			break;
		case 'o':
			outarg = optarg;
			break;
		case 'O':
			sf->backup = 1;
			sf->backup_file = optarg;
			break;
		case 'n':
			sf->noact = 1;
			break;
		case 'N':
			sf->partno = strtou32_or_err(optarg, _("failed to parse partition number")) - 1;
			break;
		case 'q':
			sf->quiet = 1;
			break;
		case 's':
			sf->act = ACT_SHOW_SIZE;
			break;
		case 'T':
			sf->act = ACT_LIST_TYPES;
			break;
		case 'u':
			if (*optarg != 'S')
				errx(EXIT_FAILURE, _("unsupported unit '%c'"), *optarg);
			break;
		case 'v':
			printf(_("%s from %s\n"), program_invocation_short_name,
						  PACKAGE_STRING);
			return EXIT_SUCCESS;
		case 'V':
			sf->verify = 1;
			break;
		case 'X':
			sf->label = optarg;
			break;
		case 'Y':
			sf->label_nested = optarg;
			break;

		case OPT_PARTUUID:
			sf->act = ACT_PARTUUID;
			break;
		case OPT_PARTTYPE:
			sf->act = ACT_PARTTYPE;
			break;
		case OPT_PARTLABEL:
			sf->act = ACT_PARTLABEL;
			break;
		case OPT_PARTATTRS:
			sf->act = ACT_PARTATTRS;
			break;
		case OPT_NOREREAD:
			sf->noreread = 1;
			break;
		case OPT_BYTES:
			bytes = 1;
			break;
		case OPT_COLOR:
			colormode = UL_COLORMODE_AUTO;
			if (optarg)
				colormode = colormode_or_err(optarg,
						_("unsupported color mode"));
			break;
		default:
			usage(stderr);
		}
	}

	colors_init(colormode, "sfdisk");

	sfdisk_init(sf);
	if (bytes)
		fdisk_set_size_unit(sf->cxt, FDISK_SIZEUNIT_BYTES);

	if (outarg)
		init_fields(NULL, outarg, NULL);

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

	case ACT_LIST_FREE:
		rc = command_list_freespace(sf, argc - optind, argv + optind);
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

	case ACT_PARTUUID:
		rc = command_partuuid(sf, argc - optind, argv + optind);
		break;

	case ACT_PARTLABEL:
		rc = command_partlabel(sf, argc - optind, argv + optind);
		break;

	case ACT_PARTATTRS:
		rc = command_partattrs(sf, argc - optind, argv + optind);
		break;

	}

	sfdisk_deinit(sf);

	DBG(MISC, ul_debug("bye! [rc=%d]", rc));
	return rc == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

