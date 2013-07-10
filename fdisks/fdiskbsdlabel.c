/*
   NetBSD disklabel editor for Linux fdisk
   Written by Bernhard Fastenrath (fasten@informatik.uni-bonn.de)
   with code from the NetBSD disklabel command:

   Copyright (c) 1987, 1988 Regents of the University of California.
   All rights reserved.

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions
   are met:
   1. Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
   2. Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
   3. All advertising materials mentioning features or use of this software
      must display the following acknowledgement:
	This product includes software developed by the University of
	California, Berkeley and its contributors.
   4. Neither the name of the University nor the names of its contributors
      may be used to endorse or promote products derived from this software
      without specific prior written permission.

   THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
   ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
   IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
   ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
   FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
   DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
   OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
   HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
   LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
   OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
   SUCH DAMAGE.

   Changes:
   19990319 - Arnaldo Carvalho de Melo <acme@conectiva.com.br> - i18n/nls

   20000101 - David Huggins-Daines <dhuggins@linuxcare.com> - Better
   support for OSF/1 disklabels on Alpha.
   Also fixed unaligned accesses in alpha_bootblock_checksum()
*/

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <ctype.h>
#include <setjmp.h>
#include <errno.h>
#include "nls.h"

#include <sys/param.h>

#include "common.h"
#include "fdisk.h"
#include "pt-mbr.h"

#include "fdiskbsdlabel.h"
#include "all-io.h"

static char *xbsd_dktypenames[] = {
	"unknown",
	"SMD",
	"MSCP",
	"old DEC",
	"SCSI",
	"ESDI",
	"ST506",
	"HP-IB",
	"HP-FL",
	"type 9",
	"floppy",
	0
};
#define BSD_DKMAXTYPES	(ARRAY_SIZE(xbsd_dktypenames) - 1)

static struct fdisk_parttype xbsd_fstypes[] = {
        {BSD_FS_UNUSED, "unused"},
	{BSD_FS_SWAP,   "swap"},
	{BSD_FS_V6,     "Version 6"},
	{BSD_FS_V7,     "Version 7"},
	{BSD_FS_SYSV,   "System V"},
	{BSD_FS_V71K,   "4.1BSD"},
	{BSD_FS_V8,     "Eighth Edition"},
	{BSD_FS_BSDFFS, "4.2BSD"},
#ifdef __alpha__
	{BSD_FS_EXT2,   "ext2"},
#else
	{BSD_FS_MSDOS,  "MS-DOS"},
#endif
	{BSD_FS_BSDLFS, "4.4LFS"},
	{BSD_FS_OTHER,  "unknown"},
	{BSD_FS_HPFS,   "HPFS"},
	{BSD_FS_ISO9660,"ISO-9660"},
	{BSD_FS_BOOT,   "boot"},
	{BSD_FS_ADOS,   "ADOS"},
	{BSD_FS_HFS,    "HFS"},
	{BSD_FS_ADVFS,	"AdvFS"},
	{ 0, NULL }
};
#define BSD_FSMAXTYPES (ARRAY_SIZE(xbsd_fstypes)-1)

/*
 * in-memory fdisk BSD stuff
 */
struct fdisk_bsd_label {
	struct fdisk_label	head;		/* generic part */

	struct dos_partition *dos_part;		/* parent */
	struct bsd_disklabel bsd;		/* on disk label */
};


static int xbsd_delete_part (struct fdisk_context *cxt, size_t partnum);
static void xbsd_edit_disklabel (struct fdisk_context *cxt);
static int xbsd_write_bootstrap (struct fdisk_context *cxt);
static void xbsd_change_fstype (struct fdisk_context *cxt);
static int xbsd_get_part_index (struct fdisk_context *cxt, int max, int *n);
static int xbsd_check_new_partition (struct fdisk_context *cxt, int *i);
static unsigned short xbsd_dkcksum (struct bsd_disklabel *lp);
static int xbsd_initlabel(struct fdisk_context *cxt);
static int xbsd_readlabel(struct fdisk_context *cxt);
static int xbsd_writelabel(struct fdisk_context *cxt);
static void sync_disks (void);

#if defined (__alpha__)
void alpha_bootblock_checksum (char *boot);
#endif

#if !defined (__alpha__)
static int xbsd_translate_fstype (int linux_type);
static void xbsd_link_part (struct fdisk_context *cxt);
#endif

#if defined (__alpha__)
/* We access this through a u_int64_t * when checksumming */
static char bsdbuffer[BSD_BBSIZE] __attribute__((aligned(8)));
#else
static char bsdbuffer[BSD_BBSIZE];
#endif


#define bsd_cround(c, n) \
	(fdisk_context_use_cylinders(c) ? ((n)/self_disklabel(c)->d_secpercyl) + 1 : (n))


