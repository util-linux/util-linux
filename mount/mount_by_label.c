#ifndef HAVE_BLKID
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
#include "get_label_uuid.h"
#include "mount_by_label.h"
#include "nls.h"

#define PROC_PARTITIONS "/proc/partitions"
#define DEVLABELDIR	"/dev"
#define VG_DIR          "/proc/lvm/VGs"
#define EVMS_VOLUME_NAME_SIZE  127
#define PROC_EVMS_VOLUMES "/proc/evms/volumes"

extern char *progname;

static struct uuidCache_s {
	struct uuidCache_s *next;
	char uuid[16];
	char *label;
	char *device;
} *uuidCache = NULL;

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
			perror("uuidcache_init_lvm");
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
				uuidcache_addentry(xstrdup(lvm_device),
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
				uuidcache_addentry(xstrdup(volname), label, uuid);
		}
	}
	
	fclose(procvol);
	
	return 1;
}

/*
 * xvm is a proprietary sgi volume manager, it goes into /proc/partitions
 * like this:
 *
 *   4     0    2210817 xvm/local/vol/myvolume/data/block
 *   4     1    2210817 xvm/local/vol/myvolume/rt/block
 *   4     2    2210817 xvm/local/vol/myvolume/log/block
 *   4     3    2210818 xvm/local/vol/discs3/data/block
 *
 * The heuristics here are that the device should start with "xvm,"
 * but should not end in "log/block" or "rt/block" - those are
 * special devices for the xfs filesystem external log & realtime device.
 */

/* Return 1 if this looks like an xvm device that should be scanned */
static int
is_xvm(char *ptname)
{
	int len;

	/* if it doesn't start with "xvm," we're done. */
	if (strncmp(ptname, "xvm", 3))
		return 0;

	len = strlen(ptname);
	/*
	 * check for "log/block" or "rt/block" on the end,
	 * these are special - don't scan.
	 */
	if (!strncmp(ptname+(len-9), "log/block", 9) ||
	    !strncmp(ptname+(len-8), "rt/block", 8))
	    	return 0;

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
		    error (_("%s: could not open %s, so UUID and LABEL "
			     "conversion cannot be done.\n"),
			   progname, PROC_PARTITIONS);
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
		if (!index(line, '\n'))
			break;

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

		for (s = ptname; *s; s++);
		if (isdigit(s[-1]) || is_xvm(ptname)) {
			
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
				uuidcache_addentry(xstrdup(device), label, uuid);
		}
	    }
	}

	fclose(procpt);

	uuidcache_init_lvm();
}

#define UUID   1
#define VOL    2

static const char *
get_spec_by_x(int n, const char *t) {
	struct uuidCache_s *uc;

	uuidcache_init();
	uc = uuidCache;

	while (uc) {
		switch (n) {
		case UUID:
			if (!memcmp(t, uc->uuid, sizeof(uc->uuid)))
				return xstrdup(uc->device);
			break;
		case VOL:
			if (uc->label && !strcmp(t, uc->label))
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

const char *
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
	die(EX_USAGE, _("%s: bad UUID"), progname);
	return NULL;		/* just for gcc */
}

const char *
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

        for (last = uuidCache; last; last = last->next) {
		if (last->label && !strcmp(last->label, label)) {
			occurrences++;
			if (occurrences == 2)
				return last->device;
		}
        }
        
        return NULL;
}

          
#endif
