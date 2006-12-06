/*
 * mount_by_label.c - aeb
 *
 * 1999-02-22 Arkadiusz Mi¶kiewicz <misiek@misiek.eu.org>
 * - added Native Language Support
 * 2000-01-20 James Antill <james@and.org>
 * - Added error message if /proc/partitions cannot be opened
 * 2000-05-09 Erik Troan <ewt@redhat.com>
 * - Added cache for UUID and disk labels
 */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <unistd.h>
#include "sundries.h"		/* for xstrdup */
#include "linux_fs.h"
#include "mount_by_label.h"
#include "nls.h"

#define PROC_PARTITIONS "/proc/partitions"
#define DEVLABELDIR	"/dev"

static struct uuidCache_s {
	struct uuidCache_s *next;
	char uuid[16];
	char *label;
	char *device;
} *uuidCache = NULL;

/* for now, only ext2 is supported */
static int
get_label_uuid(const char *device, char **label, char *uuid) {

	/* start with a test for ext2, taken from mount_guess_fstype */
	/* should merge these later */
	int fd;
	struct ext2_super_block e2sb;

	fd = open(device, O_RDONLY);
	if (fd < 0)
		return 1;

	if (lseek(fd, 1024, SEEK_SET) != 1024
	    || read(fd, (char *) &e2sb, sizeof(e2sb)) != sizeof(e2sb)
	    || (ext2magic(e2sb) != EXT2_SUPER_MAGIC)) {
		close(fd);
		return 1;
	}

	close(fd);

	/* superblock is ext2 - now what is its label? */
	memcpy(uuid, e2sb.s_uuid, sizeof(e2sb.s_uuid));
	*label = strdup(e2sb.s_volume_name);

	return 0;
}

static void
uuidcache_addentry(char *device, char *label, char *uuid) {
	struct uuidCache_s *last;

	if (!uuidCache) {
		last = uuidCache = malloc(sizeof(*uuidCache));
	} else {
		for (last = uuidCache; last->next; last = last->next) ;
		last->next = malloc(sizeof(*uuidCache));
		last = last->next;
	}
	last->next = NULL;
	last->device = device;
	last->label = label;
	memcpy(last->uuid, uuid, sizeof(last->uuid));
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

	if (uuidCache)
		return;

	procpt = fopen(PROC_PARTITIONS, "r");
	if (!procpt)
		return;

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