static inline struct fdisk_bsd_label *self_label(struct fdisk_context *cxt)
{
	assert(cxt);
	assert(cxt->label);
	assert(fdisk_is_disklabel(cxt, OSF));

	return (struct fdisk_bsd_label *) cxt->label;
}

static inline struct bsd_disklabel *self_disklabel(struct fdisk_context *cxt)
{
	assert(cxt);
	assert(cxt->label);
	assert(fdisk_is_disklabel(cxt, OSF));

	return &((struct fdisk_bsd_label *) cxt->label)->bsd;
}

#define HIDDEN_MASK	0x10

static int is_bsd_partition_type(int type)
{
	return (type == MBR_FREEBSD_PARTITION ||
		type == (MBR_FREEBSD_PARTITION ^ HIDDEN_MASK) ||
		type == MBR_NETBSD_PARTITION ||
		type == (MBR_NETBSD_PARTITION ^ HIDDEN_MASK) ||
		type == MBR_OPENBSD_PARTITION ||
		type == (MBR_OPENBSD_PARTITION ^ HIDDEN_MASK));
}

/*
 * look for DOS partition usable for nested BSD partition table
 */
static int bsd_assign_dos_partition(struct fdisk_context *cxt)
{
	struct fdisk_bsd_label *l = self_label(cxt);
	size_t i;

	for (i = 0; i < 4; i++) {
		sector_t ss;

		l->dos_part = fdisk_dos_get_partition(cxt->parent, i);

		if (!l->dos_part || !is_bsd_partition_type(l->dos_part->sys_ind))
			continue;

		ss = dos_partition_get_start(l->dos_part);
		if (!ss) {
			fprintf (stderr, _("Partition %zd: has invalid starting sector 0.\n"), i + 1);
			return -1;
		}

		if (cxt->parent->dev_path) {
			free(cxt->dev_path);
			cxt->dev_path = fdisk_partname(
						cxt->parent->dev_path, i + 1);
		}

		DBG(LABEL, dbgprint("partition %zu assigned to BSD", i + 1));
		return 0;
	}

	printf (_("There is no *BSD partition on %s.\n"), cxt->parent->dev_path);
	free(cxt->dev_path);
	cxt->dev_path = NULL;
	l->dos_part = NULL;
	return 1;
}

static int bsd_probe_label(struct fdisk_context *cxt)
{
	int rc = 0;

	if (cxt->parent)
		rc = bsd_assign_dos_partition(cxt);	/* nested BSD partiotn table */
	if (!rc)
		rc = xbsd_readlabel(cxt);
	if (!rc)
		return 1;	/* found BSD */
	return 0;		/* not found */
}

static int xbsd_write_disklabel (struct fdisk_context *cxt)
{
	printf (_("Writing disklabel to %s.\n"), cxt->dev_path);
	xbsd_writelabel(cxt);
	reread_partition_table(cxt, 0);	/* no exit yet */
	return 0;
}

static int xbsd_add_part (struct fdisk_context *cxt,
		size_t i,
		struct fdisk_parttype *t __attribute__((__unused__)))
{
	struct fdisk_bsd_label *l = self_label(cxt);
	struct bsd_disklabel *d = self_disklabel(cxt);
	struct fdisk_ask *ask;
	unsigned int begin = 0, end;
	int rc;

	if (l->dos_part) {
		begin = dos_partition_get_start(l->dos_part);
		end = begin + dos_partition_get_size(l->dos_part) - 1;
	} else
		end = d->d_secperunit - 1;

	ask = fdisk_new_ask();

	/*
	 * First sector
	 */
	if (fdisk_context_use_cylinders(cxt))
		fdisk_ask_set_query(ask, _("First cylinder"));
	else
		fdisk_ask_set_query(ask, _("First sector"));

	fdisk_ask_set_type(ask, FDISK_ASKTYPE_NUMBER);
	fdisk_ask_number_set_low(ask, bsd_cround(cxt, begin));
	fdisk_ask_number_set_default(ask, bsd_cround(cxt, begin));
	fdisk_ask_number_set_high(ask, bsd_cround(cxt, end));

	rc = fdisk_do_ask(cxt, ask);
	if (rc) {
		fdisk_free_ask(ask);
		return rc;
	}
	begin = fdisk_ask_number_get_result(ask);

	if (fdisk_context_use_cylinders(cxt))
		begin = (begin - 1) * d->d_secpercyl;

	fdisk_reset_ask(ask);

	/*
	 * Last sector
	 */
	fdisk_ask_set_type(ask, FDISK_ASKTYPE_OFFSET);

