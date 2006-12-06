/*
 *   chsh.c -- change your login shell
 *   (c) 1994 by salvatore valente <svalente@athena.mit.edu>
 *
 *   this program is free software.  you can redistribute it and
 *   modify it under the terms of the gnu general public license.
 *   there is no warranty.
 *
 *   $Author: aebr $
 *   $Revision: 1.19 $
 *   $Date: 1998/06/11 22:30:14 $
 *
 * Updated Thu Oct 12 09:33:15 1995 by faith@cs.unc.edu with security
 *   patches from Zefram <A.Main@dcs.warwick.ac.uk>
 *
 * Updated Mon Jul  1 18:46:22 1996 by janl@math.uio.no with security
 *   suggestion from Zefram.  Disallowing users with shells not in /etc/shells
 *   from changing their shell.
 *
 */

#if 0
#define _POSIX_SOURCE 1
#endif

#include <sys/types.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <pwd.h>
#include <errno.h>
#include <ctype.h>
#include <getopt.h>
#include "my_crypt.h"
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

/* Only root is allowed to assign a luser a non-listed shell, by default */
#define ONLY_LISTED_SHELLS 1


static char *whoami;

static char buf[FILENAME_MAX];

struct sinfo {
    char *username;
    char *shell;
};

static void parse_argv P((int argc, char *argv[], struct sinfo *pinfo));
static void usage P((FILE *fp));
static char *prompt P((char *question, char *def_val));
static int check_shell P((char *shell));
static boolean get_shell_list P((char *shell));
static void *xmalloc P((int bytes));
extern int setpwnam P((struct passwd *pwd));
#define memzero(ptr, size) memset((char *) ptr, 0, size)

