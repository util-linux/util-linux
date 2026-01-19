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
#include "cctype.h"
#include "env.h"
#include "closestream.h"
#include "islocal.h"
#include "nls.h"
#include "pathnames.h"
#include "setpwnam.h"
#include "strutils.h"
#include "xalloc.h"
#include "logindefs.h"

#include "ch-common.h"
#include "pwdutils.h"

#ifdef HAVE_LIBSELINUX
# include <selinux/selinux.h>
# include "selinux-utils.h"
#endif

#ifdef HAVE_LIBUSER
# include <libuser/user.h>
# include "libuser.h"
#elif CHFN_CHSH_PASSWORD
# include "auth.h"
#endif

/* we do not accept gecos field sizes longer than MAX_FIELD_SIZE */
#define MAX_FIELD_SIZE		256

enum {
	GECOS_FULL_NAME,
	GECOS_OFFICE,
	GECOS_OFFICE_PHONE,
	GECOS_HOME_PHONE,
	GECOS_OTHER		/* GECOS field "Other" is currently not supported */
};

struct gecos_field {
	char 	*name;		/* GECOS field name */
	char 	*current;	/* currently defined GECOS field data */
	char 	*new;		/* new GECOS field data */
	bool 	allowed;	/* change allowed according to /etc/login.defs (CHFN_RESTRICT) */
};

/* global structure to store GECOS fields (current,new) with metadata */
static struct gecos_field gecos_fields[] = {
	[GECOS_FULL_NAME] = { N_("Name"), NULL, NULL, false },
	[GECOS_OFFICE] = { N_("Office"), NULL, NULL, false },
	[GECOS_OFFICE_PHONE] = { N_("Office Phone"), NULL, NULL, false },
	[GECOS_HOME_PHONE] = { N_("Home Phone"), NULL, NULL, false },
	[GECOS_OTHER] = { N_("Other"), NULL, NULL, false },
};

struct chfn_control {
	struct passwd *pw;
	char *username;

	bool 	changed,	/* is change requested */
		interactive,	/* whether to prompt for fields or not */
		restricted;	/* the program is running as a non-root user with setuid */
};

static void __attribute__((__noreturn__)) usage(void)
{
	FILE *fp = stdout;
	fputs(USAGE_HEADER, fp);
	fprintf(fp, _(" %s [options] [<username>|<UID>]\n"), program_invocation_short_name);

	fputs(USAGE_SEPARATOR, fp);
	fputs(_("Change your finger information.\n"), fp);

	fputs(USAGE_OPTIONS, fp);
	fputs(_(" -f, --full-name <full-name>  real name\n"), fp);
	fputs(_(" -o, --office <office>        office number\n"), fp);
	fputs(_(" -p, --office-phone <phone>   office phone number\n"), fp);
	fputs(_(" -h, --home-phone <phone>     home phone number\n"), fp);
	fputs(USAGE_SEPARATOR, fp);
	printf( " -u, --help                   %s\n", USAGE_OPTSTR_HELP);
	printf( " -V, --version                %s\n", USAGE_OPTSTR_VERSION);
	printf(USAGE_MAN_TAIL("chfn(1)"));
	exit(EXIT_SUCCESS);
}

/*
 *  check_gecos_string () --
 *	check that the given gecos string is legal.  if it's not legal,
 *	output "msg" followed by a description of the problem, and return (-1).
 */
static int check_gecos_string(const char *msg, char *gecos)
{
	const size_t len = strlen(gecos);

	if (MAX_FIELD_SIZE < len) {
		warnx(_("field '%s' is too long"), _(msg));
		return -1;
	}
	if (illegal_passwd_chars(gecos)) {
		warnx(_("%s: has illegal characters"), gecos);
		return -1;
	}
	return 0;
}

/*
 *  parse_argv () --
 *	parse the command line arguments.
 *	returns true if no information beyond the username was given.
 */
