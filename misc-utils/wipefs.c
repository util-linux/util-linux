/*
 * wipefs - utility to wipe filesystems from device
 *
 * Copyright (C) 2009 Red Hat, Inc. All rights reserved.
 * Written by Karel Zak <kzak@redhat.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
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
#include <err.h>
#include <string.h>
#include <limits.h>

#include <blkid.h>

#include "nls.h"

struct wipe_desc {
	loff_t		offset;		/* magic string offset */
	size_t		len;		/* length of magic string */
	unsigned char	*magic;		/* magic string */

	int		zap;		/* zap this offset? */
	char		*usage;		/* raid, filesystem, ... */
	char		*type;		/* FS type */
	char		*label;		/* FS label */
	char		*uuid;		/* FS uuid */

	struct wipe_desc	*next;
};

#define WP_MODE_PRETTY		0		/* default */
#define WP_MODE_PARSABLE	1

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

	wp = calloc(1, sizeof(struct wipe_desc));
	if (!wp)
		err(EXIT_FAILURE, _("calloc failed"));

	wp->offset = offset;
	wp->next = wp0;
	wp->zap = zap;
	return wp;
}

static inline void *
xmalloc(size_t sz)
{
	void *x = malloc(sz);
	if (!x)
		err(EXIT_FAILURE, _("malloc failed"));
	return x;
}

static inline char *
xstrdup(const char *s)
{
	char *x = strdup(s);
	if (!x)
		err(EXIT_FAILURE, _("strdup failed"));
	return x;
}

static struct wipe_desc *
get_offset_from_probe(struct wipe_desc *wp, blkid_probe pr, int zap)
{
	const char *off, *type, *usage, *mag;
	size_t len;

	if (blkid_probe_lookup_value(pr, "TYPE", &type, NULL) == 0 &&
	    blkid_probe_lookup_value(pr, "SBMAGIC_OFFSET", &off, NULL) == 0 &&
	    blkid_probe_lookup_value(pr, "SBMAGIC", &mag, &len) == 0 &&
	    blkid_probe_lookup_value(pr, "USAGE", &usage, NULL) == 0) {

		loff_t offset = strtoll(off, NULL, 10);
		const char *p;

		wp = add_offset(wp, offset, zap);
		if (!wp)
			return NULL;

		wp->usage = xstrdup(usage);
		wp->type = xstrdup(type);

		wp->magic = xmalloc(wp->len);
		memcpy(wp->magic, mag, len);
		wp->len = len;

		if (blkid_probe_lookup_value(pr, "LABEL", &p, NULL) == 0)
			wp->label = xstrdup(p);

		if (blkid_probe_lookup_value(pr, "UUID", &p, NULL) == 0)
			wp->uuid = xstrdup(p);
	}

	return wp;
}

static struct wipe_desc *
read_offsets(struct wipe_desc *wp, const char *fname, int zap)
{
	blkid_probe pr;
	int rc;

	if (!fname)
		return NULL;

	pr = blkid_new_probe_from_filename(fname);
	if (!pr)
		errx(EXIT_FAILURE, _("error: %s: probing initialization failed"), fname);

	blkid_probe_enable_superblocks(pr, 0);	/* enabled by default ;-( */

	blkid_probe_enable_partitions(pr, 1);
	rc = blkid_do_fullprobe(pr);
	blkid_probe_enable_partitions(pr, 0);

	if (rc == 0) {
		const char *type = NULL;
		blkid_probe_lookup_value(pr, "PTTYPE", &type, NULL);
		errx(EXIT_FAILURE, _("error: %s: appears to contain '%s' "
				"partition table"), fname, type);
	}

	blkid_probe_enable_superblocks(pr, 1);
	blkid_probe_set_superblocks_flags(pr, BLKID_SUBLKS_MAGIC |
			BLKID_SUBLKS_TYPE | BLKID_SUBLKS_USAGE |
			BLKID_SUBLKS_LABEL | BLKID_SUBLKS_UUID);

	while (blkid_do_probe(pr) == 0) {
		wp = get_offset_from_probe(wp, pr, zap);
		if (!wp)
			break;
	}

	blkid_free_probe(pr);
	return wp;
}

