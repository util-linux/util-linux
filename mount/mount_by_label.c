/*
 * mount_by_label.c - aeb
 *
 * 1999-02-22 Arkadiusz Mi¶kiewicz <misiek@pld.ORG.PL>
 * - added Native Language Support
 * 2000-01-20 James Antill <james@and.org>
 * - Added error message if /proc/partitions cannot be opened
 * 2000-05-09 Erik Troan <ewt@redhat.com>
 * - Added cache for UUID and disk labels
 * 2000-11-07 Nathan Scott <nathans@sgi.com>
 * - Added XFS support
 * 2001-11-22 Kirby Bohling <kbohling@birddog.com>
 * - Added support of labels on LVM
 * 2002-03-21 Christoph Hellwig <hch@infradead.org>
 * - Added JFS support
 * 2002-07-11 Christoph Hellwig <hch@infradead.org>
 * - Added JFS v2 format support
 * 2002-07-26 Luciano Chavez <lnx1138@us.ibm.com>
 * - Added EVMS support
 */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>   /* needed for opendir */
#include <dirent.h>
#include "sundries.h"		/* for xstrdup */
#include "linux_fs.h"
#include "mount_by_label.h"
#include "nls.h"

#define PROC_PARTITIONS "/proc/partitions"
#define DEVLABELDIR	"/dev"
#define VG_DIR          "/proc/lvm/VGs"
#define EVMS_VOLUME_NAME_SIZE  127
#define PROC_EVMS_VOLUMES "/proc/evms/volumes"

static struct uuidCache_s {
	struct uuidCache_s *next;
	char uuid[16];
	char *label;
	char *device;
} *uuidCache = NULL;

/*
 * See whether this device has (the magic of) a RAID superblock at the end.
 * If so, it probably is, or has been, part of a RAID array.
 *
 * For the moment this test is switched off - it causes problems.
 * "Checking for a disk label should only be done on the full raid,
 *  not on the disks that form the raid array. This test causes a lot of
 *  problems when run on my striped promise fasttrak 100 array."
 */
static int
is_raid_partition(int fd) {
#if 0
	struct mdp_super_block mdsb;
	int n;

	/* hardcode 4096 here in various places, because that's
	   what it's defined to be.  Note that even if we used
	   the actual kernel headers, sizeof(mdp_super_t) is
	   slightly larger in the 2.2 kernel on 64-bit archs,
	   so using that wouldn't work. */
	lseek(fd, -4096, SEEK_END);	/* Ignore possible error
					   about return value overflow */
	n = 4096;
	if (sizeof(mdsb) < n)
		n = sizeof(mdsb);
	if (read(fd, &mdsb, n) != n)
		return 1;		/* error */
	return (mdsbmagic(mdsb) == MD_SB_MAGIC);
#else
	return 0;
#endif
}

/* for now, only ext2, ext3, xfs, ocfs are supported */
static int
get_label_uuid(const char *device, char **label, char *uuid) {
	int fd;
	int rv = 1;
	size_t namesize;
	struct ext2_super_block e2sb;
	struct xfs_super_block xfsb;
	struct jfs_super_block jfssb;
	struct ocfs_volume_header ovh;	/* Oracle */
	struct ocfs_volume_label olbl;

	fd = open(device, O_RDONLY);
	if (fd < 0)
		return rv;

	/* If there is a RAID partition, or an error, ignore this partition */
	if (is_raid_partition(fd)) {
		close(fd);
		return rv;
	}

	if (lseek(fd, 1024, SEEK_SET) == 1024
	    && read(fd, (char *) &e2sb, sizeof(e2sb)) == sizeof(e2sb)
	    && (ext2magic(e2sb) == EXT2_SUPER_MAGIC)) {
		memcpy(uuid, e2sb.s_uuid, sizeof(e2sb.s_uuid));
		namesize = sizeof(e2sb.s_volume_name);
		if ((*label = calloc(namesize + 1, 1)) != NULL)
			memcpy(*label, e2sb.s_volume_name, namesize);
		rv = 0;
	}
	else if (lseek(fd, 0, SEEK_SET) == 0
	    && read(fd, (char *) &xfsb, sizeof(xfsb)) == sizeof(xfsb)
	    && (strncmp(xfsb.s_magic, XFS_SUPER_MAGIC, 4) == 0)) {
		memcpy(uuid, xfsb.s_uuid, sizeof(xfsb.s_uuid));
		namesize = sizeof(xfsb.s_fname);
		if ((*label = calloc(namesize + 1, 1)) != NULL)
			memcpy(*label, xfsb.s_fname, namesize);
		rv = 0;
	}
	else if (lseek(fd, 0, SEEK_SET) == 0
	    && read(fd, (char *) &ovh, sizeof(ovh)) == sizeof(ovh)
	    && (strncmp(ovh.signature, OCFS_MAGIC, sizeof(OCFS_MAGIC)) == 0)
	    && (lseek(fd, 512, SEEK_SET) == 512)
	    && read(fd, (char *) &olbl, sizeof(olbl)) == sizeof(olbl)) {
		uuid[0] = '\0';
		namesize = ocfslabellen(olbl);
		if ((*label = calloc(namesize + 1, 1)) != NULL)
			memcpy(*label, olbl.label, namesize);
		rv = 0;
	}
	else if (lseek(fd, JFS_SUPER1_OFF, SEEK_SET) == JFS_SUPER1_OFF
	    && read(fd, (char *) &jfssb, sizeof(jfssb)) == sizeof(jfssb)
	    && (strncmp(jfssb.s_magic, JFS_MAGIC, 4) == 0)) {
		if (assemble4le(jfssb.s_version) == 1) {
			/* old (OS/2 compatible) jfs filesystems don't
			   have UUIDs and only have a very small label. */
			memset(uuid, 0, 16);
			namesize = sizeof(jfssb.s_fpack);
			if ((*label = calloc(namesize + 1, 1)) != NULL)
				memcpy(*label, jfssb.s_fpack, namesize);
		} else {
			memcpy(uuid, jfssb.s_uuid, sizeof(jfssb.s_uuid));
			namesize = sizeof(jfssb.s_label);
			if ((*label = calloc(namesize + 1, 1)) != NULL)
			    memcpy(*label, jfssb.s_label, namesize);
		}
		rv = 0;
	}

	close(fd);
	return rv;
}

