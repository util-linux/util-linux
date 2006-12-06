/*
 * makedev.c: Generate /dev entries
 *
 * Based on the MAKEDEV shell script, version 2.0, distributed with
 * util-linux 1.10 and written by Nick Holloway. 
 *
 * A number of bugs were fixed, and some additional features added.
 * Written 10-Dec-94 by David A. Holland, dholland@husc.harvard.edu
 *
 * Copyright 1994, 1995. All rights reserved. 
 * See the file LEGAL.NOTICE for conditions of redistribution.
 *
 * Bugs:
 *    None known right now.
 *
 * History:
 *
 * Version 1.4a: Sun Feb 26 18:08:45 1995, faith@cs.unc.edu
 *               Forced devinfo and makedev to be in /etc
 * Version 1.4: 15-Jan-95  Wrote man pages. Now reads DEVINFO.local.
 * Version 1.3: 31-Dec-94  Bug fixes. Added batches. Added omits.
 * Version 1.2: 11-Dec-94  Add configuration file parsing.
 * Version 1.1: 11-Dec-94  Distinguish block and character devices in the
 *    table of major device numbers. Changed the name and format of the
 *    update cache file to include the type. It appears that the old script
 *    was broken in this regard.
 * Version 1.0: 10-Dec-94  Initial version.
 */

static const char *version = "MAKEDEV-C version 1.4a";

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <pwd.h>
#include <grp.h>
#include <sys/stat.h>

#define YES 1
#define NO 0

static int isverbose=NO;  /* flag: print out what we do? */
static int deletion=NO;   /* flag: delete instead of create */
static int donothing=NO;  /* flag: don't actually do anything */

/*
 * Proto for main operative function.
 */
typedef enum { M_CREATE, M_OMIT } makeopts;
static void make(const char *batch_or_grp_or_devname, makeopts);

/*
 * Roll over and die.
 */
static void crash(const char *msg) {
  fprintf(stderr, "MAKEDEV: %s\n", msg);
  exit(1);
}

/*
 * Print a warning.
 */
static void warn(const char *format, ...) {
  va_list ap;
  va_start(ap, format);
  fprintf(stderr, "MAKEDEV: ");
  vfprintf(stderr, format, ap);
  fprintf(stderr, "\n");
  va_end(ap);
}

/*
 * Translate string name to uid.
 */
static uid_t name2uid(const char *name) {
  struct passwd *p = getpwnam(name);
  if (!p) warn("undefined user: %s, using uid 0", name);
  return p ? p->pw_uid : 0;  /* make things owned by root by default */
}

/*
 * Translate string name to gid.
 */
static gid_t name2gid(const char *name) {
  struct group *g = getgrnam(name);
  if (!g) warn("undefined group: %s, using gid 0", name);
  return g ? g->gr_gid : 0;  /* group 0 is a good default too */
}

/*
 * Proto for parser.
 */
static void doparse(FILE *f, int filetype, const char *filename);

/************************* device classes *************************/

/*
 * A device class is a string attached to the device which tells us
 * what set of permissions and ownership should be used. This is the
 * table of classes.
 */

typedef struct {
    const char *classname;
    const char *owner;
    const char *group;
    int mode;
} devclass;

#define MAXCLASSES 32
static devclass classes[MAXCLASSES];
static int nclasses=0;

static void addclass(const char *name, const char *o, const char *g, int m) {
  if (nclasses>=MAXCLASSES) crash("out of space for device classes");
  classes[nclasses].classname = name;
  classes[nclasses].owner = o;
  classes[nclasses].group = g;
  classes[nclasses].mode = m;
  nclasses++;
  name2uid(o);  /* check for undefined users/groups */
  name2gid(g);
}

static void loadclasses(void) {
  FILE *f = fopen("/etc/makedev.cfg", "r");
  if (!f) crash("can't find makedev.cfg");
  doparse(f, 4, "makedev.cfg");
  fclose(f);
}

/*
 * Return the index into the above table for a particular class name.
 */
static int which_class(const char *name) {
  int i;
  for (i=0; i<nclasses; i++)
    if (!strcmp(classes[i].classname, name)) return i;
  return 0;
}

/*
 * Produce an "ls -l"-ish mode string.
 */
static const char *modestring(int mode) {
  static char rv[12];
  int i,z;
  strcpy(rv, "rwxrwxrwx");
  for (i=8,z=1; i>=0; i--, z<<=1) if (!(mode&z)) rv[i]='-';
  return rv;
}

/*
 * Create (or delete, or update) a block or character device.
 */
static void class_makedev(const char *name, const char *class,
			  int major, int minor, char type) {
  int x = which_class(class), mode = classes[x].mode;
  const char *owner = classes[x].owner, *group = classes[x].group;
  if (isverbose) {
    if (deletion) printf("rm -f %s\n", name);
    else printf("%c%s   1 %-8s %-8s %3d, %3d for %s\n", type,
		modestring(mode), owner, group, major, minor, name);
  }
  if (donothing) return;
  if (unlink(name) && deletion) warn("Couldn't remove %s\n", name);
  if (!deletion) {
    dev_t q = (major<<8) | minor;
    if (mknod(name, type=='c' ? S_IFCHR : S_IFBLK,  q) ||
	chown(name, name2uid(owner), name2gid(group)) ||
	chmod(name, mode)) {
      warn("couldn't create %s: %s", name, strerror(errno));
    }
  }
}

/************************* major number list *************************/

/*
 * In Linux device major numbers can be allocated dynamically, so we go
 * look in /proc/devices to see what they are. This keeps track of things.
 */

typedef struct {
    const char *procname;
    int flag;
} majorentry;

#define MAXMAJORS 256
static majorentry cmajors[MAXMAJORS];  /* initialized to 0 */
static majorentry bmajors[MAXMAJORS];  /* initialized to 0 */
static int no_proc=0;   /* true if we didn't find /proc/devices */

/*
 * Store the name associated with a particular major device number.
 */
static void set_major(const char *procname, int ischar, int num) {
  if (num<0 || num>255) {
    warn("warning: got bogus major number %d for %s", num, procname);
    return;
  }
  if (ischar) cmajors[num].procname=procname;
  else bmajors[num].procname=procname;
}

/*
 * Look up a major device number by name; return the default value
 * if provided. A default value of -1 implies the device is only
 * dynamic, and so if there's no entry we shouldn't even note its
 * existence.
 */
static int get_major(const char *procname, int ischar, int defaalt) {
  int i;
  if (!procname) return defaalt;
  if (ischar) {
    for (i=0; i<MAXMAJORS; i++)
      if (cmajors[i].procname && !strcmp(cmajors[i].procname, procname))
	return i;
  }
  else {
    for (i=0; i<MAXMAJORS; i++)
      if (bmajors[i].procname && !strcmp(bmajors[i].procname, procname))
	return i;
  }
  return defaalt;
}

/*
 * Read /proc/devices.
 */
static void setup_majors(void) {
  FILE *f = fopen("/proc/devices", "r");
  if (!f) {
    fprintf(stderr, "MAKEDEV: warning: can't read /proc/devices\n");
    no_proc = 1;
    return;
  }
  doparse(f, 1, "/proc/devices");
  fclose(f);
}

/************************** procname list *************************/

/*
 * The names found in /proc/devices aren't usually quite the same
 * as the names we use. This is a mapping between the two namespaces.
 */
typedef struct {
    const char *procname;
    const char *groupname;
} namealias;

#define MAXALIASES 100
static namealias aliases[MAXALIASES];
static int naliases=0;

static void addalias(const char *procname, const char *groupname) {
  if (naliases>=MAXALIASES) crash("out of space for aliases");
  aliases[naliases].procname = procname;
  aliases[naliases].groupname = groupname;
  naliases++;
}

static void ignore_procname(const char *procname) {
  addalias(procname, NULL);
}

static const char *procnameof(const char *groupname) {
  int i;
  for (i=0; i<naliases; i++) if (!strcmp(groupname, aliases[i].groupname))
    return aliases[i].procname;
  return NULL;
}

static const char *groupnameof(const char *procname) {
  int i;
  for (i=0; i<naliases; i++) if (!strcmp(procname, aliases[i].procname))
    return aliases[i].groupname;
  return NULL;
}

/************************* batch list *************************/
/*
 * Create a device "batch" - a bunch of devices or groups.
 * This is used for "generic" and automatically for disk entries.
 * (Disk entries for "hd" come up with groups hda, hdb, etc., but
 * "hd" itself needs to run these too.)
 */
#define MAXTARGETS 32
#define MAXBATCHES 16

typedef struct {
    const char *name;  /* name of batch */
    const char *targets[MAXTARGETS];
    int ntargets;
    int busy;
} batch;

static batch batches[MAXBATCHES];
static int nbatches=0;

/*
 * Start a new batch.
 */
static batch *addbatch(const char *name) {
  batch *b;
  if (nbatches>=MAXBATCHES) crash("Out of space for batches");
  b = &batches[nbatches++];
  b->name = name;
  b->busy = NO;
  return b;
}

/*
 * Add something to a batch.
 */
static batch *add2batch(batch *b, const char *target) {
  if (b->ntargets>=MAXTARGETS) {
    warn("Too many targets for batch %s (max %d)", b->name, MAXTARGETS);
    return b;
  }
  b->targets[b->ntargets++] = target;
  return b;
}

/*
 * Run a batch.
 */
static void run_batch(const batch *b, makeopts m) {
  int i;
  for (i=0; i<b->ntargets; i++) make(b->targets[i], m);
}

/*
 * Try to run a batch; returns YES if it found one.
 */
static int try_run_batch(const char *name, makeopts m) {
  int i;
  for (i=0; i<nbatches; i++) {
    if (!strcmp(name, batches[i].name)) {
      if (batches[i].busy) {
	warn("Found recursive batch definition for %s", batches[i].name);
	continue;
      }
      batches[i].busy=YES;
      run_batch(&batches[i], m);
      batches[i].busy=NO;
      return YES;
    }
  }
  return NO;
}

/************************* device list *************************/

/*
 * Structure to remember the properties of an individual device.
 * NOTE: if the device is actually a symbolic link, the "class"
 * member is used to store the thing it should be linked to.
 */
typedef struct {
    const char *name;   /* file name to create */
    const char *grp;    /* device "group" name (e.g. "busmice") */
    const char *class;  /* device class ( -> owner and permissions) */
    int major, minor;   /* device number */
    char type;          /* 'c', 'b', or 'l' for symbolic link */
    int omit;           /* don't make me if this is nonzero */
} device;

/*
 * Create a device (link or actual "special file") - special files are
 * passed on to class_makedev().
 */
void makedev(device *d, makeopts m) {
  if (m==M_OMIT) {
    d->omit=1;
  }
  if (d->omit==1) return;
  if (d->type=='l') {
    if (isverbose) {
      if (deletion) printf("rm -f %s\n", d->name);
      else printf("lrwxrwxrwx   %s -> %s\n", d->name, d->class);
    }
    if (donothing) return;
    if (unlink(d->name) && deletion) warn("Couldn't remove %s\n", d->name);
    if (!deletion) {
      if (symlink(d->class, d->name)) /* class holds thing pointed to */
	warn("couldn't link %s -> %s: %s", d->name, d->class, strerror(errno));
    }
  }
  else class_makedev(d->name, d->class, d->major, d->minor, d->type);
}

/*
 * Array of devices. We allocate it once from main(); it doesn't grow.
 * Should maybe make it growable sometime. This keeps track of all possible
 * devices. We build this thing first, and then create devices from it as
 * requested.
 */
static device *devices = NULL;
static int maxdevices, ndevices;

/*
 * Allocate space for the device array.
 */
static void allocate_devs(int nd) {
  devices = malloc(nd * sizeof(device));
  if (!devices) crash("Out of memory");
  ndevices = 0;
  maxdevices = nd;
}

/*
 * Check all the devices for having valid device classes.
 */
static void check_classes(void) {
  int i;
  const char *q=NULL;
  for (i=0; i<ndevices; i++) 
    if (devices[i].type!='l' && !devices[i].omit &&
	which_class(devices[i].class)<0) {
      if (!q || strcmp(q, devices[i].class)) {
	warn("Invalid device class %s for %s", 
	     devices[i].class, devices[i].name);
	q = devices[i].class;
      }
      devices[i].class = "default";
    }
}

