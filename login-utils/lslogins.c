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
#include <sys/stat.h>
#include <sys/syslog.h>
#include <pwd.h>
#include <grp.h>
#include <shadow.h>
#include <paths.h>
#include <time.h>
#include <utmp.h>
#include <signal.h>
#include <err.h>
#include <limits.h>

#include <search.h>

#include <libsmartcols.h>
#ifdef HAVE_LIBSELINUX
#include <selinux/selinux.h>
#endif

#include "c.h"
#include "nls.h"
#include "closestream.h"
#include "xalloc.h"
#include "list.h"
#include "strutils.h"
#include "optutils.h"
#include "pathnames.h"
#include "logindefs.h"
#include "readutmp.h"

/*
 * column description
 */
struct lslogins_coldesc {
	const char *name;
	const char *help;

	double whint;	/* width hint */
};

void *usertree;
int (*cmp_fn) (const void *a, const void *b);
/* the most uber of flags */
static int uberflag;

FILE *PWDFILE;
FILE *GRPFILE;
struct utmp *wtmp, *btmp;
size_t wtmp_size, btmp_size;
int want_wtmp, want_btmp;

struct libscols_table *tb;

char *PASSWD_PATH = _PATH_PASSWD;
#define UL_UID_MIN "1000"
#define UL_UID_MAX "60000"
#define UL_SYS_UID_MIN "201"
#define UL_SYS_UID_MAX "999"

uid_t UID_MIN;
uid_t UID_MAX;

uid_t SYS_UID_MIN;
uid_t SYS_UID_MAX;
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
	out_colon = 1,
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

	int nopasswd;
	int nologin;
	int locked;

	char *sgroups;

	char *pwd_ctime;
	char *pwd_expir;
	char *pwd_ctime_min;
	char *pwd_ctime_max;

	char *last_login;
	char *last_tty;
	char *last_hostname;

	char *failed_login;
	char *failed_tty;

#ifdef HAVE_LIBSELINUX
	security_context_t context;
#endif
	char *homedir;
	char *shell;
	char *pwd_status;
	int   hushed;

};
/*
 * flags
 */
enum {
	F_EXPIR	= (1 << 0),
	F_MORE	= (1 << 1),
	F_NOPWD	= (1 << 2),
	F_SYSAC	= (1 << 3),
	F_USRAC	= (1 << 4),
	F_SORT	= (1 << 5),
	F_EXTRA	= (1 << 6),
	F_FAIL  = (1 << 7),
	F_LAST  = (1 << 8),
	F_SELINUX = (1 << 9),
};

/*
 * IDs
 */
enum {
	COL_LOGIN = 0,
	COL_UID,
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
	COL_NOLOGIN,
	COL_LOCKED,
	COL_NOPASSWD,
	COL_PWD_EXPIR,
	COL_PWD_CTIME,
	COL_PWD_CTIME_MIN,
	COL_PWD_CTIME_MAX,
	COL_SELINUX,
};

static const char *const status[] = { "0", "1", "-" };
static struct lslogins_coldesc coldescs[] =
{
	[COL_LOGIN]		= { "LOGIN",		N_("user/system login"), 0.2 },
	[COL_UID]		= { "UID",		N_("user UID"), 0.05 },
	[COL_NOPASSWD]		= { "NOPASSWD",		N_("account has a password?"), 1 },
	[COL_NOLOGIN]		= { "NOLOGIN",		N_("account has a password?"), 1 },
	[COL_LOCKED]		= { "LOCKED",		N_("account has a password?"), 1 },
	[COL_PGRP]		= { "GRP",		N_("primary group name"), 0.2 },
	[COL_PGID]		= { "GID",		N_("primary group GID"), 0.05 },
	[COL_SGRPS]		= { "SEC_GRPS",		N_("secondary group names and GIDs"), 0.5 },
	[COL_HOME]		= { "HOMEDIR",		N_("home directory"), 0.3 },
	[COL_SHELL]		= { "SHELL",		N_("login shell"), 0.1 },
	[COL_FULLNAME]		= { "FULLNAME",		N_("full user name"), 0.3 },
	[COL_LAST_LOGIN]	= { "LAST_LOGIN",	N_("date of last login"), 24 },
	[COL_LAST_TTY]		= { "LAST_TTY",		N_("last tty used"), 0.05 },
	[COL_LAST_HOSTNAME]	= { "LAST_HOSTNAME",	N_("hostname during the last session"), 0.2},
	[COL_FAILED_LOGIN]	= { "FAILED_LOGIN",	N_("date of last failed login"), 24 },
	[COL_FAILED_TTY]	= { "FAILED_TTY",	N_("where did the login fail?"), 0.05 },
	[COL_HUSH_STATUS]	= { "HUSHED",		N_("User's hush settings"), 1 },
	[COL_PWD_EXPIR]		= { "PWD_EXPIR",	N_("password expiration date"), 24 },
	[COL_PWD_CTIME]		= { "PWD_CHANGE",	N_("date of last password change"), 24 },
	[COL_PWD_CTIME_MIN]	= { "PWD_MIN",	N_("number of days required between changes"), 24 },
	[COL_PWD_CTIME_MAX]	= { "PWD_MAX",	N_("max number of days a password may remain unchanged"), 24 },
	[COL_SELINUX]		= { "CONTEXT",	N_("the user's security context"), 0.4 },
};

