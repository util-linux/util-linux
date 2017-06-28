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
#include <libgen.h>

#include <blkid.h>

#include "nls.h"
#include "xalloc.h"
#include "strutils.h"
#include "all-io.h"
#include "match.h"
#include "c.h"
#include "closestream.h"
#include "optutils.h"
#include "blkdev.h"

struct wipe_desc {
	loff_t		offset;		/* magic string offset */
	size_t		len;		/* length of magic string */
	unsigned char	*magic;		/* magic string */

	char		*usage;		/* raid, filesystem, ... */
	char		*type;		/* FS type */
	char		*label;		/* FS label */
	char		*uuid;		/* FS uuid */

	struct wipe_desc	*next;

	unsigned int	zap : 1,
			on_disk : 1,
			is_parttable : 1;

};

struct wipe_control {
	const char	*devname;
	const char	*type_pattern;

	unsigned int	noact : 1,
			all : 1,
			quiet : 1,
			backup : 1,
			force : 1,
			parsable : 1;
};

static void
print_pretty(struct wipe_desc *wp, int line)
{
	if (!line) {
		printf("offset               type\n");
		printf("----------------------------------------------------------------\n");
	}

	printf("0x%-17jx  %s   [%s]", (intmax_t)wp->offset, wp->type, _(wp->usage));

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

	printf("0x%jx,", (intmax_t)wp->offset);

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
print_all(struct wipe_control *ctl, struct wipe_desc *wp)
{
	int n = 0;

	for (/*nothing*/; wp; wp = wp->next) {
		if (ctl->parsable)
			print_parsable(wp, n++);
		else
			print_pretty(wp, n++);
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
	wp->zap = zap ? 1 : 0;
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
get_desc_for_probe(struct wipe_control *ctl, struct wipe_desc *wp, blkid_probe pr, int *found)
{
	const char *off, *type, *mag, *p, *usage = NULL;
	size_t len;
	loff_t offset;
	int rc, ispt = 0;

	if (found)
		*found = 0;

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
		usage = N_("partition table");
		ispt = 1;
	} else
		return wp;

	if (ctl->type_pattern && !match_fstype(type, ctl->type_pattern))
		return wp;

	offset = strtoll(off, NULL, 10);

	wp = add_offset(wp, offset, 0);
	if (!wp)
		return NULL;

	if (usage || blkid_probe_lookup_value(pr, "USAGE", &usage, NULL) == 0)
		wp->usage = xstrdup(usage);

	wp->type = xstrdup(type);
	wp->on_disk = 1;
	wp->is_parttable = ispt ? 1 : 0;

	wp->magic = xmalloc(len);
	memcpy(wp->magic, mag, len);
	wp->len = len;

	if (blkid_probe_lookup_value(pr, "LABEL", &p, NULL) == 0)
		wp->label = xstrdup(p);

	if (blkid_probe_lookup_value(pr, "UUID", &p, NULL) == 0)
		wp->uuid = xstrdup(p);

	if (found)
		*found = 1;
	return wp;
}

static blkid_probe
new_probe(const char *devname, int mode)
{
	blkid_probe pr = NULL;

	if (!devname)
		return NULL;

	if (mode) {
		int fd = open(devname, mode);
		if (fd < 0)
			goto error;

		pr = blkid_new_probe();
		if (!pr || blkid_probe_set_device(pr, fd, 0, 0) != 0) {
			close(fd);
			goto error;
		}
	} else
		pr = blkid_new_probe_from_filename(devname);

	if (!pr)
		goto error;

	blkid_probe_enable_superblocks(pr, 1);
	blkid_probe_set_superblocks_flags(pr,
			BLKID_SUBLKS_MAGIC |	/* return magic string and offset */
			BLKID_SUBLKS_TYPE |	/* return superblock type */
			BLKID_SUBLKS_USAGE |	/* return USAGE= */
			BLKID_SUBLKS_LABEL |	/* return LABEL= */
			BLKID_SUBLKS_UUID |	/* return UUID= */
			BLKID_SUBLKS_BADCSUM);	/* accept bad checksums */

	blkid_probe_enable_partitions(pr, 1);
	blkid_probe_set_partitions_flags(pr, BLKID_PARTS_MAGIC |
					     BLKID_PARTS_FORCE_GPT);
	return pr;
error:
	blkid_free_probe(pr);
	err(EXIT_FAILURE, _("error: %s: probing initialization failed"), devname);
}

static struct wipe_desc *
read_offsets(struct wipe_control *ctl, struct wipe_desc *wp)
{
	blkid_probe pr = new_probe(ctl->devname, 0);

	if (!pr)
		return NULL;

	while (blkid_do_probe(pr) == 0) {
		int found = 0;

		wp = get_desc_for_probe(ctl, wp, pr, &found);
		if (!wp)
			break;

		/* hide last detected signature and scan again */
		if (found) {
			blkid_probe_hide_range(pr, wp->offset, wp->len);
			blkid_probe_step_back(pr);
		}
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

static void do_wipe_real(struct wipe_control *ctl, blkid_probe pr,
			struct wipe_desc *w)
{
	size_t i;

	if (blkid_do_wipe(pr, ctl->noact) != 0)
		err(EXIT_FAILURE, _("%s: failed to erase %s magic string at offset 0x%08jx"),
		     ctl->devname, w->type, (intmax_t)w->offset);

	if (ctl->quiet)
		return;

	printf(P_("%s: %zd byte was erased at offset 0x%08jx (%s): ",
		  "%s: %zd bytes were erased at offset 0x%08jx (%s): ",
		  w->len),
	       ctl->devname, w->len, (intmax_t)w->offset, w->type);

	for (i = 0; i < w->len; i++) {
		printf("%02x", w->magic[i]);
		if (i + 1 < w->len)
			fputc(' ', stdout);
	}
	putchar('\n');
}

static void do_backup(struct wipe_desc *wp, const char *base)
{
	char *fname = NULL;
	int fd;

	xasprintf(&fname, "%s0x%08jx.bak", base, (intmax_t)wp->offset);

	fd = open(fname, O_CREAT | O_WRONLY, S_IRUSR | S_IWUSR);
	if (fd < 0)
		goto err;
	if (write_all(fd, wp->magic, wp->len) != 0)
		goto err;
	close(fd);
	free(fname);
	return;
err:
	err(EXIT_FAILURE, _("%s: failed to create a signature backup"), fname);
}

#ifdef BLKRRPART
static void rereadpt(int fd, const char *devname)
{
	struct stat st;

	if (fstat(fd, &st) || !S_ISBLK(st.st_mode))
		return;

	errno = 0;
	ioctl(fd, BLKRRPART);
	printf(_("%s: calling ioctl to re-read partition table: %m\n"), devname);
}
#endif

static struct wipe_desc *
do_wipe(struct wipe_control *ctl, struct wipe_desc *wp)
{
	int mode = O_RDWR, reread = 0, need_force = 0;
	blkid_probe pr;
	struct wipe_desc *w, *wp0;
	int zap = ctl->all ? 1 : wp->zap;
	char *backup = NULL;

	if (!ctl->force)
		mode |= O_EXCL;

	pr = new_probe(ctl->devname, mode);
	if (!pr)
		return NULL;

	if (zap && ctl->backup) {
		const char *home = getenv ("HOME");
		char *tmp = xstrdup(ctl->devname);

		if (!home)
			errx(EXIT_FAILURE, _("failed to create a signature backup, $HOME undefined"));
		xasprintf (&backup, "%s/wipefs-%s-", home, basename(tmp));
		free(tmp);
	}

	wp0 = clone_offset(wp);

	while (blkid_do_probe(pr) == 0) {
		wp = get_desc_for_probe(ctl, wp, pr, NULL);
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

		if (!ctl->force
		    && wp->is_parttable
		    && !blkid_probe_is_wholedisk(pr)) {
			warnx(_("%s: ignoring nested \"%s\" partition table "
				"on non-whole disk device"), ctl->devname, wp->type);
			need_force = 1;
			continue;
		}


		if (zap) {
			if (backup)
				do_backup(wp, backup);
			do_wipe_real(ctl, pr, wp);
			if (wp->is_parttable)
				reread = 1;
		}
	}

	for (w = wp0; w != NULL; w = w->next) {
		if (!w->on_disk && !ctl->quiet)
			warnx(_("%s: offset 0x%jx not found"),
					ctl->devname, (uintmax_t)w->offset);
	}

	if (need_force)
		warnx(_("Use the --force option to force erase."));

	fsync(blkid_probe_get_fd(pr));

#ifdef BLKRRPART
	if (reread && (mode & O_EXCL))
		rereadpt(blkid_probe_get_fd(pr), ctl->devname);
#endif

	close(blkid_probe_get_fd(pr));
	blkid_free_probe(pr);
	free_wipe(wp0);
	free(backup);

	return wp;
}


static void __attribute__((__noreturn__))
usage(void)
{
	FILE *out = stdout;
	fputs(USAGE_HEADER, out);
	fprintf(out,
	      _(" %s [options] <device>\n"), program_invocation_short_name);

	fputs(USAGE_SEPARATOR, out);
	fputs(_("Wipe signatures from a device.\n"), out);

	fputs(USAGE_OPTIONS, out);
	fputs(_(" -a, --all           wipe all magic strings (BE CAREFUL!)\n"
		" -b, --backup        create a signature backup in $HOME\n"
		" -f, --force         force erasure\n"
		" -n, --no-act        do everything except the actual write() call\n"
		" -o, --offset <num>  offset to erase, in bytes\n"
		" -p, --parsable      print out in parsable instead of printable format\n"
		" -q, --quiet         suppress output messages\n"
		" -t, --types <list>  limit the set of filesystem, RAIDs or partition tables\n"
		), out);
	print_usage_help_options(21);

	fprintf(out, USAGE_MAN_TAIL("wipefs(8)"));
	exit(EXIT_SUCCESS);
}


int
main(int argc, char **argv)
{
	struct wipe_control ctl = { .devname = NULL };
	struct wipe_desc *wp0 = NULL;
	int c, noffsets = 0;

	static const struct option longopts[] = {
	    { "all",       no_argument,       NULL, 'a' },
	    { "backup",    no_argument,       NULL, 'b' },
	    { "force",     no_argument,       NULL, 'f' },
	    { "help",      no_argument,       NULL, 'h' },
	    { "no-act",    no_argument,       NULL, 'n' },
	    { "offset",    required_argument, NULL, 'o' },
	    { "parsable",  no_argument,       NULL, 'p' },
	    { "quiet",     no_argument,       NULL, 'q' },
	    { "types",     required_argument, NULL, 't' },
	    { "version",   no_argument,       NULL, 'V' },
	    { NULL,        0, NULL, 0 }
	};

	static const ul_excl_t excl[] = {       /* rows and cols in ASCII order */
		{ 'a','o' },
		{ 0 }
	};
	int excl_st[ARRAY_SIZE(excl)] = UL_EXCL_STATUS_INIT;

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	while ((c = getopt_long(argc, argv, "abfhno:pqt:V", longopts, NULL)) != -1) {

		err_exclusive_options(c, longopts, excl, excl_st);

		switch(c) {
		case 'a':
			ctl.all = 1;
			break;
		case 'b':
			ctl.backup = 1;
			break;
		case 'f':
			ctl.force = 1;
			break;
		case 'h':
			usage();
			break;
		case 'n':
			ctl.noact = 1;
			break;
		case 'o':
			wp0 = add_offset(wp0, strtosize_or_err(optarg,
					 _("invalid offset argument")), 1);
			noffsets++;
			break;
		case 'p':
			ctl.parsable = 1;
			break;
		case 'q':
			ctl.quiet = 1;
			break;
		case 't':
			ctl.type_pattern = optarg;
			break;
		case 'V':
			printf(UTIL_LINUX_VERSION);
			return EXIT_SUCCESS;
		default:
			errtryhelp(EXIT_FAILURE);
		}
	}

	if (optind == argc) {
		warnx(_("no device specified"));
		errtryhelp(EXIT_FAILURE);

	}

	if (ctl.backup && !(ctl.all || noffsets))
		warnx(_("The --backup option is meaningless in this context"));

	if (!ctl.all && !noffsets) {
		/*
		 * Print only
		 */

		while (optind < argc) {
			ctl.devname = argv[optind++];
			wp0 = read_offsets(&ctl, NULL);
			if (wp0)
				print_all(&ctl, wp0);
			free_wipe(wp0);
		}
	} else {
		/*
		 * Erase
		 */
		while (optind < argc) {
			struct wipe_desc *wp = clone_offset(wp0);

			ctl.devname = argv[optind++];
			wp = do_wipe(&ctl, wp);
			free_wipe(wp);
		}
	}

	return EXIT_SUCCESS;
}