	if (fdisk_context_use_cylinders(cxt)) {
		fdisk_ask_set_query(ask, _("Last cylinder, +cylinders or +size{K,M,G,T,P}"));
		fdisk_ask_number_set_unit(ask,
			     cxt->sector_size *
			     fdisk_context_get_units_per_sector(cxt));
	} else {
		fdisk_ask_set_query(ask, _("Last sector, +sectors or +size{K,M,G,T,P}"));
		fdisk_ask_number_set_unit(ask,cxt->sector_size);
	}

	fdisk_ask_number_set_low(ask, bsd_cround(cxt, begin));
	fdisk_ask_number_set_default(ask, bsd_cround(cxt, end));
	fdisk_ask_number_set_high(ask, bsd_cround(cxt, end));
	fdisk_ask_number_set_base(ask, bsd_cround(cxt, begin));

	rc = fdisk_do_ask(cxt, ask);
	end = fdisk_ask_number_get_result(ask);
	fdisk_free_ask(ask);
	if (rc)
		return rc;

	if (fdisk_context_use_cylinders(cxt))
		end = end * d->d_secpercyl - 1;

	d->d_partitions[i].p_size   = end - begin + 1;
	d->d_partitions[i].p_offset = begin;
	d->d_partitions[i].p_fstype = BSD_FS_UNUSED;

	cxt->label->nparts_cur = d->d_npartitions;
	fdisk_label_set_changed(cxt->label, 1);

	return 0;
}

/* Returns 0 on success, < 0 on error. */
static int xbsd_create_disklabel(struct fdisk_context *cxt)
{
	int rc, yes = 0;
	struct bsd_disklabel *d = self_disklabel(cxt);

	fdisk_info(cxt, _("The device %s does not contain BSD disklabel."), cxt->dev_path);
	rc = fdisk_ask_yesno(cxt,
			_("Do you want to create a BSD disklabel?"),
			&yes);

	if (rc || !yes)
		return rc;
	if (cxt->parent) {
		rc = bsd_assign_dos_partition(cxt);
		if (rc == 1)
			/* not found DOS partition usable for BSD label */
			rc = -EINVAL;
	}
	if (rc)
		return rc;

	rc = xbsd_initlabel(cxt);
	if (!rc) {
		xbsd_print_disklabel (cxt, 1);
		cxt->label->nparts_cur = d->d_npartitions;
		cxt->label->nparts_max = BSD_MAXPARTITIONS;
	}

	return rc;
}


void
bsd_command_prompt (struct fdisk_context *cxt)
{

  while (1) {
    char buf[16];
    int rc;
    size_t n;

    /*
     * BIG-FAT-TODO: don't use bsd_command_prompt(), just initialialize BSD
     * stuff by label probe() libfdisk function, and use standard fdisk.c
     * menu code.
     */
    putchar ('\n');
    rc = get_user_reply(cxt, _("BSD disklabel command (m for help): "),
			    buf, sizeof(buf));
    if (rc)
      return;

    switch (tolower(buf[0])) {
      case 'd':
	if (fdisk_ask_partnum(cxt, &n, FALSE) == 0)
		xbsd_delete_part(cxt, n);
	break;
      case 'e':
	xbsd_edit_disklabel (cxt);
	break;
      case 'i':
	xbsd_write_bootstrap (cxt);
	break;
      case 'l':
	list_partition_types (cxt);
	break;
      case 'n':
	if (fdisk_ask_partnum(cxt, &n, TRUE) == 0)
	        xbsd_add_part(cxt, n, 0);
	break;
      case 'p':
	      xbsd_print_disklabel (cxt, 0);
	break;
      case 'q':
	close (cxt->dev_fd);
	exit ( EXIT_SUCCESS );
      case 'r':
	return;
      case 's':
	      xbsd_print_disklabel (cxt, 1);
	break;
      case 't':
	xbsd_change_fstype (cxt);
	break;
      case 'u':
	toggle_units(cxt);
	break;
      case 'w':
	xbsd_write_disklabel (cxt);
	break;
#if !defined (__alpha__)
      case 'x':
	xbsd_link_part (cxt);
	break;
#endif
      default:
	print_fdisk_menu(cxt);
	break;
    }
  }
}

static int xbsd_delete_part(
		struct fdisk_context *cxt,
		size_t partnum)
{
	struct bsd_disklabel *d = self_disklabel(cxt);

	d->d_partitions[partnum].p_size   = 0;
	d->d_partitions[partnum].p_offset = 0;
	d->d_partitions[partnum].p_fstype = BSD_FS_UNUSED;

	if (d->d_npartitions == partnum + 1)
		while (!d->d_partitions[d->d_npartitions - 1].p_size)
			d->d_npartitions--;

	cxt->label->nparts_cur = d->d_npartitions;
	fdisk_label_set_changed(cxt->label, 1);
	return 0;
}

