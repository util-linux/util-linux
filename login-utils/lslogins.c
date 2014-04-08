/*
 * lslogins - List information about users on the system
 *
 * Copyright (C) 2014 Ondrej Oprala <ooprala@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/types.h>
#include <pwd.h>
#include <shadow.h>
#include <paths.h>
#include <time.h>

#include "c.h"
#include "nls.h"
#include "closestream.h"
#include "xalloc.h"
#include "strutils.h"
#include "optutils.h"

/*
 * column description
 */
struct lslogins_coldesc {
	const char *name;
	const char *help;

	unsigned int  is_abbr:1;	/* name is abbreviation */
};

/* the most uber of flags */
static int uberflag;

/* we use the value of outmode to determine
 * appropriate flags for the libsmartcols table
 * (e.g., a value of out_newline would imply a raw
 * table with the column separator set to '\n').
 */
static int outmode;
/*
 * output modes
 */
enum {
	out_colon = 0,
	out_export,
	out_newline,
	out_raw,
	out_nul,
};

struct lslogins_user {
	char *login;
	uid_t uid;
	char *group;
	gid_t gid;
	char *gecos;

	int nopasswd:1;

	char *sgroups;

	struct tm *pwd_ctime;
	struct tm *pwd_expir;

	struct tm *last_login;
	char * last_tty;
	char * last_hostname;

	struct tm *failed_login;
	struct tm *failed_tty;

	char *homedir;
	char *shell;
	char *pwd_status;
	char *hush_status;
};
/*
 * flags
 */
enum {
	F_EXPIR	= (1 << 0),
	F_DUP	= (1 << 1),
	F_EXPRT	= (1 << 2),
	F_MORE	= (1 << 3),
	F_NOPWD	= (1 << 4),
	F_SYSAC	= (1 << 5),
	F_USRAC	= (1 << 6),
	F_SORT	= (1 << 7),
	F_EXTRA	= (1 << 8),
	F_FAIL  = (1 << 9),
	F_LAST  = (1 << 10),
};

/*
 * IDs
 */
enum {
	COL_LOGIN = 0,
	COL_UID,
	COL_NOPASSWD,
	COL_PGRP,
	COL_PGID,
	COL_SGRPS,
	COL_HOME,
	COL_SHELL,
	COL_FULLNAME,
	COL_LAST_LOGIN,
	COL_LAST_TTY,
	COL_LAST_HOSTNAME,
	COL_FAILED_LOGIN,
	COL_FAILED_TTY,
	COL_HUSH_STATUS,
	COL_PWD_STATUS,
	COL_PWD_EXPIR,
	COL_PWD_CTIME,
	/*COL_PWD_CTIME_MAX,
	COL_PWD_CTIME_MIN,*/
};

static struct lslogins_coldesc coldescs[] =
{
	[COL_LOGIN]		= { "LOGIN",		N_("user/system login"), 1 },
	[COL_UID]		= { "UID",		N_("user UID") },
	[COL_NOPASSWD]		= { "HAS PASSWORD",	N_("account has a password?") },
	[COL_PGRP]		= { "GRP",		N_("primary group name") },
	[COL_PGID]		= { "GRP_GID",		N_("primary group GID") },
	[COL_SGRPS]		= { "SEC_GRPS",		N_("secondary group names and GIDs") },
	[COL_HOME]		= { "HOMEDIR",		N_("home directory") },
	[COL_SHELL]		= { "SHELL",		N_("login shell") },
	[COL_FULLNAME]		= { "FULLNAME",		N_("full user name") },
	[COL_LAST_LOGIN]	= { "LAST_LOGIN",	N_("date of last login") },
	[COL_LAST_TTY]		= { "LAST_TTY",		N_("last tty used") },
	[COL_LAST_HOSTNAME]	= { "LAST_HOSTNAME",	N_("hostname during the last session") },
	[COL_FAILED_LOGIN]	= { "FAILED_LOGIN",	N_("date of last failed login") },
	[COL_FAILED_TTY]	= { "FAILED_TTY",	N_("where did the login fail?") },
	[COL_HUSH_STATUS]	= { "HUSH_STATUS",	N_("User's hush settings") },
	[COL_PWD_STATUS]	= { "PWD_STATUS",	N_("password status - see the -x option description") },
	[COL_PWD_EXPIR]		= { "PWD_EXPIR",	N_("password expiration date") },
	[COL_PWD_CTIME]		= { "PWD_CHANGE",	N_("date of last password change") },
	/*[COL_PWD_CTIME_MAX]	= { "PWD UNTIL",	N_("max number of days a password may remain unchanged") },
	[COL_PWD_CTIME_MIN]	= { "PWD CAN CHANGE",	N_("number of days required between changes") },*/
};

