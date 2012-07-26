/*
 * wipefs - utility to wipe filesystems from device
 *
 * Copyright (C) 2009 Red Hat, Inc. All rights reserved.
 * Written by Karel Zak <kzak@redhat.com>
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
#include <sys/stat.h>
#include <sys/types.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <string.h>
#include <limits.h>

#include <blkid.h>

#include "nls.h"
#include "xalloc.h"
#include "strutils.h"
#include "all-io.h"
#include "match.h"
#include "c.h"
#include "closestream.h"
#include "optutils.h"

struct wipe_desc {
	loff_t		offset;		/* magic string offset */
	size_t		len;		/* length of magic string */
	unsigned char	*magic;		/* magic string */

	int		zap;		/* zap this offset? */
	char		*usage;		/* raid, filesystem, ... */
	char		*type;		/* FS type */
	char		*label;		/* FS label */
	char		*uuid;		/* FS uuid */

	int		on_disk;

	struct wipe_desc	*next;
};

enum {
	WP_MODE_PRETTY,		/* default */
	WP_MODE_PARSABLE
};

static const char *type_pattern;

static void
print_pretty(struct wipe_desc *wp, int line)
{
	if (!line) {
		printf("offset               type\n");
		printf("----------------------------------------------------------------\n");
	}

	printf("0x%-17jx  %s   [%s]", wp->offset, wp->type, wp->usage);

	if (wp->label && *wp->label)
		printf("\n%27s %s", "LABEL:", wp->label);
	if (wp->uuid)
		printf("\n%27s %s", "UUID: ", wp->uuid);
	puts("\n");
}

static void
print_parsable(struct wipe_desc *wp, int line)
{
	char enc[256];

	if (!line)
		printf("# offset,uuid,label,type\n");

	printf("0x%jx,", wp->offset);

	if (wp->uuid) {
		blkid_encode_string(wp->uuid, enc, sizeof(enc));
		printf("%s,", enc);
	} else
		fputc(',', stdout);

	if (wp->label) {
		blkid_encode_string(wp->label, enc, sizeof(enc));
		printf("%s,", enc);
	} else
		fputc(',', stdout);

	blkid_encode_string(wp->type, enc, sizeof(enc));
	printf("%s\n", enc);
}

static void
print_all(struct wipe_desc *wp, int mode)
{
	int n = 0;

	while (wp) {
		switch (mode) {
		case WP_MODE_PRETTY:
			print_pretty(wp, n++);
			break;
		case WP_MODE_PARSABLE:
			print_parsable(wp, n++);
			break;
		default:
			abort();
		}
		wp = wp->next;
	}
}

static struct wipe_desc *
add_offset(struct wipe_desc *wp0, loff_t offset, int zap)
{
	struct wipe_desc *wp = wp0;

	while (wp) {
		if (wp->offset == offset)
			return wp;
		wp = wp->next;
	}

	wp = xcalloc(1, sizeof(struct wipe_desc));
	wp->offset = offset;
	wp->next = wp0;
	wp->zap = zap;
	return wp;
}

static struct wipe_desc *
clone_offset(struct wipe_desc *wp0)
{
	struct wipe_desc *wp = NULL;

	while(wp0) {
		wp = add_offset(wp, wp0->offset, wp0->zap);
		wp0 = wp0->next;
	}

	return wp;
}