int main (argc, argv)
    int argc;
    char *argv[];
{
    char *cp, *shell, *oldshell, *pwdstr;
    uid_t uid;
    struct sinfo info;
    struct passwd *pw;
    extern int errno;
#if REQUIRE_PASSWORD && USE_PAM
    pam_handle_t *pamh = NULL;
    int retcode;
    struct pam_conv conv = { misc_conv, NULL };
#endif

    /* whoami is the program name for error messages */
    whoami = argv[0];
    if (! whoami) whoami = "chsh";
    for (cp = whoami; *cp; cp++)
	if (*cp == '/') whoami = cp + 1;

    uid = getuid ();
    memzero (&info, sizeof (info));

    parse_argv (argc, argv, &info);
    pw = NULL;
    if (! info.username) {
	pw = getpwuid (uid);
	if (! pw) {
	    fprintf (stderr, "%s: you (user %d) don't exist.\n", whoami, uid);
	    return (-1); }
    }
    else {
	pw = getpwnam (info.username);
	if (! pw) {
	    cp = info.username;
	    fprintf (stderr, "%s: user \"%s\" does not exist.\n", whoami, cp);
	    return (-1); }
    }

    if (!(is_local(pw->pw_name))) {
       fprintf (stderr, "%s: can only change local entries; use yp%s instead.\n
",
           whoami, whoami);
       exit(1);
    }

    oldshell = pw->pw_shell;
    if (!oldshell[0]) oldshell = "/bin/sh";

    /* reality check */
    if (uid != 0 && (uid != pw->pw_uid || !get_shell_list(oldshell))) {
	errno = EACCES;
	fprintf(stderr,"%s: Your shell is not in /etc/shells, shell change"
		" denied\n",whoami);
	return (-1);
    }
    
    shell = info.shell;

    printf( "Changing shell for %s.\n", pw->pw_name );

#if REQUIRE_PASSWORD
# if USE_PAM
    if(uid != 0) {
        if (pam_start("chsh", pw->pw_name, &conv, &pamh)) {
	    puts("Password error.");
	    exit(1);
	}
        if (pam_authenticate(pamh, 0)) {
	    puts("Password error.");
	    exit(1);
	}
        retcode = pam_acct_mgmt(pamh, 0);
        if (retcode == PAM_NEW_AUTHTOK_REQD) {
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
    if(uid != 0 && pw->pw_passwd && pw->pw_passwd[0]) {
	pwdstr = getpass("Password: ");
	if(strncmp(pw->pw_passwd,
		   crypt(pwdstr, pw->pw_passwd), 13)) {
	    puts("Incorrect password.");
	    exit(1);
	}
    }
# endif /* USE_PAM */
#endif /* REQUIRE_PASSWORD */

    if (! shell) {
	shell = prompt ("New shell", oldshell);
	if (! shell) return 0;
    }
    
    if (check_shell (shell) < 0) return (-1);

    if (! strcmp (pw->pw_shell, shell)) {
	printf ("Shell not changed.\n");
	return 0;
    }
    if (!strcmp(shell, "/bin/sh")) shell = "";
    pw->pw_shell = shell;
    if (setpwnam (pw) < 0) {
	perror ("setpwnam");
	printf( "Shell *NOT* changed.  Try again later.\n" );
	return (-1);
    }
    printf ("Shell changed.\n");
    return 0;
}

/*
 *  parse_argv () --
 *	parse the command line arguments, and fill in "pinfo" with any
 *	information from the command line.
 */
static void parse_argv (argc, argv, pinfo)
    int argc;
    char *argv[];
    struct sinfo *pinfo;
{
    int index, c;

    static struct option long_options[] = {
	{ "shell",	 required_argument, 0, 's' },
	{ "list-shells", no_argument,	    0, 'l' },
	{ "help",	 no_argument,	    0, 'u' },
	{ "version",	 no_argument,	    0, 'v' },
	{ NULL,		 no_argument,	    0, '0' },
    };

    optind = c = 0;
    while (c != EOF) {
	c = getopt_long (argc, argv, "s:luv", long_options, &index);
	switch (c) {
	case EOF:
	    break;
	case 'v':
	    printf ("%s\n", util_linux_version);
	    exit (0);
	case 'u':
	    usage (stdout);
	    exit (0);
	case 'l':
	    get_shell_list (NULL);
	    exit (0);
	case 's':
	    if (! optarg) {
		usage (stderr);
		exit (-1);
	    }
	    pinfo->shell = optarg;
	    break;
	default:
	    usage (stderr);
	    exit (-1);
	}
    }
    /* done parsing arguments.	check for a username. */
    if (optind < argc) {
	if (optind + 1 < argc) {
	    usage (stderr);
	    exit (-1);
	}
	pinfo->username = argv[optind];
    }
}

/*
 *  usage () --
 *	print out a usage message.
 */
static void usage (fp)
    FILE *fp;
{
    fprintf (fp, "Usage: %s [ -s shell ] ", whoami);
    fprintf (fp, "[ --list-shells ] [ --help ] [ --version ]\n");
    fprintf (fp, "       [ username ]\n");
}

/*
 *  prompt () --
 *	ask the user for a given field and return it.
 */
static char *prompt (question, def_val)
    char *question;
    char *def_val;
{
    int len;
    char *ans, *cp;
  
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
    cp = (char *) xmalloc (len + 1);
    strcpy (cp, ans);
    return cp;
}

/*
 *  check_shell () -- if the shell is completely invalid, print
 *	an error and return (-1).
 *	if the shell is a bad idea, print a warning.
 */
static int check_shell (shell)
    char *shell;
{
    int i, c;

    if (*shell != '/') {
	printf ("%s: shell must be a full path name.\n", whoami);
	return (-1);
    }
    if (access (shell, F_OK) < 0) {
	printf ("%s: \"%s\" does not exist.\n", whoami, shell);
	return (-1);
    }
    if (access (shell, X_OK) < 0) {
	printf ("%s: \"%s\" is not executable.\n", whoami, shell);
	return (-1);
    }
    /* keep /etc/passwd clean. */
    for (i = 0; i < strlen (shell); i++) {
	c = shell[i];
	if (c == ',' || c == ':' || c == '=' || c == '"' || c == '\n') {
	    printf ("%s: '%c' is not allowed.\n", whoami, c);
	    return (-1);
	}
	if (iscntrl (c)) {
	    printf ("%s: Control characters are not allowed.\n", whoami);
	    return (-1);
	}
    }
#if ONLY_LISTED_SHELLS
    if (! get_shell_list (shell)) {
       if (!getuid())
	  printf ("Warning: \"%s\" is not listed in /etc/shells\n", shell);
       else {
	  printf ("%s: \"%s\" is not listed in /etc/shells.\n",
		  whoami, shell);
	  printf( "%s: use -l option to see list\n", whoami );
	  exit(1);
       }
    }
#else
    if (! get_shell_list (shell)) {
       printf ("Warning: \"%s\" is not listed in /etc/shells.\n", shell);
       printf( "Use %s -l to see list.\n", whoami );
    }
#endif
    return 0;
}

/*
 *  get_shell_list () -- if the given shell appears in /etc/shells,
 *	return true.  if not, return false.
 *	if the given shell is NULL, /etc/shells is outputted to stdout.
 */
static boolean get_shell_list (shell_name)
    char *shell_name;
{
    FILE *fp;
    boolean found;
    int len;

    found = false;
    fp = fopen ("/etc/shells", "r");
    if (! fp) {
	if (! shell_name) printf ("No known shells.\n");
	return true;
    }
    while (fgets (buf, sizeof (buf), fp) != NULL) {
	/* ignore comments */
	if (*buf == '#') continue;
	len = strlen (buf);
	/* strip the ending newline */
	if (buf[len - 1] == '\n') buf[len - 1] = 0;
	/* ignore lines that are too damn long */
	else continue;
	/* check or output the shell */
	if (shell_name) {
	    if (! strcmp (shell_name, buf)) {
		found = true;
		break;
	    }
	}
	else printf ("%s\n", buf);
    }
    fclose (fp);
    return found;
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
