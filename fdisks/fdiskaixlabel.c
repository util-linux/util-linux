/*
  Changes:
  Sat Mar 20 09:51:38 EST 1999 Arnaldo Carvalho de Melo <acme@conectiva.com.br>
	Internationalization
*/
#include <stdio.h>              /* stderr */
#include <string.h>             /* strstr */
#include <unistd.h>             /* write */

#include <endian.h>

#include "common.h"
#include "fdisk.h"
#include "fdiskaixlabel.h"
#include "nls.h"

static	int     other_endian = 0;
static  short	volumes=1;

/*
 * only dealing with free blocks here
 */

static void
aix_info( void ) {
    puts(
	_("\n\tThere is a valid AIX label on this disk.\n"
	"\tUnfortunately Linux cannot handle these\n"
	"\tdisks at the moment.  Nevertheless some\n"
	"\tadvice:\n"
	"\t1. fdisk will destroy its contents on write.\n"
	"\t2. Be sure that this disk is NOT a still vital\n"
	"\t   part of a volume group. (Otherwise you may\n"
	"\t   erase the other disks as well, if unmirrored.)\n"
	"\t3. Before deleting this physical volume be sure\n"
	"\t   to remove the disk logically from your AIX\n"
	"\t   machine.  (Otherwise you become an AIXpert).")
    );
}

static void aix_nolabel(struct fdisk_context *cxt)
{
    struct aix_partition *aixlabel = (struct aix_partition *) cxt->firstsector;

    aixlabel->magic = 0;
    partitions = 4;
    fdisk_zeroize_firstsector(cxt);
    return;
}

static int aix_probe_label(struct fdisk_context *cxt)
{
    struct aix_partition *aixlabel = (struct aix_partition *) cxt->firstsector;

    if (aixlabel->magic != AIX_LABEL_MAGIC &&
	aixlabel->magic != AIX_LABEL_MAGIC_SWAPPED) {
	other_endian = 0;
	return 0;
    }
    other_endian = (aixlabel->magic == AIX_LABEL_MAGIC_SWAPPED);
    disklabel = AIX_LABEL;
    partitions= 1016;
    volumes = 15;
    aix_info();
    aix_nolabel(cxt);		/* %% */
    return 1;
}

static void aix_add_partition(
		struct fdisk_context *cxt __attribute__((__unused__)),
		int partnum __attribute__((__unused__)),
		int parttype __attribute__((__unused__)))
{
	printf(_("\tSorry - this fdisk cannot handle AIX disk labels."
		 "\n\tIf you want to add DOS-type partitions, create"
		 "\n\ta new empty DOS partition table first. (Use o.)"
		 "\n\tWARNING: "
		 "This will destroy the present disk contents.\n"));
}

const struct fdisk_label aix_label =
{
	.name = "aix",
	.probe = aix_probe_label,
	.write = NULL,
	.verify = NULL,
	.create = NULL,
	.part_add = aix_add_partition,
	.part_delete = NULL,
};
