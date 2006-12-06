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
 * 1999-02-22 Arkadiusz Mi¶kiewicz <misiek@pld.ORG.PL>
 * - added Native Language Support
 *
 * Fri Dec  1 23:31:00 2000: Sepp Wijnands <mrrazz@garbage-coderz.net>
 * added probes for cramfs, hfs, hpfs and adfs.
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

static inline int
assemble4le(unsigned char *p) {
	return (p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
}

/*
    char *guess_fstype_from_superblock(const char *device);

    Probes the device and attempts to determine the type of filesystem
    contained within.

    Original routine by <jmorriso@bogomips.ww.ubc.ca>; made into a function
    for mount(8) by Mike Grupenhoff <kashmir@umiacs.umd.edu>.
    Read the superblock only once - aeb
    Added iso9660, romfs, qnx4, udf, swap - aeb
    Added a test for high sierra (iso9660) - quinlan@bucknell.edu
    Corrected the test for xiafs - aeb
    Added ufs from a patch by jj. But maybe there are several types of ufs?
    Added ntfs from a patch by Richard Russon.
    Added a very weak heuristic for vfat - aeb
    Added xfs - 2000-03-21 Martin K. Petersen <mkp@linuxcare.com>
    Added cramfs, hfs, hpfs, adfs - Sepp Wijnands <mrrazz@garbage-coderz.net>
    Added ext3 - Andrew Morton
*/
static char
*magic_known[] = {
	"adfs", "bfs", "cramfs", "ext", "ext2", "ext3",
	"hfs", "hpfs", "iso9660", "minix", "ntfs",
	"qnx4", "romfs", "swap", "udf", "ufs",
	"xfs", "xiafs"
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

/* rather weak necessary condition */
static int
may_be_adfs(const u_char *s) {
	u_char *p;
	int sum;

	p = (u_char *) s + 511;
	sum = 0;
	while(--p != s)
		sum = (sum >> 8) + (sum & 0xff) + *p;

	return (sum == p[511]);
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
	char qnx4fs_magic[10];	/* ignore first 4 bytes */
	long bfs_magic;
	struct ntfs_super_block ntfssb;
	struct fat_super_block fatsb;
	struct xfs_super_block xfsb;
	struct cramfs_super_block cramfssb;
    } xsb;
    struct ufs_super_block ufssb;
    union {
	struct iso_volume_descriptor iso;
	struct hs_volume_descriptor hs;
    } isosb;
    struct hfs_super_block hfssb;
    struct hpfs_super_block hpfssb;
    struct adfs_super_block adfssb;
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

    /* ext2 has magic in little-endian on disk, so "swapped" is
       superfluous; however, there have existed strange byteswapped
       PPC ext2 systems */
    if (ext2magic(sb.e2s) == EXT2_SUPER_MAGIC
	|| ext2magic(sb.e2s) == EXT2_PRE_02B_MAGIC
	|| ext2magic(sb.e2s) == swapped(EXT2_SUPER_MAGIC)) {
	 type = "ext2";

	 /* maybe even ext3? */
	 if ((assemble4le(sb.e2s.s_feature_compat)
	      & EXT3_FEATURE_COMPAT_HAS_JOURNAL) &&
	     assemble4le(sb.e2s.s_journal_inum) != 0)
		 type = "ext3,ext2";
    }

    else if (minixmagic(sb.ms) == MINIX_SUPER_MAGIC
	     || minixmagic(sb.ms) == MINIX_SUPER_MAGIC2
	     || minixmagic(sb.ms) == swapped(MINIX_SUPER_MAGIC2))
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
	 else if(!strncmp(xsb.xfsb.s_magic, XFS_SUPER_MAGIC, 4))
	      type = "xfs";
	 else if(!strncmp(xsb.qnx4fs_magic+4, "QNX4FS", 6))
	      type = "qnx4fs";
	 else if(xsb.bfs_magic == 0x1badface)
	      type = "bfs";
	 else if(!strncmp(xsb.ntfssb.s_magic, NTFS_SUPER_MAGIC,
			  sizeof(xsb.ntfssb.s_magic)))
	      type = "ntfs";
	 else if(cramfsmagic(xsb.cramfssb) == CRAMFS_SUPER_MAGIC)
	      type = "cramfs";
	 else if ((!strncmp(xsb.fatsb.s_os, "MSDOS", 5) ||
		   !strncmp(xsb.fatsb.s_os, "MSWIN", 5) ||
		   !strncmp(xsb.fatsb.s_os, "MTOOL", 5) ||
		   !strncmp(xsb.fatsb.s_os, "mkdosfs", 7) ||
		   !strncmp(xsb.fatsb.s_os, "kmkdosfs", 8) ||
		   /* Michal Svec: created by fdformat, old msdos utility for
		      formatting large (1.7) floppy disks. */
		   !strncmp(xsb.fatsb.s_os, "CH-FOR18", 8))
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
        if (lseek(fd, 0x400, SEEK_SET) != 0x400
            || read(fd, (char *) &hfssb, sizeof(hfssb)) != sizeof(hfssb))
             goto io_error;

        /* also check if block size is equal to 512 bytes,
           since the hfs driver currently only has support
           for block sizes of 512 bytes long, and to be
           more accurate (sb magic is only a short int) */
        if ((hfsmagic(hfssb) == HFS_SUPER_MAGIC &&
	     hfsblksize(hfssb) == 0x20000) ||
            (swapped(hfsmagic(hfssb)) == HFS_SUPER_MAGIC &&
             hfsblksize(hfssb) == 0x200))
             type = "hfs";
    }

    if (!type) {
        if (lseek(fd, 0x2000, SEEK_SET) != 0x2000
            || read(fd, (char *) &hpfssb, sizeof(hpfssb)) != sizeof(hpfssb))
             goto io_error;

        if (hpfsmagic(hpfssb) == HPFS_SUPER_MAGIC)
             type = "hpfs";
    }

    if (!type) {
        if (lseek(fd, 0xc00, SEEK_SET) != 0xc00
            || read(fd, (char *) &adfssb, sizeof(adfssb)) != sizeof(adfssb))
             goto io_error;

	/* only a weak test */
        if (may_be_adfs((u_char *) &adfssb)
            && (adfsblksize(adfssb) >= 8 &&
                adfsblksize(adfssb) <= 10))
             type = "adfs";
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

static char *
procfsnext(FILE *procfs) {
   char line[100];
   char fsname[100];

   while (fgets(line, sizeof(line), procfs)) {
      if (sscanf (line, "nodev %[^\n]\n", fsname) == 1) continue;
      if (sscanf (line, " %[^ \n]\n", fsname) != 1) continue;
      return strdup(fsname);
   }
   return 0;
}

/* Only use /proc/filesystems here, this is meant to test what
   the kernel knows about, so /etc/filesystems is irrelevant.
   Return: 1: yes, 0: no, -1: cannot open procfs */
int
is_in_procfs(const char *type) {
    FILE *procfs;
    char *fsname;
    int ret = -1;

    procfs = fopen(PROC_FILESYSTEMS, "r");
    if (procfs) {
	ret = 0;
	while ((fsname = procfsnext(procfs)) != NULL)
	    if (!strcmp(fsname, type)) {
		ret = 1;
		break;
	    }
	fclose(procfs);
	procfs = NULL;
    }
    return ret;
}

/* Try all types in FILESYSTEMS, except those in *types,
   in case *types starts with "no" */
/* return: 0: OK, -1: error in errno, 1: type not found */
/* when 0 or -1 is returned, *types contains the type used */
/* when 1 is returned, *types is NULL */
int
procfsloop(int (*mount_fn)(struct mountargs *), struct mountargs *args,
	   char **types) {
	char *files[2] = { ETC_FILESYSTEMS, PROC_FILESYSTEMS };
	FILE *procfs;
	char *fsname;
	char *notypes = NULL;
	int no = 0;
	int ret = 1;
	int errsv = 0;
	int i;

	if (!strncmp(*types, "no", 2)) {
		no = 1;
		notypes = (*types) + 2;
	}
	*types = NULL;

	/* Use PROC_FILESYSTEMS only when ETC_FILESYSTEMS does not exist.
	   In some cases trying a filesystem that the kernel knows about
	   on the wrong data will crash the kernel; in such cases
	   ETC_FILESYSTEMS can be used to list the filesystems that we
	   are allowed to try, and in the order they should be tried.
	   End ETC_FILESYSTEMS with a line containing a single '*' only,
	   if PROC_FILESYSTEMS should be tried afterwards. */

	for (i=0; i<2; i++) {
		procfs = fopen(files[i], "r");
		if (!procfs)
			continue;
		while ((fsname = procfsnext(procfs)) != NULL) {
			if (!strcmp(fsname, "*")) {
				fclose(procfs);
				goto nexti;
			}
			if (tested (fsname))
				continue;
			if (no && matching_type(fsname, notypes))
				continue;
			args->type = fsname;
			if (verbose) {
				printf(_("Trying %s\n"), fsname);
				fflush(stdout);
			}
			if ((*mount_fn) (args) == 0) {
				*types = fsname;
				ret = 0;
				break;
			} else if (errno != EINVAL &&
				   is_in_procfs(fsname) == 1) {
				*types = "guess";
				ret = -1;
				errsv = errno;
				break;
			}
		}
		fclose(procfs);
		errno = errsv;
		return ret;
	nexti:;
	}
	return 1;
}
