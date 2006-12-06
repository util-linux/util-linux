/* checktty.c - linked into login, checks user against /etc/usertty
   Created 25-Aug-95 by Peter Orbaek <poe@daimi.aau.dk>
*/

#include <pwd.h>
#include <grp.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>
#include <malloc.h>
#include <netdb.h>
#include <sys/syslog.h>

#ifdef linux
#  include <sys/sysmacros.h>
#  include <linux/major.h>
#endif

#include "pathnames.h"

/* functions in login.c */
void badlogin(char *s);
void sleepexit(int);
extern struct hostent hostaddress;
extern char *hostname;

#ifdef TESTING
struct hostent hostaddress;
char *hostname;

void 
badlogin(char *s)
{
    printf("badlogin: %s\n", s);
}

void
sleepexit(int x)
{
    printf("sleepexit %d\n", x);
    exit(1);
}
#endif

#define NAMELEN 128

/* linked list of names */
struct grplist {
    struct grplist *next;
    char name[NAMELEN];
};

struct grplist *mygroups = NULL;

enum State { StateUsers, StateGroups, StateClasses };

#define CLASSNAMELEN 32

struct ttyclass {
    struct grplist *first;
    struct ttyclass *next;
    char classname[CLASSNAMELEN];
};

struct ttyclass *ttyclasses = NULL;

static void
add_group(char *group)
{
    struct grplist *ge;

    ge = (struct grplist *)malloc(sizeof(struct grplist));

    /* we can't just bail out at this stage! */
    if (!ge) {
	printf("login: memory low, login may fail\n");
	syslog(LOG_WARNING, "can't malloc grplist");
	return;
    }

    ge->next = mygroups;
    strncpy(ge->name, group, NAMELEN);
    mygroups = ge;
}

static int
am_in_group(char *group)
{
    struct grplist *ge;

    for (ge = mygroups; ge; ge = ge->next) {
	if (strcmp(ge->name, group) == 0) return 1;
    }
    return 0;
}

static void
find_groups(gid_t defgrp, char *user)
{
    struct group *gp;
    char **p;

    setgrent();
    while ((gp = getgrent())) {
	if (gp->gr_gid == defgrp) {
	    add_group(gp->gr_name);
	} else {
	    for(p = gp->gr_mem; *p; p++) {
		if (strcmp(user, *p) == 0) {
		    add_group(gp->gr_name);
		    break;
		}
	    }
	}
	    
    }
    endgrent();
}

static struct ttyclass *
new_class(char *class)
{
    struct ttyclass *tc;

    tc = (struct ttyclass *)malloc(sizeof(struct ttyclass));
    if (tc == NULL) {
	printf("login: memory low, login may fail\n");
	syslog(LOG_WARNING, "can't malloc for ttyclass");
	return NULL;
    }

    tc->next = ttyclasses;
    tc->first = NULL;
    strncpy(tc->classname, class, CLASSNAMELEN);
    ttyclasses = tc;
    return tc;
}

static void
add_to_class(struct ttyclass *tc, char *tty)
{
    struct grplist *ge;

    if (tc == NULL) return;

    ge = (struct grplist *)malloc(sizeof(struct grplist));
    if (ge == NULL) {
	printf("login: memory low, login may fail\n");
	syslog(LOG_WARNING, "can't malloc for grplist");
	return;
    }

    ge->next = tc->first;
    strncpy(ge->name, tty, NAMELEN);
    tc->first = ge;
}


/* return true if tty is a pty. Very linux dependent */
static int
isapty(tty)
     char *tty;
{
    char devname[100];
    struct stat stb;

#ifdef linux
    strcpy(devname, "/dev/");
    strncat(devname, tty, 80);
    if((stat(devname, &stb) >= 0)
       && major(stb.st_rdev) == TTY_MAJOR
       && minor(stb.st_rdev) >= 192) {
	return 1;
    }
#endif
    return 0;
}

/* match the hostname hn against the pattern pat */
static int
hnmatch(hn, pat)
     char *hn;
     char *pat;
{
    int x1, x2, x3, x4, y1, y2, y3, y4;
    unsigned long p, mask, a;
    unsigned char *ha;
    int n, m;

    if ((hn == NULL) && (strcmp(pat, "localhost") == 0)) return 1;
    if ((hn == NULL) || hn[0] == 0) return 0;

    if (pat[0] >= '0' && pat[0] <= '9') {
	/* pattern is an IP QUAD address and a mask x.x.x.x/y.y.y.y */
	sscanf(pat, "%d.%d.%d.%d/%d.%d.%d.%d", &x1, &x2, &x3, &x4,
	       &y1, &y2, &y3, &y4);
	p = (((unsigned long)x1<<24)+((unsigned long)x2<<16)
	     +((unsigned long)x3<<8)+((unsigned long)x4));
	mask = (((unsigned long)y1<<24)+((unsigned long)y2<<16)
		+((unsigned long)y3<<8)+((unsigned long)y4));

	if (!hostaddress.h_addr_list || !hostaddress.h_addr_list[0])
	  return 0;

	ha = (unsigned char *)hostaddress.h_addr_list[0];
	a = (((unsigned long)ha[0]<<24)+((unsigned long)ha[1]<<16)
	     +((unsigned long)ha[2]<<8)+((unsigned long)ha[3]));
	return ((p & mask) == (a & mask));
    } else {
	/* pattern is a suffix of a FQDN */
	n = strlen(pat);
	m = strlen(hn);
	if (n > m) return 0;
	return (strcasecmp(pat, hn + m - n) == 0);
    }
}

static char *wdays[] = { "sun", "mon", "tue", "wed", "thu", "fri", "sat" };