static void parse_argv(struct chfn_control *ctl, int argc, char **argv)
{
	int index, c, status = 0;
	static const struct option long_options[] = {
		{ "full-name",    required_argument, NULL, 'f' },
		{ "office",       required_argument, NULL, 'o' },
		{ "office-phone", required_argument, NULL, 'p' },
		{ "home-phone",   required_argument, NULL, 'h' },
		{ "help",         no_argument,       NULL, 'u' },
		{ "version",      no_argument,       NULL, 'V' },
		{ NULL, 0, NULL, 0 },
	};

	while ((c = getopt_long(argc, argv, "f:r:p:h:o:uvV", long_options,
				&index)) != -1) {
		switch (c) {
		case 'f':
			if (!gecos_fields[GECOS_FULL_NAME].allowed)
				errx(EXIT_FAILURE, _("login.defs forbids setting %s"),
							_(gecos_fields[GECOS_FULL_NAME].name));
			status += check_gecos_string(gecos_fields[GECOS_FULL_NAME].name, optarg);
			gecos_fields[GECOS_FULL_NAME].new = optarg;
			break;
		case 'o':
			if (!gecos_fields[GECOS_OFFICE].allowed)
				errx(EXIT_FAILURE, _("login.defs forbids setting %s"),
							_(gecos_fields[GECOS_OFFICE].name));
			status += check_gecos_string(gecos_fields[GECOS_OFFICE].name, optarg);
			gecos_fields[GECOS_OFFICE].new = optarg;
			break;
		case 'p':
			if (!gecos_fields[GECOS_OFFICE_PHONE].allowed)
				errx(EXIT_FAILURE, _("login.defs forbids setting %s"),
							_(gecos_fields[GECOS_OFFICE_PHONE].name));
			status += check_gecos_string(gecos_fields[GECOS_OFFICE_PHONE].name, optarg);
			gecos_fields[GECOS_OFFICE_PHONE].new = optarg;
			break;
		case 'h':
			if (!gecos_fields[GECOS_HOME_PHONE].allowed)
				errx(EXIT_FAILURE, _("login.defs forbids setting %s"),
							_(gecos_fields[GECOS_HOME_PHONE].name));
			status += check_gecos_string(gecos_fields[GECOS_HOME_PHONE].name, optarg);
			gecos_fields[GECOS_HOME_PHONE].new = optarg;
			break;
		case 'v': /* deprecated */
		case 'V':
			print_version(EXIT_SUCCESS);
		case 'u':
			usage();
		default:
			errtryhelp(EXIT_FAILURE);
		}
		ctl->changed = true;
		ctl->interactive = false;
	}
	if (status != 0)
		exit(EXIT_FAILURE);
	/* done parsing arguments.  check for a username. */
	if (optind < argc) {
		if (optind + 1 < argc) {
			warnx(_("cannot handle multiple usernames or UIDs"));
			errtryhelp(EXIT_FAILURE);
		}
		ctl->username = argv[optind];
	}
}

/*
 *  parse_passwd () --
 *	take a struct password and fill in the fields of the struct finfo.
 */
static void parse_passwd(struct chfn_control *ctl)
{
	char *gecos;

	if (!ctl->pw)
		return;
	/* use pw_gecos - we take a copy since PAM destroys the original */
	gecos = xstrdup(ctl->pw->pw_gecos);
	/* extract known fields */
	gecos_fields[GECOS_FULL_NAME].current = strsep(&gecos, ",");
	gecos_fields[GECOS_OFFICE].current = strsep(&gecos, ",");
	gecos_fields[GECOS_OFFICE_PHONE].current = strsep(&gecos, ",");
	gecos_fields[GECOS_HOME_PHONE].current = strsep(&gecos, ",");
	/*  extra fields contain site-specific information, and can
	 *  not be changed by this version of chfn.  */
	gecos_fields[GECOS_OTHER].current = strsep(&gecos, ",");
}

/*
 *  ask_new_field () --
 *	ask the user for a given field and check that the string is legal.
 */
