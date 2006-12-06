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
 * added Native Language Support
 *
 * 2000-12-01 Sepp Wijnands <mrrazz@garbage-coderz.net>
 * added probes for cramfs, hfs, hpfs and adfs.
 *
 * 2001-10-26 Tim Launchbury
 * added sysv magic.
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
#include "mount_blkid.h"
#include "mount_guess_fstype.h"
#include "sundries.h"		/* for xstrdup */
#include "nls.h"

#define ETC_FILESYSTEMS		"/etc/filesystems"
#define PROC_FILESYSTEMS	"/proc/filesystems"

#ifdef HAVE_BLKID

char *
do_guess_fstype(const char *device) 
{
	return blkid_get_tag_value(blkid, "TYPE", device);
}

static int
known_fstype(const char *fstype) 
{
	return blkid_known_fstype(fstype);
}
	
#else
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
    char *guess_fstype(const char *device);

    Probes the device and attempts to determine the type of filesystem
    contained within.

    Original routine by <jmorriso@bogomips.ww.ubc.ca>; made into a function
    for mount(8) by Mike Grupenhoff <kashmir@umiacs.umd.edu>.
    Corrected the test for xiafs - aeb
    Read the superblock only once - aeb
    Added a very weak heuristic for vfat - aeb
    Added efs, iso9660, minix-v2, romfs, qnx4, udf, vxfs, swap - aeb
    Added a test for high sierra (iso9660) - quinlan@bucknell.edu
    Added ufs from a patch by jj. But maybe there are several types of ufs?
    Added ntfs from a patch by Richard Russon.
    Added xfs - 2000-03-21 Martin K. Petersen <mkp@linuxcare.com>
    Added cramfs, hfs, hpfs, adfs - Sepp Wijnands <mrrazz@garbage-coderz.net>
    Added ext3 - Andrew Morton
    Added jfs - Christoph Hellwig
    Added sysv - Tim Launchbury
    Added udf - Bryce Nesbitt
    Added ocfs, ocfs2 - Manish Singh - http://oss.oracle.com/projects/ocfs2/
*/
static char
*magic_known[] = {
	"adfs", "bfs", "cramfs", "efs", "ext", "ext2", "ext3",
	"hfs", "hpfs", "iso9660", "jfs", "minix", "ntfs", "ocfs", "ocfs2",
	"qnx4", "reiserfs", "romfs", "swap", "sysv", "udf", "ufs",
	"vxfs", "xfs", "xiafs"
};

static int
known_fstype(const char *fstype) {
	char **m;

	for (m = magic_known; m - magic_known < SIZE(magic_known); m++)
		if (!strcmp(*m, fstype))
			return 1;
	return 0;
}

/*
 * udf magic - I find that trying to mount garbage as an udf fs
 * causes a very large kernel delay, almost killing the machine.
 * So, we do not try udf unless there is positive evidence that it
 * might work. Strings below taken from ECMA 167.
 */
/*
 * It seems that before udf 2.00 the volume descriptor was not well
 * defined.  For 2.00 you're supposed to keep scanning records until
 * you find one NOT in this list.  (See ECMA 2/8.3.1).
 */
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

/* we saw "CD001" - may be iso9660 or udf - Bryce Nesbitt */
static int
is_really_udf(int fd) {
	int j, bs;
	struct iso_volume_descriptor isosb;

	/* determine the block size by scanning in 2K increments
	   (block sizes larger than 2K will be null padded) */
	for (bs = 1; bs < 16; bs++) {
		lseek(fd, bs*2048+32768, SEEK_SET);
		if (read(fd, (char *)&isosb, sizeof(isosb)) != sizeof(isosb))
			return 0;
		if (isosb.id[0])
			break;
	}

	/* Scan up to another 64 blocks looking for additional VSD's */
	for (j = 1; j < 64; j++) {
		if (j > 1) {
			lseek(fd, j*bs*2048+32768, SEEK_SET);
			if (read(fd, (char *)&isosb, sizeof(isosb))
			    != sizeof(isosb))
				return 0;
		}
		/* If we find NSR0x then call it udf:
		   NSR01 for UDF 1.00
		   NSR02 for UDF 1.50
		   NSR03 for UDF 2.00 */
		if (!strncmp(isosb.id, "NSR0", 4))
			return 1;
		if (!may_be_udf(isosb.id))
			return 0;
	}

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
	while (--p != s)
		sum = (sum >> 8) + (sum & 0xff) + *p;

	return (sum == p[511]);
}