/*
 * Create an entry in the device table for a single device.
 */
static void init(const char *name, const char *grp, const char *class,
		 int major, int minor, int type) {
  if (major < 0) return;
  if (!strchr("bcl", type)) {
    warn("invalid device type %c for %s (skipping)", type, name);
    return;
  }
  if (ndevices>=maxdevices) crash("out of space for devices");
  devices[ndevices].name = name;
  devices[ndevices].grp = grp;
  devices[ndevices].class = class;
  devices[ndevices].major = major;
  devices[ndevices].minor = minor;
  devices[ndevices].type = type;
  devices[ndevices].omit = 0;
  ndevices++;
}

/*
 * Create an entry for a symbolic link "device", such as /dev/fd
 * (which is a symbolic link to /proc/self/fd)
 */
static void initlink(const char *name, const char *grp, const char *target) {
  init(name, grp, target, 0, 0, 'l');
}

/*
 * Init lots of devices. This creates a number of devices, numbered between
 * lo and hi. The idea is that "base" contains a %d or %x (or something like
 * that) in it which pulls in the number. The device group can also do this,
 * though this will in most cases not be useful. "baseminor" is the minor
 * number of the first device created.
 */
static void initlots(const char *base, int lo, int hi, const char *grp,
		     const char *class,
		     int maj, int baseminor, int type) {
  char buf[32], gbuf[32];
  int i;
  if (maj<0) return;
  for (i=lo; i<hi; i++) {
    sprintf(buf, base, i);
    if (grp) sprintf(gbuf, grp, i);  /* grp is permitted to contain a %d */
    init(strdup(buf), grp ? strdup(gbuf) : NULL, class, 
	 maj, baseminor+i-lo, type);
  }
}

/*
 * Init a whole (hard) disk's worth of devices - given `hd', it makes
 * hda1...hda8 through hdd1...hdd8 in one fell swoop. "low" and "high"
 * are the letters to use ('a' and 'd' for the previous example).
 * "nparts" is the number of partitions to create, ordinarily 8.
 * "maj" is the major device number; minmult is the multiplier for the
 * minor number. That is, if hda starts at 0, and hdb starts at 64, minmult
 * is 64.
 *
 * Note that it creates "hda", "hdb", etc. too, and puts things in the
 * groups "hda", "hdb", etc. as appropriate. The class is set to "disk".
 */
static void initdisk(const char *base, int low, int high, int nparts,
	      int maj, int minmult) {
  char buf[16], buf2[16];
  int i;
  batch *b;
  if (maj<0) return;
  if (low>=high) return;
  b = addbatch(base);
  for (i=low; i<=high; i++) {
    char *q;
    sprintf(buf, "%s%c", base, i);
    q = strdup(buf);
    init(q, q, "disk", maj, (i-low)*minmult,   'b');
    strcpy(buf2, buf);
    strcat(buf2, "%d");
    initlots(buf2, 1, nparts, buf, "disk", maj, (i-low)*minmult+1, 'b');
    add2batch(b, q);
  }
}

static void initdevs(void) {
  FILE *f = fopen("/etc/devinfo", "r");
  if (!f) crash("Can't find devinfo");
  doparse(f,3, "devinfo");
  fclose(f);
  f = fopen("/etc/devinfo.local", "r");
  if (!f) f = fopen("/usr/local/etc/devinfo.local", "r");
  if (f) {
    doparse(f,3, "devinfo.local");
    fclose(f);
  }
}

/************************** update *************************/

/*
 * Call make() with our names for something that appeared in /proc/devices.
 */

static void transmake(const char *procname, makeopts m) {
  const char *gname = groupnameof(procname);
  if (gname) make(gname, m);
}

/*
 * Update a device that appeared in MAKEDEV.cache. Whenever we update,
 * we save what we did into MAKEDEV.cache; this lets us avoid doing
 * them over the next time. We only do something if the device has
 * disappeared or the major number has changed.
 *
 * Note that this caching made the shell version go much faster (it took
 * around 15 seconds with the cache, vs. over a minute if the cache was
 * blown away.) For us, it still does, but it hardly matters: it shaves
 * one second off a two-second execution.
 *
 * Also note the old script used DEVICES instead of MAKEDEV.cache. We
 * changed because the old file didn't record whether something was
 * a block or character device; since the sets of numbers are independent,
 * this was bound to break.
 */
static void update2(const char *name, int ischar, int major) {
  int now = get_major(name, ischar, -1);
  if (now<0) {
    deletion = 1;   /* must have been zero if we're doing an update */
    transmake(name, M_CREATE);
    deletion = 0;
  }
  else if (now!=major) { /* oops, it moved; remake it */
    transmake(name, M_CREATE);
    if (ischar) cmajors[now].flag=1;
    else bmajors[now].flag=1;
  }
  else {
    if (ischar) cmajors[now].flag=1; /* unchanged; inhibit remaking it */
    else bmajors[now].flag=1; /* unchanged; inhibit remaking it */
  }
}

static void updatefromcache(const char *name, int major, int type) {
  update2(name, type=='c', major);
}


/*
 * Update. Read the information stored in MAKEDEV.cache from the last
 * update; fix anything that changed; then create any new devices that
 * weren't listed the last time. (We use the "flag" field in the
 * majors array to check this.) At that point, write out a new
 * cache file.
 */
#define CACHEFILE "MAKEDEV.cache"

static void update(void) {
  FILE *f;
  int i;
  if (no_proc) { warn("Couldn't read anything from /proc/devices"); return; }
  if (deletion) { warn("update and -d are incompatible"); return; }
  f = fopen(CACHEFILE, "r");
  if (f) {
    doparse(f, 2, CACHEFILE);
    fclose(f);
  }
  for (i=0; i<MAXMAJORS; i++) {
    if (cmajors[i].procname && !cmajors[i].flag) {
      transmake(cmajors[i].procname, M_CREATE);
      cmajors[i].flag=1;
    }
    if (bmajors[i].procname && !bmajors[i].flag) {
      transmake(bmajors[i].procname, M_CREATE);
      bmajors[i].flag=1;
    }
  }
  if (donothing) return;
  f = fopen(CACHEFILE, "w");
  if (f) {
    for (i=0; i<MAXMAJORS; i++)  {
      if (cmajors[i].procname) fprintf(f, "%s %d char\n", cmajors[i].procname, i);
      if (bmajors[i].procname) fprintf(f, "%s %d block\n", bmajors[i].procname, i);
    }
    fclose(f);
  }
  else warn("warning: can't write MAKEDEV.cache");
}

/************************* work *************************/

/*
 * Create (or delete, etc. according to flags) a device or device group.
 * The "generic" group is handled specially by recursing once.
 * "update" is handled specially; see update() below.
 * "local" issues a warning; people should use DEVINFO.local instead.
 */
static void make(const char *what, makeopts m) {
  int i;
  if (!strcmp(what, "update")) {
    if (m!=M_CREATE) warn("update not compatible with those options");
    else update();
  }
  else if (!strcmp(what, "local")) {
    warn("The local target is obsolete.");
  }
  else if (!try_run_batch(what, m)) {
    int found=0;
    for (i=0; i<ndevices; i++) {
      if ((devices[i].grp && !strcmp(what, devices[i].grp)) ||
          !strcmp(what, devices[i].name)) {
        makedev(&devices[i], m);
        found = 1;
      }
    }
    if (!found) warn("unknown device or device group %s", what);
  }
}

/*
 * A major improvement over the shell version...
 */
static void usage(void) {
  printf("MAKEDEV-C usage:\n");
  printf("    MAKEDEV-C [-vdcn] device [device...]\n");
  printf("      -v                 Verbose output\n");
  printf("      -d                 Remove specified devices\n");
  printf("      -c                 Create devices (default)\n");
  printf("      -n                 Don't actually do anything (implies -v)\n");
  printf("      -V                 Print version information\n");
  printf("\n");
}

/*
 * We should use getopt one of these days.
 */
int main(int argc, char **argv) {
  int i,j, done=0;
  for (i=1; i<argc && argv[i][0]=='-' && !done; i++)
    for (j=1; argv[i][j] && !done; j++) switch(argv[i][j]) {
        case '-': done=1; break;
	case 'v': isverbose = 1; break;
	case 'd': deletion = 1; break;
	case 'c': deletion = 0; break;
	case 'n': donothing = 1; isverbose = 1; break;
	case 'h': usage(); exit(0);
	case 'V': printf("MAKEDEV-C: %s\n", version); exit(0);
	default: fprintf(stderr, "MAKEDEV-C: unknown flag %c\n", argv[i][j]);
	  exit(1);
    }
  setup_majors();      /* read major device numbers from /proc */
  allocate_devs(1500); /* make space to hold devices */
  initdevs();          /* set up device structures */
  loadclasses();       /* load device classes from config file */
  check_classes();     /* make sure no devices have bogus classes */
  if (i==argc) warn("didn't do anything; try -h for help.");
  else for (; i<argc; i++) make(argv[i], M_CREATE);
  return 0;
}



/*

 AnaGram Parsing Engine
 Copyright (c) 1993, Parsifal Software.
 All Rights Reserved.
 This module may be copied and/or distributed at the discretion of the
 AnaGram licensee.

*/



#ifndef MAKEDEV_H
#include "makedev.h"
#endif

#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define RULE_CONTEXT (&((PCB).cs[(PCB).ssx]))
#define ERROR_CONTEXT ((PCB).cs[(PCB).error_frame_ssx])
#define CONTEXT ((PCB).cs[(PCB).ssx])



parse_pcb_type parse_pcb;
#define PCB parse_pcb

#line 698 "/usr/local/src/makedev/makedev.syn"
/************************* parsing support *************************/

/*
 * Don't use the built-in error printing.
 */
#define SYNTAX_ERROR
#define PARSER_STACK_OVERFLOW
#define REDUCTION_TOKEN_ERROR

static void doparse(FILE *f, int filetype, const char *filename) {
  char *x;
  int i=0, len;
  if (filetype<1 || filetype>4) crash("tried to parse a bad file type");
  if (filetype!=1) { /* /proc/devices won't stat intelligently */
    struct stat buf;
    if (fstat(fileno(f), &buf)) crash("fstat failed?!?");
    len = buf.st_size;
  }
  else len=1023;
  x = malloc(len+1);
  if (!x) crash("Out of memory");

  len = fread(x, 1, len, f);  /* it shouldn't return a short count... */
  if (len<0) crash("fread failed?!?");
  x[len]=0;

  init_parse();
  PCB.input_code = filetype+'0';
  parse();
  PCB.column--; /* correct for the filetype token */
  while (!PCB.exit_flag) {
    PCB.input_code = x[i++];
    parse();
  }
  if (PCB.exit_flag == AG_SYNTAX_ERROR_CODE) {
    warn("syntax error: %s, line %d, column %d in file %s",
         PCB.error_message, PCB.line, PCB.column, filename);
    crash("Sorry, can't continue.");
  }
  else if (PCB.exit_flag != AG_SUCCESS_CODE) {
    crash("parser stack overflow!");
  }
}

#define STRINGSIZE 8192
static char string_space[STRINGSIZE];
static int stringptr=0;

static const char *string_start(int c) {
  if (stringptr>=STRINGSIZE) crash("out of string space");
  return string_space[stringptr]=c, string_space+stringptr++;
}

static void string_push(int c) {
  if (stringptr>=STRINGSIZE) crash("out of string space");
  string_space[stringptr++] = c;
}

static void string_finish(void) {
  string_push(0);
}