static char *ask_new_field(struct chfn_control *ctl, const char *question,
			   char *def_val)
{
	int len;
	char *buf = NULL; /* leave initialized to NULL or getline segfaults */
	size_t dummy = 0;

	if (!def_val)
		def_val = "";

	while (true) {
		printf("%s [%s]:", question, def_val);
		__fpurge(stdin);

		putchar(' ');
		fflush(stdout);

		if (getline(&buf, &dummy, stdin) < 0)
			errx(EXIT_FAILURE, _("Aborted."));

		/* remove whitespace from string start and end */
		ltrim_whitespace((unsigned char *) buf);
		len = rtrim_whitespace((unsigned char *) buf);
		if (len == 0) {
			free(buf);
			return xstrdup(def_val);
		}
		if (c_strcasecmp(buf, "none") == 0) {
			free(buf);
			ctl->changed = true;
			return xstrdup("");
		}
		if (check_gecos_string(question, buf) >= 0)
			break;
	}
	ctl->changed = true;
	return buf;
}

/*
 *  get_login_defs()
 *	find /etc/login.defs CHFN_RESTRICT and save restrictions to run time
 */
static void get_login_defs(struct chfn_control *ctl)
{
	const char *s;
	int invalid = 0;

	if (!ctl->restricted) {
		for (size_t i = 0; i < ARRAY_SIZE(gecos_fields); i++) {
			/* allow the change of all fields */
			gecos_fields[i].allowed = true;
		}
		return;
	}

	s = getlogindefs_str("CHFN_RESTRICT", "");
	if (strcmp(s, "yes") == 0) {
		gecos_fields[GECOS_OFFICE].allowed = true;
		gecos_fields[GECOS_OFFICE_PHONE].allowed = true;
		gecos_fields[GECOS_HOME_PHONE].allowed = true;
		return;
	}

	if (strcmp(s, "no") == 0) {
		for (size_t i = 0; i < ARRAY_SIZE(gecos_fields); i++) {
			/* disallow the change of all fields */
			gecos_fields[i].allowed = false;
		}
	}

	for (size_t i = 0; s[i]; i++) {
		switch (s[i]) {
		case 'f':
			gecos_fields[GECOS_FULL_NAME].allowed = true;
			break;
		case 'r':
			gecos_fields[GECOS_OFFICE].allowed = true;
			break;
		case 'w':
			gecos_fields[GECOS_OFFICE_PHONE].allowed = true;
			break;
		case 'h':
			gecos_fields[GECOS_HOME_PHONE].allowed = true;
			break;
		default:
			invalid = 1;
		}
	}
	if (invalid)
		warnx(_("%s: CHFN_RESTRICT has unexpected value: %s"), _PATH_LOGINDEFS, s);

	if (!gecos_fields[GECOS_FULL_NAME].allowed
			&& !gecos_fields[GECOS_OFFICE].allowed
			&& !gecos_fields[GECOS_OFFICE_PHONE].allowed
			&& !gecos_fields[GECOS_HOME_PHONE].allowed)
		errx(EXIT_FAILURE, _("%s: CHFN_RESTRICT does not allow any changes"), _PATH_LOGINDEFS);
}

/*
 *  ask_info () --
 *	prompt the user for the finger information and store it.
 */
static void ask_info(struct chfn_control *ctl)
{
	/* GECOS_OTHER is currently not supported */
	for (size_t i = 0; i < GECOS_OTHER; i++) {
		struct gecos_field *gf = &gecos_fields[i];

		if (gf->allowed) {
			gf->new = ask_new_field(ctl, gf->name, gf->current);
		}
	}
	putchar('\n');
}

/*
 *  find_field () --
 *	find field value in non-interactive mode; can be new, old, or blank
 */
static char *find_field(char *nf, char *of)
{
	if (nf)
		return nf;
	if (of)
		return of;
	return xstrdup("");
}

/*
 *  add_missing () --
 *	add not supplied field values when in non-interactive mode
 */
static void add_missing(void)
{
	for (size_t i = 0; i < ARRAY_SIZE(gecos_fields); i++) {
		struct gecos_field *gf = &gecos_fields[i];
		gf->new = find_field(gf->new, gf->current);
	}
	printf("\n");
}

/*
 *  save_new_data () --
 *	save the given finger info in /etc/passwd.
 *	return zero on success.
 */