void
xbsd_print_disklabel (struct fdisk_context *cxt, int show_all)
{
  struct bsd_disklabel *lp = self_disklabel(cxt);
  struct bsd_partition *pp;
  FILE *f = stdout;
  int i, j;

  if (show_all) {
    fprintf(f, "# %s:\n", cxt->dev_path);
    if ((unsigned) lp->d_type < BSD_DKMAXTYPES)
      fprintf(f, _("type: %s\n"), xbsd_dktypenames[lp->d_type]);
    else
      fprintf(f, _("type: %d\n"), lp->d_type);
    fprintf(f, _("disk: %.*s\n"), (int) sizeof(lp->d_typename), lp->d_typename);
    fprintf(f, _("label: %.*s\n"), (int) sizeof(lp->d_packname), lp->d_packname);
    fprintf(f, _("flags:"));
    if (lp->d_flags & BSD_D_REMOVABLE)
      fprintf(f, _(" removable"));
    if (lp->d_flags & BSD_D_ECC)
      fprintf(f, _(" ecc"));
    if (lp->d_flags & BSD_D_BADSECT)
      fprintf(f, _(" badsect"));
    fprintf(f, "\n");
    /* On various machines the fields of *lp are short/int/long */
    /* In order to avoid problems, we cast them all to long. */
    fprintf(f, _("bytes/sector: %ld\n"), (long) lp->d_secsize);
    fprintf(f, _("sectors/track: %ld\n"), (long) lp->d_nsectors);
    fprintf(f, _("tracks/cylinder: %ld\n"), (long) lp->d_ntracks);
    fprintf(f, _("sectors/cylinder: %ld\n"), (long) lp->d_secpercyl);
    fprintf(f, _("cylinders: %ld\n"), (long) lp->d_ncylinders);
    fprintf(f, _("rpm: %d\n"), lp->d_rpm);
    fprintf(f, _("interleave: %d\n"), lp->d_interleave);
    fprintf(f, _("trackskew: %d\n"), lp->d_trackskew);
    fprintf(f, _("cylinderskew: %d\n"), lp->d_cylskew);
    fprintf(f, _("headswitch: %ld\t\t# milliseconds\n"),
	    (long) lp->d_headswitch);
    fprintf(f, _("track-to-track seek: %ld\t# milliseconds\n"),
	    (long) lp->d_trkseek);
    fprintf(f, _("drivedata: "));
    for (i = ARRAY_SIZE(lp->d_drivedata)- 1; i >= 0; i--)
      if (lp->d_drivedata[i])
	break;
    if (i < 0)
      i = 0;
    for (j = 0; j <= i; j++)
      fprintf(f, "%ld ", (long) lp->d_drivedata[j]);
  }
  fprintf (f, _("\n%d partitions:\n"), lp->d_npartitions);
  fprintf (f, _("#       start       end      size     fstype   [fsize bsize   cpg]\n"));
  pp = lp->d_partitions;
  for (i = 0; i < lp->d_npartitions; i++, pp++) {
    if (pp->p_size) {
      if (fdisk_context_use_cylinders(cxt) && lp->d_secpercyl) {
	fprintf(f, "  %c: %8ld%c %8ld%c %8ld%c  ",
		'a' + i,
		(long) pp->p_offset / lp->d_secpercyl + 1,
		(pp->p_offset % lp->d_secpercyl) ? '*' : ' ',
		(long) (pp->p_offset + pp->p_size + lp->d_secpercyl - 1)
			/ lp->d_secpercyl,
		((pp->p_offset + pp->p_size) % lp->d_secpercyl) ? '*' : ' ',
		(long) pp->p_size / lp->d_secpercyl,
		(pp->p_size % lp->d_secpercyl) ? '*' : ' ');
      } else {
	fprintf(f, "  %c: %8ld  %8ld  %8ld   ",
		'a' + i,
		(long) pp->p_offset,
		(long) pp->p_offset + pp->p_size - 1,
		(long) pp->p_size);
      }
      if ((unsigned) pp->p_fstype < BSD_FSMAXTYPES)
	fprintf(f, "%8.8s", xbsd_fstypes[pp->p_fstype].name);
      else
	fprintf(f, "%8x", pp->p_fstype);
      switch (pp->p_fstype) {
	case BSD_FS_UNUSED:
	  fprintf(f, "    %5ld %5ld %5.5s ",
		  (long) pp->p_fsize, (long) pp->p_fsize * pp->p_frag, "");
	  break;

	case BSD_FS_BSDFFS:
	  fprintf(f, "    %5ld %5ld %5d ",
		  (long) pp->p_fsize, (long) pp->p_fsize * pp->p_frag,
		  pp->p_cpg);
	  break;

	default:
	  fprintf(f, "%22.22s", "");
	  break;
      }
      fprintf(f, "\n");
    }
  }
}

