/*
 *   chfn.c -- change your finger information
 *   (c) 1994 by salvatore valente <svalente@athena.mit.edu>
 *
 *   this program is free software.  you can redistribute it and
 *   modify it under the terms of the gnu general public license.
 *   there is no warranty.
 *
 *   $Author: aebr $
 *   $Revision: 1.15 $
 *   $Date: 1997/07/06 23:10:41 $
 *
 * Updated Thu Oct 12 09:19:26 1995 by faith@cs.unc.edu with security
 * patches from Zefram <A.Main@dcs.warwick.ac.uk>
 *
 *  Hacked by Peter Breitenlohner, peb@mppmu.mpg.de,
 *    to allow peaceful coexistence with yp: using the changes by
 *     Alvaro Martinez Echevarria, alvaro@enano.etsit.upm.es, from
 *     passwd.c (now moved to setpwnam.c);
 *    to remove trailing empty fields.  Oct 5, 96.
 *
 */

static char rcsId[] = "$Version: $Id: chfn.c,v 1.15 1997/07/06 23:10:41 aebr Exp $ $";

#define _XOPEN_SOURCE		/* for crypt() */
#define _BSD_SOURCE           /* for strcasecmp() */

#include <sys/types.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <pwd.h>
#include <errno.h>
#include <ctype.h>
#include <getopt.h>
#include "../version.h"

#if REQUIRE_PASSWORD && USE_PAM
#include <security/pam_appl.h>
#include <security/pam_misc.h>
#endif

extern int is_local(char *);

#undef P
#if __STDC__
#define P(foo) foo
#else
#define P(foo) ()
#endif

typedef unsigned char boolean;
#define false 0
#define true 1

static char *whoami;

static char buf[1024];

struct finfo {
    struct passwd *pw;
    char *username;
    char *full_name;
    char *office;
    char *office_phone;
    char *home_phone;
    char *other;
};

static boolean parse_argv P((int argc, char *argv[], struct finfo *pinfo));
static void usage P((FILE *fp));
static void parse_passwd P((struct passwd *pw, struct finfo *pinfo));
static void ask_info P((struct finfo *oldfp, struct finfo *newfp));
static char *prompt P((char *question, char *def_val));
static int check_gecos_string P((char *msg, char *gecos));
static boolean set_changed_data P((struct finfo *oldfp, struct finfo *newfp));
static int save_new_data P((struct finfo *pinfo));
static void *xmalloc P((int bytes));

extern int setpwnam P((struct passwd *pwd));

#define memzero(ptr, size) memset((char *) ptr, 0, size)

int main (argc, argv)
    int argc;
    char *argv[];
{
    char *cp, *pwdstr;
    uid_t uid;
    struct finfo oldf, newf;
    boolean interactive;
    int status;
    extern int errno;
#if REQUIRE_PASSWORD && USE_PAM
    pam_handle_t *pamh = NULL;
    int retcode;
    struct pam_conv conv = { misc_conv, NULL };
#endif

    /* whoami is the program name for error messages */
    whoami = argv[0];
    if (! whoami) whoami = "chfn";
    for (cp = whoami; *cp; cp++)
	if (*cp == '/') whoami = cp + 1;

    /*
     *	"oldf" contains the users original finger information.
     *	"newf" contains the changed finger information, and contains NULL
     *	       in fields that haven't been changed.
     *	in the end, "newf" is folded into "oldf".
     *	the reason the new finger information is not put _immediately_ into
     *	"oldf" is that on the command line, new finger information can
     *	be specified before we know what user the information is being
     *	specified for.
     */
    uid = getuid ();
    memzero (&oldf, sizeof (oldf));
    memzero (&newf, sizeof (newf));

    interactive = parse_argv (argc, argv, &newf);
    if (! newf.username) {
	parse_passwd (getpwuid (uid), &oldf);
	if (! oldf.username) {
	    fprintf (stderr, "%s: you (user %d) don't exist.\n", whoami, uid);
	    return (-1); }
    }
    else {
	parse_passwd (getpwnam (newf.username), &oldf);
	if (! oldf.username) {
	    cp = newf.username;
	    fprintf (stderr, "%s: user \"%s\" does not exist.\n", whoami, cp);
	    return (-1); }
    }

    if (!(is_local(oldf.username))) {
       fprintf (stderr, "%s: can only change local entries; use yp%s instead.\n",
           whoami, whoami);
       exit(1);
    }

    /* Reality check */
    if (uid != 0 && uid != oldf.pw->pw_uid) {
	errno = EACCES;
	perror (whoami);
	return (-1);
    }

    printf ("Changing finger information for %s.\n", oldf.username);

#if REQUIRE_PASSWORD
# if USE_PAM
    if(uid != 0) {
        if (pam_start("chfn", oldf.username, &conv, &pamh)) {
	    puts("Password error.");
	    exit(1);
	}
        if (pam_authenticate(pamh, 0)) {
	    puts("Password error.");
	    exit(1);
	}
        retcode = pam_acct_mgmt(pamh, 0);
        if (retcode == PAM_AUTHTOKEN_REQD) {
	    retcode = pam_chauthtok(pamh, PAM_CHANGE_EXPIRED_AUTHTOK);
        } else if (retcode) {
	    puts("Password error.");
	    exit(1);
	}
        if (pam_setcred(pamh, 0)) {
	    puts("Password error.");
	    exit(1);
	}
        /* no need to establish a session; this isn't a session-oriented
         * activity... */
    }
# else /* USE_PAM */
    /* require password, unless root */
    if(uid != 0 && oldf.pw->pw_passwd && oldf.pw->pw_passwd[0]) {
	pwdstr = getpass("Password: ");
	if(strncmp(oldf.pw->pw_passwd,
		   crypt(pwdstr, oldf.pw->pw_passwd), 13)) {
	    puts("Incorrect password.");
	    exit(1);
	}
    }
# endif /* USE_PAM */
#endif /* REQUIRE_PASSWORD */


    if (interactive) ask_info (&oldf, &newf);

    if (! set_changed_data (&oldf, &newf)) {
	printf ("Finger information not changed.\n");
	return 0;
    }
    status = save_new_data (&oldf);
    return status;
}