static int save_new_data(struct chfn_control *ctl)
{
	char *gecos;
	int len;

	/* create the new gecos string */
	len = xasprintf(&gecos, "%s,%s,%s,%s,%s",
			gecos_fields[GECOS_FULL_NAME].new,
			gecos_fields[GECOS_OFFICE].new,
			gecos_fields[GECOS_OFFICE_PHONE].new,
			gecos_fields[GECOS_HOME_PHONE].new,
			gecos_fields[GECOS_OTHER].new );

	/* remove trailing empty fields (but not subfields of ctl->newf.other) */
	if (!gecos_fields[GECOS_OTHER].new || !*gecos_fields[GECOS_OTHER].new) {
		while (len > 0 && gecos[len - 1] == ',')
			len--;
		gecos[len] = '\0';
	}

#ifdef HAVE_LIBUSER
	if (set_value_libuser("chfn", ctl->username, ctl->pw->pw_uid,
			LU_GECOS, gecos) < 0) {
#else /* HAVE_LIBUSER */
	/* write the new struct passwd to the passwd file. */
	ctl->pw->pw_gecos = gecos;
	if (setpwnam(ctl->pw, ".chfn") < 0) {
		warn("setpwnam failed");
#endif
		printf(_
		       ("Finger information *NOT* changed. Try again later.\n"));
		free(gecos);
		return -1;
	}
	free(gecos);
	printf(_("Finger information changed.\n"));
	return 0;
}

int main(int argc, char **argv)
{
	uid_t uid;
	struct chfn_control ctl = {
		.interactive = true,
		.restricted = true
	};

	sanitize_env();
	setlocale(LC_ALL, "");	/* both for messages and for iscntrl() below */
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	close_stdout_atexit();

	uid = getuid();

	if (!is_privileged_execution() && uid == 0)
		ctl.restricted = false;

	/* check /etc/login.defs CHFN_RESTRICT */
	get_login_defs(&ctl);

	parse_argv(&ctl, argc, argv);
	if (!ctl.username) {
		ctl.pw = getpwuid(uid);
		if (!ctl.pw)
			errx(EXIT_FAILURE, _("your user %d does not exist"),
			     uid);
	} else {
		ctl.pw = ul_getuserpw_str(ctl.username);
		if (!ctl.pw)
			errx(EXIT_FAILURE, _("user \"%s\" does not exist"),
			     ctl.username);
	}
	ctl.username = ctl.pw->pw_name;
	parse_passwd(&ctl);
#ifndef HAVE_LIBUSER
	if (!(is_local(ctl.username)))
		errx(EXIT_FAILURE, _("can only change local entries"));
#endif

#ifdef HAVE_LIBSELINUX
	if (is_selinux_enabled() > 0) {
		char *user_cxt = NULL;

		if (uid == 0 && !ul_selinux_has_access("passwd", "chfn", &user_cxt))
			errx(EXIT_FAILURE,
			     _("%s is not authorized to change "
			       "the finger info of %s"),
			     user_cxt ? : _("Unknown user context"),
			     ctl.username);

		if (ul_setfscreatecon_from_file(_PATH_PASSWD) != 0)
			errx(EXIT_FAILURE,
			     _("can't set default context for %s"), _PATH_PASSWD);
	}
#endif

#ifdef HAVE_LIBUSER
	/* If we're setuid and not really root, disallow the password change. */
	if (is_privileged_execution() && uid != ctl.pw->pw_uid) {
#else
	if (uid != 0 && uid != ctl.pw->pw_uid) {
#endif
		errno = EACCES;
		err(EXIT_FAILURE, _("running UID doesn't match UID of the user you are "
		      "attempting to alter, change denied"));
	}

	printf(_("Changing finger information for %s.\n"), ctl.username);

#if !defined(HAVE_LIBUSER) && defined(CHFN_CHSH_PASSWORD)
	if (!auth_pam("chfn", uid, ctl.username)) {
		return EXIT_FAILURE;
	}
#endif

	if (ctl.interactive)
		ask_info(&ctl);

	add_missing();

	if (!ctl.changed) {
		printf(_("Finger information not changed.\n"));
		return EXIT_SUCCESS;
	}

	return save_new_data(&ctl) == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