#line 790 "makedev.c"
#line 840 "/usr/local/src/makedev/makedev.syn"
  static const char *cur_group=NULL, *cur_class=NULL;
  static int cur_type;
  static int cur_maj=0, cur_min=0, cur_bot=0, cur_top=0, ishex=0;

  static void dhsproc(const char *g, const char *p, int t, int m) {
    cur_group = g;
    cur_type = t;
    cur_maj = get_major(p, (t=='c'), m);
    cur_min = 0;
    cur_bot = cur_top = ishex = 0;
    if (p) addalias(p,g);
  }

  static void newdev(const char *n) {
    if (cur_maj<0) return;
    init(n, cur_group, cur_class, cur_maj, cur_min, cur_type);
  }
  static void devrange(const char *n, const char *n1) {
    char temp[32];
    if (cur_maj<0) return;
    sprintf(temp, "%s%%d%s", n, n1 ? n1 : "");
    initlots(temp, cur_bot, cur_top, cur_group, cur_class,
	     cur_maj, cur_min, cur_type);
  }
  static void doinitlink(const char *src, const char *tg) {
    if (cur_maj>=0) initlink(src, cur_group, tg);
  }

#line 820 "makedev.c"
#ifndef CONVERT_CASE
#define CONVERT_CASE(c) (c)
#endif
#ifndef TAB_SPACING
#define TAB_SPACING 8
#endif

#define ag_rp_1(n, s) (set_major(s,YES,n))

#define ag_rp_2(n, s) (set_major(s,NO,n))

#define ag_rp_3(n, maj, t) (updatefromcache(n,maj,t))

#define ag_rp_4() ('b')

#define ag_rp_5() ('c')

#define ag_rp_8(n, i) (add2batch(addbatch(n), i))

#define ag_rp_9(b, i) (add2batch(b,i))

#define ag_rp_10(n) (n)

#define ag_rp_11(n) (ignore_procname(n))

#define ag_rp_12(t, g, p) (dhsproc(g,p,t,-1))

#define ag_rp_13(t, g, p, m) (dhsproc(g,p,t,m))

#define ag_rp_14(t, g, m) (dhsproc(g,NULL,t,m))

#define ag_rp_15(classname) (classname)

#define ag_rp_16(c, min) ((cur_class=c, cur_min=min))

#define ag_rp_17(a, b) (cur_bot=a, cur_top=b, ishex=0)

#define ag_rp_18(a, b) (cur_bot=a, cur_top=b, ishex=1)

#define ag_rp_19(n) (newdev(n))

#define ag_rp_20(n, n1) (devrange(n,n1))

#define ag_rp_21(n) (devrange(n,NULL))

#define ag_rp_22(n, a, b, p, m) (initdisk(n, a, b, p, cur_maj, m))

#define ag_rp_23(n, tg) (doinitlink(n, tg))

#define ag_rp_24(n) (n)

#define ag_rp_25(n) (n)

#define ag_rp_26(n) (n)

#define ag_rp_27(n, o, g, m) (addclass(n,o,g,m))

#define ag_rp_28(n) (make(n, M_OMIT))

#define ag_rp_29(n) (make(n, M_OMIT))

#define ag_rp_30(n) (n)

#define ag_rp_31(s) (string_finish(), s)

#define ag_rp_32(s) (s)

#define ag_rp_33(c) (string_start(c))

#define ag_rp_34(s, c) (string_push(c), s)

#define ag_rp_35(s) (string_finish(), s)

#define ag_rp_36(c) (string_start(c))

#define ag_rp_37(s, c) (string_push(c), s)

#define ag_rp_38(c) (c)

#define ag_rp_39() ('\\')

#define ag_rp_40() ('"')

#define ag_rp_41(d) (d-'0')

#define ag_rp_42(n, d) (n*10 + d-'0')

#define ag_rp_43(d) (d)

#define ag_rp_44(n, d) (16*n+d)

#define ag_rp_45(d) (d)

#define ag_rp_46(n, d) (16*n+d)

#define ag_rp_47(d) (d-'0')

#define ag_rp_48(d) (10 + (d&7))

#define ag_rp_49(d) (d-'0')

#define ag_rp_50(n, d) (n*8+d-'0')

#define ag_rp_51(x, t) (x+t)

#define ag_rp_52(x, t) (x-t)

#define ag_rp_53(t, f) (t*f)

#define ag_rp_54(f) (-f)

#define ag_rp_55(x) (x)


#define READ_COUNTS 
#define WRITE_COUNTS 
static parse_vs_type ag_null_value;
#define V(i,t) (*(t *) (&(PCB).vs[(PCB).ssx + i]))
#define VS(i) (PCB).vs[(PCB).ssx + i]

#ifndef GET_CONTEXT
#define GET_CONTEXT CONTEXT = (PCB).input_context
#endif

typedef enum {
  ag_action_1,
  ag_action_2,
  ag_action_3,
  ag_action_4,
  ag_action_5,
  ag_action_6,
  ag_action_7,
  ag_action_8,
  ag_action_9,
  ag_action_10,
  ag_action_11,
  ag_action_12
} ag_parser_action;

static int ag_ap;



static const unsigned char ag_rpx[] = {
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  1,  2,
    0,  0,  0,  3,  4,  5,  4,  5,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  6,  0,  0,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16, 17, 18,
   19, 20, 21, 22, 23, 24,  0,  0,  0,  0,  0, 25, 26,  0,  0,  0, 27, 28,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
   29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,  0,  0, 41, 42, 43, 44,
   45, 46, 47, 48,  0, 49, 50,  0, 51,  0,  0, 52, 53
};

static unsigned char ag_key_itt[] = {
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1,
 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0,
 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 0
};

static unsigned short ag_key_pt[] = {
  1,121,  1,122,  1,125,  1,126,  1,138,  1,139,0
};

static unsigned char ag_key_ch[] = {
    0, 47,255, 42,255, 42, 47,255, 88,120,255, 97,108,255,104,108,255, 45,
   47, 48, 66, 67, 98, 99,105,111,255, 42, 47,255, 47, 99,111,255, 42, 47,
  255, 97,108,255, 47, 98, 99,105,255, 42, 47,255, 47,255, 42, 47,255, 47,
   66, 67,255, 47, 99,111,255, 97,108,255, 47, 98, 99,105,255, 47,255, 47,
   66, 67,255, 42, 47,255, 97,108,255,104,108,255, 47, 66, 67, 98, 99,105,
  111,255, 97,108,255,104,108,255, 47, 66, 67, 98, 99,105,111,255, 99,111,
  255, 97,108,255, 98, 99,105,255, 66, 67,255, 42, 47,255, 45, 47,255, 88,
  120,255, 47, 48,255, 42, 47,255, 47, 98, 99,255, 98, 99,255, 42, 47,255,
   97,108,255,104,108,255, 47, 98, 99,105,111,255, 45,255, 88,120,255, 48,
  255
};

static unsigned char ag_key_act[] = {
  0,3,4,3,4,0,0,4,0,0,4,7,7,4,7,7,4,3,2,2,3,3,2,2,7,7,4,0,0,4,2,7,7,4,0,
  0,4,7,7,4,2,2,7,7,4,0,0,4,2,4,0,0,4,2,3,3,4,3,7,7,4,7,7,4,3,2,7,7,4,3,
  4,3,3,3,4,0,0,4,7,7,4,7,7,4,2,3,3,2,2,7,7,4,7,7,4,7,7,4,3,3,3,2,2,7,7,
  4,7,7,4,7,7,4,2,7,7,4,3,3,4,0,0,4,3,2,4,0,0,4,3,2,4,0,0,4,2,7,7,4,7,7,
  4,0,0,4,7,7,4,7,7,4,2,2,2,7,7,4,3,4,0,0,4,2,4
};

static unsigned char ag_key_parm[] = {
    0, 80,  0, 84,  0, 80, 86,  0,145,144,  0,  6,  0,  0,  2,  8,  0,137,
    0,  0,118,117,  0,  0,  4, 10,  0, 80, 86,  0,  0,  8, 10,  0, 80, 86,
    0,  6,  0,  0,  0,  0,  2,  4,  0, 80, 86,  0,  0,  0, 80, 86,  0,  0,
  118,117,  0, 86,  8, 10,  0,  6,  0,  0, 86,  0,  2,  4,  0, 86,  0, 86,
  118,117,  0, 80, 86,  0,  6,  0,  0,  2,  8,  0,  0,118,117,  0,  0,  4,
   10,  0,  6,  0,  0,  2,  8,  0, 86,118,117,  0,  0,  4, 10,  0,  8, 10,
    0,  6,  0,  0,  0,  2,  4,  0,118,117,  0, 80, 86,  0,137,  0,  0,145,
  144,  0, 80,  0,  0, 80, 86,  0,  0,  0,  2,  0,  0,  2,  0, 80, 86,  0,
    6,  0,  0,  2,  8,  0,  0,  0,  0,  4, 10,  0,137,  0,145,144,  0,  0,
    0
};

static unsigned short ag_key_jmp[] = {
    0,  0,  0,  2,  0,  0,  0,  0,  0,  0,  0, 38, 42,  0, 46, 49,  0,  4,
    5,  8,  6, 20, 11, 14, 53, 59,  0,  0,  0,  0, 27, 63, 68,  0,  0,  0,
    0, 72, 76,  0, 34, 37, 80, 84,  0,  0,  0,  0, 45,  0,  0,  0,  0, 50,
   90,104,  0,122,124,129,  0,135,139,  0,133, 61,143,147,  0,153,  0,155,
  157,171,  0,  0,  0,  0,221,225,  0,229,232,  0, 75,189,203, 78, 81,236,
  242,  0,280,284,  0,288,291,  0,246,248,262, 92, 95,295,301,  0,305,310,
    0,314,318,  0,109,322,326,  0,332,346,  0,  0,  0,  0,364,119,  0,  0,
    0,  0,366,125,  0,  0,  0,  0,131,368,373,  0,377,382,  0,  0,  0,  0,
  386,390,  0,394,397,  0,141,144,147,401,407,  0,411,  0,  0,  0,  0,158,
    0
};

static unsigned short ag_key_index[] = {
    1,  3, 17,  0,  3,  3, 30, 40, 48, 53, 57, 64, 69, 71,  0,  0, 84, 98,
  106,112,  0,116,  0,  1,  1,  0,  0,106, 48, 48, 48, 48,  1,  1,  0,  0,
    0, 69,112,  0,122, 48,  0,  0, 48, 48, 69, 69,116, 48, 69, 69,  0,128,
    0,  0,  0, 69,  0, 69,  0,  0,134,138,  0,  0,  0,128,  0,  0, 69, 48,
   69,  0,150, 64,  0,156,  0, 69,  0,116,  0,116, 69,  0,  0,  0,  1,  0,
    0, 69, 69,  0,  1,  0,128,161,  0,  0,  0,  0,  0, 69, 69,  0, 57,  0,
    0,  0, 69,  0, 64, 69,  1,  0,  1,  1,  0,  0,  0,  0,  0,161, 64, 48,
   69, 69, 48,  0,128,  0, 48,  0,  0,161,161, 69, 69, 69, 69,  0,  0,  0,
    0,  0,128,161,161,128,161,  1,  0, 69, 69,  0,  1,  0, 69,  0
};

static unsigned char ag_key_ends[] = {
42,0, 47,0, 62,0, 108,111,99,107,32,100,101,118,105,99,101,115,58,0, 
104,97,114,97,99,116,101,114,32,100,101,118,105,99,101,115,58,0, 
116,99,104,0, 111,99,107,0, 97,114,0, 97,115,115,0, 
103,110,111,114,101,0, 109,105,116,0, 108,97,115,115,0, 
109,105,116,0, 116,99,104,0, 111,99,107,0, 104,97,114,0, 
103,110,111,114,101,0, 108,111,99,107,32,100,101,118,105,99,101,115,58,0, 
104,97,114,97,99,116,101,114,32,100,101,118,105,99,101,115,58,0, 
47,0, 108,97,115,115,0, 109,105,116,0, 47,0, 116,99,104,0, 
111,99,107,0, 104,97,114,0, 103,110,111,114,101,0, 47,0, 47,0, 
108,111,99,107,32,100,101,118,105,99,101,115,58,0, 
104,97,114,97,99,116,101,114,32,100,101,118,105,99,101,115,58,0, 
108,111,99,107,32,100,101,118,105,99,101,115,58,0, 
104,97,114,97,99,116,101,114,32,100,101,118,105,99,101,115,58,0, 
116,99,104,0, 111,99,107,0, 97,114,0, 97,115,115,0, 
103,110,111,114,101,0, 109,105,116,0, 47,0, 
108,111,99,107,32,100,101,118,105,99,101,115,58,0, 
104,97,114,97,99,116,101,114,32,100,101,118,105,99,101,115,58,0, 
116,99,104,0, 111,99,107,0, 97,114,0, 97,115,115,0, 
103,110,111,114,101,0, 109,105,116,0, 108,97,115,115,0, 
109,105,116,0, 116,99,104,0, 111,99,107,0, 104,97,114,0, 
103,110,111,114,101,0, 108,111,99,107,32,100,101,118,105,99,101,115,58,0, 
104,97,114,97,99,116,101,114,32,100,101,118,105,99,101,115,58,0, 
62,0, 42,0, 108,111,99,107,0, 104,97,114,0, 108,111,99,107,0, 
104,97,114,0, 116,99,104,0, 111,99,107,0, 97,114,0, 97,115,115,0, 
103,110,111,114,101,0, 109,105,116,0, 62,0, 
};
#define AG_TCV(x) (((int)(x) >= -1 && (int)(x) <= 255) ? ag_tcv[(x) + 1] : 0)