/*
 *  parse_argv () --
 *	parse the command line arguments.
 *	returns true if no information beyond the username was given.
 */
static boolean parse_argv (argc, argv, pinfo)
    int argc;
    char *argv[];
    struct finfo *pinfo;
{
    int index, c, status;
    boolean info_given;

    static struct option long_options[] = {
	{ "full-name",	  required_argument, 0, 'f' },
	{ "office",	  required_argument, 0, 'o' },
	{ "office-phone", required_argument, 0, 'p' },
	{ "home-phone",   required_argument, 0, 'h' },
	{ "help",	  no_argument,       0, 'u' },
	{ "version",	  no_argument,	     0, 'v' },
	{ NULL,		  no_argument,	     0, '0' },
    };

    optind = 0;
    info_given = false;
    while (true) {
	c = getopt_long (argc, argv, "f:r:p:h:o:uv", long_options, &index);
	if (c == EOF) break;
	/* version?  output version and exit. */
	if (c == 'v') {
	    printf ("%s\n", util_linux_version);
	    exit (0);
	}
	if (c == 'u') {
	    usage (stdout);
	    exit (0);
	}
	/* all other options must have an argument. */
	if (! optarg) {
	    usage (stderr);
	    exit (-1);
	}
	/* ok, we were given an argument */
	info_given = true;
	status = 0;

	strncpy (buf, whoami, sizeof(buf)-128); 
	buf[sizeof(buf)-128-1] = 0;
	strcat (buf, ": ");

	/* now store the argument */
	switch (c) {
	case 'f':
	    pinfo->full_name = optarg;
	    strcat (buf, "full name");
	    status = check_gecos_string (buf, optarg);
	    break;
	case 'o':
	    pinfo->office = optarg;
	    strcat (buf, "office");
	    status = check_gecos_string (buf, optarg);
	    break;
	case 'p':
	    pinfo->office_phone = optarg;
	    strcat (buf, "office phone");
	    status = check_gecos_string (buf, optarg);
	    break;
	case 'h':
	    pinfo->home_phone = optarg;
	    strcat (buf, "home phone");
	    status = check_gecos_string (buf, optarg);
	    break;
	default:
	    usage (stderr);
	    status = (-1);
	}
	if (status < 0) exit (status);
    }
    /* done parsing arguments.	check for a username. */
    if (optind < argc) {
	if (optind + 1 < argc) {
	    usage (stderr);
	    exit (-1);
	}
	pinfo->username = argv[optind];
    }
    return (! info_given);
}

/*
 *  usage () --
 *	print out a usage message.
 */
static void usage (fp)
    FILE *fp;
{
    fprintf (fp, "Usage: %s [ -f full-name ] [ -o office ] ", whoami);
    fprintf (fp, "[ -p office-phone ]\n	[ -h home-phone ] ");
    fprintf (fp, "[ --help ] [ --version ]\n");
}

/*
 *  parse_passwd () --
 *	take a struct password and fill in the fields of the
 *	struct finfo.
 */
static void parse_passwd (pw, pinfo)
    struct passwd *pw;
    struct finfo *pinfo;
{
    char *cp;

    if (pw) {
	pinfo->pw = pw;
	pinfo->username = pw->pw_name;
	/* use pw_gecos */
	cp = pw->pw_gecos;
	pinfo->full_name = cp;
	cp = strchr (cp, ',');
	if (cp) { *cp = 0, cp++; } else return;
	pinfo->office = cp;
	cp = strchr (cp, ',');
	if (cp) { *cp = 0, cp++; } else return;
	pinfo->office_phone = cp;
	cp = strchr (cp, ',');
	if (cp) { *cp = 0, cp++; } else return;
	pinfo->home_phone = cp;
	/*  extra fields contain site-specific information, and
	 *  can not be changed by this version of chfn.	 */
	cp = strchr (cp, ',');
	if (cp) { *cp = 0, cp++; } else return;
	pinfo->other = cp;
    }
}