/* example timespecs:

   mon:tue:wed:8-17

   meaning monday, tuesday or wednesday between 8:00 and 17:59

   4:5:13:fri

   meaning fridays from 4:00 to 5:59 and from 13:00 to 13:59
*/
static int
timeok(struct tm *t, char *spec)
{
    char *p, *q;
    int dayok = 0;
    int hourok = 0;
    int h, h2;
    char *sp;

    sp = spec;
    while ((p = strsep(&sp, ":"))) {
	if (*p >= '0' && *p <= '9') {
	    h = atoi(p);
	    if (h == t->tm_hour) hourok = 1;
	    if ((q = strchr(p, '-')) && (q[1] >= '0' && q[1] <= '9')) {
		h2 = atoi(q+1);
		if (h <= t->tm_hour && t->tm_hour <= h2) hourok = 1;
	    }
	} else if (strcasecmp(wdays[t->tm_wday], p) == 0) {
	    dayok = 1;
	}
    }

    return (dayok && hourok);
}

/* return true if tty equals class or is in the class defined by class.
   Also return true if hostname matches the hostname pattern, class
   or a pattern in the class named by class. */
static int
in_class(char *tty, char *class)
{
    struct ttyclass *tc;
    struct grplist *ge;
    time_t t;
    char *p;
    char timespec[256];
    struct tm *tm;
    char *n;

    time(&t);
    tm = localtime(&t);

    if (class[0] == '[') {
	if ((p = strchr(class, ']'))) {
	    *p = 0;
	    strcpy(timespec, class+1);
	    *p = ']';
	    if(!timeok(tm, timespec)) return 0;
	    class = p+1;
	}
	/* really ought to warn about syntax error */
    }

    if (strcmp(tty, class) == 0) return 1;

    if ((class[0] == '@') && isapty(tty)
	&& hnmatch(hostname, class+1)) return 1;

    for (tc = ttyclasses; tc; tc = tc->next) {
	if (strcmp(tc->classname, class) == 0) {
	    for (ge = tc->first; ge; ge = ge->next) {

		n = ge->name;
		if (n[0] == '[') {
		    if ((p = strchr(n, ']'))) {
			*p = 0;
			strcpy(timespec, n+1);
			*p = ']';
			if(!timeok(tm, timespec)) continue;
			n = p+1;
		    }
		    /* really ought to warn about syntax error */
		}

		if (strcmp(n, tty) == 0) return 1;

		if ((n[0] == '@') && isapty(tty)
		    && hnmatch(hostname, n+1)) return 1;
	    }
	    return 0;
	}
    }
    return 0;
}

void
checktty(user, tty, pwd)
     char *user;
     char *tty;
     struct passwd *pwd;
{
    FILE *f;
    char buf[256], defaultbuf[256];
    char *ptr;
    enum State state = StateUsers;
    int found_match = 0;

    /* no /etc/usertty, default to allow access */
#ifdef TESTING
    if (!(f = fopen("usertty", "r"))) return;
#else
    if (!(f = fopen(_PATH_USERTTY, "r"))) return;
#endif

    if (pwd == NULL) return;  /* misspelled username handled elsewhere */

    find_groups(pwd->pw_gid, user);

    defaultbuf[0] = 0;
    while(fgets(buf, 255, f)) {

	/* strip comments */
	for(ptr = buf; ptr < buf + 256; ptr++) 
	  if(*ptr == '#') *ptr = 0;

	if (buf[0] == '*') {
	    strncpy(defaultbuf, buf, 256);
	    continue;
	}

	if (strncmp("GROUPS", buf, 6) == 0) {
	    state = StateGroups;
	    continue;
	} else if (strncmp("USERS", buf, 5) == 0) {
	    state = StateUsers;
	    continue;
	} else if (strncmp("CLASSES", buf, 7) == 0) {
	    state = StateClasses;
	    continue;
	}

	strtok(buf, " \t");
	if((state == StateUsers && (strncmp(user, buf, 8) == 0))
	   || (state == StateGroups && am_in_group(buf))) {
	    found_match = 1;  /* we found a line matching the user */
	    while((ptr = strtok(NULL, "\t\n "))) {
		if (in_class(tty, ptr)) {
		    fclose(f);
		    return;
		}
	    }
	} else if (state == StateClasses) {
	    /* define a new tty/host class */
	    struct ttyclass *tc = new_class(buf);

	    while ((ptr = strtok(NULL, "\t\n "))) {
		add_to_class(tc, ptr);
	    }
	}
    }
    fclose(f);

    /* user is not explicitly mentioned in /etc/usertty, if there was
       a default rule, use that */
    if (defaultbuf[0]) {
	strtok(defaultbuf, " \t");
	while((ptr = strtok(NULL, "\t\n "))) {
	    if (in_class(tty, ptr)) return;
	}

	/* there was a default rule, but user didn't match, reject! */
	printf("Login on %s from %s denied by default.\n", tty, hostname);
	badlogin(user);
	sleepexit(1);
    }

    if (found_match) {
	/* if we get here, /etc/usertty exists, there's a line
	   matching our username, but it doesn't contain the
	   name of the tty where the user is trying to log in.
	   So deny access! */

	printf("Login on %s from %s denied.\n", tty, hostname);
	badlogin(user);
	sleepexit(1);
    }

    /* users not matched in /etc/usertty are by default allowed access
       on all tty's */
}

#ifdef TESTING
main(int argc, char *argv[]) 
{
    struct passwd *pw;

    pw = getpwnam(argv[1]);
    checktty(argv[1], argv[2], pw);
}
#endif