static int
column_name_to_id(const char *name, size_t namesz)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(coldescs); i++) {
		const char *cn = coldescs[i].name;

		if (!strncasecmp(name, cn, namesz) && !*(cn + namesz))
			return i;
	}
	warnx(_("unknown column: %s"), name);
	return -1;
}

static void __attribute__((__noreturn__)) usage(FILE *out)
{
	size_t i;

	fputs(USAGE_HEADER, out);
	fprintf(out, _(" %s [options]\n"), program_invocation_short_name);

	fputs(USAGE_OPTIONS, out);
	fputs(_(" -a, --acc-expiration     Display data\n"), out);
	fputs(_(" -c, --colon-separate     Display data in a format similar to /etc/passwd\n"), out);
	fputs(_(" -d, --duplicates         Display users with duplicate UIDs\n"), out);
	fputs(_(" -e, --export             Display in an export-able output format\n"), out);
	fputs(_(" -f, --failed             Display data about the last users' failed logins\n"), out);
	fputs(_(" -g, --groups=<GROUPS>    Display users belonging to a group in GROUPS\n"), out);
	fputs(_(" -l, --logins=<LOGINS>    Display only users from LOGINS\n"), out);
	fputs(_(" --last                   Show info about the last login sessions\n"), out);
	fputs(_(" -m, --more               Display secondary groups as well\n"), out);
	fputs(_(" -n, --newline            Display each piece of information on a new line\n"), out);
	fputs(_(" -o, --output[=<LIST>]    Define the columns to output\n"), out);
	fputs(_(" -p, --no-password        Display users without a password\n"), out);
	fputs(_(" -r, --raw                Display the raw table\n"), out);
	fputs(_(" -s, --sys-accs[=<UID>]   Display system accounts\n"), out);
	fputs(_(" -t, --sort               Sort output by login instead of UID\n"), out);
	fputs(_(" -u, --user-accs[=<UID>]  Display user accounts\n"), out);
	fputs(_(" -x, --extra              Display extra information\n"), out);
	fputs(_(" -z, --print0             Delimit user entries with a nul character"), out);
	fputs(USAGE_SEPARATOR, out);
	fputs(USAGE_HELP, out);
	fputs(USAGE_VERSION, out);

	fprintf(out, _("\nAvailable columns:\n"));

	for (i = 0; i < ARRAY_SIZE(coldescs); i++)
		fprintf(out, " %14s  %s\n", coldescs[i].name, _(coldescs[i].help));

	fprintf(out, _("\nFor more details see lslogins(1).\n"));

	exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}

