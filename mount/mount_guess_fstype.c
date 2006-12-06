/*
 * Thu Jul 14 07:32:40 1994: faith@cs.unc.edu added changes from Adam
 * J. Richter (adam@adam.yggdrasil.com) so that /proc/filesystems is used
 * if no -t option is given.  I modified his patches so that, if
 * /proc/filesystems is not available, the behavior of mount is the same as
 * it was previously.
 *
 * Wed Feb 8 09:23:18 1995: Mike Grupenhoff <kashmir@umiacs.UMD.EDU> added
 * a probe of the superblock for the type before /proc/filesystems is
 * checked.
 *
 * Fri Apr  5 01:13:33 1996: quinlan@bucknell.edu, fixed up iso9660 autodetect
 *
 * Wed Nov  11 11:33:55 1998: K.Garloff@ping.de, try /etc/filesystems before
 * /proc/filesystems
 * [This was mainly in order to specify vfat before fat; these days we often
 *  detect *fat and then assume vfat, so perhaps /etc/filesystems isnt
 *  so useful anymore.]
 *
 * 1999-02-22 Arkadiusz Mi¶kiewicz <misiek@misiek.eu.org>
 * - added Native Language Support
 *
 * aeb - many changes.
 *
 */

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "linux_fs.h"
#include "mount_guess_fstype.h"
#include "sundries.h"		/* for xstrdup */
#include "nls.h"

#define ETC_FILESYSTEMS		"/etc/filesystems"
#define PROC_FILESYSTEMS	"/proc/filesystems"

#define SIZE(a) (sizeof(a)/sizeof(a[0]))

/* Most file system types can be recognized by a `magic' number
   in the superblock.  Note that the order of the tests is
   significant: by coincidence a filesystem can have the
   magic numbers for several file system types simultaneously.
   For example, the romfs magic lives in the 1st sector;
   xiafs does not touch the 1st sector and has its magic in
   the 2nd sector; ext2 does not touch the first two sectors. */

static inline unsigned short
swapped(unsigned short a) {
     return (a>>8) | (a<<8);
}

/*
    char *guess_fstype_from_superblock(const char *device);

    Probes the device and attempts to determine the type of filesystem
    contained within.

    Original routine by <jmorriso@bogomips.ww.ubc.ca>; made into a function
    for mount(8) by Mike Grupenhoff <kashmir@umiacs.umd.edu>.
    Read the superblock only once - aeb
    Added a test for iso9660 - aeb
    Added a test for high sierra (iso9660) - quinlan@bucknell.edu
    Corrected the test for xiafs - aeb
    Added romfs - aeb
    Added ufs from a patch by jj. But maybe there are several types of ufs?
    Added ntfs from a patch by Richard Russon.
    Added a very weak heuristic for vfat - aeb
    Added qnx4 - aeb
    Added swap - aeb
    Added xfs - 2000-03-21 Martin K. Petersen <mkp@linuxcare.com>

    Currently supports: minix, ext, ext2, xiafs, iso9660, romfs,
    ufs, ntfs, vfat, qnx4, bfs, xfs
*/
static char
*magic_known[] = { "minix", "ext", "ext2", "xiafs", "iso9660", "romfs",
		   "ufs", "ntfs", "qnx4", "bfs", "udf", "xfs",
		   "swap"	/* last - just to warn the user */
};

static int
tested(const char *device) {
    char **m;

    for (m = magic_known; m - magic_known < SIZE(magic_known); m++)
        if (!strcmp(*m, device))
	    return 1;
    return 0;
}

/* udf magic - I find that trying to mount garbage as an udf fs
   causes a very large kernel delay, almost killing the machine.
   So, we do not try udf unless there is positive evidence that it
   might work. Try iso9660 first, it is much more likely.

   Strings below taken from ECMA 167. */
static char
*udf_magic[] = { "BEA01", "BOOT2", "CD001", "CDW02", "NSR02",
		 "NSR03", "TEA01" };