int columns[ARRAY_SIZE(coldescs)];
int ncolumns;

static int
column_name_to_id(const char *name, size_t namesz)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(coldescs); i++) {
		const char *cn = coldescs[i].name;

		if (!strncasecmp(name, cn, namesz) && !*(cn + namesz)) {
			if (i <= COL_LAST_HOSTNAME && i >= COL_LAST_LOGIN)
				want_wtmp = 1;
			if (i == COL_FAILED_TTY && i >= COL_FAILED_LOGIN)
				want_btmp = 1;
			return i;
		}
	}
	warnx(_("unknown column: %s"), name);
	return -1;
}

static char *make_time(struct tm *tm)
{
	char *t, *s;

	if (!tm)
		return NULL;

	s = asctime(tm);
	if (!s)
		return NULL;

	if (*(t = s + strlen(s) - 1) == '\n')
		*t = '\0';
	return xstrdup(s);
}
static void __attribute__((__noreturn__)) usage(FILE *out)
{
	size_t i;

	fputs(USAGE_HEADER, out);
	fprintf(out, _(" %s [options]\n"), program_invocation_short_name);

	fputs(USAGE_OPTIONS, out);
	fputs(_(" -a, --acc-expiration     Display data\n"), out);
	fputs(_(" -c, --colon-separate     Display data in a format similar to /etc/passwd\n"), out);
	fputs(_(" -e, --export             Display in an export-able output format\n"), out);
	fputs(_(" -f, --failed             Display data about the last users' failed logins\n"), out);
	fputs(_(" -g, --groups=<GROUPS>    Display users belonging to a group in GROUPS\n"), out);
	fputs(_(" -l, --logins=<LOGINS>    Display only users from LOGINS\n"), out);
	fputs(_(" --last                   Show info about the users' last login sessions\n"), out);
	fputs(_(" -m, --supp-groups         Display supplementary groups as well\n"), out);
	fputs(_(" -n, --newline            Display each piece of information on a new line\n"), out);
	fputs(_(" -o, --output[=<LIST>]    Define the columns to output\n"), out);
	fputs(_(" -r, --raw                Display the raw table\n"), out);
	fputs(_(" -s, --sys-accs           Display system accounts\n"), out);
	fputs(_(" -t, --sort               Sort output by login instead of UID\n"), out);
	fputs(_(" -u, --user-accs          Display user accounts\n"), out);
	fputs(_(" -x, --extra              Display extra information\n"), out);
	fputs(_(" -z, --print0             Delimit user entries with a nul character\n"), out);
	fputs(_(" -Z, --context            Display the users' security context\n"), out);
	fputs(_("--path-wtmp               Set an alternate path for wtmp\n"), out);
	fputs(_("--path-btmp               Set an alternate path for btmp\n"), out);
	fputs(_("--path-passwd             Set an alternate path for passwd\n"), out);
	fputs(_("--path-shadow             Set an alternate path for shadow\n"), out);
	fputs(USAGE_SEPARATOR, out);
	fputs(USAGE_HELP, out);
	fputs(USAGE_VERSION, out);

	fprintf(out, _("\nAvailable columns:\n"));

	for (i = 0; i < ARRAY_SIZE(coldescs); i++)
		fprintf(out, " %14s  %s\n", coldescs[i].name, _(coldescs[i].help));

	fprintf(out, _("\nFor more details see lslogins(1).\n"));

	exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}