int main(int argc, char *argv[])
{
	int c;
	int columns[ARRAY_SIZE(coldescs)], ncolumns = 0;
	char *logins = NULL, *groups = NULL;

	/* long only options. */
	enum {
		OPT_LAST = CHAR_MAX + 1,
		OPT_VER,
	};
	static const struct option longopts[] = {
		{ "acc-expiration", no_argument,	0, 'a' },
		{ "colon",          no_argument,	0, 'c' },
		{ "duplicates",     no_argument,	0, 'd' },
		{ "export",         no_argument,	0, 'e' },
		{ "failed",         no_argument,	0, 'f' },
		{ "groups",         required_argument,	0, 'g' },
		{ "help",           no_argument,	0, 'h' },
		{ "logins",         required_argument,	0, 'l' },
		{ "more",           no_argument,	0, 'm' },
		{ "newline",        no_argument,	0, 'n' },
		{ "output",         optional_argument,	0, 'o' },
		{ "no-password",    no_argument,	0, 'p' },
		{ "last",           no_argument,	0, OPT_LAST },
		{ "raw",            no_argument,	0, 'r' },
		{ "sys-accs",   optional_argument,	0, 's' },
		{ "sort",           no_argument,	0, 't' },
		{ "user-accs",  optional_argument,	0, 'u' },
		{ "version",        no_argument,	0, OPT_VER },
		{ "extra",          no_argument,	0, 'x' },
		{ "print0",         no_argument,	0, 'z' },
		{ NULL,             0, 			0,  0  }
	};

	static const ul_excl_t excl[] = {	/* rows and cols in ASCII order */
		{ 'c','e','n','r','z' },
		{ 0 }
	};
	int excl_st[ARRAY_SIZE(excl)] = UL_EXCL_STATUS_INIT;

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	while ((c = getopt_long(argc, argv, "acdefg:hl:mno::prs::tu::xz",
				longopts, NULL)) != -1) {

		err_exclusive_options(c, longopts, excl, excl_st);

		switch (c) {
			case 'a':
				uberflag |= F_EXPIR;
				break;
			case 'c':
				outmode = out_colon;
				break;
			case 'd':
				uberflag |= F_DUP;
				break;
			case 'e':
				outmode = out_export;
				break;
			case 'f':
				uberflag |= F_FAIL;
				break;
			case 'g':
				groups = strdup(optarg);
				if (!groups)
					return EXIT_FAILURE;
				break;
			case 'h':
				usage(stdout);
			case 'l':
				logins = strdup(optarg);
				if (!logins)
					return EXIT_FAILURE;
				break;
			case 'm':
				uberflag |= F_MORE;
				break;
			case 'n':
				outmode = out_newline;
				break;
			case 'o':
				if (optarg) {
					if (*optarg == '=')
						optarg++;
					ncolumns = string_to_idarray(optarg,
							columns, ARRAY_SIZE(columns),
							column_name_to_id);
					if (ncolumns < 0)
						return EXIT_FAILURE;
				}
				break;
			case 'p':
				uberflag |= F_NOPWD;
				break;
			case 'r':
				outmode = out_raw;
				break;
			case OPT_LAST:
				uberflag |= F_LAST;
				break;
			case 's':
				uberflag |= F_SYSAC;
				break;
			case 't':
				uberflag |= F_SORT;
				break;
			case 'u':
				uberflag |= F_USRAC;
				break;
			case OPT_VER:
				printf(_("%s from %s\n"), program_invocation_short_name,
				       PACKAGE_STRING);
				return EXIT_SUCCESS;
			case 'x':
				uberflag |= F_EXTRA;
				break;
			case 'z':
				outmode = out_nul;
				break;
			default:
				usage(stderr);
		}
	}
	if (argc != optind)
		usage(stderr);

	if (!ncolumns) {
		columns[ncolumns++] = COL_LOGIN;
		columns[ncolumns++] = COL_UID;
		columns[ncolumns++] = COL_PGRP;
		columns[ncolumns++] = COL_PGID;
		columns[ncolumns++] = COL_FULLNAME;

		if (uberflag & F_NOPWD) {
			columns[ncolumns++] = COL_NOPASSWD;
		}
		if (uberflag & F_MORE) {
			columns[ncolumns++] = COL_SGRPS;
		}
		if (uberflag & F_EXPIR) {
			columns[ncolumns++] = COL_PWD_CTIME;
			columns[ncolumns++] = COL_PWD_EXPIR;
		}
		if (uberflag & F_LAST) {
			columns[ncolumns++] = COL_LAST_LOGIN;
			columns[ncolumns++] = COL_LAST_TTY;
			columns[ncolumns++] = COL_LAST_HOSTNAME;
		}
		if (uberflag & F_FAIL) {
			columns[ncolumns++] = COL_FAILED_LOGIN;
			columns[ncolumns++] = COL_FAILED_TTY;
		}
		if (uberflag & F_EXTRA) {
			columns[ncolumns++] = COL_HOME;
			columns[ncolumns++] = COL_SHELL;
			columns[ncolumns++] = COL_PWD_STATUS;
			columns[ncolumns++] = COL_HUSH_STATUS;
		/*	columns[ncolumns++] = COL_PWD_CTIME_MAX;
			columns[ncolumns++] = COL_PWD_CTIME_MIN; */
		}
	}
	return EXIT_SUCCESS;
}
