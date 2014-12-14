/*
 *   chfn.c -- change your finger information
 *   (c) 1994 by salvatore valente <svalente@athena.mit.edu>
 *   (c) 2012 by Cody Maloney <cmaloney@theoreticalchaos.com>
 *
 *   this program is free software.  you can redistribute it and
 *   modify it under the terms of the gnu general public license.
 *   there is no warranty.
 *
 *   $Author: aebr $
 *   $Revision: 1.18 $
 *   $Date: 1998/06/11 22:30:11 $
 *
 * Updated Thu Oct 12 09:19:26 1995 by faith@cs.unc.edu with security
 * patches from Zefram <A.Main@dcs.warwick.ac.uk>
 *
 * Hacked by Peter Breitenlohner, peb@mppmu.mpg.de,
 * to remove trailing empty fields.  Oct 5, 96.
 *
 *  1999-02-22 Arkadiusz Miśkiewicz <misiek@pld.ORG.PL>
 *  - added Native Language Support
 */

#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <pwd.h>
#include <stdbool.h>
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
#include "setpwnam.h"
#include "strutils.h"
#include "xalloc.h"

#ifdef HAVE_LIBSELINUX
# include <selinux/selinux.h>
# include <selinux/av_permissions.h>
# include "selinux_utils.h"
#endif

#ifdef HAVE_LIBUSER
# include <libuser/user.h>
# include "libuser.h"
#elif CHFN_CHSH_PASSWORD
# include "auth.h"
#endif

struct finfo {
	struct passwd *pw;
	char *username;
	char *full_name;
	char *office;
	char *office_phone;
	char *home_phone;
	char *other;
};

struct chfn_control {
	unsigned int
		interactive:1;		/* whether to prompt for fields or not */
};

/* we do not accept gecos field sizes longer than MAX_FIELD_SIZE */
#define MAX_FIELD_SIZE		256

