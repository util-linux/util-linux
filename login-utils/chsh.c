/*
 *   chsh.c -- change your login shell
 *   (c) 1994 by salvatore valente <svalente@athena.mit.edu>
 *
 *   this program is free software.  you can redistribute it and
 *   modify it under the terms of the gnu general public license.
 *   there is no warranty.
 *
 *   faith
 *   1.1.1.1
 *   1995/02/22 19:09:23
 *
 */

#define _POSIX_SOURCE 1

#include <sys/types.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <pwd.h>
#include <errno.h>
#include <ctype.h>
#include <getopt.h>

#undef P
#if __STDC__
#define P(foo) foo
#else
#define P(foo) ()
#endif

typedef unsigned char boolean;
#define false 0
#define true 1

static char *version_string = "chsh 0.9 beta";
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
    char *cp, *shell;
    uid_t uid;
    struct sinfo info;
    struct passwd *pw;
    extern int errno;

    /* whoami is the program name for error messages */
    whoami = argv[0];
    if (! whoami) whoami = "chsh";
    for (cp = whoami; *cp; cp++)
	if (*cp == '/') whoami = cp + 1;

    umask (022);

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

    /* reality check */
    if (uid != 0 && uid != pw->pw_uid) {
	errno = EACCES;
	perror (whoami);
	return (-1);
    }
    
    shell = info.shell;
    if (! shell) {
	printf ("Changing shell for %s.\n", pw->pw_name);
	shell = prompt ("New shell", pw->pw_shell);
	if (! shell) return 0;
    }
    if (check_shell (shell) < 0) return (-1);

    if (! strcmp (pw->pw_shell, shell)) {
	printf ("Shell not changed.\n");
	return 0;
    }
    pw->pw_shell = shell;
    if (setpwnam (pw) < 0) {
	perror ("setpwnam");
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
	    printf ("%s\n", version_string);
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
    strcpy (cp, buf);
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
    if (! get_shell_list (shell))
	printf ("warning: \"%s\" is not listed as a valid shell.\n", shell);
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