static uint32_t ask_uint32(struct fdisk_context *cxt,
		uint32_t dflt, char *mesg)
{
	uintmax_t res;

	if (fdisk_ask_number(cxt, min(dflt, (uint32_t) 1), dflt,
				UINT32_MAX, mesg, &res) == 0)
		return res;
	return dflt;
}

static uint16_t ask_uint16(struct fdisk_context *cxt,
		uint16_t dflt, char *mesg)
{
	uintmax_t res;

	if (fdisk_ask_number(cxt, min(dflt, (uint16_t) 1),
				dflt, UINT16_MAX, mesg, &res) == 0)
		return res;
	return dflt;
}

static void xbsd_edit_disklabel(struct fdisk_context *cxt)
{
	struct bsd_disklabel *d = self_disklabel(cxt);
	uintmax_t res;

#if defined (__alpha__) || defined (__ia64__)
	if (fdisk_ask_number(cxt, DEFAULT_SECTOR_SIZE, d->d_secsize,
			     UINT32_MAX, _("bytes/sector"), &res) == 0)
		d->d_secsize = res;

	d->d_nsectors = ask_uint32(cxt, d->d_nsectors, _("sectors/track"));
	d->d_ntracks = ask_uint32(cxt, d->d_ntracks, _("tracks/cylinder"));
	d->d_ncylinders = ask_uint32(cxt, d->d_ncylinders  ,_("cylinders"));
#endif
	if (fdisk_ask_number(cxt, 1, d->d_nsectors * d->d_ntracks,
			     d->d_nsectors * d->d_ntracks,
			     _("sectors/cylinder"), &res) == 0)
		d->d_secpercyl = res;

	d->d_rpm = ask_uint16(cxt, d->d_rpm, _("rpm"));
	d->d_interleave = ask_uint16(cxt, d->d_interleave, _("interleave"));
	d->d_trackskew = ask_uint16(cxt, d->d_trackskew, _("trackskew"));
	d->d_cylskew = ask_uint16(cxt, d->d_cylskew, _("cylinderskew"));

	d->d_headswitch = ask_uint32(cxt, d->d_headswitch, _("headswitch"));
	d->d_trkseek = ask_uint32(cxt, d->d_trkseek, _("track-to-track seek"));

	d->d_secperunit = d->d_secpercyl * d->d_ncylinders;
}

static int xbsd_get_bootstrap(struct fdisk_context *cxt,
			char *path, void *ptr, int size)
{
	int fd;

	if ((fd = open(path, O_RDONLY)) < 0) {
		fdisk_warn(cxt, _("open failed %s"), path);
		return -errno;
	}

	if (read_all(fd, ptr, size) != size) {
		fdisk_warn(cxt, _("read failed %s"), path);
		close(fd);
		return -errno;
	}

	fdisk_info(cxt, "bootstrap file %s successfully loaded", path);
	close (fd);
	return 0;
}

static int xbsd_write_bootstrap (struct fdisk_context *cxt)
{
	struct bsd_disklabel dl, *d = self_disklabel(cxt);
	struct fdisk_bsd_label *l = self_label(cxt);
	char *name = d->d_type == BSD_DTYPE_SCSI ? "sd" : "wd";
	char buf[BUFSIZ];
	char *res, *dp, *p;
	int rc;
	sector_t sector;

	snprintf(buf, sizeof(buf),
		_("Bootstrap: %1$sboot -> boot%1$s (default %1$s)"),
		name);
	rc = fdisk_ask_string(cxt, buf, &res);
	if (rc)
		goto done;
	if (res && *res)
		name = res;

	snprintf(buf, sizeof(buf), "%s/%sboot", BSD_LINUX_BOOTDIR, name);
	rc = xbsd_get_bootstrap(cxt, buf,
			bsdbuffer,
			(int) d->d_secsize);
	if (rc)
		goto done;

	/* We need a backup of the disklabel (might have changed). */
	dp = &bsdbuffer[BSD_LABELSECTOR * DEFAULT_SECTOR_SIZE];
	memmove(&dl, dp, sizeof(struct bsd_disklabel));

	/* The disklabel will be overwritten by 0's from bootxx anyway */
	memset(dp, 0, sizeof(struct bsd_disklabel));

	snprintf(buf, sizeof(buf), "%s/boot%s", BSD_LINUX_BOOTDIR, name);
	rc = xbsd_get_bootstrap(cxt, buf,
			&bsdbuffer[d->d_secsize],
			(int) d->d_bbsize - d->d_secsize);
	if (rc)
		goto done;

	/* check end of the bootstrap */
	for (p = dp; p < dp + sizeof(struct bsd_disklabel); p++) {
		if (!*p)
			continue;
		fdisk_warnx(cxt, _("Bootstrap overlaps with disk label!"));
		return -EINVAL;
	}

	/* move disklabel back */
	memmove(dp, &dl, sizeof(struct bsd_disklabel));

	sector = 0;
	if (l->dos_part)
		sector = dos_partition_get_start(l->dos_part);
#if defined (__alpha__)
	alpha_bootblock_checksum(bsdbuffer);
#endif
	if (lseek(cxt->dev_fd, (off_t) sector * DEFAULT_SECTOR_SIZE, SEEK_SET) == -1) {
		fdisk_warn(cxt, _("seek failed %s"), cxt->dev_path);
		rc = -errno;
		goto done;
	}
	if (write_all(cxt->dev_fd, bsdbuffer, BSD_BBSIZE)) {
		fdisk_warn(cxt, _("write failed %s"), cxt->dev_path);
		rc = -errno;
		goto done;
	}

	fdisk_info(cxt, _("Bootstrap installed on %s."), cxt->dev_path);
	sync_disks ();

	rc = 0;
done:
	free(res);
	return rc;
}