struct lslogins_sgroups {
	char *gid;
	char *uname;
	struct lslogins_sgroups *next;
};

static char *uidtostr(uid_t uid)
{
	char *str_uid = NULL;
	return (0 > xasprintf(&str_uid, "%u", uid)) ? NULL : str_uid;
}

static char *gidtostr(gid_t gid)
{
	char *str_gid = NULL;
	return (0 > xasprintf(&str_gid, "%u", gid)) ? NULL : str_gid;
}

static struct lslogins_sgroups *build_sgroups_list(int len, gid_t *list, int *slen)
{
	int n = 0;
	struct lslogins_sgroups *sgrps, *retgrps;
	struct group grpbuf, *grp;
	size_t buflen = 1024;
	char *buf = NULL;

	*slen = 0;

	if (!len || !list)
		return NULL;
	buf = xcalloc(1, sizeof(char) * buflen);

	retgrps = sgrps = calloc(1, sizeof(struct lslogins_sgroups));
	while (n < len) {
		if (sgrps->next)
			sgrps = sgrps->next;

		sgrps->gid = gidtostr(list[n]);
		getgrgid_r((list[n]), &grpbuf, buf, buflen, &grp);
		sgrps->uname = xstrdup(grp->gr_name);

		*slen += strlen(sgrps->gid) + strlen(sgrps->uname);

		sgrps->next = calloc(1, sizeof(struct lslogins_sgroups));

		++n;

	}
	/* a pair of parentheses for each group + (n - 1) commas in between */
	slen += 3 * n - 1;

	free(buf);
	free (sgrps->next);
	sgrps->next = NULL;

	return retgrps;
}
static void free_sgroups_list(struct lslogins_sgroups *sgrps)
{
	struct lslogins_sgroups *tmp;

	if (!sgrps)
		return;

	tmp = sgrps->next;
	while (tmp) {
		free(sgrps->gid);
		free(sgrps->uname);
		free(sgrps);
		sgrps = tmp;
		tmp = tmp->next;
	}
}
static char *build_sgroups_string(int len, gid_t *list)
{
	char *ret = NULL, *slist;
	int slen, prlen;
	struct lslogins_sgroups *sgrps;

	sgrps = build_sgroups_list(len, list, &slen);

	if (!sgrps)
		return NULL;

	ret = slist = calloc (1, sizeof(char) * (slen + 1));

	while (sgrps->next) {
		prlen = sprintf(slist, "%s(%s),", sgrps->gid, sgrps->uname);
		if (prlen < 0) {
			free_sgroups_list(sgrps);
			return NULL;
		}
		slist += prlen;
		sgrps = sgrps->next;
	}
	prlen = sprintf(slist, "%s(%s)", sgrps->gid, sgrps->uname);

	return ret;
}

static struct utmp *get_recent_wtmp(const char *username)
{
	size_t n = 0;
	size_t len;

	if (!username)
		return NULL;

	len = strlen(username);
	n = wtmp_size - 1;
	while (n) {
		if (!strncmp(username, wtmp[n].ut_user, len < UT_NAMESIZE ? len : UT_NAMESIZE))
			return wtmp + n;
		--n;
	}
	return NULL;

}
static struct utmp *get_recent_btmp(const char *username)
{
	size_t n = 0;
	size_t len;

	if (!username)
		return NULL;

	len = strlen(username);
	n = btmp_size - 1;
	while (n) {
		if (!strncmp(username, btmp[n].ut_user, len < UT_NAMESIZE ? len : UT_NAMESIZE))
			return btmp + n;
		--n;
	}
	return NULL;

}

static int parse_utmp(char *path)
{
	int rc = 0;

	rc = read_utmp(path, &wtmp_size, &wtmp);
	if (rc < 0 && errno != EACCES)
		err(EXIT_FAILURE, "%s: %s", _PATH_WTMP, strerror(errno));
		return rc;

	return rc;
}

static int get_sgroups(int *len, gid_t **list, struct passwd *pwd)
{
	int n = 0;
	gid_t *safelist;

	getgrouplist(pwd->pw_name, pwd->pw_gid, *list, len);
	if (!*len)
		return -1;

	*list = malloc(*len * sizeof(gid_t));

	if (-1 == getgrouplist(pwd->pw_name, pwd->pw_gid, *list, len))
		return -1;

	while (n < *len) {
		if ((*list)[n] == pwd->pw_gid)
			break;
		++n;
	}
	(*list)[n] = (*list)[--(*len)];

	safelist = xrealloc (*list, *len * sizeof(gid_t));
	if (!safelist && *len) {
		free (*list);
		return -1;
	}
	*list = safelist;

	return 0;

}