static const unsigned char ag_tcv[] = {
   18, 18,152,152,152,152,152,152,152,152,150, 93,152,152,150,152,152,152,
  152,152,152,152,152,152,152,152,152,152,152,152,152,152,152,151,153, 95,
   87,153,153,153,153,130,128,149,148,127,133,153,135,154,113,114,115,116,
  154,154,154,155,155,131,153,153,129,153,153,153,156,156,156,156,156,156,
  157,157,157,157,157,157,157,157,157,157,157,157,157,157,157,157,157,157,
  157,157,134, 99,132,153,157,153,156,119,120,156,156,156,157,157,157,157,
  157,157,157,157,157,157,157,157,157,157,157,157,157,157,157,157,124,153,
  123,153,152,152,152,152,152,152,152,152,152,152,152,152,152,152,152,152,
  152,152,152,152,152,152,152,152,152,152,152,152,152,152,152,152,152,152,
  152,152,152,152,152,152,152,152,152,152,152,152,152,152,152,152,152,152,
  152,152,152,152,152,152,152,152,152,152,152,152,152,152,152,152,152,152,
  152,152,152,152,152,152,152,152,152,152,152,152,152,152,152,152,152,152,
  152,152,152,152,152,152,152,152,152,152,152,152,152,152,152,152,152,152,
  152,152,152,152,152,152,152,152,152,152,152,152,152,152,152,152,152,152,
  152,152,152,152,152
};

#ifndef SYNTAX_ERROR
#define SYNTAX_ERROR fprintf(stderr,"%s, line %d, column %d\n", \
  (PCB).error_message, (PCB).line, (PCB).column)
#endif

#ifndef FIRST_LINE
#define FIRST_LINE 1
#endif

#ifndef FIRST_COLUMN
#define FIRST_COLUMN 1
#endif


#ifndef PARSER_STACK_OVERFLOW
#define PARSER_STACK_OVERFLOW {fprintf(stderr, \
   "\nParser stack overflow, line %d, column %d\n",\
   (PCB).line, (PCB).column);}
#endif

#ifndef REDUCTION_TOKEN_ERROR
#define REDUCTION_TOKEN_ERROR {fprintf(stderr, \
    "\nReduction token error, line %d, column %d\n", \
    (PCB).line, (PCB).column);}
#endif


typedef enum
  {ag_accept_key, ag_set_key, ag_jmp_key, ag_end_key, ag_no_match_key,
   ag_cf_accept_key, ag_cf_set_key, ag_cf_end_key} key_words;


static void ag_track(void) {
  int ag_k = 0;
  while (ag_k < (PCB).rx) {
    int ag_ch = (PCB).lab[ag_k++];
    switch (ag_ch) {
    case '\n':
      (PCB).column = 1, (PCB).line++;
    case '\r':
    case '\f':
      break;
    case '\t':
      (PCB).column += (TAB_SPACING) - ((PCB).column - 1) % (TAB_SPACING);
      break;
    default:
      (PCB).column++;
    }
  }
  ag_k = 0;
  while ((PCB).rx < (PCB).fx) (PCB).lab[ag_k++] = (PCB).lab[(PCB).rx++];
  (PCB).fx = ag_k;
  (PCB).rx = 0;
}


static void ag_prot(void) {
  int ag_k = 38 - ++(PCB).btsx;
  if (ag_k <= (PCB).ssx) {
    (PCB).exit_flag = AG_STACK_ERROR_CODE;
    PARSER_STACK_OVERFLOW;
    return;
  }
  (PCB).bts[(PCB).btsx] = (PCB).sn;
  (PCB).bts[ag_k] = (PCB).ssx;
  (PCB).vs[ag_k] = (PCB).vs[(PCB).ssx];
  (PCB).ss[ag_k] = (PCB).ss[(PCB).ssx];
}

static void ag_undo(void) {
  if ((PCB).drt == -1) return;
  while ((PCB).btsx) {
    int ag_k = 38 - (PCB).btsx;
    (PCB).sn = (PCB).bts[(PCB).btsx--];
    (PCB).ssx = (PCB).bts[ag_k];
    (PCB).vs[(PCB).ssx] = (PCB).vs[ag_k];
    (PCB).ss[(PCB).ssx] = (PCB).ss[ag_k];
  }
  (PCB).token_number = (parse_token_type) (PCB).drt;
  (PCB).ssx = (PCB).dssx;
  (PCB).sn = (PCB).dsn;
  (PCB).drt = -1;
}


static const unsigned char ag_tstt[] = {
151,150,80,0,2,111,112,
157,156,155,154,153,152,151,150,149,148,135,134,133,132,131,130,129,128,127,
  124,123,120,119,116,115,114,113,99,95,93,87,0,82,83,
151,150,80,0,2,
116,115,114,113,0,5,6,8,10,12,
157,156,155,154,153,152,151,150,149,148,135,134,133,132,131,130,129,128,127,
  124,123,120,119,116,115,114,113,99,95,93,87,0,
84,0,
151,150,80,0,2,111,112,
151,150,80,0,2,111,112,
151,150,80,0,2,111,112,
151,150,80,0,2,111,112,
139,138,93,87,86,0,3,13,14,15,85,88,92,140,
126,125,122,121,120,119,93,87,86,0,3,11,14,15,85,88,92,140,
157,156,133,120,119,95,93,87,86,0,3,9,14,15,85,88,92,140,
118,117,93,87,86,0,3,7,14,15,85,88,92,140,
157,156,155,154,153,152,151,150,149,148,135,134,133,132,131,130,129,128,127,
  124,123,120,119,116,115,114,113,99,95,87,18,0,90,91,
93,0,
151,150,80,0,2,111,112,
157,156,155,154,139,138,133,127,126,125,123,122,121,120,119,118,117,116,115,
  114,113,95,93,87,86,18,0,3,88,92,140,
139,138,0,69,70,71,72,73,75,
126,125,122,121,120,119,0,29,30,31,32,33,34,35,36,42,45,
157,156,133,120,119,95,0,1,4,26,27,28,141,142,
118,117,0,16,17,19,22,
157,156,155,154,153,152,151,150,149,148,135,134,133,132,131,130,129,128,127,
  124,123,120,119,116,115,114,113,99,95,87,18,0,
151,150,80,0,2,111,112,
151,150,80,0,2,111,112,
157,156,133,124,120,119,95,0,1,4,26,37,141,142,
157,156,133,120,119,95,0,1,4,26,141,142,
139,138,18,0,69,71,72,73,75,
151,150,80,0,2,111,112,
151,150,80,0,2,111,112,
151,150,80,0,2,111,112,
151,150,80,0,2,111,112,
151,150,80,0,2,111,112,
151,150,80,0,2,111,112,
130,0,50,
157,156,133,120,119,95,0,1,4,26,46,141,142,
124,0,37,
157,156,133,124,120,119,95,93,87,86,0,3,14,15,37,85,88,92,140,
126,125,122,121,120,119,18,0,29,30,31,32,33,34,36,42,45,
157,156,155,154,153,151,149,148,135,134,133,132,131,130,129,128,127,124,123,
  120,119,116,115,114,113,99,87,0,96,97,
151,150,80,0,2,111,112,
157,156,155,154,151,150,133,120,119,116,115,114,113,80,0,2,111,112,
155,154,116,115,114,113,0,25,100,
157,156,133,120,119,95,18,0,1,4,26,27,141,142,
151,150,80,0,2,111,112,
151,150,80,0,2,111,112,
87,86,0,3,14,85,88,92,140,
87,86,0,3,14,85,88,92,140,
118,117,18,0,16,19,22,
151,150,80,0,2,111,112,
157,156,133,120,119,95,93,87,86,0,3,14,15,85,88,92,140,
87,86,0,3,14,85,88,92,140,
131,0,57,
151,150,80,0,2,111,112,
157,156,133,120,119,95,0,1,4,26,51,141,142,
124,0,37,
127,123,0,41,48,49,
157,156,133,120,119,95,93,87,86,0,3,14,15,85,88,92,140,
157,156,133,120,119,95,0,1,4,26,38,65,141,142,
157,156,133,123,120,119,95,93,87,86,0,3,14,15,85,88,92,140,
99,95,0,
157,156,155,154,153,151,149,148,135,134,133,132,131,130,129,128,127,124,123,
  120,119,116,115,114,113,99,95,87,0,97,
151,150,80,0,2,111,112,
155,154,122,121,120,119,116,115,114,113,0,29,30,31,32,33,100,
155,154,116,115,114,113,0,23,24,25,100,
155,154,116,115,114,113,0,20,21,25,100,
157,156,133,120,119,95,0,1,4,26,76,77,141,142,
151,150,80,0,2,111,112,
157,156,133,120,119,95,0,1,4,26,141,142,
129,127,0,48,52,
157,156,133,120,119,95,93,87,86,0,3,14,15,85,88,92,140,
151,150,80,0,2,111,112,
157,156,133,123,120,119,95,93,87,86,0,3,14,15,85,88,92,140,
157,156,133,120,119,95,0,1,4,26,47,141,142,
151,150,80,0,2,111,112,
126,125,122,121,120,119,93,87,86,18,0,3,14,15,85,88,92,140,
157,156,133,120,119,95,0,1,4,26,43,44,141,142,
137,134,130,0,50,55,56,59,60,68,
157,156,133,120,119,95,0,1,4,26,38,39,40,65,141,142,
87,86,0,3,14,85,88,92,140,
157,156,155,154,133,120,119,116,115,114,113,95,0,1,4,26,100,141,142,
155,154,116,115,114,113,0,23,25,100,
157,156,155,154,133,120,119,116,115,114,113,95,0,1,4,26,100,141,142,
155,154,116,115,114,113,0,20,25,100,
157,156,133,127,123,120,119,95,93,87,86,0,3,14,15,85,88,92,140,
157,156,133,123,120,119,95,0,1,4,26,41,76,141,142,
157,156,133,120,119,95,0,1,4,26,141,142,
155,154,116,115,114,113,0,25,100,
151,150,80,0,2,111,112,
157,156,133,120,119,95,0,1,4,26,53,141,142,
157,156,133,120,119,95,0,1,4,26,47,141,142,
157,156,133,127,123,120,119,95,93,87,86,0,3,14,15,85,88,92,140,
157,156,133,127,123,120,119,95,93,87,86,0,3,14,15,85,88,92,140,
157,156,133,123,120,119,95,0,1,4,26,41,43,141,142,
151,150,80,0,2,111,112,
157,156,133,120,119,95,0,1,4,26,141,142,
151,150,80,0,2,111,112,
157,156,155,154,145,144,133,120,119,116,115,114,113,0,25,63,66,100,101,102,
  103,
157,156,133,130,120,119,95,0,1,4,26,50,55,56,65,141,142,
157,156,133,120,119,95,0,1,4,26,141,142,
131,0,57,
157,156,133,120,119,95,0,1,4,26,38,65,141,142,
123,0,41,
87,86,0,3,14,85,88,92,140,
87,86,0,3,14,85,88,92,140,
127,0,48,49,
139,138,93,87,86,18,0,3,14,15,85,88,92,140,
154,116,115,114,113,0,74,78,106,
155,154,128,116,115,114,113,0,54,100,
128,127,0,48,54,
157,156,133,127,123,120,119,95,93,87,86,0,3,14,15,85,88,92,140,
127,0,48,49,
126,125,122,121,120,119,93,87,86,18,0,3,14,15,85,88,92,140,
87,86,0,3,14,85,88,92,140,
151,150,80,0,2,111,112,
133,0,61,
151,150,80,0,2,111,112,
151,150,80,0,2,111,112,
156,155,154,120,119,116,115,114,113,0,100,104,105,
156,155,154,133,120,119,116,115,114,113,0,61,100,104,105,
155,154,133,116,115,114,113,0,61,100,
130,0,50,55,56,
128,0,54,
155,154,145,144,133,130,116,115,114,113,0,25,50,58,61,63,100,101,102,103,
  107,109,
126,125,122,121,120,119,93,87,86,18,0,3,14,15,85,88,92,140,
151,150,80,0,2,111,112,
154,116,115,114,113,0,106,
87,86,0,3,14,85,88,92,140,
151,150,80,0,2,111,112,
155,154,116,115,114,113,0,25,100,
151,150,80,0,2,111,112,
157,156,133,120,119,0,66,
151,150,80,0,2,111,112,
156,155,154,120,119,116,115,114,113,0,64,100,104,105,
155,154,116,115,114,113,0,25,100,
155,154,145,144,133,130,116,115,114,113,0,25,50,58,61,63,100,101,102,103,
  107,109,
155,154,145,144,133,130,116,115,114,113,0,25,50,61,63,100,101,102,103,109,
156,155,154,120,119,116,115,114,113,0,100,104,105,
155,154,116,115,114,113,0,100,
149,0,110,
148,133,87,86,0,3,14,61,85,88,92,108,140,
155,154,128,116,115,114,113,0,54,100,
132,0,62,
156,155,154,132,120,119,116,115,114,113,0,62,100,104,105,
155,154,132,116,115,114,113,0,62,100,
148,133,128,0,54,61,108,
151,150,80,0,2,111,112,
155,154,145,144,133,130,116,115,114,113,0,25,50,61,63,100,101,102,103,109,
155,154,145,144,133,130,116,115,114,113,0,25,50,61,63,100,101,102,103,107,
  109,
151,150,80,0,2,111,112,
155,154,145,144,133,130,116,115,114,113,0,25,50,61,63,100,101,102,103,107,
  109,
151,150,80,0,2,111,112,
155,154,116,115,114,113,0,25,100,
149,0,110,
149,0,110,
155,154,135,116,115,114,113,0,67,100,
151,150,80,0,2,111,112,
155,154,116,115,114,113,0,25,100,
155,154,116,115,114,113,87,86,0,3,14,85,88,92,100,140,
  0
};