/* TODO: remove this, use regular change_partition_type() in fdisk.c */
static void xbsd_change_fstype (struct fdisk_context *cxt)
{
  int i;
  struct fdisk_parttype *t;
  struct bsd_disklabel *d = self_disklabel(cxt);

  if (xbsd_get_part_index(cxt, d->d_npartitions, &i))
	  return;
  t = ask_partition_type(cxt);

  if (t) {
    d->d_partitions[i].p_fstype = t->type;
    fdisk_free_parttype(t);
    fdisk_label_set_changed(cxt->label, 1);
  }
}

static int xbsd_get_part_index(struct fdisk_context *cxt, int max, int *n)
{
	int c;
	char prompt[256];

	snprintf(prompt, sizeof(prompt), _("Partition (a-%c): "), 'a' + max - 1);
	do {
		char *res;
		int rc = fdisk_ask_string(cxt, prompt, &res);

		if (rc)
			return rc;
		c = tolower(*res);
		free(res);
	} while (c < 'a' || c > 'a' + max - 1);

	*n = c - 'a';
	return 0;
}

static int
xbsd_check_new_partition(struct fdisk_context *cxt, int *i)
{
	int rc;
	struct bsd_disklabel *d = self_disklabel(cxt);

	/* room for more? various BSD flavours have different maxima */
	if (d->d_npartitions == BSD_MAXPARTITIONS) {
		int t;

		for (t = 0; t < BSD_MAXPARTITIONS; t++)
			if (d->d_partitions[t].p_size == 0)
				break;

		if (t == BSD_MAXPARTITIONS) {
			fprintf (stderr, _("The maximum number of partitions "
					   "has been created\n"));
			return -EINVAL;
		}
	}

	rc = xbsd_get_part_index(cxt, BSD_MAXPARTITIONS, i);
	if (rc)
		return rc;

	if (*i >= d->d_npartitions)
		d->d_npartitions = (*i) + 1;

	if (d->d_partitions[*i].p_size != 0) {
		fprintf (stderr, _("This partition already exists.\n"));
		return -EINVAL;
	}

	return 0;
}

static unsigned short
xbsd_dkcksum (struct bsd_disklabel *lp) {
	unsigned short *start, *end;
	unsigned short sum = 0;

	start = (unsigned short *) lp;
	end = (unsigned short *) &lp->d_partitions[lp->d_npartitions];
	while (start < end)
		sum ^= *start++;
	return sum;
}

static int xbsd_initlabel (struct fdisk_context *cxt)
{
	struct fdisk_bsd_label *l = self_label(cxt);
	struct bsd_disklabel *d = self_disklabel(cxt);
	struct bsd_partition *pp;

	memset (d, 0, sizeof (struct bsd_disklabel));

	d -> d_magic = BSD_DISKMAGIC;

	if (strncmp (cxt->dev_path, "/dev/sd", 7) == 0)
		d -> d_type = BSD_DTYPE_SCSI;
	else
		d -> d_type = BSD_DTYPE_ST506;

#if !defined (__alpha__)
	d -> d_flags = BSD_D_DOSPART;
#else
	d -> d_flags = 0;
#endif
	d -> d_secsize = DEFAULT_SECTOR_SIZE;		/* bytes/sector  */
	d -> d_nsectors = cxt->geom.sectors;		/* sectors/track */
	d -> d_ntracks = cxt->geom.heads;		/* tracks/cylinder (heads) */
	d -> d_ncylinders = cxt->geom.cylinders;
	d -> d_secpercyl  = cxt->geom.sectors * cxt->geom.heads;/* sectors/cylinder */
	if (d -> d_secpercyl == 0)
		d -> d_secpercyl = 1;		/* avoid segfaults */
	d -> d_secperunit = d -> d_secpercyl * d -> d_ncylinders;

	d -> d_rpm = 3600;
	d -> d_interleave = 1;
	d -> d_trackskew = 0;
	d -> d_cylskew = 0;
	d -> d_headswitch = 0;
	d -> d_trkseek = 0;

	d -> d_magic2 = BSD_DISKMAGIC;
	d -> d_bbsize = BSD_BBSIZE;
	d -> d_sbsize = BSD_SBSIZE;

	if (l->dos_part) {
		d->d_npartitions = 4;
		pp = &d->d_partitions[2];	/* Partition C should be
						   the NetBSD partition */
		pp->p_offset = dos_partition_get_start(l->dos_part);
		pp->p_size   = dos_partition_get_size(l->dos_part);
		pp->p_fstype = BSD_FS_UNUSED;
		pp = &d -> d_partitions[3];	/* Partition D should be
						   the whole disk */
		pp->p_offset = 0;
		pp->p_size   = d->d_secperunit;
		pp->p_fstype = BSD_FS_UNUSED;
	} else {
		d->d_npartitions = 3;
		pp = &d->d_partitions[2];	/* Partition C should be
						   the whole disk */
		pp->p_offset = 0;
		pp->p_size   = d->d_secperunit;
		pp->p_fstype = BSD_FS_UNUSED;
	}

	return 0;
}