static struct lslogins_user *get_user_info(const char *username, const int *columns, const int ncolumns)
{
	struct lslogins_user *user;
	struct passwd pwdbuf, *pwd;
	struct group grpbuf, *grp;
	struct spwd shadowbuf, *shadow;
	struct utmp *user_wtmp = NULL, *user_btmp = NULL;
	int n = 0;
	time_t time;
	struct tm tm;
	uid_t uid;
	size_t buflen = 1024;
	char *buf = NULL, *spbuf = NULL, *gbuf = NULL;

	user = xcalloc(1, sizeof(struct lslogins_user));

	buf = xcalloc(1, sizeof(char) * buflen);

	if (username)
		getpwnam_r(username, &pwdbuf, buf, buflen, &pwd);
	else
		fgetpwent_r(PWDFILE, &pwdbuf, buf, buflen, &pwd);

	if (!pwd)
		return NULL;

	uid = pwd->pw_uid;
	if ((uberflag & F_USRAC) &&
	    strcmp("nfsnobody", pwd->pw_name)) {
		if (uid < UID_MIN || uid > UID_MAX) {
			errno = EAGAIN;
			return NULL;
		}
	}
	else if (uberflag & F_SYSAC) {
		if (uid < SYS_UID_MIN || uid > SYS_UID_MAX) {
			errno = EAGAIN;
			return NULL;
		}
	}
	gbuf = xcalloc(1, sizeof(char) * buflen);
	getgrgid_r(pwd->pw_gid, &grpbuf, gbuf, buflen, &grp);
	if (!grp)
		return NULL;

	if (wtmp)
		user_wtmp = get_recent_wtmp(pwd->pw_name);
	if (btmp)
		user_btmp = get_recent_btmp(pwd->pw_name);

	/* sufficient permissions to get a shadow entry? */
	errno = 0;
	spbuf = xcalloc(1, sizeof(char) * buflen);
	lckpwdf();
	getspnam_r(pwd->pw_name, &shadowbuf, spbuf, buflen, &shadow);
	ulckpwdf();
	if (!shadow && errno != EACCES)
		err(EXIT_FAILURE, "%s", strerror(errno));

	if (shadow) {
		/* we want these dates in seconds */
		shadow->sp_min *= 86400;
		shadow->sp_max*= 86400;
		shadow->sp_lstchg *= 86400;
	}
	/* we store the uid unconditionally, for sorting */
	/* TODO: only if not -t --> faster?? */
	user->uid = pwd->pw_uid;

