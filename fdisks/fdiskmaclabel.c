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
#include "fdiskmaclabel.h"
#include "nls.h"

#define MAC_BITMASK 0xffff0000


static	int     other_endian = 0;
static  short	volumes=1;

/*
 * only dealing with free blocks here
 */

static void
mac_info( void ) {
    puts(
	_("\n\tThere is a valid Mac label on this disk.\n"
	"\tUnfortunately fdisk(1) cannot handle these disks.\n"
	"\tUse either pdisk or parted to modify the partition table.\n"
	"\tNevertheless some advice:\n"
	"\t1. fdisk will destroy its contents on write.\n"
	"\t2. Be sure that this disk is NOT a still vital\n"
	"\t   part of a volume group. (Otherwise you may\n"
	"\t   erase the other disks as well, if unmirrored.)\n")
    );
}

void
mac_nolabel(struct fdisk_context *cxt)
{
    struct mac_partition *maclabel = (struct mac_partition *) cxt->firstsector;

    maclabel->magic = 0;
    partitions = 4;
    fdisk_zeroize_firstsector(cxt);
    return;
}

static int
mac_probe_label(struct fdisk_context *cxt)
{
	struct mac_partition *maclabel = (struct mac_partition *) cxt->firstsector;

	/*
	Conversion: only 16 bit should compared
	e.g.: HFS Label is only 16bit long
	*/
	int magic_masked = 0 ;
	magic_masked =  maclabel->magic & MAC_BITMASK ;

	switch (magic_masked) {
		case MAC_LABEL_MAGIC :
		case MAC_LABEL_MAGIC_2:
		case MAC_LABEL_MAGIC_3:
			goto IS_MAC;
			break;
		default:
			other_endian = 0;
			return 0;


	}

IS_MAC:
    other_endian = (maclabel->magic == MAC_LABEL_MAGIC_SWAPPED); // =?
    disklabel = MAC_LABEL;
    partitions= 1016; // =?
    volumes = 15;	// =?
    mac_info();
    mac_nolabel(cxt);		/* %% */
    return 1;
}

static void mac_add_partition(
			struct fdisk_context *cxt __attribute__ ((__unused__)),
			int partnum __attribute__ ((__unused__)),
			int parttype __attribute__ ((__unused__)))
{
	printf(_("\tSorry - this fdisk cannot handle Mac disk labels."
		 "\n\tIf you want to add DOS-type partitions, create"
		 "\n\ta new empty DOS partition table first. (Use o.)"
		 "\n\tWARNING: "
		 "This will destroy the present disk contents.\n"));
}

const struct fdisk_label mac_label =
{
	.name = "mac",
	.probe = mac_probe_label,
	.write = NULL,
	.verify = NULL,
	.create = NULL,
	.part_add = mac_add_partition,
	.part_delete = NULL,
};