/*
 * Read a xbsd_disklabel from sector 0 or from the starting sector of p.
 * If it has the right magic, return 0.
 */
static int xbsd_readlabel(struct fdisk_context *cxt)
{
	struct fdisk_bsd_label *l;
	struct bsd_disklabel *d;
	int t;
	off_t offset = 0;

	l = self_label(cxt);
	d = self_disklabel(cxt);

	if (l->dos_part)
		/* BSD is nested within DOS partition, get the begin of the
		 * partition. Note that DOS uses native sector size. */
		offset = dos_partition_get_start(l->dos_part) * cxt->sector_size;

	if (lseek(cxt->dev_fd, offset, SEEK_SET) == -1)
		return -1;
	if (read_all(cxt->dev_fd, bsdbuffer, sizeof(bsdbuffer)) < 0)
		return errno ? -errno : -1;

	/* The offset to begin of the disk label. Note that BSD uses
	 * 512-byte (default) sectors. */
	memmove(d, &bsdbuffer[BSD_LABELSECTOR * DEFAULT_SECTOR_SIZE
			      + BSD_LABELOFFSET], sizeof(*d));

	if (d->d_magic != BSD_DISKMAGIC || d->d_magic2 != BSD_DISKMAGIC) {
		DBG(LABEL, dbgprint("not found magic"));
		return -1;
	}

	for (t = d->d_npartitions; t < BSD_MAXPARTITIONS; t++) {
		d->d_partitions[t].p_size   = 0;
		d->d_partitions[t].p_offset = 0;
		d->d_partitions[t].p_fstype = BSD_FS_UNUSED;
	}

	if (d->d_npartitions > BSD_MAXPARTITIONS)
		fdisk_warnx(cxt, ("Too many partitions (%d, maximum is %d)."),
				d->d_npartitions, BSD_MAXPARTITIONS);

	cxt->label->nparts_cur = d->d_npartitions;
	cxt->label->nparts_max = BSD_MAXPARTITIONS;
	DBG(LABEL, dbgprint("read BSD label"));
	return 0;
}

static int xbsd_writelabel(struct fdisk_context *cxt)
{
	off_t offset = 0;
	struct fdisk_bsd_label *l;
	struct bsd_disklabel *d;

	l = self_label(cxt);
	d = self_disklabel(cxt);

	if (l->dos_part)
		offset = dos_partition_get_start(l->dos_part) * cxt->sector_size;

	d->d_checksum = 0;
	d->d_checksum = xbsd_dkcksum(d);

	/* Update label with in boot block. */
	memmove(&bsdbuffer[BSD_LABELSECTOR * DEFAULT_SECTOR_SIZE
			   + BSD_LABELOFFSET], d, sizeof(*d));

#if defined (__alpha__) && BSD_LABELSECTOR == 0
	/* write the check sum to the end of the first sector */
	alpha_bootblock_checksum(bsdbuffer);
#endif
	if (lseek(cxt->dev_fd, offset, SEEK_SET) == -1) {
		fdisk_warn(cxt, _("seek failed: %d"), cxt->dev_path);
		return -errno;
	}
	if (write_all(cxt->dev_fd, bsdbuffer, sizeof(bsdbuffer))) {
		fdisk_warn(cxt, _("write failed: %d"), cxt->dev_path);
		return -errno;
	}
	sync_disks ();
	return 0;
}

static void
sync_disks (void)
{
  printf (_("\nSyncing disks.\n"));
  sync ();
  sleep (4);
}

