/*
 *   chsh.c -- change your login shell
 *   (c) 1994 by salvatore valente <svalente@athena.mit.edu>
 *   (c) 2012 by Cody Maloney <cmaloney@theoreticalchaos.com>
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
 *   1999-02-22 Arkadiusz Miśkiewicz <misiek@pld.ORG.PL>
 *   - added Native Language Support
 */

#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "c.h"
#include "env.h"
#include "closestream.h"
#include "islocal.h"
#include "nls.h"
#include "pathnames.h"
#include "setpwnam.h"
#include "strutils.h"
#include "xalloc.h"

#include "ch-common.h"

#ifdef HAVE_LIBSELINUX
# include <selinux/selinux.h>
# include "selinux_utils.h"
#endif


#ifdef HAVE_LIBUSER
# include <libuser/user.h>
# include "libuser.h"
#elif CHFN_CHSH_PASSWORD
# include "auth.h"
#endif

#ifdef HAVE_LIBREADLINE
# define _FUNCTION_DEF
# include <readline/readline.h>
#endif

struct sinfo {
	char *username;
	char *shell;
};


static void __attribute__((__noreturn__)) usage (FILE *fp)
{
	fputs(USAGE_HEADER, fp);
	fprintf(fp, _(" %s [options] [<username>]\n"), program_invocation_short_name);

	fputs(USAGE_SEPARATOR, fp);
	fputs(_("Change your login shell.\n"), fp);

	fputs(USAGE_OPTIONS, fp);
	fputs(_(" -s, --shell <shell>  specify login shell\n"), fp);
	fputs(_(" -l, --list-shells    print list of shells and exit\n"), fp);
	fputs(USAGE_SEPARATOR, fp);
	fputs(_(" -u, --help     display this help and exit\n"), fp);
	fputs(_(" -v, --version  output version information and exit\n"), fp);
	fprintf(fp, USAGE_MAN_TAIL("chsh(1)"));
	exit(fp == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}

/*
 *  get_shell_list () -- if the given shell appears in /etc/shells,
 *	return true.  if not, return false.
 *	if the given shell is NULL, /etc/shells is outputted to stdout.
 */
static int get_shell_list(const char *shell_name)
{
	FILE *fp;
	int found = 0;
	char *buf = NULL;
	size_t sz = 0;
	ssize_t len;

	fp = fopen(_PATH_SHELLS, "r");
	if (!fp) {
		if (!shell_name)
			warnx(_("No known shells."));
		return 0;
	}
	while ((len = getline(&buf, &sz, fp)) != -1) {
		/* ignore comments and blank lines */
		if (*buf == '#' || len < 2)
			continue;
		/* strip the ending newline */
		if (buf[len - 1] == '\n')
			buf[len - 1] = 0;
		/* check or output the shell */
		if (shell_name) {
			if (!strcmp(shell_name, buf)) {
				found = 1;
				break;
			}
		} else
			printf("%s\n", buf);
	}
	fclose(fp);
	free(buf);
	return found;
}

/*
 *  parse_argv () --
 *	parse the command line arguments, and fill in "pinfo" with any
 *	information from the command line.
 */
static void parse_argv(int argc, char **argv, struct sinfo *pinfo)
{
	static const struct option long_options[] = {
		{"shell",       required_argument, NULL, 's'},
		{"list-shells", no_argument,       NULL, 'l'},
		{"help",        no_argument,       NULL, 'h'},
		{"version",     no_argument,       NULL, 'v'},
		{NULL, 0, NULL, 0},
	};
	int c;

	while ((c = getopt_long(argc, argv, "s:lhuv", long_options, NULL)) != -1) {
		switch (c) {
		case 'v':
			printf(UTIL_LINUX_VERSION);
			exit(EXIT_SUCCESS);
		case 'u': /* deprecated */
		case 'h':
			usage(stdout);
		case 'l':
			get_shell_list(NULL);
			exit(EXIT_SUCCESS);
		case 's':
			if (!optarg)
				usage(stderr);
			pinfo->shell = optarg;
			break;
		default:
			errtryhelp(EXIT_FAILURE);
		}
	}
	/* done parsing arguments.  check for a username. */
	if (optind < argc) {
		if (optind + 1 < argc)
			usage(stderr);
		pinfo->username = argv[optind];
	}
}

/*
 *  ask_new_shell () --
 *	ask the user for a shell and return it.
 */
static char *ask_new_shell(char *question, char *oldshell)
{
	int len;
	char *ans = NULL;
#ifndef HAVE_LIBREADLINE
	size_t dummy = 0;
#endif

	if (!oldshell)
		oldshell = "";
	printf("%s [%s]: ", question, oldshell);
#ifdef HAVE_LIBREADLINE
	if ((ans = readline(NULL)) == NULL)
#else
	if (getline(&ans, &dummy, stdin) < 0)
#endif
		return NULL;
	/* remove the newline at the end of ans. */
	ltrim_whitespace((unsigned char *) ans);
	len = rtrim_whitespace((unsigned char *) ans);
	if (len == 0)
		return NULL;
	return ans;
}

/*
 *  check_shell () -- if the shell is completely invalid, print
 *	an error and exit.
 */
static void check_shell(const char *shell)
{
	if (*shell != '/')
		errx(EXIT_FAILURE, _("shell must be a full path name"));
	if (access(shell, F_OK) < 0)
		errx(EXIT_FAILURE, _("\"%s\" does not exist"), shell);
	if (access(shell, X_OK) < 0)
		errx(EXIT_FAILURE, _("\"%s\" is not executable"), shell);
	if (illegal_passwd_chars(shell))
		errx(EXIT_FAILURE, _("%s: has illegal characters"), shell);
	if (!get_shell_list(shell)) {
#ifdef ONLY_LISTED_SHELLS
		if (!getuid())
			warnx(_("Warning: \"%s\" is not listed in %s."), shell,
			      _PATH_SHELLS);
		else
			errx(EXIT_FAILURE,
			     _("\"%s\" is not listed in %s.\n"
			       "Use %s -l to see list."), shell, _PATH_SHELLS,
			     program_invocation_short_name);
#else
		warnx(_("\"%s\" is not listed in %s.\n"
			"Use %s -l to see list."), shell, _PATH_SHELLS,
		       program_invocation_short_name);
#endif
	}
}

int main(int argc, char **argv)
{
	char *oldshell;
	int nullshell = 0;
	const uid_t uid = getuid();
	struct sinfo info = { NULL };
	struct passwd *pw;

	sanitize_env();
	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	parse_argv(argc, argv, &info);
	if (!info.username) {
		pw = getpwuid(uid);
		if (!pw)
			errx(EXIT_FAILURE, _("you (user %d) don't exist."),
			     uid);
	} else {
		pw = getpwnam(info.username);
		if (!pw)
			errx(EXIT_FAILURE, _("user \"%s\" does not exist."),
			     info.username);
	}

#ifndef HAVE_LIBUSER
	if (!(is_local(pw->pw_name)))
		errx(EXIT_FAILURE, _("can only change local entries"));
#endif

#ifdef HAVE_LIBSELINUX
	if (is_selinux_enabled() > 0) {
		if (uid == 0) {
			access_vector_t av = get_access_vector("passwd", "chsh");

			if (selinux_check_passwd_access(av) != 0) {
				security_context_t user_context;
				if (getprevcon(&user_context) < 0)
					user_context =
					    (security_context_t) NULL;

				errx(EXIT_FAILURE,
				     _("%s is not authorized to change the shell of %s"),
				     user_context ? : _("Unknown user context"),
				     pw->pw_name);
			}
		}
		if (setupDefaultContext(_PATH_PASSWD) != 0)
			errx(EXIT_FAILURE,
			     _("can't set default context for %s"), _PATH_PASSWD);
	}
#endif

	oldshell = pw->pw_shell;
	if (oldshell == NULL || *oldshell == '\0') {
		oldshell = _PATH_BSHELL;	/* default */
		nullshell = 1;
	}

	/* reality check */
#ifdef HAVE_LIBUSER
	/* If we're setuid and not really root, disallow the password change. */
	if (geteuid() != getuid() && uid != pw->pw_uid) {
#else
	if (uid != 0 && uid != pw->pw_uid) {
#endif
		errno = EACCES;
		err(EXIT_FAILURE,
		    _("running UID doesn't match UID of user we're "
		      "altering, shell change denied"));
	}
	if (uid != 0 && !get_shell_list(oldshell)) {
		errno = EACCES;
		err(EXIT_FAILURE, _("your shell is not in %s, "
				    "shell change denied"), _PATH_SHELLS);
	}

	printf(_("Changing shell for %s.\n"), pw->pw_name);

#if !defined(HAVE_LIBUSER) && defined(CHFN_CHSH_PASSWORD)
	if (!auth_pam("chsh", uid, pw->pw_name)) {
		return EXIT_FAILURE;
	}
#endif
	if (!info.shell) {
		info.shell = ask_new_shell(_("New shell"), oldshell);
		if (!info.shell)
			return EXIT_SUCCESS;
	}

	check_shell(info.shell);

	if (!nullshell && strcmp(oldshell, info.shell) == 0)
		errx(EXIT_SUCCESS, _("Shell not changed."));

#ifdef HAVE_LIBUSER
	if (set_value_libuser("chsh", pw->pw_name, uid,
	    LU_LOGINSHELL, info.shell) < 0)
		errx(EXIT_FAILURE, _("Shell *NOT* changed.  Try again later."));
#else
	pw->pw_shell = info.shell;
	if (setpwnam(pw, ".chsh") < 0)
		err(EXIT_FAILURE, _("setpwnam failed\n"
			"Shell *NOT* changed.  Try again later."));
#endif

	printf(_("Shell changed.\n"));
	return EXIT_SUCCESS;
}