static void
uuidcache_addentry(char *device, char *label, char *uuid) {
	struct uuidCache_s *last;

	if (!uuidCache) {
		last = uuidCache = malloc(sizeof(*uuidCache));
	} else {
		for (last = uuidCache; last->next; last = last->next);
		last->next = malloc(sizeof(*uuidCache));
		last = last->next;
	}
	last->next = NULL;
	last->device = device;
	last->label = label;
	memcpy(last->uuid, uuid, sizeof(last->uuid));
}

/* LVM support - Kirby Bohling */
static void
uuidcache_init_lvm(void) {
	char buffer[PATH_MAX];
	char lvm_device[PATH_MAX];
	DIR *vg_dir, *lv_list;
	struct dirent *vg_iter, *lv_iter;
	char uuid[16], *label;

	vg_dir = opendir(VG_DIR);
	if (vg_dir == NULL)	/* to be expected */
		return;

	seekdir(vg_dir, 2);
	while ((vg_iter = readdir(vg_dir)) != 0) {
		sprintf(buffer, "%s/%s/LVs", VG_DIR, vg_iter->d_name);
		lv_list = opendir(buffer);
		if (lv_list == NULL) {
			perror("mount (init_lvm)");
			continue;
		}
		seekdir(lv_list, 2);
		while ((lv_iter = readdir(lv_list)) != 0) {
			/* Now we have the file.. could open it and read out
			 * where the device is, read the first line, second
			 * field... Instead we guess.
			 */
			sprintf(lvm_device, "%s/%s/%s", DEVLABELDIR,
				vg_iter->d_name, lv_iter->d_name);
			if (!get_label_uuid(lvm_device, &label, uuid))
				uuidcache_addentry(strdup(lvm_device),
						   label, uuid);
		}
		closedir(lv_list);
	}
	closedir(vg_dir);
}

static int
uuidcache_init_evms(void) {
	FILE *procvol;
	char *label;
	char uuid[16];
	char volname[EVMS_VOLUME_NAME_SIZE+1];
	char line[EVMS_VOLUME_NAME_SIZE+80];

	procvol = fopen(PROC_EVMS_VOLUMES, "r");
	if (!procvol)
		return 0;

	while (fgets(line, sizeof(line), procvol)) {
		if (sscanf(line, "%*d %*d %*d %*s %*s %[^\n]", volname) == 1) {
			if (!get_label_uuid(volname, &label, uuid))
				uuidcache_addentry(strdup(volname), label, uuid);
		}
	}
	
	fclose(procvol);
	
	return 1;
}