static int
write_all(int fd, const void *buf, size_t count)
{
	while(count) {
		ssize_t tmp;

		errno = 0;
		tmp = write(fd, buf, count);
		if (tmp > 0) {
			count -= tmp;
			if (count)
				buf += tmp;
		} else if (errno != EINTR && errno != EAGAIN)
			return -1;
	}
	return 0;
}

static int
do_wipe_offset(int fd, struct wipe_desc *wp, const char *fname, int noact)
{
	char buf[BUFSIZ];
	int i;
	off_t l;
	size_t len;

	if (!wp->type) {
		warnx(_("can't found a magic string at offset "
				"0x%jx - ignore."), wp->offset);
		return 0;
	}

	l = lseek(fd, wp->offset, SEEK_SET);
	if (l == (off_t) -1)
		err(EXIT_FAILURE, _("%s: failed to seek to offset 0x%jx"),
				fname, wp->offset);

	len = wp->len > sizeof(buf) ? sizeof(buf) : wp->len;

	memset(buf, 0, len);
	if (noact == 0 && write_all(fd, buf, len))
		err(EXIT_FAILURE, _("%s: write failed"), fname);

	printf(_("%zd bytes ["), wp->len);

	for (i = 0; i < len; i++) {
		printf("%02x", wp->magic[i]);
		if (i + 1 < len)
			fputc(' ', stdout);
	}

	printf(_("] erased at offset 0x%jx (%s)\n"), wp->offset, wp->type);
	return 0;
}

static int
do_wipe(struct wipe_desc *wp, const char *fname, int noact)
{
	int fd;

	fd = open(fname, O_WRONLY);
	if (fd < 0)
		err(EXIT_FAILURE, _("%s: open failed"), fname);

	while (wp) {
		if (wp->zap)
			do_wipe_offset(fd, wp, fname, noact);
		wp = wp->next;
	}

	close(fd);
	return 0;
}

static loff_t
strtoll_offset(const char *str)
{
	char *end = NULL;
	loff_t off;

	errno = 0;
	off = strtoll(str, &end, 0);

	if ((errno == ERANGE && (off == LLONG_MAX || off == LONG_MIN)) ||
	    (errno != 0 && off == 0))
		err(EXIT_FAILURE, _("invalid offset '%s' value specified"), str);

	if (*end != '\0')
		errx(EXIT_FAILURE, _("invalid offset '%s' value specified"), str);

	return off;
}

static void __attribute__((__noreturn__))
usage(FILE *out)
{
	fprintf(out, _("Usage: %s [options] <device>\n\nOptions:\n"),
			program_invocation_short_name);

	fprintf(out, _(
	" -a, --all           wipe all magic strings (BE CAREFUL!)\n"
	" -h, --help          this help\n"
	" -n, --no-act        everything to be done except for the write() call\n"
	" -o, --offset <num>  offset to erase, in bytes\n"
	" -p, --parsable      print out in parsable instead of printable format\n"));

	fprintf(out, _("\nFor more information see wipefs(8).\n"));

	exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}


int
main(int argc, char **argv)
{
	struct wipe_desc *wp = NULL;
	int c, all = 0, has_offset = 0, noact = 0, mode = 0;
	const char *fname;

	struct option longopts[] = {
	    { "all",       0, 0, 'a' },
	    { "help",      0, 0, 'h' },
	    { "no-act",    0, 0, 'n' },
	    { "offset",    1, 0, 'o' },
	    { "parsable",  0, 0, 'p' },
	    { NULL,        0, 0, 0 }
	};

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

	while ((c = getopt_long(argc, argv, "ahno:p", longopts, NULL)) != -1) {
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
			wp = add_offset(wp, strtoll_offset(optarg), 1);
			has_offset++;
			break;
		case 'p':
			mode = WP_MODE_PARSABLE;
			break;
		default:
			usage(stderr);
			break;
		}
	}

	if (wp && all)
		errx(EXIT_FAILURE, _("--offset and --all are mutually exclusive"));
	if (optind == argc)
		usage(stderr);

	fname = argv[optind++];

	wp = read_offsets(wp, fname, all);

	if (wp) {
		if (has_offset || all)
			do_wipe(wp, fname, noact);
		else
			print_all(wp, mode);
	}
	return EXIT_SUCCESS;
}