char *
do_guess_fstype(const char *device) {
    int fd;
    char *type = NULL;
    union {
	struct xiafs_super_block xiasb;
	char romfs_magic[8];
	char qnx4fs_magic[10];	/* ignore first 4 bytes */
	long bfs_magic;
	struct ntfs_super_block ntfssb;
	struct fat_super_block fatsb;
	struct xfs_super_block xfsb;
	struct cramfs_super_block cramfssb;
	struct ocfs_volume_header ovh;
	struct efs_volume_header efsvh;
	struct efs_super efssb;
    } xsb;			/* stuff at 0 */
    union {
	struct minix_super_block ms;
	struct ext_super_block es;
	struct ext2_super_block e2s;
	struct vxfs_super_block vs;
    } sb;			/* stuff at 1024 */
    struct ufs_super_block ufssb;
    union {
	struct iso_volume_descriptor iso;
	struct hs_volume_descriptor hs;
    } isosb;
    struct reiserfs_super_block reiserfssb;	/* block 64 or 8 */
    struct jfs_super_block jfssb;		/* block 32 */
    struct hfs_super_block hfssb;
    struct hpfs_super_block hpfssb;
    struct adfs_super_block adfssb;
    struct sysv_super_block svsb;
    struct ocfs2_super_block osb;
    struct stat statbuf;

    /* opening and reading an arbitrary unknown path can have
       undesired side effects - first check that `device' refers
       to a block device or ordinary file */
    if (stat (device, &statbuf) ||
	!(S_ISBLK(statbuf.st_mode) || S_ISREG(statbuf.st_mode)))
      return 0;

    fd = open(device, O_RDONLY);
    if (fd < 0)
      return 0;

    /* do seeks and reads in disk order, otherwise a very short
       partition may cause a failure because of read error */

    if (!type) {
	 /* block 0 */
	 if (lseek(fd, 0, SEEK_SET) != 0
	     || read(fd, (char *) &xsb, sizeof(xsb)) != sizeof(xsb))
	      goto try_iso9660;
	 /* Gyorgy Kovesdi: none of my photocds has a readable block 0 */

	 if (xiafsmagic(xsb.xiasb) == _XIAFS_SUPER_MAGIC)
	      type = "xiafs";
	 else if(!strncmp(xsb.romfs_magic, "-rom1fs-", 8))
	      type = "romfs";
	 else if(!strncmp(xsb.xfsb.s_magic, XFS_SUPER_MAGIC, 4))
	      type = "xfs";
	 else if(!strncmp(xsb.ovh.signature, OCFS_MAGIC, sizeof(OCFS_MAGIC)))
	      type = "ocfs";
	 else if(!strncmp(xsb.qnx4fs_magic+4, "QNX4FS", 6))
	      type = "qnx4";
	 else if(xsb.bfs_magic == 0x1badface)
	      type = "bfs";
	 else if(!strncmp(xsb.ntfssb.s_magic, NTFS_SUPER_MAGIC,
			  sizeof(xsb.ntfssb.s_magic)))
	      type = "ntfs";
	 else if(cramfsmagic(xsb.cramfssb) == CRAMFS_SUPER_MAGIC ||
		 cramfsmagic(xsb.cramfssb) == CRAMFS_SUPER_MAGIC_BE)
	      type = "cramfs";
	 else if (assemble4be(xsb.efsvh.vh_magic) == EFS_VHMAGIC)
	      type = "efs";		/* EFS volume header */
	 				/* might check checksum here */
	 else if (assemble4be(xsb.efssb.fs_magic) == EFS_SBMAGIC ||
		  assemble4be(xsb.efssb.fs_magic) == EFS_SBMAGIC2)
		 type = "efs";		/* EFS partition */
	 else if ((!strncmp(xsb.fatsb.s_os, "MSDOS", 5) ||
		   !strncmp(xsb.fatsb.s_os, "MSWIN", 5) ||
		   !strncmp(xsb.fatsb.s_os, "MTOOL", 5) ||
		   !strncmp(xsb.fatsb.s_os, "IBM", 3) ||
		   !strncmp(xsb.fatsb.s_os, "DRDOS", 5) ||
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
	    /* sector 1 */
	    if (lseek(fd, 512 , SEEK_SET) != 512
		|| read(fd, (char *) &svsb, sizeof(svsb)) != sizeof(svsb))
		    goto io_error;
	    if (sysvmagic(svsb) == SYSV_SUPER_MAGIC )
		    type = "sysv";
    }

    if (!type) {
	/* block 1 */
	if (lseek(fd, 1024, SEEK_SET) != 1024 ||
	    read(fd, (char *) &sb, sizeof(sb)) != sizeof(sb))
		goto io_error;

	/* ext2 has magic in little-endian on disk, so "swapped" is
	   superfluous; however, there have existed strange byteswapped
	   PPC ext2 systems */
	if (ext2magic(sb.e2s) == EXT2_SUPER_MAGIC ||
	    ext2magic(sb.e2s) == EXT2_PRE_02B_MAGIC ||
	    ext2magic(sb.e2s) == swapped(EXT2_SUPER_MAGIC)) {
		type = "ext2";

	     /* maybe even ext3? */
	     if ((assemble4le(sb.e2s.s_feature_compat)
		  & EXT3_FEATURE_COMPAT_HAS_JOURNAL) &&
		 assemble4le(sb.e2s.s_journal_inum) != 0)
		     type = "ext3";	/* "ext3,ext2" */
	}

	else if (minixmagic(sb.ms) == MINIX_SUPER_MAGIC ||
		 minixmagic(sb.ms) == MINIX_SUPER_MAGIC2 ||
		 minixmagic(sb.ms) == swapped(MINIX_SUPER_MAGIC2) ||
		 minixmagic(sb.ms) == MINIX2_SUPER_MAGIC ||
		 minixmagic(sb.ms) == MINIX2_SUPER_MAGIC2)
		type = "minix";

	else if (extmagic(sb.es) == EXT_SUPER_MAGIC)
		type = "ext";

	else if (vxfsmagic(sb.vs) == VXFS_SUPER_MAGIC)
		type = "vxfs";
    }

    if (!type) {
	/* block 1 */
        if (lseek(fd, 0x400, SEEK_SET) != 0x400
            || read(fd, (char *) &hfssb, sizeof(hfssb)) != sizeof(hfssb))
             goto io_error;

        /* also check if block size is equal to 512 bytes,
	   or a multiple. (I see 1536 here.) */
        if (hfsmagic(hfssb) == HFS_SUPER_MAGIC &&	/* always BE */
	    hfsblksize(hfssb) != 0 &&
	    (hfsblksize(hfssb) & 0x1ff) == 0)
             type = "hfs";
    }

    if (!type) {
	/* block 3 */
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
	 int mag;

	 /* block 8 */
	 if (lseek(fd, 8192, SEEK_SET) != 8192
	     || read(fd, (char *) &ufssb, sizeof(ufssb)) != sizeof(ufssb))
	      goto io_error;

	 mag = ufsmagic(ufssb);
	 if (mag == UFS_SUPER_MAGIC_LE || mag == UFS_SUPER_MAGIC_BE)
	      type = "ufs";
    }

    if (!type) {
	/* block 8 */
	if (lseek(fd, REISERFS_OLD_DISK_OFFSET_IN_BYTES, SEEK_SET) !=
				REISERFS_OLD_DISK_OFFSET_IN_BYTES
	    || read(fd, (char *) &reiserfssb, sizeof(reiserfssb)) !=
		sizeof(reiserfssb))
	    goto io_error;
	if (reiserfs_magic_version(reiserfssb.s_magic))
	    type = "reiserfs";
    }

    if (!type) {
	/* block 8 */
        if (lseek(fd, 0x2000, SEEK_SET) != 0x2000
            || read(fd, (char *) &hpfssb, sizeof(hpfssb)) != sizeof(hpfssb))
             goto io_error;

        if (hpfsmagic(hpfssb) == HPFS_SUPER_MAGIC)
             type = "hpfs";
    }

    if (!type) {
	 /* block 32 */
	 if (lseek(fd, JFS_SUPER1_OFF, SEEK_SET) != JFS_SUPER1_OFF
	     || read(fd, (char *) &jfssb, sizeof(jfssb)) != sizeof(jfssb))
	      goto io_error;
	 if (!strncmp(jfssb.s_magic, JFS_MAGIC, 4))
	      type = "jfs";
    }

    if (!type) {
	 /* block 32 */
    try_iso9660:
	 if (lseek(fd, 0x8000, SEEK_SET) != 0x8000
	     || read(fd, (char *) &isosb, sizeof(isosb)) != sizeof(isosb))
	      goto io_error;

	 if (strncmp(isosb.hs.id, HS_STANDARD_ID, sizeof(isosb.hs.id)) == 0) {
		 /* "CDROM" */
		 type = "iso9660";
	 } else if (strncmp(isosb.iso.id, ISO_STANDARD_ID,
			  sizeof(isosb.iso.id)) == 0) {
		 /* CD001 */
		 type = "iso9660";
		 if (is_really_udf(fd))
			 type = "udf";
	 } else if (may_be_udf(isosb.iso.id))
		 type = "udf";
    }

    if (!type) {
	/* block 64 */
	if (lseek(fd, REISERFS_DISK_OFFSET_IN_BYTES, SEEK_SET) !=
		REISERFS_DISK_OFFSET_IN_BYTES
	    || read(fd, (char *) &reiserfssb, sizeof(reiserfssb)) !=
		sizeof(reiserfssb))
	    goto io_error;
	if (reiserfs_magic_version(reiserfssb.s_magic))
	    type = "reiserfs";
    }

    if (!type) {
	    int blksize, blkoff;

	    for (blksize = OCFS2_MIN_BLOCKSIZE;
		 blksize <= OCFS2_MAX_BLOCKSIZE;
		 blksize <<= 1) {
		    blkoff = blksize * OCFS2_SUPER_BLOCK_BLKNO;
		    if (lseek(fd, blkoff, SEEK_SET) != blkoff
			|| read(fd, (char *) &osb, sizeof(osb)) != sizeof(osb))
			    goto io_error;
		    if (strncmp(osb.signature, OCFS2_SUPER_BLOCK_SIGNATURE,
				sizeof(OCFS2_SUPER_BLOCK_SIGNATURE)) == 0)
			    type = "ocfs2";
	    }
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
    if (errno)
	 perror(device);
    else
	 fprintf(stderr, _("mount: error while guessing filesystem type\n"));
    close(fd);
    return 0;
}

#endif

static struct tried {
	struct tried *next;
	char *type;
} *tried = NULL;

static int
was_tested(const char *fstype) {
	struct tried *t;

	if (known_fstype(fstype))
		return 1;
	for (t = tried; t; t = t->next) {
		if (!strcmp(t->type, fstype))
			return 1;
	}
	return 0;
}

static void
set_tested(const char *fstype) {
	struct tried *t = xmalloc(sizeof(struct tried));

	t->next = tried;
	t->type = xstrdup(fstype);
	tried = t;
}

static void
free_tested(void) {
	struct tried *t, *tt;

	t = tried;
	while(t) {
		free(t->type);
		tt = t->next;
		free(t);
		t = tt;
	}
	tried = NULL;
}

char *
guess_fstype(const char *spec) {
	char *type = do_guess_fstype(spec);
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
      return xstrdup(fsname);
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
	   const char **types) {
	char *files[2] = { ETC_FILESYSTEMS, PROC_FILESYSTEMS };
	FILE *procfs;
	char *fsname;
	const char *notypes = NULL;
	int no = 0;
	int ret = 1;
	int errsv = 0;
	int i;

	if (*types && !strncmp(*types, "no", 2)) {
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
			if (was_tested (fsname))
				continue;
			if (no && matching_type(fsname, notypes))
				continue;
			set_tested (fsname);
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
		free_tested();
		fclose(procfs);
		errno = errsv;
		return ret;
	nexti:;
	}
	return 1;
}