static unsigned char ag_astt[1821] = {
  1,1,1,8,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
  1,8,1,1,9,9,1,5,3,1,1,1,1,7,0,1,1,1,1,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,
  9,9,9,9,9,9,9,9,9,9,9,9,9,5,3,7,1,1,1,5,1,1,3,1,1,1,5,1,1,3,1,1,1,5,1,1,3,
  1,1,1,5,1,1,3,8,8,8,1,1,7,1,3,1,1,1,1,1,1,8,8,8,8,8,8,8,1,1,7,1,3,1,1,1,1,
  1,1,8,8,8,8,8,8,8,1,1,7,1,3,1,1,1,1,1,1,8,8,8,1,1,7,1,3,1,1,1,1,1,1,1,1,1,
  1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,5,1,3,3,7,1,1,1,5,
  1,1,3,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,8,1,1,5,7,3,1,1,1,1,1,7,
  1,1,1,1,1,1,1,1,1,1,1,1,7,1,2,2,2,2,1,1,1,1,1,2,2,2,2,2,1,7,2,2,1,1,1,1,1,
  1,1,7,1,1,1,1,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,
  9,5,1,1,1,5,1,1,3,1,1,1,5,1,1,3,2,2,2,1,2,2,1,7,2,2,1,1,1,1,2,2,2,2,2,1,7,
  2,2,1,1,1,1,1,3,7,3,3,3,1,1,1,1,1,5,1,1,3,1,1,1,5,1,1,3,1,1,1,5,1,1,3,1,1,
  1,5,1,1,3,1,1,1,5,1,1,3,1,1,1,5,1,1,3,1,7,1,2,2,2,2,2,1,7,2,2,1,1,1,1,1,7,
  1,8,8,8,1,8,8,8,8,1,1,7,1,1,1,1,1,1,1,1,1,1,1,1,1,1,3,7,1,2,2,2,2,3,1,1,1,
  2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,1,2,7,1,2,1,1,1,5,1,1,3,
  10,10,10,10,1,1,10,10,10,10,10,10,10,1,5,1,1,3,1,1,1,1,1,1,7,1,2,2,2,2,2,2,
  1,3,7,2,2,1,3,1,1,1,1,1,5,1,1,3,1,1,1,5,1,1,3,1,1,8,1,1,1,1,1,1,1,1,8,1,1,
  1,1,1,1,1,1,3,7,3,1,1,1,1,1,5,1,1,3,8,8,8,8,8,8,8,1,1,7,1,1,1,1,1,1,1,1,1,
  8,1,2,1,1,1,1,1,7,1,1,1,1,5,1,1,3,2,2,2,2,2,1,7,2,2,2,1,1,1,1,7,1,1,1,8,1,
  1,1,8,8,8,8,8,8,8,1,1,7,1,1,1,1,1,1,1,2,2,2,2,2,1,7,2,2,2,3,1,1,1,8,8,8,8,
  8,8,8,8,1,1,7,1,1,1,1,1,1,1,2,2,7,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
  2,2,2,2,2,1,2,2,7,2,1,1,1,5,1,1,3,1,1,1,1,1,1,1,1,1,1,7,1,2,2,2,2,2,1,1,1,
  1,1,1,7,1,1,1,2,1,1,1,1,1,1,7,1,1,1,2,2,2,2,2,2,1,7,2,2,1,1,1,1,1,1,1,1,5,
  1,1,3,2,2,2,2,2,1,7,2,2,1,1,1,1,1,7,1,1,8,8,8,8,8,8,8,1,1,7,1,1,1,1,1,1,1,
  1,1,1,5,1,1,3,5,5,5,5,5,5,5,8,1,1,7,1,3,3,1,1,1,1,2,2,2,2,2,1,7,2,2,2,1,1,
  1,1,1,1,5,1,1,3,5,5,5,5,5,5,8,1,1,5,7,1,3,3,1,1,1,1,2,2,2,2,2,1,7,2,2,1,1,
  1,1,1,1,1,1,7,1,1,2,1,1,1,2,2,2,2,2,1,8,2,2,2,1,1,1,1,1,1,1,1,8,1,2,1,1,1,
  1,2,2,1,1,2,2,2,1,1,1,1,1,7,2,2,1,2,1,1,1,1,1,1,1,1,5,3,1,2,2,2,1,1,2,2,2,
  1,1,1,1,1,7,2,2,1,2,1,1,1,1,1,1,1,1,5,3,1,2,8,8,8,8,8,8,8,8,8,1,1,7,1,1,1,
  1,1,1,1,2,2,2,1,2,2,1,7,2,2,1,1,3,1,1,2,2,2,2,2,1,7,2,2,1,1,1,1,1,1,1,1,1,
  7,1,2,1,1,1,5,1,1,3,2,2,2,2,2,1,7,2,2,2,1,1,1,2,2,2,2,2,1,7,2,2,2,1,1,1,5,
  5,5,5,5,5,5,5,8,1,1,7,1,2,2,1,1,1,1,8,8,8,8,8,8,8,8,8,1,1,7,1,1,1,1,1,1,1,
  2,2,2,1,2,2,1,7,2,2,1,1,3,1,1,1,1,1,5,1,1,3,2,2,2,2,2,1,7,2,2,1,1,1,1,1,1,
  5,1,1,3,1,1,1,1,1,1,1,1,1,1,1,1,1,7,1,1,1,2,1,1,1,2,2,2,1,2,2,1,7,2,2,2,1,
  1,2,1,1,1,2,2,2,2,2,1,7,2,2,1,1,1,1,7,1,2,2,2,2,2,1,5,2,2,2,3,1,1,1,1,7,1,
  1,1,8,1,2,1,1,1,1,1,1,8,1,2,1,1,1,1,1,5,1,2,5,5,8,1,1,5,7,1,3,3,1,1,1,1,1,
  1,1,1,1,7,1,1,2,1,1,1,1,1,1,1,7,2,2,1,1,7,1,2,5,5,5,5,5,5,5,5,8,1,1,7,1,2,
  2,1,1,1,1,1,5,1,2,5,5,5,5,5,5,8,1,1,5,7,1,3,3,1,1,1,1,1,1,8,1,2,1,1,1,1,1,
  1,1,5,1,1,3,1,7,1,1,1,1,5,1,1,3,1,1,1,5,1,1,3,1,1,1,1,1,1,1,1,1,7,2,2,2,1,
  1,1,1,1,1,1,1,1,1,7,1,2,2,2,1,1,1,1,1,1,1,7,1,2,1,7,1,1,2,1,7,2,1,1,1,1,1,
  1,1,1,1,1,7,1,1,1,1,1,2,1,1,1,1,1,5,5,5,5,5,5,8,1,1,5,7,1,3,3,1,1,1,1,1,1,
  1,5,1,1,3,1,1,1,1,1,4,2,1,1,8,1,2,1,1,1,1,1,1,1,5,1,1,3,1,1,1,1,1,1,7,1,2,
  1,1,1,5,1,1,3,1,1,1,1,1,7,1,1,1,1,5,1,1,3,1,1,1,1,1,1,1,1,1,7,1,2,2,2,1,1,
  1,1,1,1,7,1,2,1,1,1,1,1,1,1,1,1,1,7,1,1,1,1,1,2,1,1,1,1,1,1,1,1,1,1,1,1,1,
  1,1,7,1,1,1,1,2,1,1,1,2,1,1,1,1,1,1,1,1,1,5,2,2,2,1,1,1,1,1,1,5,2,1,5,1,1,
  1,1,1,8,1,2,1,1,1,1,1,1,1,1,1,1,1,1,1,7,2,2,1,7,1,1,1,1,1,1,1,1,1,1,1,7,2,
  2,2,2,1,1,1,1,1,1,1,7,2,2,1,1,1,7,2,1,1,1,1,1,5,1,1,3,1,1,1,1,1,1,1,1,1,1,
  7,1,1,1,1,2,1,1,1,2,1,1,1,1,1,1,1,1,1,1,7,1,1,1,1,2,1,1,1,1,1,1,1,1,5,1,1,
  3,1,1,1,1,1,1,1,1,1,1,7,1,1,1,1,2,1,1,1,1,1,1,1,1,5,1,1,3,1,1,1,1,1,1,7,1,
  2,1,4,1,1,4,1,1,1,1,1,1,1,1,7,1,2,1,1,1,5,1,1,3,1,1,1,1,1,1,7,1,2,1,1,1,1,
  1,1,1,1,8,1,2,1,1,1,2,1,11
};