static struct wipe_desc *
get_desc_for_probe(struct wipe_desc *wp, blkid_probe pr)
{
	const char *off, *type, *mag, *p, *usage = NULL;
	size_t len;
	loff_t offset;
	int rc;

	/* superblocks */
	if (blkid_probe_lookup_value(pr, "TYPE", &type, NULL) == 0) {
		rc = blkid_probe_lookup_value(pr, "SBMAGIC_OFFSET", &off, NULL);
		if (!rc)
			rc = blkid_probe_lookup_value(pr, "SBMAGIC", &mag, &len);
		if (rc)
			return wp;

	/* partitions */
	} else if (blkid_probe_lookup_value(pr, "PTTYPE", &type, NULL) == 0) {
		rc = blkid_probe_lookup_value(pr, "PTMAGIC_OFFSET", &off, NULL);
		if (!rc)
			rc = blkid_probe_lookup_value(pr, "PTMAGIC", &mag, &len);
		if (rc)
			return wp;
		usage = "partition table";
	} else
		return wp;

	if (type_pattern && !match_fstype(type, type_pattern))
		return wp;

	offset = strtoll(off, NULL, 10);

	wp = add_offset(wp, offset, 0);
	if (!wp)
		return NULL;

	if (usage || blkid_probe_lookup_value(pr, "USAGE", &usage, NULL) == 0)
		wp->usage = xstrdup(usage);

	wp->type = xstrdup(type);
	wp->on_disk = 1;

	wp->magic = xmalloc(len);
	memcpy(wp->magic, mag, len);
	wp->len = len;

	if (blkid_probe_lookup_value(pr, "LABEL", &p, NULL) == 0)
		wp->label = xstrdup(p);

	if (blkid_probe_lookup_value(pr, "UUID", &p, NULL) == 0)
		wp->uuid = xstrdup(p);

	return wp;
}

static blkid_probe
new_probe(const char *devname, int mode)
{
	blkid_probe pr;

	if (!devname)
		return NULL;

	if (mode) {
		int fd = open(devname, mode);
		if (fd < 0)
			goto error;

		pr = blkid_new_probe();
		if (pr && blkid_probe_set_device(pr, fd, 0, 0))
			goto error;
	} else
		pr = blkid_new_probe_from_filename(devname);

	if (!pr)
		goto error;

	blkid_probe_enable_superblocks(pr, 1);
	blkid_probe_set_superblocks_flags(pr, BLKID_SUBLKS_MAGIC |
			BLKID_SUBLKS_TYPE | BLKID_SUBLKS_USAGE |
			BLKID_SUBLKS_LABEL | BLKID_SUBLKS_UUID);

	blkid_probe_enable_partitions(pr, 1);
	blkid_probe_set_partitions_flags(pr, BLKID_PARTS_MAGIC);

	return pr;
error:
	err(EXIT_FAILURE, _("error: %s: probing initialization failed"), devname);
	return NULL;
}

static struct wipe_desc *
read_offsets(struct wipe_desc *wp, const char *devname)
{
	blkid_probe pr = new_probe(devname, 0);

	if (!pr)
		return NULL;

	while (blkid_do_probe(pr) == 0) {
		wp = get_desc_for_probe(wp, pr);
		if (!wp)
			break;
	}

	blkid_free_probe(pr);
	return wp;
}

static void
free_wipe(struct wipe_desc *wp)
{
	while (wp) {
		struct wipe_desc *next = wp->next;

		free(wp->usage);
		free(wp->type);
		free(wp->magic);
		free(wp->label);
		free(wp->uuid);
		free(wp);

		wp = next;
	}
}

static void do_wipe_real(blkid_probe pr, const char *devname, struct wipe_desc *w, int noact, int quiet)
{
	size_t i;

	if (blkid_do_wipe(pr, noact))
		warn(_("%s: failed to erase %s magic string at offset 0x%08jx"),
		     devname, w->type, w->offset);

	if (quiet)
		return;

	printf(_("%s: %zd bytes were erased at offset 0x%08jx (%s): "),
		devname, w->len, w->offset, w->type);

	for (i = 0; i < w->len; i++) {
		printf("%02x", w->magic[i]);
		if (i + 1 < w->len)
			fputc(' ', stdout);
	}
	putchar('\n');
}