#if !defined (__alpha__)
static int
xbsd_translate_fstype (int linux_type)
{
  switch (linux_type)
  {
    case 0x01: /* DOS 12-bit FAT   */
    case 0x04: /* DOS 16-bit <32M  */
    case 0x06: /* DOS 16-bit >=32M */
    case 0xe1: /* DOS access       */
    case 0xe3: /* DOS R/O          */
    case 0xf2: /* DOS secondary    */
      return BSD_FS_MSDOS;
    case 0x07: /* OS/2 HPFS        */
      return BSD_FS_HPFS;
    default:
      return BSD_FS_OTHER;
  }
}

/*
 * link partition from parent (DOS) to nested BSD partition table
 */
static void
xbsd_link_part (struct fdisk_context *cxt)
{
	size_t k;
	int i;
	struct dos_partition *p;
	struct bsd_disklabel *d = self_disklabel(cxt);

	if (!cxt->parent || !fdisk_is_disklabel(cxt->parent, DOS))
		return;		/* not nested PT */

	if (fdisk_ask_partnum(cxt->parent, &k, FALSE))
		return;

	if (xbsd_check_new_partition(cxt, &i))
		return;

	/* TODO: what about to update label->dos_part? */
	p = fdisk_dos_get_partition(cxt->parent, k);

	d->d_partitions[i].p_size   = dos_partition_get_size(p);
	d->d_partitions[i].p_offset = dos_partition_get_start(p);
	d->d_partitions[i].p_fstype = xbsd_translate_fstype(p->sys_ind);
}
#endif

#if defined (__alpha__)

#if !defined(__GLIBC__)
typedef unsigned long long u_int64_t;
#endif

void
alpha_bootblock_checksum (char *boot)
{
  u_int64_t *dp, sum;
  int i;

  dp = (u_int64_t *)boot;
  sum = 0;
  for (i = 0; i < 63; i++)
    sum += dp[i];
  dp[63] = sum;
}
#endif /* __alpha__ */

static struct fdisk_parttype *xbsd_get_parttype(
		struct fdisk_context *cxt,
		size_t n)
{
	struct fdisk_parttype *t;
	struct bsd_disklabel *d = self_disklabel(cxt);

	if (n >= d->d_npartitions)
		return NULL;

	t = fdisk_get_parttype_from_code(cxt, d->d_partitions[n].p_fstype);
	if (!t)
		t = fdisk_new_unknown_parttype(d->d_partitions[n].p_fstype, NULL);
	return t;
}

static int xbsd_set_parttype(
		struct fdisk_context *cxt,
		size_t partnum,
		struct fdisk_parttype *t)
{
	struct bsd_partition *p;
	struct bsd_disklabel *d = self_disklabel(cxt);

	if (partnum >= d->d_npartitions || !t || t->type > UINT8_MAX)
		return -EINVAL;

	p = &d->d_partitions[partnum];
	if (t->type == p->p_fstype)
		return 0;

	p->p_fstype = t->type;
	fdisk_label_set_changed(cxt->label, 1);
	return 0;
}

static int bsd_get_partition_status(
		struct fdisk_context *cxt,
		size_t partnum,
		int *status)
{
	struct bsd_partition *p;
	struct bsd_disklabel *d = self_disklabel(cxt);

	if (!status || partnum >= BSD_MAXPARTITIONS)
		return -EINVAL;

	p = &d->d_partitions[partnum];
	*status = FDISK_PARTSTAT_NONE;

	if (p->p_size)
		*status = FDISK_PARTSTAT_USED;

	return 0;
}


static const struct fdisk_label_operations bsd_operations =
{
	.probe		= bsd_probe_label,
	.write		= xbsd_write_disklabel,
	.create		= xbsd_create_disklabel,
	.part_add	= xbsd_add_part,
	.part_delete	= xbsd_delete_part,
	.part_get_type	= xbsd_get_parttype,
	.part_set_type	= xbsd_set_parttype,
	.part_get_status= bsd_get_partition_status,
};


/*
 * allocates BSD label driver
 */
struct fdisk_label *fdisk_new_bsd_label(struct fdisk_context *cxt)
{
	struct fdisk_label *lb;
	struct fdisk_bsd_label *bsd;

	assert(cxt);

	bsd = calloc(1, sizeof(*bsd));
	if (!bsd)
		return NULL;

	/* initialize generic part of the driver */
	lb = (struct fdisk_label *) bsd;
	lb->name = "bsd";
	lb->id = FDISK_DISKLABEL_OSF;
	lb->op = &bsd_operations;
	lb->parttypes = xbsd_fstypes;
	lb->nparttypes = ARRAY_SIZE(xbsd_fstypes);

	lb->flags |= FDISK_LABEL_FL_INCHARS_PARTNO;
	lb->flags |= FDISK_LABEL_FL_REQUIRE_GEOMETRY;

	return lb;
}