	while (n < ncolumns) {
		switch (columns[n++]) {
			case COL_LOGIN:
				user->login = xstrdup(pwd->pw_name);
				break;
			case COL_UID:
				/* just a placeholder, see above*/
				break;
			case COL_PGRP:
				user->group = xstrdup(grp->gr_name);
				break;
			case COL_PGID:
				user->gid = (pwd->pw_gid);
				break;
			case COL_SGRPS:
				{
					int n = 0;
					gid_t *list = NULL;

					if (get_sgroups(&n, &list, pwd))
						err(EXIT_FAILURE, "Unable to get secondary groups(%s)", strerror(errno));
					user->sgroups = build_sgroups_string(n, list);
					if (!user->sgroups)
						user->sgroups = "";
					break;
				}
			case COL_HOME:
				user->homedir = xstrdup(pwd->pw_dir);
				break;
			case COL_SHELL:
				user->shell = xstrdup(pwd->pw_shell);
				break;
			case COL_FULLNAME:
				user->gecos = xstrdup(pwd->pw_gecos);
				break;
				/* xstrdup us? ? */
			case COL_LAST_LOGIN:
				if (user_wtmp) {
					time = user_wtmp->ut_tv.tv_sec;
					localtime_r(&time, &tm);
					user->last_login = make_time(&tm);
				}
				else
					user->last_login = "-";
				break;
			case COL_LAST_TTY:
				if (user_wtmp)
					user->last_tty = user_wtmp->ut_line;
				else
					user->last_tty = "-";
				break;
			case COL_LAST_HOSTNAME:
				if (user_wtmp)
					user->last_hostname = user_wtmp->ut_host;
				else
					user->last_hostname = "-";
				break;
			case COL_FAILED_LOGIN:
				if (user_btmp) {
					time = user_btmp->ut_tv.tv_sec;
					localtime_r(&time, &tm);
					user->failed_login = make_time(&tm);
				}
				else
					user->failed_login = "-";
				break;
			case COL_FAILED_TTY:
				if (user_btmp)
					user->failed_tty = user_btmp->ut_line;
				else
					user->failed_tty = "-";
				break;
			case COL_HUSH_STATUS:
				user->hushed = get_hushlogin_status(pwd, 0);
				if (user->hushed == -1)
					user->hushed = 2;
				break;
			case COL_NOPASSWD:
				if (shadow) {
					if (!*shadow->sp_pwdp) /* '\0' */
						user->nopasswd = 1;
					else
						user->nopasswd = 0;
				}
				else
					user->nopasswd = 2;
				break;
			case COL_NOLOGIN:
				if ((pwd->pw_uid && !(close(open("/etc/nologin", O_RDONLY)))) ||
				    strstr(pwd->pw_shell, "nologin")) {
					user->nologin = 1;
				}
				else
					user->nologin = 0;
				break;
			case COL_LOCKED:
				if (shadow) {
					if (*shadow->sp_pwdp == '!')
						user->locked = 1;
					else
						user->locked = 0;
					break;
				}
				else
					user->locked = 2;
				break;
			case COL_PWD_EXPIR:
				if (shadow && shadow->sp_expire != -1) {
					localtime_r(&shadow->sp_expire, &tm);
					user->pwd_expir = make_time(&tm);
				}
				else
					user->pwd_expir = "-";
				break;
			case COL_PWD_CTIME:
				if (shadow) {
					localtime_r(&shadow->sp_lstchg, &tm);
					user->pwd_ctime = make_time(&tm);
				}
				else
					user->pwd_ctime = "-";
				break;
			case COL_PWD_CTIME_MIN:

				if (shadow) {
					if (shadow->sp_min <= 0) {
						user->pwd_ctime_min = "unlimited";
					}
					else {
						localtime_r(&shadow->sp_min, &tm);
						user->pwd_ctime_min = make_time(&tm);
					}
				}
				else
					user->pwd_ctime_min = "-";
				break;
			case COL_PWD_CTIME_MAX:
				if (shadow) {
					if (shadow->sp_max <= 0)
						user->pwd_ctime_max = "unlimited";
					else {
					localtime_r(&shadow->sp_max, &tm);
					user->pwd_ctime_max = make_time(&tm);
					}
				}
				else
					user->pwd_ctime_max = "-";
				break;
			case COL_SELINUX:
				{
#ifdef HAVE_LIBSELINUX
					/* typedefs and pointers are pure evil */
					security_context_t con = NULL;
					if (getcon(&con))
						return NULL;
					user->context = con;
#endif
				}
				break;
			default:
				/* something went very wrong here */
				err(EXIT_FAILURE, "fatal: unknown error");
		}
	}

	free(buf);
	free(spbuf);
	return user;
}
/* some UNIX implementations set errno iff a passwd/grp/...
 * entry was not found. The original UNIX logins utility always
 * ignores invalid login/group names, so we're going to as well.*/
#define IS_REAL_ERRNO(e) !((e) == ENOENT || (e) == ESRCH || (e) == EBADF || (e) == EPERM || (e) == EAGAIN)
#define HANDLE_ERROR(e) do { if ((e) && IS_REAL_ERRNO(e)) { err(1,"%s",strerror(errno)); return -1;} } while (0)

static struct lslogins_user *get_next_user(const int *columns, const int ncolumns)
{
	struct lslogins_user *u;
	errno = 0;
	while (!(u = get_user_info(NULL, columns, ncolumns))) {
		/* no "false" errno-s here, if we're unable to
		 * get a valid user entry for any reason, quit */
		if (errno == EAGAIN)
			continue;
		return NULL;
	}
	return u;
}

static int get_user(struct lslogins_user **user,
		const char *username, const int *columns, const int ncolumns)
{
	*user = get_user_info(username, columns, ncolumns);
	if (!*user && errno)
		HANDLE_ERROR(errno);
	return 0;
}