static int
may_be_udf(const char *id) {
    char **m;

    for (m = udf_magic; m - udf_magic < SIZE(udf_magic); m++)
       if (!strncmp(*m, id, 5))
	  return 1;
    return 0;
}

static int
may_be_swap(const char *s) {
	return (strncmp(s-10, "SWAP-SPACE", 10) == 0 ||
		strncmp(s-10, "SWAPSPACE2", 10) == 0);
}

static char *
fstype(const char *device) {
    int fd;
    char *type = NULL;
    union {
	struct minix_super_block ms;
	struct ext_super_block es;
	struct ext2_super_block e2s;
    } sb;
    union {
	struct xiafs_super_block xiasb;
	char romfs_magic[8];
	char xfs_magic[4];
	char qnx4fs_magic[10];	/* ignore first 4 bytes */
	long bfs_magic;
	struct ntfs_super_block ntfssb;
	struct fat_super_block fatsb;
    } xsb;
    struct ufs_super_block ufssb;
    union {
	struct iso_volume_descriptor iso;
	struct hs_volume_descriptor hs;
    } isosb;
    struct stat statbuf;

    /* opening and reading an arbitrary unknown path can have
       undesired side effects - first check that `device' refers
       to a block device */
    if (stat (device, &statbuf) || !S_ISBLK(statbuf.st_mode))
      return 0;

    fd = open(device, O_RDONLY);
    if (fd < 0)
      return 0;

    if (lseek(fd, 1024, SEEK_SET) != 1024
	|| read(fd, (char *) &sb, sizeof(sb)) != sizeof(sb))
	 goto io_error;

    if (ext2magic(sb.e2s) == EXT2_SUPER_MAGIC
	|| ext2magic(sb.e2s) == EXT2_PRE_02B_MAGIC
	|| ext2magic(sb.e2s) == swapped(EXT2_SUPER_MAGIC))
	 type = "ext2";

    else if (minixmagic(sb.ms) == MINIX_SUPER_MAGIC
	     || minixmagic(sb.ms) == MINIX_SUPER_MAGIC2)
	 type = "minix";

    else if (extmagic(sb.es) == EXT_SUPER_MAGIC)
	 type = "ext";

    if (!type) {
	 if (lseek(fd, 0, SEEK_SET) != 0
	     || read(fd, (char *) &xsb, sizeof(xsb)) != sizeof(xsb))
	      goto io_error;

	 if (xiafsmagic(xsb.xiasb) == _XIAFS_SUPER_MAGIC)
	      type = "xiafs";
	 else if(!strncmp(xsb.romfs_magic, "-rom1fs-", 8))
	      type = "romfs";
	 else if(!strncmp(xsb.xfs_magic, "XFSB", 4) ||
		 !strncmp(xsb.xfs_magic, "BSFX", 4))
	      type = "xfs";
	 else if(!strncmp(xsb.qnx4fs_magic+4, "QNX4FS", 6))
	      type = "qnx4fs";
	 else if(xsb.bfs_magic == 0x1badface)
	      type = "bfs";
	 else if(!strncmp(xsb.ntfssb.s_magic, NTFS_SUPER_MAGIC,
			  sizeof(xsb.ntfssb.s_magic)))
	      type = "ntfs";
	 else if ((!strncmp(xsb.fatsb.s_os, "MSDOS", 5) ||
		   !strncmp(xsb.fatsb.s_os, "MSWIN", 5) ||
		   !strncmp(xsb.fatsb.s_os, "MTOOL", 5) ||
		   !strncmp(xsb.fatsb.s_os, "mkdosfs", 7) ||
		   !strncmp(xsb.fatsb.s_os, "kmkdosfs", 8))
		  && (!strncmp(xsb.fatsb.s_fs, "FAT12   ", 8) ||
		      !strncmp(xsb.fatsb.s_fs, "FAT16   ", 8) ||
		      !strncmp(xsb.fatsb.s_fs2, "FAT32   ", 8)))
	      type = "vfat";	/* only guessing - might as well be fat or umsdos */
    }

    if (!type) {
	 if (lseek(fd, 8192, SEEK_SET) != 8192
	     || read(fd, (char *) &ufssb, sizeof(ufssb)) != sizeof(ufssb))
	      goto io_error;

	 if (ufsmagic(ufssb) == UFS_SUPER_MAGIC) /* also test swapped version? */
	      type = "ufs";
    }

    if (!type) {
	 if (lseek(fd, 0x8000, SEEK_SET) != 0x8000
	     || read(fd, (char *) &isosb, sizeof(isosb)) != sizeof(isosb))
	      goto io_error;

	 if(strncmp(isosb.iso.id, ISO_STANDARD_ID, sizeof(isosb.iso.id)) == 0
	    || strncmp(isosb.hs.id, HS_STANDARD_ID, sizeof(isosb.hs.id)) == 0)
	      type = "iso9660";
	 else if (may_be_udf(isosb.iso.id))
	      type = "udf";
    }

    if (!type) {
	    /* perhaps the user tries to mount the swap space
	       on a new disk; warn her before she does mke2fs on it */
	    int pagesize = getpagesize();
	    int rd;
	    char buf[32768];

	    rd = pagesize;
	    if (rd < 8192)
		    rd = 8192;
	    if (rd > sizeof(buf))
		    rd = sizeof(buf);
	    if (lseek(fd, 0, SEEK_SET) != 0
		|| read(fd, buf, rd) != rd)
		    goto io_error;
	    if (may_be_swap(buf+pagesize) ||
		may_be_swap(buf+4096) || may_be_swap(buf+8192))
		    type = "swap";
    }

    close (fd);
    return(type);

io_error:
    perror(device);
    close(fd);
    return 0;
}