static struct wipe_desc *
do_wipe(struct wipe_desc *wp, const char *devname, int noact, int all, int quiet)
{
	blkid_probe pr = new_probe(devname, O_RDWR);
	struct wipe_desc *w, *wp0 = clone_offset(wp);
	int zap = all ? 1 : wp->zap;

	if (!pr)
		return NULL;

	while (blkid_do_probe(pr) == 0) {
		wp = get_desc_for_probe(wp, pr);
		if (!wp)
			break;

		/* Check if offset is in provided list */
		w = wp0;
		while(w && w->offset != wp->offset)
			w = w->next;
		if (wp0 && !w)
			continue;

		/* Mark done if found in provided list */
		if (w)
			w->on_disk = wp->on_disk;

		if (!wp->on_disk)
			continue;

		if (zap)
			do_wipe_real(pr, devname, wp, noact, quiet);
	}

	for (w = wp0; w != NULL; w = w->next) {
		if (!w->on_disk && !quiet)
			warnx(_("%s: offset 0x%jx not found"), devname, w->offset);
	}

	fsync(blkid_probe_get_fd(pr));
	close(blkid_probe_get_fd(pr));
	blkid_free_probe(pr);
	free_wipe(wp0);

	return wp;
}


static void __attribute__((__noreturn__))
usage(FILE *out)
{
	fputs(_("\nUsage:\n"), out);
	fprintf(out,
	      _(" %s [options] <device>\n"), program_invocation_short_name);

	fputs(_("\nOptions:\n"), out);
	fputs(_(" -a, --all           wipe all magic strings (BE CAREFUL!)\n"
		" -h, --help          show this help text\n"
		" -n, --no-act        do everything except the actual write() call\n"
		" -o, --offset <num>  offset to erase, in bytes\n"
		" -p, --parsable      print out in parsable instead of printable format\n"
		" -q, --quiet         suppress output messages\n"
		" -t, --types <list>  limit the set of filesystem, RAIDs or partition tables\n"
		" -V, --version       output version information and exit\n"), out);

	fprintf(out, _("\nFor more information see wipefs(8).\n"));

	exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}


int
main(int argc, char **argv)
{
	struct wipe_desc *wp0 = NULL, *wp;
	int c, all = 0, has_offset = 0, noact = 0, quiet = 0;
	int mode = WP_MODE_PRETTY;

	static const struct option longopts[] = {
	    { "all",       0, 0, 'a' },
	    { "help",      0, 0, 'h' },
	    { "no-act",    0, 0, 'n' },
	    { "offset",    1, 0, 'o' },
	    { "parsable",  0, 0, 'p' },
	    { "quiet",     0, 0, 'q' },
	    { "types",     1, 0, 't' },
	    { "version",   0, 0, 'V' },
	    { NULL,        0, 0, 0 }
	};

	static const ul_excl_t excl[] = {       /* rows and cols in in ASCII order */
		{ 'a','o' },
		{ 0 }
	};
	int excl_st[ARRAY_SIZE(excl)] = UL_EXCL_STATUS_INIT;

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	while ((c = getopt_long(argc, argv, "ahno:pqt:V", longopts, NULL)) != -1) {

		err_exclusive_options(c, longopts, excl, excl_st);

		switch(c) {
		case 'a':
			all++;
			break;
		case 'h':
			usage(stdout);
			break;
		case 'n':
			noact++;
			break;
		case 'o':
			wp0 = add_offset(wp0, strtosize_or_err(optarg,
					 _("invalid offset argument")), 1);
			has_offset++;
			break;
		case 'p':
			mode = WP_MODE_PARSABLE;
			break;
		case 'q':
			quiet++;
			break;
		case 't':
			type_pattern = optarg;
			break;
		case 'V':
			printf(_("%s from %s\n"), program_invocation_short_name,
				PACKAGE_STRING);
			return EXIT_SUCCESS;
		default:
			usage(stderr);
			break;
		}
	}

	if (optind == argc)
		usage(stderr);

	if (!all && !has_offset) {
		/*
		 * Print only
		 */
		while (optind < argc) {
			wp0 = read_offsets(NULL, argv[optind++]);
			if (wp0)
				print_all(wp0, mode);
			free_wipe(wp0);
		}
	} else {
		/*
		 * Erase
		 */
		while (optind < argc) {
			wp = clone_offset(wp0);
			wp = do_wipe(wp, argv[optind++], noact, all, quiet);
			free_wipe(wp);
		}
	}

	return EXIT_SUCCESS;
}