static void *user_in_tree(void **rootp, struct lslogins_user *u)
{
	void *rc;
	rc = tfind(u, rootp, cmp_fn);
	if (!rc)
		tdelete(u, rootp, cmp_fn);
	return rc;
}

static int create_usertree(char *logins, char *groups, const int *const columns, const int ncolumns)
{
	char *username, *gname, *buf;
	struct lslogins_user *user = NULL, tmpuser;
	struct group grpbuf, *grp;
	int n = 0;
	size_t buflen = 1024;
	char *saveptr;

	if (!logins && !groups) {
		while ((user = get_next_user(columns, ncolumns)))
			tsearch(user, &usertree, cmp_fn);

		HANDLE_ERROR(errno);
	}
	if (logins) {
		while ((username = strtok_r(logins, ",", &saveptr)) != NULL) {
			logins = NULL;
			if (get_user(&user, username, columns, ncolumns))
				return -1;
			if (user) /* otherwise an invalid user name has probably been given */
				tsearch(user, &usertree, cmp_fn);
		}
	}
	if (groups) {
		while ((gname = strtok_r(groups, ",", &saveptr)) != NULL) {
			groups = NULL;
			errno = 0;

			buf = xcalloc(1, sizeof(char) * buflen);
			getgrnam_r(gname, &grpbuf, buf, buflen, &grp);
			if (!grp) {
				HANDLE_ERROR(errno);
				continue; /* not a "real" errno */
			}

			while ((username = grp->gr_mem[n++])) {
				tmpuser.login = username;
				if (!user_in_tree(&usertree, &tmpuser)) {
					if (get_user(&user, username, columns, ncolumns))
						return -1;
					if (user) /* otherwise an invalid user name has probably been given */
						tsearch(user, &usertree, cmp_fn);
				}
			}
		}
	}
	return 0;
}
static int cmp_uname(const void *a, const void *b)
{
	return strcmp(((struct lslogins_user *)a)->login,
		      ((struct lslogins_user *)b)->login);
}
static int cmp_uid(const void *a, const void *b)
{
	uid_t x = ((struct lslogins_user *)a)->uid;
	uid_t z = ((struct lslogins_user *)b)->uid;
	return x > z ? 1 : (x < z ? -1 : 0);
}

static struct libscols_table *setup_table(const int *const columns, const int ncolumns)
{
	struct libscols_table *tb = scols_new_table();
	int n = 0;
	if (!tb)
		return NULL;

	switch(outmode) {
		case out_colon:
			scols_table_enable_raw(tb, 1);
			scols_table_set_column_separator(tb, ":");
			break;
		case out_newline:
			scols_table_set_column_separator(tb, "\n");
			/* fallthrough */
		case out_export:
			scols_table_enable_export(tb, 1);
			break;
		case out_nul:
			scols_table_set_line_separator(tb, "\0");
			/* fallthrough */
		case out_raw:
			scols_table_enable_raw(tb, 1);
			break;
		default:
			break;
	}

	while (n < ncolumns) {
		if (!scols_table_new_column(tb, coldescs[columns[n]].name,
					    coldescs[columns[n]].whint, 0))
			goto fail;
		++n;
	}