static void
uuidcache_init(void) {
	char line[100];
	char *s;
	int ma, mi, sz;
	static char ptname[100];
	FILE *procpt;
	char uuid[16], *label;
	char device[110];
	int firstPass;
	int handleOnFirst;
#if 0
	char iobuf[32*1024];	/* For setvbuf */
#endif

	if (uuidCache)
		return;

	if (uuidcache_init_evms())
		return;

	procpt = fopen(PROC_PARTITIONS, "r");
	if (!procpt) {
		static int warn = 0;
		if (!warn++)
		    error (_("mount: could not open %s, so UUID and LABEL "
			     "conversion cannot be done.\n"),
		       PROC_PARTITIONS);
		return;
	}
#if 0
/* Ugly kludge - the contents of /proc/partitions change in time,
   and this causes failures when the file is not read in one go.
   In particular, one cannot use stdio on /proc/partitions.
   Doing this ourselves is not easy either, since stat returns 0
   so the size is unknown. We might try increasing buffer sizes
   until a single read gets all. For now only pick a largish buffer size. */
/* All these troubles are mainly caused by people who patch the kernel
   to keep statistics in /proc/partitions. Of course, statistics belong
   in some /proc/diskstats, not in some /proc file that happened to
   exist already. */

	setvbuf(procpt, iobuf, _IOFBF, sizeof(iobuf));
#endif

	for (firstPass = 1; firstPass >= 0; firstPass--) {
	    fseek(procpt, 0, SEEK_SET);

	    while (fgets(line, sizeof(line), procpt)) {
		if (sscanf (line, " %d %d %d %[^\n ]",
			    &ma, &mi, &sz, ptname) != 4)
			continue;

		/* skip extended partitions (heuristic: size 1) */
		if (sz == 1)
			continue;

		/* look only at md devices on first pass */
		handleOnFirst = !strncmp(ptname, "md", 2);
		if (firstPass != handleOnFirst)
			continue;

		/* skip entire disk (minor 0, 64, ... on ide;
		   0, 16, ... on sd) */
		/* heuristic: partition name ends in a digit */
		/* devfs has .../disc and .../part1 etc. */

		for(s = ptname; *s; s++);
		if (isdigit(s[-1])) {
		/*
		 * Note: this is a heuristic only - there is no reason
		 * why these devices should live in /dev.
		 * Perhaps this directory should be specifiable by option.
		 * One might for example have /devlabel with links to /dev
		 * for the devices that may be accessed in this way.
		 * (This is useful, if the cdrom on /dev/hdc must not
		 * be accessed.)
		 */
			sprintf(device, "%s/%s", DEVLABELDIR, ptname);
			if (!get_label_uuid(device, &label, uuid))
				uuidcache_addentry(strdup(device), label, uuid);
		}
	    }
	}

	fclose(procpt);

	uuidcache_init_lvm();
}

#define UUID   1
#define VOL    2

static char *
get_spec_by_x(int n, const char *t) {
	struct uuidCache_s *uc;

	uuidcache_init();
	uc = uuidCache;

	while(uc) {
		switch (n) {
		case UUID:
			if (!memcmp(t, uc->uuid, sizeof(uc->uuid)))
				return xstrdup(uc->device);
			break;
		case VOL:
			if (!strcmp(t, uc->label))
				return xstrdup(uc->device);
			break;
		}
		uc = uc->next;
	}
	return NULL;
}

static u_char
fromhex(char c) {
	if (isdigit(c))
		return (c - '0');
	else if (islower(c))
		return (c - 'a' + 10);
	else
		return (c - 'A' + 10);
}

char *
get_spec_by_uuid(const char *s) {
	u_char uuid[16];
	int i;

	if (strlen(s) != 36 ||
	    s[8] != '-' || s[13] != '-' || s[18] != '-' || s[23] != '-')
		goto bad_uuid;
	for (i=0; i<16; i++) {
	    if (*s == '-') s++;
	    if (!isxdigit(s[0]) || !isxdigit(s[1]))
		    goto bad_uuid;
	    uuid[i] = ((fromhex(s[0])<<4) | fromhex(s[1]));
	    s += 2;
	}
	return get_spec_by_x(UUID, uuid);

 bad_uuid:
	die(EX_USAGE, _("mount: bad UUID"));
	return NULL;		/* just for gcc */
}

char *
get_spec_by_volume_label(const char *s) {
	return get_spec_by_x(VOL, s);
}

const char *
get_volume_label_by_spec(const char *spec) {
        struct uuidCache_s *uc;

        uuidcache_init();
        uc = uuidCache;

	while(uc) {
		if (!strcmp(spec, uc->device))
			return uc->label;
 		uc = uc->next;
	}
	return NULL;
}

/*
 * second_occurrence_of_vol_label()
 * As labels are user defined they are not necessarily 
 * system-wide unique. Make sure that they are.
 */
const char *
second_occurrence_of_vol_label (const char *label) {
  	struct uuidCache_s *last;
        int occurrences = 0;

        uuidcache_init();

        for (last = uuidCache; last->next; last = last->next) {
		if (!strcmp(last->label, label)) {
			occurrences++;
			if (occurrences == 2)
				return last->device;
		}
        }
        
        return NULL;
}

          
