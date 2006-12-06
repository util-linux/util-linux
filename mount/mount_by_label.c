#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <unistd.h>
#include "sundries.h"		/* for xstrdup */
#include "linux_fs.h"
#include "mount_by_label.h"

#define PROC_PARTITIONS "/proc/partitions"
#define DEVLABELDIR	"/dev"

static FILE *procpt;

static void
procptclose(void) {
    if (procpt)
        fclose (procpt);
    procpt = 0;
}

static int
procptopen(void) {
    return ((procpt = fopen(PROC_PARTITIONS, "r")) != NULL);
}

static char *
procptnext(void) {
   char line[100];
   char *s;
   int ma, mi, sz;
   static char ptname[100];

   while (fgets(line, sizeof(line), procpt)) {
      if (sscanf (line, " %d %d %d %[^\n]\n", &ma, &mi, &sz, ptname) != 4)
	      continue;

      /* skip extended partitions (heuristic: size 1) */
      if (sz == 1)
	      continue;

      /* skip entire disk (minor 0, 64, ... on ide; 0, 16, ... on sd) */
      /* heuristic: partition name ends in a digit */
      for(s = ptname; *s; s++);
      if (isdigit(s[-1]))
	      return ptname;
   }
   return 0;
}

#define UUID	1
#define VOL	2

/* for now, only ext2 is supported */
static int
has_right_label(const char *device, int n, const char *label) {

	/* start with a test for ext2, taken from mount_guess_fstype */
	/* should merge these later */
	int fd;
	char *s;
	struct ext2_super_block e2sb;

	fd = open(device, O_RDONLY);
	if (fd < 0)
		return 0;

	if (lseek(fd, 1024, SEEK_SET) != 1024
	    || read(fd, (char *) &e2sb, sizeof(e2sb)) != sizeof(e2sb)
	    || (ext2magic(e2sb) != EXT2_SUPER_MAGIC)) {
		close(fd);
		return 0;
	}

	close(fd);

	/* superblock is ext2 - now what is its label? */
	s = ((n == UUID) ? e2sb.s_uuid : e2sb.s_volume_name);
	return (strncmp(s, label, 16) == 0);
}

static char *
get_spec_by_x(int n, const char *t) {
	char *pt;
	char device[110];

	if(!procptopen())
		return NULL;
	while((pt = procptnext()) != NULL) {
		/* Note: this is a heuristic only - there is no reason
		   why these devices should live in /dev.
		   Perhaps this directory should be specifiable by option.
		   One might for example have /devlabel with links to /dev
		   for the devices that may be accessed in this way.
		   (This is useful, if the cdrom on /dev/hdc must not
		   be accessed.)
		*/
		sprintf(device, "%s/%s", DEVLABELDIR, pt);
		if (has_right_label(device, n, t)) {
			procptclose();
			return xstrdup(device);
		}
	}
	procptclose();
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
	die(EX_USAGE, "mount: bad UUID");
}

char *
get_spec_by_volume_label(const char *s) {
	return get_spec_by_x(VOL, s);
}