static unsigned char ag_pstt[] = {
2,2,1,3,2,2,3,
4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,5,4,5,
122,122,1,124,122,
6,7,8,9,3,0,13,12,11,10,
74,74,74,74,74,74,74,74,74,74,74,74,74,74,74,74,74,74,74,74,74,74,74,74,74,
  74,74,74,74,74,74,76,
77,5,
2,2,1,123,2,2,128,
2,2,1,123,2,2,127,
2,2,1,123,2,2,126,
2,2,1,123,2,2,125,
18,18,15,14,14,10,17,4,18,18,17,14,15,16,
19,19,19,19,19,19,15,14,14,11,17,3,19,19,17,14,15,16,
20,20,20,20,20,20,15,14,14,12,17,2,20,20,17,14,15,16,
21,21,15,14,14,13,17,1,21,21,17,14,15,16,
22,22,22,22,22,22,22,22,22,22,22,22,22,22,22,22,22,22,22,22,22,22,22,22,22,
  22,22,22,22,22,22,85,22,88,
89,15,
2,2,1,123,2,2,152,
80,80,80,80,80,80,80,80,80,80,80,80,80,80,80,80,80,80,80,80,80,80,15,14,14,
  80,17,79,14,15,16,
23,24,18,27,27,27,27,26,25,
32,33,28,29,30,31,19,34,22,23,24,25,38,38,37,36,35,
92,92,92,92,92,39,20,90,91,42,43,43,41,40,
44,45,21,48,48,47,46,
84,84,84,84,84,84,84,84,84,84,84,84,84,84,84,84,84,84,84,84,84,84,84,84,84,
  84,84,84,84,84,84,86,
2,2,1,123,2,2,151,
2,2,1,123,2,2,150,
92,92,92,49,92,92,39,25,90,91,51,50,41,40,
92,92,92,92,92,39,26,90,91,52,41,40,
23,24,62,27,61,61,61,26,25,
2,2,1,123,2,2,134,
2,2,1,123,2,2,133,
2,2,1,123,2,2,132,
2,2,1,123,2,2,131,
2,2,1,123,2,2,138,
2,2,1,123,2,2,137,
53,34,54,
92,92,92,92,92,39,35,90,91,55,56,41,40,
49,36,57,
58,58,58,49,58,58,58,15,14,14,37,17,58,58,59,17,14,15,16,
32,33,28,29,30,31,28,38,34,22,23,24,25,27,37,36,35,
97,97,97,97,97,97,97,97,97,97,97,97,97,97,97,97,97,97,97,97,97,97,97,97,97,
  60,97,39,61,95,
2,2,1,123,2,2,154,
93,93,93,93,2,2,93,93,93,93,93,93,93,1,123,2,2,153,
62,62,62,62,62,62,42,63,100,
92,92,92,92,92,39,20,43,90,91,42,19,41,40,
2,2,1,123,2,2,130,
2,2,1,123,2,2,129,
14,14,15,17,64,17,14,15,16,
14,14,15,17,65,17,14,15,16,
44,45,9,48,8,47,46,
2,2,1,123,2,2,136,
66,66,66,66,66,66,15,14,14,50,17,66,66,17,14,15,16,
14,14,15,17,66,17,14,15,16,
67,52,68,
2,2,1,123,2,2,142,
92,92,92,92,92,39,54,90,91,58,69,41,40,
49,55,70,
71,74,73,75,72,73,
76,76,76,76,76,76,15,14,14,57,17,76,76,17,14,15,16,
92,92,92,92,92,39,58,90,91,57,34,77,41,40,
78,78,78,78,78,78,78,15,14,14,59,17,78,78,17,14,15,16,
98,99,60,
97,97,97,97,97,97,97,97,97,97,97,97,97,97,97,97,97,97,97,97,97,97,97,97,97,
  60,94,97,61,96,
2,2,1,123,2,2,155,
62,62,28,29,30,31,62,62,62,62,63,79,22,23,24,25,101,
62,62,62,62,62,62,64,81,81,80,100,
62,62,62,62,62,62,65,83,83,82,100,
92,92,92,92,92,39,66,90,91,84,85,85,41,40,
2,2,1,123,2,2,143,
92,92,92,92,92,39,68,90,91,86,41,40,
88,71,69,87,89,
90,90,90,90,90,90,15,14,14,70,17,90,90,17,14,15,16,
2,2,1,123,2,2,139,
5,5,5,5,5,5,5,15,14,14,72,17,41,41,17,14,15,16,
92,92,92,92,92,39,73,90,91,43,91,41,40,
2,2,1,123,2,2,135,
5,5,5,5,5,5,15,14,14,5,75,17,38,38,17,14,15,16,
92,92,92,92,92,39,76,90,91,92,93,93,41,40,
94,96,53,77,99,100,52,98,97,95,
92,92,92,92,92,39,102,90,91,57,101,101,102,77,41,40,
14,14,15,17,21,17,14,15,16,
92,92,62,62,92,92,92,62,62,62,62,39,80,90,91,103,101,41,40,
62,62,62,62,62,62,15,14,80,100,
92,92,62,62,92,92,92,62,62,62,62,39,82,90,91,104,101,41,40,
62,62,62,62,62,62,12,11,82,100,
105,105,105,105,105,105,105,105,15,14,14,84,17,105,105,17,14,15,16,
92,92,92,74,92,92,39,85,90,91,84,106,68,41,40,
92,92,92,92,92,39,86,90,91,107,41,40,
62,62,62,62,62,62,87,108,100,
2,2,1,123,2,2,141,
92,92,92,92,92,39,89,90,91,59,109,41,40,
92,92,92,92,92,39,90,90,91,43,110,41,40,
5,5,5,5,5,5,5,5,15,14,14,91,17,42,42,17,14,15,16,
111,111,111,111,111,111,111,111,15,14,14,92,17,111,111,17,14,15,16,
92,92,92,74,92,92,39,93,90,91,92,112,36,41,40,
2,2,1,123,2,2,149,
92,92,92,92,92,39,95,90,91,113,41,40,
2,2,1,123,2,2,146,
114,114,62,62,116,117,114,114,114,62,62,62,62,97,120,119,115,100,118,118,
  118,
92,92,92,53,92,92,39,98,90,91,57,99,100,54,121,41,40,
92,92,92,92,92,39,99,90,91,122,41,40,
67,100,123,
92,92,92,92,92,39,32,90,91,57,30,77,41,40,
74,102,124,
14,14,15,17,17,17,14,15,16,
14,14,15,17,16,17,14,15,16,
71,40,72,70,
5,5,15,14,14,5,106,17,69,69,17,14,15,16,
125,125,125,125,125,107,127,126,110,
62,62,128,62,62,62,62,108,47,101,
128,71,109,129,45,
5,5,5,5,5,5,5,5,15,14,14,110,17,39,39,17,14,15,16,
71,40,72,44,
5,5,5,5,5,5,15,14,14,5,112,17,37,37,17,14,15,16,
14,14,15,17,56,17,14,15,16,
2,2,1,123,2,2,148,
130,115,131,
2,2,1,123,2,2,157,
2,2,1,123,2,2,156,
132,62,62,132,132,62,62,62,62,118,108,104,109,
132,62,62,130,132,132,62,62,62,62,119,133,108,105,109,
62,62,130,62,62,62,62,120,134,101,
53,121,99,100,53,
128,122,48,
62,62,116,117,130,53,62,62,62,62,123,138,135,140,136,137,100,118,118,118,
  139,139,
5,5,5,5,5,5,15,14,14,5,124,17,33,33,17,14,15,16,
2,2,1,123,2,2,159,
125,125,125,125,125,71,111,
14,14,15,17,65,17,14,15,16,
2,2,1,123,2,2,140,
62,62,62,62,62,62,129,141,100,
2,2,1,123,2,2,145,
114,114,114,114,114,131,142,
2,2,1,123,2,2,158,
132,62,62,132,132,62,62,62,62,133,143,108,106,109,
62,62,62,62,62,62,134,144,100,
62,62,116,117,130,53,62,62,62,62,135,138,135,145,136,137,100,118,118,118,
  139,139,
62,62,116,117,130,53,62,62,62,62,136,138,135,136,137,100,118,118,118,119,
132,62,62,132,132,62,62,62,62,118,108,105,109,
62,62,62,62,62,62,117,101,
146,112,147,
149,130,14,14,15,17,49,148,17,14,15,150,16,
62,62,128,62,62,62,62,141,46,101,
151,142,152,
132,62,62,151,132,132,62,62,62,62,143,51,108,107,109,
62,62,151,62,62,62,62,144,50,101,
149,130,128,145,120,148,150,
2,2,1,123,2,2,161,
62,62,116,117,130,53,62,62,62,62,147,138,135,136,137,100,118,118,118,116,
62,62,116,117,130,53,62,62,62,62,148,138,135,136,137,100,118,118,118,153,
  153,
2,2,1,123,2,2,160,
62,62,116,117,130,53,62,62,62,62,150,138,135,136,137,100,118,118,118,154,
  154,
2,2,1,123,2,2,144,
62,62,62,62,62,62,152,155,100,
146,114,147,
146,113,147,
62,62,156,62,62,62,62,155,157,101,
2,2,1,123,2,2,147,
62,62,62,62,62,62,157,158,100,
62,62,62,62,62,62,14,14,15,17,55,17,14,15,101,16,
  0
};


static const unsigned short ag_sbt[] = {
     0,   7,  41,  46,  56,  88,  90,  97, 104, 111, 118, 132, 150, 168,
   182, 216, 218, 225, 256, 265, 282, 296, 303, 335, 342, 349, 363, 375,
   384, 391, 398, 405, 412, 419, 426, 429, 442, 445, 464, 481, 511, 518,
   536, 545, 559, 566, 573, 582, 591, 598, 605, 622, 631, 634, 641, 654,
   657, 663, 680, 694, 712, 715, 745, 752, 769, 780, 791, 805, 812, 824,
   829, 846, 853, 871, 884, 891, 909, 923, 933, 949, 958, 977, 987,1006,
  1016,1035,1050,1062,1071,1078,1091,1104,1123,1142,1157,1164,1176,1183,
  1204,1221,1233,1236,1250,1253,1262,1271,1275,1289,1298,1308,1313,1332,
  1336,1354,1363,1370,1373,1380,1387,1400,1415,1425,1430,1433,1455,1473,
  1480,1487,1496,1503,1512,1519,1526,1533,1547,1556,1578,1598,1611,1619,
  1622,1635,1645,1648,1663,1673,1680,1687,1707,1728,1735,1756,1763,1772,
  1775,1778,1788,1795,1804,1820
};


static const unsigned short ag_sbe[] = {
     3,  38,  44,  50,  87,  89,  93, 100, 107, 114, 123, 141, 159, 173,
   213, 217, 221, 251, 258, 271, 288, 298, 334, 338, 345, 356, 369, 378,
   387, 394, 401, 408, 415, 422, 427, 435, 443, 455, 471, 508, 514, 532,
   542, 552, 562, 569, 575, 584, 594, 601, 614, 624, 632, 637, 647, 655,
   659, 672, 686, 704, 714, 743, 748, 762, 775, 786, 797, 808, 818, 826,
   838, 849, 863, 877, 887, 901, 915, 926, 939, 951, 970, 983, 999,1012,
  1027,1042,1056,1068,1074,1084,1097,1115,1134,1149,1160,1170,1179,1196,
  1211,1227,1234,1242,1251,1255,1264,1272,1281,1294,1305,1310,1324,1333,
  1346,1356,1366,1371,1376,1383,1396,1410,1422,1426,1431,1443,1465,1476,
  1485,1489,1499,1509,1515,1524,1529,1542,1553,1566,1588,1607,1617,1620,
  1626,1642,1646,1658,1670,1676,1683,1697,1717,1731,1745,1759,1769,1773,
  1776,1785,1791,1801,1812,1820
};


static const unsigned char ag_fl[] = {
  2,2,2,2,2,0,1,1,2,3,1,2,3,1,2,3,3,3,1,2,3,4,1,1,1,1,1,2,3,1,2,0,1,6,3,
  1,2,6,4,5,0,2,4,1,3,6,8,6,3,4,5,5,2,4,3,10,4,1,1,1,1,2,3,1,1,7,3,1,2,6,
  3,1,1,1,2,0,1,3,1,2,1,1,1,1,2,0,1,0,2,2,1,1,1,2,3,1,2,1,2,2,1,2,1,1,2,
  2,1,2,1,1,1,2,1,3,3,1,3,1,1,2,3,1,2,0,1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
  2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2
};