	return tb;
fail:
	scols_unref_table(tb);
	return NULL;
}
static void fill_table(const void *u, const VISIT which, const int depth __attribute__((unused)))
{
	struct libscols_line *ln;
	struct lslogins_user *user = *(struct lslogins_user **)u;
	int n = 0;

	if ((which == preorder) || (which == endorder))
		return;

	ln = scols_table_new_line(tb, NULL);
	while (n < ncolumns) {
		switch (columns[n]) {
			case COL_LOGIN:
				if (scols_line_set_data(ln, n, user->login))
					goto fail;
				break;
			case COL_UID:
				{
					char *str_uid = uidtostr(user->uid);
					if (!str_uid || scols_line_set_data(ln, n, str_uid))
						goto fail;
					break;
				}
			case COL_NOPASSWD:
				if (scols_line_set_data(ln, n, status[user->nopasswd]))
					goto fail;
				break;
			case COL_NOLOGIN:
				if (scols_line_set_data(ln, n, status[user->nologin]))
					goto fail;
				break;
			case COL_LOCKED:
				if (scols_line_set_data(ln, n, status[user->locked]))
					goto fail;
				break;
			case COL_PGRP:
				if (scols_line_set_data(ln, n, user->group))
					goto fail;
				break;
			case COL_PGID:
				{
					char *str_gid = gidtostr(user->gid);
					if (!str_gid || scols_line_set_data(ln, n, str_gid))
						goto fail;
					break;
				}
			case COL_SGRPS:
				if (scols_line_set_data(ln, n, user->sgroups))
					goto fail;
				break;
			case COL_HOME:
				if (scols_line_set_data(ln, n, user->homedir))
					goto fail;
				break;
			case COL_SHELL:
				if (scols_line_set_data(ln, n, user->shell))
					goto fail;
				break;
			case COL_FULLNAME:
				if (scols_line_set_data(ln, n, user->gecos))
					goto fail;
				break;
			case COL_LAST_LOGIN:
				if (scols_line_set_data(ln, n, user->last_login))
					goto fail;
				break;
			case COL_LAST_TTY:
				if (scols_line_set_data(ln, n, user->last_tty))
					goto fail;
				break;
			case COL_LAST_HOSTNAME:
				if (scols_line_set_data(ln, n, user->last_hostname))
					goto fail;
				break;
			case COL_FAILED_LOGIN:
				if (scols_line_set_data(ln, n, user->failed_login))
					goto fail;
				break;
			case COL_FAILED_TTY:
				if (scols_line_set_data(ln, n, user->failed_tty))
					goto fail;
				break;
			case COL_HUSH_STATUS:
				if (scols_line_set_data(ln, n, status[user->hushed]))
					goto fail;
				break;
#define PWD_TIME(S,L,T) strftime((S),(L), "%a %b %d %Y", (T))
			case COL_PWD_EXPIR:
				if (scols_line_set_data(ln, n, user->pwd_expir))
					goto fail;
				break;
			case COL_PWD_CTIME:
				if (scols_line_set_data(ln, n, user->pwd_ctime))
					goto fail;
				break;
			case COL_PWD_CTIME_MIN:
				if (scols_line_set_data(ln, n, user->pwd_ctime_min))
					goto fail;
				break;
			case COL_PWD_CTIME_MAX:
				if (scols_line_set_data(ln, n, user->pwd_ctime_max))
					goto fail;
				break;
#ifdef HAVE_LIBSELINUX
			case COL_SELINUX:
				if (scols_line_set_data(ln, n, user->context))
					goto fail;
				break;
#endif
			default:
				/* something went very wrong here */
				err(EXIT_FAILURE, "fatal: unknown error");
		}
		++n;
	}
	return;
fail:
	exit(1);
}
static int print_user_table(void)
{
	tb = setup_table(columns, ncolumns);
	if (!tb)
		return -1;

	twalk(usertree, fill_table);
	scols_print_table(tb);
	scols_unref_table(tb);
	return 0;
}