/*
 *  ask_info () --
 *	prompt the user for the finger information and store it.
 */
static void ask_info (oldfp, newfp)
    struct finfo *oldfp;
    struct finfo *newfp;
{
    newfp->full_name = prompt ("Name", oldfp->full_name);
    newfp->office = prompt ("Office", oldfp->office);
    newfp->office_phone	= prompt ("Office Phone", oldfp->office_phone);
    newfp->home_phone = prompt ("Home Phone", oldfp->home_phone);
    printf ("\n");
}

/*
 *  prompt () --
 *	ask the user for a given field and check that the string is legal.
 */
static char *prompt (question, def_val)
    char *question;
    char *def_val;
{
    static char *blank = "none";
    int len;
    char *ans, *cp;
  
    while (true) {
	if (! def_val) def_val = "";
	printf("%s [%s]: ", question, def_val);
	*buf = 0;
	if (fgets (buf, sizeof (buf), stdin) == NULL) {
	    printf ("\nAborted.\n");
	    exit (-1);
	}
	/* remove the newline at the end of buf. */
	ans = buf;
	while (isspace (*ans)) ans++;
	len = strlen (ans);
	while (len > 0 && isspace (ans[len-1])) len--;
	if (len <= 0) return NULL;
	ans[len] = 0;
	if (! strcasecmp (ans, blank)) return "";
	if (check_gecos_string (NULL, ans) >= 0) break;
    }
    cp = (char *) xmalloc (len + 1);
    strcpy (cp, ans);
    return cp;
}

/*
 *  check_gecos_string () --
 *	check that the given gecos string is legal.  if it's not legal,
 *	output "msg" followed by a description of the problem, and
 *	return (-1).
 */
static int check_gecos_string (msg, gecos)
    char *msg;
    char *gecos;
{
    int i, c;

    for (i = 0; i < strlen (gecos); i++) {
	c = gecos[i];
	if (c == ',' || c == ':' || c == '=' || c == '"' || c == '\n') {
	    if (msg) printf ("%s: ", msg);
	    printf ("'%c' is not allowed.\n", c);
	    return (-1);
	}
	if (iscntrl (c)) {
	    if (msg) printf ("%s: ", msg);
	    printf ("Control characters are not allowed.\n");
	    return (-1);
	}
    }
    return (0);
}

/*
 *  set_changed_data () --
 *	incorporate the new data into the old finger info.
 */
static boolean set_changed_data (oldfp, newfp)
    struct finfo *oldfp;
    struct finfo *newfp;
{
    boolean changed = false;

    if (newfp->full_name) {
	oldfp->full_name = newfp->full_name; changed = true; }
    if (newfp->office) {
	oldfp->office = newfp->office; changed = true; }
    if (newfp->office_phone) {
	oldfp->office_phone = newfp->office_phone; changed = true; }
    if (newfp->home_phone) {
	oldfp->home_phone = newfp->home_phone; changed = true; }

    return changed;
}

/*
 *  save_new_data () --
 *	save the given finger info in /etc/passwd.
 *	return zero on success.
 */
static int save_new_data (pinfo)
     struct finfo *pinfo;
{
    char *gecos;
    int len;

    /* null fields will confuse printf(). */
    if (! pinfo->full_name) pinfo->full_name = "";
    if (! pinfo->office) pinfo->office = "";
    if (! pinfo->office_phone) pinfo->office_phone = "";
    if (! pinfo->home_phone) pinfo->home_phone = "";
    if (! pinfo->other) pinfo->other = "";

    /* create the new gecos string */
    len = (strlen (pinfo->full_name) + strlen (pinfo->office) +
	   strlen (pinfo->office_phone) + strlen (pinfo->home_phone) +
	   strlen (pinfo->other) + 4);
    gecos = (char *) xmalloc (len + 1);
    sprintf (gecos, "%s,%s,%s,%s,%s", pinfo->full_name, pinfo->office,
	     pinfo->office_phone, pinfo->home_phone, pinfo->other);

    /* remove trailing empty fields (but not subfields of pinfo->other) */
    if (! pinfo->other[0] ) {
       while (len > 0 && gecos[len-1] == ',') len--;
       gecos[len] = 0;
    }

    /* write the new struct passwd to the passwd file. */
    pinfo->pw->pw_gecos = gecos;
    if (setpwnam (pinfo->pw) < 0) {
	perror ("setpwnam");
	printf( "Finger information *NOT* changed.  Try again later.\n" );
	return (-1);
    }
    printf ("Finger information changed.\n");
    return 0;
}

/*
 *  xmalloc () -- malloc that never fails.
 */
static void *xmalloc (bytes)
    int bytes;
{
    void *vp;

    vp = malloc (bytes);
    if (! vp && bytes > 0) {
	perror ("malloc failed");
	exit (-1);
    }
    return vp;
}