static void __attribute__((__noreturn__)) usage(FILE *fp)
{
	fputs(USAGE_HEADER, fp);
	fprintf(fp, _(" %s [options] [<username>]\n"), program_invocation_short_name);
	fputs(USAGE_OPTIONS, fp);
	fputs(_(" -f, --full-name <full-name>  real name\n"), fp);
	fputs(_(" -o, --office <office>        office number\n"), fp);
	fputs(_(" -p, --office-phone <phone>   office phone number\n"), fp);
	fputs(_(" -h, --home-phone <phone>     home phone number\n"), fp);
	fputs(USAGE_SEPARATOR, fp);
	fputs(_(" -u, --help     display this help and exit\n"), fp);
	fputs(_(" -v, --version  output version information and exit\n"), fp);
	fprintf(fp, USAGE_MAN_TAIL("chfn(1)"));
	exit(fp == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}

/*
 *  check_gecos_string () --
 *	check that the given gecos string is legal.  if it's not legal,
 *	output "msg" followed by a description of the problem, and return (-1).
 */
static int check_gecos_string(const char *msg, char *gecos)
{
	unsigned int i, c;
	const size_t len = strlen(gecos);

	if (MAX_FIELD_SIZE < len) {
		warnx(_("field %s is too long"), msg);
		return -1;
	}
	for (i = 0; i < len; i++) {
		c = gecos[i];
		if (c == ',' || c == ':' || c == '=' || c == '"' || c == '\n') {
			warnx(_("%s: '%c' is not allowed"), msg, c);
			return -1;
		}
		if (iscntrl(c)) {
			warnx(_("%s: control characters are not allowed"), msg);
			return -1;
		}
	}
	return 0;
}

/*
 *  parse_argv () --
 *	parse the command line arguments.
 *	returns true if no information beyond the username was given.
 */
static void parse_argv(struct chfn_control *ctl, int argc, char *argv[], struct finfo *pinfo)
{
	int index, c, status;

	static struct option long_options[] = {
		{"full-name", required_argument, 0, 'f'},
		{"office", required_argument, 0, 'o'},
		{"office-phone", required_argument, 0, 'p'},
		{"home-phone", required_argument, 0, 'h'},
		{"help", no_argument, 0, 'u'},
		{"version", no_argument, 0, 'v'},
		{NULL, no_argument, 0, '0'},
	};

	optind = 0;
	while (true) {
		c = getopt_long(argc, argv, "f:r:p:h:o:uv", long_options,
				&index);
		if (c == -1)
			break;
		/* version?  output version and exit. */
		if (c == 'v') {
			printf(UTIL_LINUX_VERSION);
			exit(EXIT_SUCCESS);
		}
		if (c == 'u')
			usage(stdout);
		/* all other options must have an argument. */
		if (!optarg)
			usage(stderr);
		/* ok, we were given an argument */
		ctl->interactive = 0;

		/* now store the argument */
		switch (c) {
		case 'f':
			pinfo->full_name = optarg;
			status = check_gecos_string(_("Name"), optarg);
			break;
		case 'o':
			pinfo->office = optarg;
			status = check_gecos_string(_("Office"), optarg);
			break;
		case 'p':
			pinfo->office_phone = optarg;
			status = check_gecos_string(_("Office Phone"), optarg);
			break;
		case 'h':
			pinfo->home_phone = optarg;
			status = check_gecos_string(_("Home Phone"), optarg);
			break;
		default:
			usage(stderr);
		}
		if (status != 0)
			exit(EXIT_FAILURE);
	}
	/* done parsing arguments.  check for a username. */
	if (optind < argc) {
		if (optind + 1 < argc)
			usage(stderr);
		pinfo->username = argv[optind];
	}
	return;
}

/*
 *  parse_passwd () --
 *	take a struct password and fill in the fields of the struct finfo.
 */
static void parse_passwd(struct passwd *pw, struct finfo *pinfo)
{
	char *gecos;

	if (!pw)
		return;
	pinfo->pw = pw;
	pinfo->username = pw->pw_name;
	/* use pw_gecos - we take a copy since PAM destroys the original */
	gecos = xstrdup(pw->pw_gecos);
	/* extract known fields */
	pinfo->full_name = strsep(&gecos, ",");
	pinfo->office = strsep(&gecos, ",");
	pinfo->office_phone = strsep(&gecos, ",");
	pinfo->home_phone = strsep(&gecos, ",");
	/*  extra fields contain site-specific information, and can
	 *  not be changed by this version of chfn.  */
	pinfo->other = strsep(&gecos, ",");
}

/*
 *  prompt () --
 *	ask the user for a given field and check that the string is legal.
 */
static char *prompt(const char *question, char *def_val)
{
	int len;
	char *ans;
	char buf[MAX_FIELD_SIZE + 2];

	if (!def_val)
		def_val = "";
	while (true) {
		printf("%s [%s]: ", question, def_val);
		__fpurge(stdin);
		if (fgets(buf, sizeof(buf), stdin) == NULL)
			errx(EXIT_FAILURE, _("Aborted."));
		ans = buf;
		/* remove white spaces from string end */
		ltrim_whitespace((unsigned char *) ans);
		len = rtrim_whitespace((unsigned char *) ans);
		if (len == 0)
			return NULL;
		if (!strcasecmp(ans, "none"))
			return "";
		if (check_gecos_string(question, ans) >= 0)
			break;
	}
	return xstrdup(ans);
}

/*
 *  ask_info () --
 *	prompt the user for the finger information and store it.
 */
static void ask_info(struct finfo *oldfp, struct finfo *newfp)
{
	newfp->full_name = prompt(_("Name"), oldfp->full_name);
	newfp->office = prompt(_("Office"), oldfp->office);
	newfp->office_phone = prompt(_("Office Phone"), oldfp->office_phone);
	newfp->home_phone = prompt(_("Home Phone"), oldfp->home_phone);
	printf("\n");
}

/*
 *  set_changed_data () --
 *	incorporate the new data into the old finger info.
 */
static int set_changed_data(struct finfo *oldfp, struct finfo *newfp)
{
	int changed = false;

	if (newfp->full_name) {
		oldfp->full_name = newfp->full_name;
		changed = true;
	}
	if (newfp->office) {
		oldfp->office = newfp->office;
		changed = true;
	}
	if (newfp->office_phone) {
		oldfp->office_phone = newfp->office_phone;
		changed = true;
	}
	if (newfp->home_phone) {
		oldfp->home_phone = newfp->home_phone;
		changed = true;
	}

	return changed;
}

/*
 *  save_new_data () --
 *	save the given finger info in /etc/passwd.
 *	return zero on success.
 */
static int save_new_data(struct finfo *pinfo)
{
	char *gecos;
	int len;

	/* null fields will confuse printf(). */
	if (!pinfo->full_name)
		pinfo->full_name = "";
	if (!pinfo->office)
		pinfo->office = "";
	if (!pinfo->office_phone)
		pinfo->office_phone = "";
	if (!pinfo->home_phone)
		pinfo->home_phone = "";
	if (!pinfo->other)
		pinfo->other = "";

	/* create the new gecos string */
	len = xasprintf(&gecos, "%s,%s,%s,%s,%s", pinfo->full_name, pinfo->office,
		  pinfo->office_phone, pinfo->home_phone, pinfo->other);

	/* remove trailing empty fields (but not subfields of pinfo->other) */
	if (!pinfo->other[0]) {
		while (len > 0 && gecos[len - 1] == ',')
			len--;
		gecos[len] = 0;
	}

#ifdef HAVE_LIBUSER
	if (set_value_libuser("chfn", pinfo->pw->pw_name, pinfo->pw->pw_uid,
			LU_GECOS, gecos) < 0) {
#else /* HAVE_LIBUSER */
	/* write the new struct passwd to the passwd file. */
	pinfo->pw->pw_gecos = gecos;
	if (setpwnam(pinfo->pw) < 0) {
		warn("setpwnam failed");
#endif
		printf(_
		       ("Finger information *NOT* changed.  Try again later.\n"));
		return -1;
	}
	free(gecos);
	printf(_("Finger information changed.\n"));
	return 0;
}

int main(int argc, char **argv)
{
	uid_t uid;
	struct finfo oldf, newf;
	struct chfn_control ctl = {
		.interactive = 1
	};

	sanitize_env();
	setlocale(LC_ALL, "");	/* both for messages and for iscntrl() below */
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	/*
	 *  "oldf" contains the users original finger information.
	 *  "newf" contains the changed finger information, and contains NULL
	 *         in fields that haven't been changed.
	 *  in the end, "newf" is folded into "oldf".
	 *
	 *  the reason the new finger information is not put _immediately_
	 *  into "oldf" is that on the command line, new finger information
	 *  can be specified before we know what user the information is
	 *  being specified for.
	 */
	uid = getuid();
	memset(&oldf, 0, sizeof(oldf));
	memset(&newf, 0, sizeof(newf));

	parse_argv(&ctl, argc, argv, &newf);
	if (!newf.username) {
		parse_passwd(getpwuid(uid), &oldf);
		if (!oldf.username)
			errx(EXIT_FAILURE, _("you (user %d) don't exist."),
			     uid);
	} else {
		parse_passwd(getpwnam(newf.username), &oldf);
		if (!oldf.username)
			errx(EXIT_FAILURE, _("user \"%s\" does not exist."),
			     newf.username);
	}

#ifndef HAVE_LIBUSER
	if (!(is_local(oldf.username)))
		errx(EXIT_FAILURE, _("can only change local entries"));
#endif

#ifdef HAVE_LIBSELINUX
	if (is_selinux_enabled() > 0) {
		if (uid == 0) {
			if (checkAccess(oldf.username, PASSWD__CHFN) != 0) {
				security_context_t user_context;
				if (getprevcon(&user_context) < 0)
					user_context = NULL;
				errx(EXIT_FAILURE,
				     _("%s is not authorized to change "
				       "the finger info of %s"),
				     user_context ? : _("Unknown user context"),
				     oldf.username);
			}
		}
		if (setupDefaultContext(_PATH_PASSWD))
			errx(EXIT_FAILURE,
			     _("can't set default context for %s"), _PATH_PASSWD);
	}
#endif

#ifdef HAVE_LIBUSER
	/* If we're setuid and not really root, disallow the password change. */
	if (geteuid() != getuid() && uid != oldf.pw->pw_uid) {
#else
	if (uid != 0 && uid != oldf.pw->pw_uid) {
#endif
		errno = EACCES;
		err(EXIT_FAILURE, _("running UID doesn't match UID of user we're "
		      "altering, change denied"));
	}

	printf(_("Changing finger information for %s.\n"), oldf.username);

#if !defined(HAVE_LIBUSER) && defined(CHFN_CHSH_PASSWORD)
	if(!auth_pam("chfn", uid, oldf.username)) {
		return EXIT_FAILURE;
	}
#endif

	if (ctl.interactive)
		ask_info(&oldf, &newf);

	if (!set_changed_data(&oldf, &newf)) {
		printf(_("Finger information not changed.\n"));
		return EXIT_SUCCESS;
	}

	return save_new_data(&oldf) == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