static const unsigned char ag_ptt[] = {
    0,  5,  5,  5,  5, 15, 15, 17, 17,  7, 21, 21, 16, 24, 24, 16, 20, 23,
   28, 28,  9, 27, 29, 29, 29, 29, 35, 35, 11, 39, 39, 40, 40, 34, 34, 44,
   44, 34, 34, 46, 49, 49, 46, 47, 43, 36, 36, 36, 55, 56, 59, 59, 38, 38,
   38, 38, 38, 65, 51, 53, 70, 70, 13, 69, 69, 71, 72, 77, 77, 72, 76, 74,
    2, 82, 82, 83, 83,  2, 85, 85, 14, 88, 88, 90, 90, 91, 91, 92, 92,140,
   26, 26,141,141,142, 96, 96, 97, 97, 97, 25, 25,103,103, 63, 63, 64, 64,
  104,104, 78, 78, 58, 58, 58,107,107,109,109,109,109,111,111,112,112,  6,
    8, 10, 12, 19, 22, 30, 31, 32, 33, 41, 37, 42, 45, 48, 54, 52, 50, 57,
   62, 61, 60, 67, 66, 68, 73, 75,  3,  1,  4,100,101,102,105,106,108,110
};




static void ag_ra(void)
{
  switch(ag_rpx[ag_ap]) {
  case   1: ag_rp_1(V(0,int), V(1,const char *)); break;
  case   2: ag_rp_2(V(0,int), V(1,const char *)); break;
  case   3: ag_rp_3(V(0,const char *), V(1,int), V(2,char)); break;
  case   4: V(0,char) = ag_rp_4(); break;
  case   5: V(0,char) = ag_rp_5(); break;
  case   6: V(0,batch *) = ag_rp_8(V(0,const char *), V(3,const char *)); break;
  case   7: V(0,batch *) = ag_rp_9(V(0,batch *), V(2,const char *)); break;
  case   8: V(0,const char *) = ag_rp_10(V(0,const char *)); break;
  case   9: ag_rp_11(V(0,const char *)); break;
  case  10: ag_rp_12(V(0,char), V(2,const char *), V(4,const char *)); break;
  case  11: ag_rp_13(V(0,char), V(2,const char *), V(4,const char *), V(6,int)); break;
  case  12: ag_rp_14(V(0,char), V(2,const char *), V(4,int)); break;
  case  13: V(0,const char *) = ag_rp_15(V(1,const char *)); break;
  case  14: ag_rp_16(V(0,const char *), V(2,int)); break;
  case  15: ag_rp_17(V(1,int), V(3,int)); break;
  case  16: ag_rp_18(V(1,int), V(3,int)); break;
  case  17: ag_rp_19(V(0,const char *)); break;
  case  18: ag_rp_20(V(0,const char *), V(2,const char *)); break;
  case  19: ag_rp_21(V(0,const char *)); break;
  case  20: ag_rp_22(V(0,const char *), V(2,int), V(4,int), V(6,int), V(8,int)); break;
  case  21: ag_rp_23(V(0,const char *), V(2,const char *)); break;
  case  22: V(0,const char *) = ag_rp_24(V(0,const char *)); break;
  case  23: V(0,const char *) = ag_rp_25(V(0,const char *)); break;
  case  24: V(0,const char *) = ag_rp_26(V(0,const char *)); break;
  case  25: ag_rp_27(V(1,const char *), V(3,const char *), V(4,const char *), V(5,int)); break;
  case  26: ag_rp_28(V(1,const char *)); break;
  case  27: ag_rp_29(V(0,const char *)); break;
  case  28: V(0,int) = ag_rp_30(V(0,int)); break;
  case  29: V(0,const char *) = ag_rp_31(V(0,const char *)); break;
  case  30: V(0,const char *) = ag_rp_32(V(0,const char *)); break;
  case  31: V(0,const char *) = ag_rp_33(V(0,int)); break;
  case  32: V(0,const char *) = ag_rp_34(V(0,const char *), V(1,int)); break;
  case  33: V(0,const char *) = ag_rp_35(V(1,const char *)); break;
  case  34: V(0,const char *) = ag_rp_36(V(0,char)); break;
  case  35: V(0,const char *) = ag_rp_37(V(0,const char *), V(1,char)); break;
  case  36: V(0,char) = ag_rp_38(V(0,int)); break;
  case  37: V(0,char) = ag_rp_39(); break;
  case  38: V(0,char) = ag_rp_40(); break;
  case  39: V(0,int) = ag_rp_41(V(0,int)); break;
  case  40: V(0,int) = ag_rp_42(V(0,int), V(1,int)); break;
  case  41: V(0,int) = ag_rp_43(V(1,int)); break;
  case  42: V(0,int) = ag_rp_44(V(0,int), V(1,int)); break;
  case  43: V(0,int) = ag_rp_45(V(0,int)); break;
  case  44: V(0,int) = ag_rp_46(V(0,int), V(1,int)); break;
  case  45: V(0,int) = ag_rp_47(V(0,int)); break;
  case  46: V(0,int) = ag_rp_48(V(0,int)); break;
  case  47: V(0,int) = ag_rp_49(V(0,int)); break;
  case  48: V(0,int) = ag_rp_50(V(0,int), V(1,int)); break;
  case  49: V(0,int) = ag_rp_51(V(0,int), V(2,int)); break;
  case  50: V(0,int) = ag_rp_52(V(0,int), V(2,int)); break;
  case  51: V(0,int) = ag_rp_53(V(0,int), V(2,int)); break;
  case  52: V(0,int) = ag_rp_54(V(1,int)); break;
  case  53: V(0,int) = ag_rp_55(V(1,int)); break;
  }
}

#define TOKEN_NAMES parse_token_names
const char *parse_token_names[158] = {
  "file format",
  "identifier",
  "white space",
  "simple eol",
  "quoted string",
  "file format",
  "",
  "devices",
  "",
  "cache",
  "",
  "devinfo",
  "",
  "config",
  "eol",
  "",
  "device list",
  "",
  "eof",
  "",
  "character device",
  "",
  "",
  "block device",
  "",
  "number",
  "name",
  "cachedevice",
  "",
  "devicetype",
  "",
  "",
  "",
  "",
  "device block",
  "",
  "device header spec",
  "",
  "device decl",
  "",
  "",
  "",
  "",
  "ignoramus",
  "",
  "",
  "batch list",
  "batch item",
  "",
  "",
  "",
  "groupname",
  "",
  "procname",
  "",
  "class",
  "device tail",
  "",
  "expr",
  "device range",
  "",
  "",
  "",
  "hex number",
  "auto hex",
  "devname",
  "letter",
  "",
  "",
  "config decl",
  "",
  "class decl",
  "omit decl",
  "",
  "mode",
  "",
  "single omit",
  "",
  "octal number",
  "",
  "",
  "",
  "",
  "",
  "",
  "",
  "",
  "",
  "",
  "",
  "",
  "",
  "",
  "",
  "",
  "",
  "qstring",
  "qstring char",
  "qchar",
  "",
  "digit",
  "",
  "",
  "",
  "hex digit",
  "",
  "octal digit",
  "term",
  "",
  "factor",
  "",
  "",
  "",
  "",
  "",
  "",
  "",
  "",
  "",
  "",
  "",
  "",
  "",
  "",
  "",
  "",
  "",
  "",
  "",
  "",
  "",
  "",
  "",
  "",
  "",
  "",
  "letter",
  "",
  "",
  "",
  "simple eol",
  "identifier",
  "quoted string",
  "digit",
  "",
  "",
  "",
  "octal digit",
  "",
  "",
  "",
  "",
  "",
  "",
  "",
  "",
  "",
  "",

};

static char ag_msg[82];
static char ag_mst[] = "Missing %s";
static char ag_uet[] = "Unexpected %s";
static char ag_ac[4] = "' '";

static void ag_diagnose(void) {
  int ag_snd = (PCB).sn, ag_k;
  const char *ag_p;
  const char *ag_fmt = ag_uet;

  ag_k = ag_sbt[ag_snd];
  if (*TOKEN_NAMES[ag_tstt[ag_k]] && ag_astt[ag_k + 1] == ag_action_8) {
    ag_p = TOKEN_NAMES[ag_tstt[ag_k]];
    ag_fmt = ag_mst;
  }
  else if ((PCB).token_number && *TOKEN_NAMES[(PCB).token_number]) {
    ag_p = TOKEN_NAMES[(PCB).token_number];
  }
  else if (isprint((*(PCB).lab)) && (*(PCB).lab) != '\\') {
    ag_ac[1] = (*(PCB).lab);
    ag_p = ag_ac;
  }
  else ag_p = "input";
  sprintf(ag_msg, ag_fmt, ag_p);
  (PCB).error_message = ag_msg;


}
static int ag_action_1_r_proc(void);
static int ag_action_2_r_proc(void);
static int ag_action_3_r_proc(void);
static int ag_action_4_r_proc(void);
static int ag_action_1_s_proc(void);
static int ag_action_3_s_proc(void);
static int ag_action_1_proc(void);
static int ag_action_2_proc(void);
static int ag_action_3_proc(void);
static int ag_action_4_proc(void);
static int ag_action_5_proc(void);
static int ag_action_6_proc(void);
static int ag_action_7_proc(void);
static int ag_action_8_proc(void);
static int ag_action_9_proc(void);
static int ag_action_10_proc(void);
static int ag_action_11_proc(void);
static int ag_action_8_proc(void);


static int (*ag_r_procs_scan[])(void) = {
  ag_action_1_r_proc,
  ag_action_2_r_proc,
  ag_action_3_r_proc,
  ag_action_4_r_proc
};

static int (*ag_s_procs_scan[])(void) = {
  ag_action_1_s_proc,
  ag_action_2_r_proc,
  ag_action_3_s_proc,
  ag_action_4_r_proc
};

static int (*ag_gt_procs_scan[])(void) = {
  ag_action_1_proc,
  ag_action_2_proc,
  ag_action_3_proc,
  ag_action_4_proc,
  ag_action_5_proc,
  ag_action_6_proc,
  ag_action_7_proc,
  ag_action_8_proc,
  ag_action_9_proc,
  ag_action_10_proc,
  ag_action_11_proc,
  ag_action_8_proc
};


static int ag_action_10_proc(void) {
  (PCB).btsx = 0, (PCB).drt = -1;
  ag_track();
  return 0;
}

static int ag_action_11_proc(void) {
  (PCB).btsx = 0, (PCB).drt = -1;
  (*(int *) &(PCB).vs[(PCB).ssx]) = *(PCB).lab;
  (PCB).ssx--;
  ag_ra();
  (PCB).ssx++;
  ag_track();
  return 0;
}

static int ag_action_3_r_proc(void) {
  int ag_sd = ag_fl[ag_ap] - 1;
  if (ag_sd) (PCB).sn = (PCB).ss[(PCB).ssx -= ag_sd];
  (PCB).btsx = 0, (PCB).drt = -1;
  (PCB).reduction_token = (parse_token_type) ag_ptt[ag_ap];
  ag_ra();
  return 1;
}

static int ag_action_3_s_proc(void) {
  int ag_sd = ag_fl[ag_ap] - 1;
  if (ag_sd) (PCB).sn = (PCB).ss[(PCB).ssx -= ag_sd];
  (PCB).btsx = 0, (PCB).drt = -1;
  (PCB).reduction_token = (parse_token_type) ag_ptt[ag_ap];
  ag_ra();
  return 1;
}

static int ag_action_4_r_proc(void) {
  int ag_sd = ag_fl[ag_ap] - 1;
  if (ag_sd) (PCB).sn = (PCB).ss[(PCB).ssx -= ag_sd];
  (PCB).reduction_token = (parse_token_type) ag_ptt[ag_ap];
  return 1;
}

static int ag_action_2_proc(void) {
  (PCB).btsx = 0, (PCB).drt = -1;
  if ((PCB).ssx >= 38) {
    (PCB).exit_flag = AG_STACK_ERROR_CODE;
    PARSER_STACK_OVERFLOW;
  }
  (*(int *) &(PCB).vs[(PCB).ssx]) = *(PCB).lab;
  (PCB).ss[(PCB).ssx] = (PCB).sn;
  (PCB).ssx++;
  (PCB).sn = ag_ap;
  ag_track();
  return 0;
}

static int ag_action_9_proc(void) {
  if((PCB).drt == -1) {
    (PCB).drt=(PCB).token_number;
    (PCB).dssx=(PCB).ssx;
    (PCB).dsn=(PCB).sn;
  }
  ag_prot();
  (PCB).ss[(PCB).ssx] = (PCB).sn;
  (PCB).ssx++;
  (PCB).sn = ag_ap;
  (PCB).rx = 0;
  return (PCB).exit_flag == AG_RUNNING_CODE;
}