static void free_user(void *u)
{
	free(u);
}
int main(int argc, char *argv[])
{
	int c;
	char *logins = NULL, *groups = NULL;
	char *path_wtmp = _PATH_WTMP, *path_btmp = _PATH_BTMP;

	/* long only options. */
	enum {
		OPT_LAST = CHAR_MAX + 1,
		OPT_VER,
		OPT_WTMP,
		OPT_BTMP,
		OPT_PASSWD,
		OPT_SHADOW,
	};
	static const struct option longopts[] = {
		{ "acc-expiration", no_argument,	0, 'a' },
		{ "colon",          no_argument,	0, 'c' },
		{ "export",         no_argument,	0, 'e' },
		{ "failed",         no_argument,	0, 'f' },
		{ "groups",         required_argument,	0, 'g' },
		{ "help",           no_argument,	0, 'h' },
		{ "logins",         required_argument,	0, 'l' },
		{ "supp-groups",     no_argument,	0, 'm' },
		{ "newline",        no_argument,	0, 'n' },
		{ "output",         required_argument,	0, 'o' },
		{ "last",           no_argument,	0, OPT_LAST },
		{ "raw",            no_argument,	0, 'r' },
		{ "system-accs",    no_argument,	0, 's' },
		{ "sort-by-name",   no_argument,	0, 't' },
		{ "user-accs",      no_argument,	0, 'u' },
		{ "version",        no_argument,	0, OPT_VER },
		{ "extra",          no_argument,	0, 'x' },
		{ "print0",         no_argument,	0, 'z' },
		{ "path-wtmp",      required_argument,	0, OPT_WTMP },
		{ "path-btmp",      required_argument,	0, OPT_BTMP },
		{ "path-passwd",    required_argument,	0, OPT_PASSWD },
		{ "path-shadow",    required_argument,	0, OPT_SHADOW },
#ifdef HAVE_LIBSELINUX
		{ "context",        no_argument,	0, 'Z' },
#endif
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

	cmp_fn = cmp_uid;

	while ((c = getopt_long(argc, argv, "acefg:hl:mno:rstuxzZ",
				longopts, NULL)) != -1) {

		err_exclusive_options(c, longopts, excl, excl_st);

		switch (c) {
			case 'a':
				uberflag |= F_EXPIR;
				break;
			case 'c':
				outmode = out_colon;
				break;
				/* a slow option; however the only reason to use it
				 * is after hand-editing the /etc/passwd file */
			case 'e':
				outmode = out_export;
				break;
			case 'f':
				uberflag |= F_FAIL;
				break;
			case 'g':
				groups = xstrdup(optarg);
				if (!groups)
					return EXIT_FAILURE;
				break;
			case 'h':
				usage(stdout);
			case 'l':
				logins = xstrdup(optarg);
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
			case 'r':
				outmode = out_raw;
				break;
			case OPT_LAST:
				uberflag |= F_LAST;
				break;
			case 's':
				SYS_UID_MIN = strtoul(getlogindefs_str("SYS_UID_MIN", UL_SYS_UID_MIN), NULL, 0);
				SYS_UID_MAX = strtoul(getlogindefs_str("SYS_UID_MAX", UL_SYS_UID_MAX), NULL, 0);
				uberflag |= F_SYSAC;
				break;
			case 't':
				cmp_fn = cmp_uname;
				uberflag |= F_SORT;
				break;
			case 'u':
				UID_MIN = strtoul(getlogindefs_str("UID_MIN", UL_UID_MIN), NULL, 0);
				UID_MAX = strtoul(getlogindefs_str("UID_MAX", UL_UID_MAX), NULL, 0);
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
			case OPT_WTMP:
				path_wtmp = optarg;
				break;
			case OPT_BTMP:
				path_btmp = optarg;
				break;
			case OPT_PASSWD:
				PASSWD_PATH = optarg;
				break;
			case OPT_SHADOW:
				break;
			case 'Z':
#ifdef HAVE_LIBSELINUX
				if (0 < is_selinux_enabled())
					uberflag |= F_SELINUX;
				else
#endif
					err(0, "warning: --context only works on a system with SELinux enabled");
				break;
			default:
				usage(stderr);
		}
	}
	if (argc != optind)
		usage(stderr);

	/* lslogins -u -s == lslogins */
	if (uberflag & F_USRAC && uberflag & F_SYSAC)
		uberflag &= ~(F_USRAC & F_SYSAC);

	if (!ncolumns) {
		columns[ncolumns++] = COL_LOGIN;
		columns[ncolumns++] = COL_UID;  /* TODO: always get this info, for sorting */
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
			want_wtmp = 1;
		}
		if (uberflag & F_FAIL) {
			columns[ncolumns++] = COL_FAILED_LOGIN;
			columns[ncolumns++] = COL_FAILED_TTY;
			want_btmp = 1;
		}
		if (uberflag & F_EXTRA) {
			columns[ncolumns++] = COL_HOME;
			columns[ncolumns++] = COL_SHELL;
			columns[ncolumns++] = COL_NOPASSWD;
			columns[ncolumns++] = COL_NOLOGIN;
			columns[ncolumns++] = COL_LOCKED;
			columns[ncolumns++] = COL_HUSH_STATUS;
			columns[ncolumns++] = COL_PWD_CTIME_MIN;
			columns[ncolumns++] = COL_PWD_CTIME_MAX;
		}
		if (uberflag & F_SELINUX)
			columns[ncolumns++] = COL_SELINUX;
	}

	if (want_wtmp)
		parse_utmp(path_wtmp);
	if (want_btmp)
		parse_utmp(path_btmp);

	PWDFILE = fopen(PASSWD_PATH,"r");
	if (create_usertree(logins, groups, columns, ncolumns))
		return EXIT_FAILURE;

	print_user_table();
	tdestroy(usertree, free_user);

	return EXIT_SUCCESS;
}