char *
guess_fstype_from_superblock(const char *spec) {
	char *type = fstype(spec);
	if (verbose) {
	    printf (_("mount: you didn't specify a filesystem type for %s\n"),
		    spec);
	    if (!type)
	      printf (_("       I will try all types mentioned in %s or %s\n"),
		      ETC_FILESYSTEMS, PROC_FILESYSTEMS);
	    else if (!strcmp(type, "swap"))
	      printf (_("       and it looks like this is swapspace\n"));
	    else
	      printf (_("       I will try type %s\n"), type);
	}
	return type;
}

static FILE *procfs;

static void
procfsclose(void) {
	if (procfs)
		fclose (procfs);
	procfs = 0;
}

static int
procfsopen(void) {
	procfs = fopen(ETC_FILESYSTEMS, "r");
	if (!procfs)
		procfs = fopen(PROC_FILESYSTEMS, "r");
	return (procfs != NULL);
}

static char *
procfsnext(void) {
   char line[100];
   static char fsname[100];

   while (fgets(line, sizeof(line), procfs)) {
      if (sscanf (line, "nodev %[^\n]\n", fsname) == 1) continue;
      if (sscanf (line, " %[^ \n]\n", fsname) != 1) continue;
      return fsname;
   }
   return 0;
}

int
is_in_procfs(const char *type) {
    char *fsname;

    if (procfsopen()) {
	while ((fsname = procfsnext()) != NULL)
	  if (!strcmp(fsname, type))
	    return 1;
    }
    return 0;
}

/* return: 0: OK, -1: error in errno, 1: type not found */
/* when 1 is returned, *type is NULL */
int
procfsloop(int (*mount_fn)(struct mountargs *), struct mountargs *args,
	   char **type) {
   char *fsname;

   *type = NULL;
   if (!procfsopen())
     return 1;
   while ((fsname = procfsnext()) != NULL) {
      if (tested (fsname))
	 continue;
      args->type = fsname;
      if (verbose) {
	 printf(_("Trying %s\n"), fsname);
	 fflush(stdout);
      }
      if ((*mount_fn) (args) == 0) {
	 *type = xstrdup(fsname);
	 procfsclose();
	 return 0;
      } else if (errno != EINVAL) {
         *type = "guess";
	 procfsclose();
	 return -1;
      }
   }
   procfsclose();
   return 1;
}

int
have_procfs(void) {
	return procfs != NULL;
}