static int ag_action_2_r_proc(void) {
  (PCB).ssx++;
  (PCB).sn = ag_ap;
  return 0;
}

static int ag_action_7_proc(void) {
  --(PCB).ssx;
  (PCB).exit_flag = AG_SUCCESS_CODE;
  (PCB).rx = 0;
  return 0;
}

static int ag_action_1_proc(void) {
  (PCB).exit_flag = AG_SUCCESS_CODE;
  ag_track();
  return 0;
}

static int ag_action_1_r_proc(void) {
  (PCB).exit_flag = AG_SUCCESS_CODE;
  return 0;
}

static int ag_action_1_s_proc(void) {
  (PCB).exit_flag = AG_SUCCESS_CODE;
  return 0;
}

static int ag_action_4_proc(void) {
  int ag_sd = ag_fl[ag_ap] - 1;
  (PCB).reduction_token = (parse_token_type) ag_ptt[ag_ap];
  (PCB).btsx = 0, (PCB).drt = -1;
  (*(int *) &(PCB).vs[(PCB).ssx]) = *(PCB).lab;
  if (ag_sd) (PCB).sn = (PCB).ss[(PCB).ssx -= ag_sd];
  else (PCB).ss[(PCB).ssx] = (PCB).sn;
  ag_track();
  while ((PCB).exit_flag == AG_RUNNING_CODE) {
    unsigned ag_t1 = ag_sbe[(PCB).sn] + 1;
    unsigned ag_t2 = ag_sbt[(PCB).sn+1] - 1;
    do {
      unsigned ag_tx = (ag_t1 + ag_t2)/2;
      if (ag_tstt[ag_tx] < (const unsigned char)(PCB).reduction_token) ag_t1 = ag_tx + 1;
      else ag_t2 = ag_tx;
    } while (ag_t1 < ag_t2);
    ag_ap = ag_pstt[ag_t1];
    if ((ag_s_procs_scan[ag_astt[ag_t1]])() == 0) break;
  }
  return 0;
}

static int ag_action_3_proc(void) {
  int ag_sd = ag_fl[ag_ap] - 1;
  (PCB).btsx = 0, (PCB).drt = -1;
  (*(int *) &(PCB).vs[(PCB).ssx]) = *(PCB).lab;
  if (ag_sd) (PCB).sn = (PCB).ss[(PCB).ssx -= ag_sd];
  else (PCB).ss[(PCB).ssx] = (PCB).sn;
  ag_track();
  (PCB).reduction_token = (parse_token_type) ag_ptt[ag_ap];
  ag_ra();
  while ((PCB).exit_flag == AG_RUNNING_CODE) {
    unsigned ag_t1 = ag_sbe[(PCB).sn] + 1;
    unsigned ag_t2 = ag_sbt[(PCB).sn+1] - 1;
    do {
      unsigned ag_tx = (ag_t1 + ag_t2)/2;
      if (ag_tstt[ag_tx] < (const unsigned char)(PCB).reduction_token) ag_t1 = ag_tx + 1;
      else ag_t2 = ag_tx;
    } while (ag_t1 < ag_t2);
    ag_ap = ag_pstt[ag_t1];
    if ((ag_s_procs_scan[ag_astt[ag_t1]])() == 0) break;
  }
  return 0;
}

static int ag_action_8_proc(void) {
  ag_undo();
  (PCB).rx = 0;
  (PCB).exit_flag = AG_SYNTAX_ERROR_CODE;
  ag_diagnose();
  SYNTAX_ERROR;
  {(PCB).rx = 1; ag_track();}
  return (PCB).exit_flag == AG_RUNNING_CODE;
}

static int ag_action_5_proc(void) {
  int ag_sd = ag_fl[ag_ap];
  if((PCB).drt == -1) {
    (PCB).drt=(PCB).token_number;
    (PCB).dssx=(PCB).ssx;
    (PCB).dsn=(PCB).sn;
  }
  if (ag_sd) (PCB).sn = (PCB).ss[(PCB).ssx -= ag_sd];
  else {
    ag_prot();
    (PCB).ss[(PCB).ssx] = (PCB).sn;
  }
  (PCB).rx = 0;
  (PCB).reduction_token = (parse_token_type) ag_ptt[ag_ap];
  ag_ra();
  while ((PCB).exit_flag == AG_RUNNING_CODE) {
    unsigned ag_t1 = ag_sbe[(PCB).sn] + 1;
    unsigned ag_t2 = ag_sbt[(PCB).sn+1] - 1;
    do {
      unsigned ag_tx = (ag_t1 + ag_t2)/2;
      if (ag_tstt[ag_tx] < (const unsigned char)(PCB).reduction_token) ag_t1 = ag_tx + 1;
      else ag_t2 = ag_tx;
    } while (ag_t1 < ag_t2);
    ag_ap = ag_pstt[ag_t1];
    if ((ag_r_procs_scan[ag_astt[ag_t1]])() == 0) break;
  }
  return (PCB).exit_flag == AG_RUNNING_CODE;
}

static int ag_action_6_proc(void) {
  int ag_sd = ag_fl[ag_ap];
  (PCB).reduction_token = (parse_token_type) ag_ptt[ag_ap];
  if((PCB).drt == -1) {
    (PCB).drt=(PCB).token_number;
    (PCB).dssx=(PCB).ssx;
    (PCB).dsn=(PCB).sn;
  }
  if (ag_sd) {
    (PCB).sn = (PCB).ss[(PCB).ssx -= ag_sd];
  }
  else {
    ag_prot();
    (PCB).vs[(PCB).ssx] = ag_null_value;
    (PCB).ss[(PCB).ssx] = (PCB).sn;
  }
  (PCB).rx = 0;
  while ((PCB).exit_flag == AG_RUNNING_CODE) {
    unsigned ag_t1 = ag_sbe[(PCB).sn] + 1;
    unsigned ag_t2 = ag_sbt[(PCB).sn+1] - 1;
    do {
      unsigned ag_tx = (ag_t1 + ag_t2)/2;
      if (ag_tstt[ag_tx] < (const unsigned char)(PCB).reduction_token) ag_t1 = ag_tx + 1;
      else ag_t2 = ag_tx;
    } while (ag_t1 < ag_t2);
    ag_ap = ag_pstt[ag_t1];
    if ((ag_r_procs_scan[ag_astt[ag_t1]])() == 0) break;
  }
  return (PCB).exit_flag == AG_RUNNING_CODE;
}


void init_parse(void) {
  unsigned ag_t1 = 0;
  (PCB).rx = (PCB).fx = 0;
  (PCB).ss[0] = (PCB).sn = (PCB).ssx = 0;
  (PCB).exit_flag = AG_RUNNING_CODE;
  (PCB).key_sp = NULL;
  (PCB).key_state = 0;
  (PCB).line = FIRST_LINE;
   (PCB).column = FIRST_COLUMN;
  (PCB).btsx = 0, (PCB).drt = -1;
  while (ag_tstt[ag_t1] == 0) {
    ag_ap = ag_pstt[ag_t1];
    (ag_gt_procs_scan[ag_astt[ag_t1]])();
    ag_t1 = ag_sbt[(PCB).sn];
  }
}

void parse(void) {
  (PCB).lab[(PCB).fx++] = (PCB).input_code;
  while ((PCB).exit_flag == AG_RUNNING_CODE) {
    while (1) {
      unsigned char *ag_p;
      int ag_ch;
      if ((PCB).rx >= (PCB).fx) return;
      ag_ch = CONVERT_CASE((PCB).lab[(PCB).rx++]);
      if ((PCB).key_sp) {
        if (ag_ch != *(PCB).key_sp++) {
          (PCB).rx = (PCB).save_index;
          (PCB).key_sp = NULL;
          (PCB).key_state = 0;
          break;
        } else if (*(PCB).key_sp) continue;
        if (ag_key_act[(PCB).key_state] == ag_cf_end_key) {
          int ag_k1;
          int ag_k2;
          if ((PCB).rx >= (PCB).fx) {
            (PCB).rx--;
            (PCB).key_sp--;
            return;
          }
          (PCB).key_sp = NULL;
          ag_k1 = ag_key_parm[(PCB).key_state];
          ag_k2 = ag_key_pt[ag_k1];
          if (ag_key_itt[ag_k2 + CONVERT_CASE((PCB).lab[(PCB).rx])])
            (PCB).rx = (PCB).save_index;
          else {
            (PCB).token_number =  (parse_token_type) ag_key_pt[ag_k1+1];
            (PCB).key_state = 0;
          }
          break;
        }
        else {
          (PCB).token_number = (parse_token_type) ag_key_parm[(PCB).key_state];
          (PCB).key_state = 0;
          (PCB).key_sp = NULL;
        }
        break;
      }
      if ((PCB).key_state == 0) {
        (PCB).token_number = (parse_token_type) AG_TCV(ag_ch);
        if (((PCB).key_state = ag_key_index[(PCB).sn]) == 0) break;
        (PCB).save_index = 1;
      }
      ag_p = &ag_key_ch[(PCB).key_state];
      while (*ag_p < ag_ch) ag_p++;
      if (*ag_p == ag_ch) {
        (PCB).key_state = (int)(ag_p - ag_key_ch);
        switch (ag_key_act[(PCB).key_state]) {
        case ag_cf_set_key: {
          int ag_k1;
          int ag_k2;
          if ((PCB).rx >= (PCB).fx) {
            (PCB).rx--;
            return;
          }
          ag_k1 = ag_key_parm[(PCB).key_state];
          ag_k2 = ag_key_pt[ag_k1];
          (PCB).key_state = ag_key_jmp[(PCB).key_state];
          if (ag_key_itt[ag_k2 + CONVERT_CASE((PCB).lab[(PCB).rx])]) break;
          (PCB).save_index = (PCB).rx;
          (PCB).token_number = (parse_token_type) ag_key_pt[ag_k1+1];
          break;
        }
        case ag_set_key:
          (PCB).save_index = (PCB).rx;
          (PCB).token_number = (parse_token_type) ag_key_parm[(PCB).key_state];
        case ag_jmp_key:
          (PCB).key_state = ag_key_jmp[(PCB).key_state];
          continue;
        case ag_cf_end_key:
        case ag_end_key:
          (PCB).key_sp = ag_key_ends + ag_key_jmp[(PCB).key_state];
          continue;
        case ag_accept_key:
          (PCB).token_number = (parse_token_type) ag_key_parm[(PCB).key_state];
          (PCB).key_state = 0;
          break;
        case ag_cf_accept_key: {
          int ag_k1;
          int ag_k2;
          if ((PCB).rx >= (PCB).fx) {
            (PCB).rx--;
            return;
          }
          ag_k1 = ag_key_parm[(PCB).key_state];
          ag_k2 = ag_key_pt[ag_k1];
          if (ag_key_itt[ag_k2 + CONVERT_CASE((PCB).lab[(PCB).rx])])
            (PCB).rx = (PCB).save_index;
          else {
            (PCB).rx--;
            (PCB).token_number = (parse_token_type) ag_key_pt[ag_k1+1];
            (PCB).key_state = 0;
          }
          break;
        }
        }
        break;
      } else {
        (PCB).rx = (PCB).save_index;
        (PCB).key_state = 0;
        break;
      }
    }

    {
      unsigned ag_t1 = ag_sbt[(PCB).sn];
      unsigned ag_t2 = ag_sbe[(PCB).sn] - 1;
      do {
        unsigned ag_tx = (ag_t1 + ag_t2)/2;
        if (ag_tstt[ag_tx] > (const unsigned char)(PCB).token_number)
          ag_t1 = ag_tx + 1;
        else ag_t2 = ag_tx;
      } while (ag_t1 < ag_t2);
      if (ag_tstt[ag_t1] != (PCB).token_number)  ag_t1 = ag_sbe[(PCB).sn];
      ag_ap = ag_pstt[ag_t1];
      (ag_gt_procs_scan[ag_astt[ag_t1]])();
    }
  }

}


