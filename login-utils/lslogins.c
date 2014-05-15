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
	const char *pretty_name;

	double whint;	/* width hint */
	long flag;
};

static int lslogins_flag;

#define UL_UID_MIN 1000
#define UL_UID_MAX 60000
#define UL_SYS_UID_MIN 201
#define UL_SYS_UID_MAX 999

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
	out_pretty,
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
	char *pwd_warn;
	char *pwd_expire;
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
 * time modes
 * */
enum {
	TIME_INVALID = 0,
	TIME_SHORT_RELATIVE,
	TIME_SHORT,
	TIME_FULL,
	TIME_ISO,
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
	COL_GECOS,
	COL_LAST_LOGIN,
	COL_LAST_TTY,
	COL_LAST_HOSTNAME,
	COL_FAILED_LOGIN,
	COL_FAILED_TTY,
	COL_HUSH_STATUS,
	COL_NOLOGIN,
	COL_LOCKED,
	COL_NOPASSWD,
	COL_PWD_WARN,
	COL_PWD_CTIME,
	COL_PWD_CTIME_MIN,
	COL_PWD_CTIME_MAX,
	COL_PWD_EXPIR,
	COL_SELINUX,
};

static const char *const status[] = { "0", "1", "-" };
static struct lslogins_coldesc coldescs[] =
{
	[COL_LOGIN]		= { "LOGIN",		N_("user/system login"), "Login", 0.2, SCOLS_FL_NOEXTREMES },
	[COL_UID]		= { "UID",		N_("user UID"), "UID", 0.05, SCOLS_FL_RIGHT},
	[COL_NOPASSWD]		= { "NOPASSWD",		N_("account has a password?"), "No password", 1 },
	[COL_NOLOGIN]		= { "NOLOGIN",		N_("account has a password?"), "No login", 1 },
	[COL_LOCKED]		= { "LOCKED",		N_("account has a password?"), "Locked", 1 },
	[COL_PGRP]		= { "GROUP",		N_("primary group name"), "Primary group", 0.2 },
	[COL_PGID]		= { "GID",		N_("primary group GID"), "GID", 0.05, SCOLS_FL_RIGHT },
	[COL_SGRPS]		= { "SUPP-GROUPS",	N_("secondary group names and GIDs"), "Secondary groups", 0.5 },
	[COL_HOME]		= { "HOMEDIR",		N_("home directory"), "Home directory", 0.3 },
	[COL_SHELL]		= { "SHELL",		N_("login shell"), "Shell", 0.1 },
	[COL_GECOS]		= { "GECOS",		N_("full user name"), "Comment field", 0.3, SCOLS_FL_TRUNC },
	[COL_LAST_LOGIN]	= { "LAST-LOGIN",	N_("date of last login"), "Last login", 24 },
	[COL_LAST_TTY]		= { "LAST-TTY",		N_("last tty used"), "Last terminal", 0.05 },
	[COL_LAST_HOSTNAME]	= { "LAST-HOSTNAME",	N_("hostname during the last session"), "Last hostname",  0.2},
	[COL_FAILED_LOGIN]	= { "FAILED-LOGIN",	N_("date of last failed login"), "Failed login", 24 },
	[COL_FAILED_TTY]	= { "FAILED-TTY",	N_("where did the login fail?"), "Failed login terminal", 0.05 },
	[COL_HUSH_STATUS]	= { "HUSHED",		N_("User's hush settings"), "Hushed", 1 },
	[COL_PWD_WARN]		= { "PWD-WARN",		N_("password warn interval"), "Days to passwd warning", 24 },
	[COL_PWD_EXPIR]		= { "PWD-EXPIR",	N_("password expiration date"), "Password expiration", 24 },
	[COL_PWD_CTIME]		= { "PWD-CHANGE",	N_("date of last password change"), "Password changed", 24 },
	[COL_PWD_CTIME_MIN]	= { "PWD-MIN",		N_("number of days required between changes"), "Minimal change time", 24 },
	[COL_PWD_CTIME_MAX]	= { "PWD-MAX",		N_("max number of days a password may remain unchanged"), "Maximal change time", 24 },
	[COL_SELINUX]		= { "CONTEXT",		N_("the user's security context"), "Selinux context", 0.4 },
};

struct lslogins_control {
	struct utmp *wtmp;
	size_t wtmp_size;

	struct utmp *btmp;
	size_t btmp_size;

	void *usertree;

	uid_t UID_MIN;
	uid_t UID_MAX;

	uid_t SYS_UID_MIN;
	uid_t SYS_UID_MAX;

	int (*cmp_fn) (const void *a, const void *b);

	char **ulist;
	size_t ulsiz;

	int sel_enabled;
	unsigned int time_mode;
};
/* these have to remain global since there's no other
 * reasonable way to pass them for each call of fill_table()
 * via twalk() */
static struct libscols_table *tb;
static int columns[ARRAY_SIZE(coldescs)];
static int ncolumns;

static int date_is_today(time_t t)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return t / 86400 == tv.tv_sec / 86400;
}

static int
column_name_to_id(const char *name, size_t namesz)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(coldescs); i++) {
		const char *cn = coldescs[i].name;

		if (!strncasecmp(name, cn, namesz) && !*(cn + namesz)) {
			return i;
		}
	}
	warnx(_("unknown column: %s"), name);
	return -1;
}
static char *make_time(int mode, time_t time)
{
	char *s;
	struct tm tm;
	char buf[32] = {0};

	localtime_r(&time, &tm);

	switch(mode) {
		case TIME_FULL:
			asctime_r(&tm, buf);
			if (*(s = buf + strlen(buf) - 1) == '\n')
				*s = '\0';
			break;
		case TIME_SHORT_RELATIVE:
			if (date_is_today(time))
				strftime(buf, 32, "%H:%M:%S", &tm);
			else /*fallthrough*/
		case TIME_SHORT:
			strftime(buf, 32, "%a %b %d %Y", &tm);
			break;
		case TIME_ISO:
			strftime(buf, 32, "%Y-%m-%dT%H:%M:%S%z", &tm);
			break;
		default:
			exit(1);
	}
	return xstrdup(buf);
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
	fputs(_(" --fulltimes              Show dates in a long format\n"), out);
	fputs(_(" -g, --groups=<groups>    Display users belonging to a group in GROUPS\n"), out);
	fputs(_(" -i, --iso                Display dates in the ISO-8601 format\n"), out);
	fputs(_(" -l, --logins=<logins>    Display only users from LOGINS\n"), out);
	fputs(_(" --last                   Show info about the users' last login sessions\n"), out);
	fputs(_(" -m, --supp-groups        Display supplementary groups as well\n"), out);
	fputs(_(" -n, --newline            Display each piece of information on a new line\n"), out);
	fputs(_(" --notruncate             Don't truncate output\n"), out);
	fputs(_(" -o, --output[=<list>]    Define the columns to output\n"), out);
	fputs(_(" -r, --raw                Display the raw table\n"), out);
	fputs(_(" -s, --system-accs        Display system accounts\n"), out);
	fputs(_(" -t, --sort               Sort output by login instead of UID\n"), out);
	fputs(_(" --time-format=<type>     Display dates in type <type>, where type is one of short|full|iso\n"), out);
	fputs(_(" -u, --user-accs          Display user accounts\n"), out);
	fputs(_(" -x, --extra              Display extra information\n"), out);
	fputs(_(" -z, --print0             Delimit user entries with a nul character\n"), out);
	fputs(_(" -Z, --context            Display the users' security context\n"), out);
	fputs(_(" --wtmp-file              Set an alternate path for wtmp\n"), out);
	fputs(_(" --btmp-file              Set an alternate path for btmp\n"), out);
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
	xasprintf(&str_uid, "%u", uid);
	return str_uid;
}

static char *gidtostr(gid_t gid)
{
	char *str_gid = NULL;
	xasprintf(&str_gid, "%u", gid);
	return str_gid;
}

static struct lslogins_sgroups *build_sgroups_list(int len, gid_t *list, int *slen)
{
	int n = 0;
	struct lslogins_sgroups *sgrps, *retgrps;
	struct group *grp;
	char *buf = NULL;

	if (!len || !list)
		return NULL;

	*slen = 0;

	retgrps = sgrps = xcalloc(1, sizeof(struct lslogins_sgroups));
	while (n < len) {
		if (sgrps->next)
			sgrps = sgrps->next;
		/* TODO: rewrite */
		sgrps->gid = gidtostr(list[n]);

		grp = getgrgid(list[n]);
		if (!grp) {
			free(retgrps);
			return NULL;
		}
		sgrps->uname = xstrdup(grp->gr_name);

		*slen += strlen(sgrps->gid) + strlen(sgrps->uname);

		sgrps->next = xcalloc(1, sizeof(struct lslogins_sgroups));

		++n;
	}

	/* space for a pair of parentheses for each gname + (n - 1) commas in between */
	*slen += 3 * n - 1;

	free(buf);
	free(sgrps->next);
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

	ret = slist = xcalloc(1, sizeof(char) * (slen + 1));

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

	free_sgroups_list(sgrps);
	return ret;
}

static struct utmp *get_last_wtmp(struct lslogins_control *ctl, const char *username)
{
	size_t n = 0;
	size_t len;

	if (!username)
		return NULL;

	len = strlen(username);
	n = ctl->wtmp_size - 1;
	do {
		if (!strncmp(username, ctl->wtmp[n].ut_user,
		    len < UT_NAMESIZE ? len : UT_NAMESIZE))
			return ctl->wtmp + n;
	} while (n--);
	return NULL;

}
static struct utmp *get_last_btmp(struct lslogins_control *ctl, const char *username)
{
	size_t n = 0;
	size_t len;

	if (!username)
		return NULL;

	len = strlen(username);
	n = ctl->btmp_size - 1;
	do {
		if (!strncmp(username, ctl->btmp[n].ut_user,
		    len < UT_NAMESIZE ? len : UT_NAMESIZE))
			return ctl->btmp + n;
	}while (n--);
	return NULL;

}

static int parse_wtmp(struct lslogins_control *ctl, char *path)
{
	int rc = 0;

	rc = read_utmp(path, &ctl->wtmp_size, &ctl->wtmp);
	if (rc < 0 && errno != EACCES)
		err(EXIT_FAILURE, "%s", path);
	return rc;
}

static int parse_btmp(struct lslogins_control *ctl, char *path)
{
	int rc = 0;

	rc = read_utmp(path, &ctl->btmp_size, &ctl->btmp);
	if (rc < 0 && errno != EACCES)
		err(EXIT_FAILURE, "%s", path);
	return rc;
}
static int get_sgroups(int *len, gid_t **list, struct passwd *pwd)
{
	int n = 0;

	*len = 0;
	*list = NULL;

	/* first let's get a supp. group count */
	getgrouplist(pwd->pw_name, pwd->pw_gid, *list, len);
	if (!*len)
		return -1;

	*list = xcalloc(1, *len * sizeof(gid_t));

	/* now for the actual list of GIDs */
	if (-1 == getgrouplist(pwd->pw_name, pwd->pw_gid, *list, len))
		return -1;

	/* getgroups also returns the user's primary GID - dispose of it */
	while (n < *len) {
		if ((*list)[n] == pwd->pw_gid)
			break;
		++n;
	}
	(*list)[n] = (*list)[--(*len)];

	/* probably too costly to do for sizeof(gid_t) worth of memory */
	//*list = xrealloc(*list, *len * sizeof(gid_t));

	return 0;
}

static struct lslogins_user *get_user_info(struct lslogins_control *ctl, const char *username)
{
	struct lslogins_user *user;
	struct passwd *pwd;
	struct group *grp;
	struct spwd *shadow;
	struct utmp *user_wtmp = NULL, *user_btmp = NULL;
	int n = 0;
	time_t time;
	uid_t uid;
	errno = 0;

	if (username)
		pwd = getpwnam(username);
	else
		pwd = getpwent();

	if (!pwd)
		return NULL;

	uid = pwd->pw_uid;
	/* nfsnobody is an exception to the UID_MAX limit.
	 * This is "nobody" on some systems; the decisive
	 * point is the UID - 65534 */
	if ((lslogins_flag & F_USRAC) &&
	    strcmp("nfsnobody", pwd->pw_name)) {
		if (uid < ctl->UID_MIN || uid > ctl->UID_MAX) {
			errno = EAGAIN;
			return NULL;
		}
	}
	else if (lslogins_flag & F_SYSAC) {
		if (uid < ctl->SYS_UID_MIN || uid > ctl->SYS_UID_MAX) {
			errno = EAGAIN;
			return NULL;
		}
	}

	user = xcalloc(1, sizeof(struct lslogins_user));

	grp = getgrgid(pwd->pw_gid);
	if (!grp)
		return NULL;

	if (ctl->wtmp)
		user_wtmp = get_last_wtmp(ctl, pwd->pw_name);
	if (ctl->btmp)
		user_btmp = get_last_btmp(ctl, pwd->pw_name);

	/* sufficient permissions to get a shadow entry? */
	errno = 0;
	lckpwdf();
	shadow = getspnam(pwd->pw_name);
	ulckpwdf();

	if (!shadow) {
		if (errno != EACCES)
			err(EXIT_FAILURE, "%s", strerror(errno));
	}
	else {
		/* we want these dates in seconds */
		shadow->sp_lstchg *= 86400;
		shadow->sp_expire *= 86400;
	}

	while (n < ncolumns) {
		switch (columns[n++]) {
			case COL_LOGIN:
				user->login = xstrdup(pwd->pw_name);
				break;
			case COL_UID:
				user->uid = pwd->pw_uid;
				break;
			case COL_PGRP:
				user->group = xstrdup(grp->gr_name);
				break;
			case COL_PGID:
				user->gid = pwd->pw_gid;
				break;
			case COL_SGRPS:
				{
					int n = 0;
					gid_t *list = NULL;

					if (get_sgroups(&n, &list, pwd))
						err(1, NULL);

					user->sgroups = build_sgroups_string(n, list);

					if (!user->sgroups)
						user->sgroups = xstrdup(status[2]);
					break;
				}
			case COL_HOME:
				user->homedir = xstrdup(pwd->pw_dir);
				break;
			case COL_SHELL:
				user->shell = xstrdup(pwd->pw_shell);
				break;
			case COL_GECOS:
				user->gecos = xstrdup(pwd->pw_gecos);
				break;
			case COL_LAST_LOGIN:
				if (user_wtmp) {
					time = user_wtmp->ut_tv.tv_sec;
					user->last_login = make_time(ctl->time_mode, time);
				}
				else
					user->last_login = xstrdup(status[2]);
				break;
			case COL_LAST_TTY:
				if (user_wtmp)
					user->last_tty = xstrdup(user_wtmp->ut_line);
				else
					user->last_tty = xstrdup(status[2]);
				break;
			case COL_LAST_HOSTNAME:
				if (user_wtmp)
					user->last_hostname = xstrdup(user_wtmp->ut_host);
				else
					user->last_hostname = xstrdup(status[2]);
				break;
			case COL_FAILED_LOGIN:
				if (user_btmp) {
					time = user_btmp->ut_tv.tv_sec;
					user->failed_login = make_time(ctl->time_mode, time);
				}
				else
					user->failed_login = xstrdup(status[2]);
				break;
			case COL_FAILED_TTY:
				if (user_btmp)
					user->failed_tty = xstrdup(user_btmp->ut_line);
				else
					user->failed_tty = xstrdup(status[2]);
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
				}
				else
					user->nopasswd = 2;
				break;
			case COL_NOLOGIN:
				if ((pwd->pw_uid && !(close(open("/etc/nologin", O_RDONLY)))) ||
				    strstr(pwd->pw_shell, "nologin")) {
					user->nologin = 1;
				}
				break;
			case COL_LOCKED:
				if (shadow) {
					if (*shadow->sp_pwdp == '!')
						user->locked = 1;
				}
				else
					user->locked = 2;
				break;
			case COL_PWD_WARN:
				if (shadow && shadow->sp_warn >= 0) {
					xasprintf(&user->pwd_warn, "%ld", shadow->sp_warn);
				}
				else
					user->pwd_warn = xstrdup(status[2]);
				break;
			case COL_PWD_EXPIR:
				if (shadow && shadow->sp_expire >= 0)
					user->pwd_expire = make_time(TIME_SHORT, shadow->sp_expire);
				else
					user->pwd_expire = xstrdup(status[2]);
				break;
			case COL_PWD_CTIME:
				/* sp_lstchg is specified in days, showing hours (especially in non-GMT
				 * timezones) would only serve to confuse */
				if (shadow)
					user->pwd_ctime = make_time(TIME_SHORT, shadow->sp_lstchg);
				else
					user->pwd_ctime = xstrdup(status[2]);
				break;
			case COL_PWD_CTIME_MIN:
				if (shadow) {
					if (shadow->sp_min <= 0)
						user->pwd_ctime_min = xstrdup("unlimited");
					else
						xasprintf(&user->pwd_ctime_min, "%ld", shadow->sp_min);
				}
				else
					user->pwd_ctime_min = xstrdup(status[2]);
				break;
			case COL_PWD_CTIME_MAX:
				if (shadow) {
					if (shadow->sp_max <= 0)
						user->pwd_ctime_max = xstrdup("unlimited");
					else
						xasprintf(&user->pwd_ctime_max, "%ld", shadow->sp_max);
				}
				else
					user->pwd_ctime_max = xstrdup(status[2]);
				break;
			case COL_SELINUX:
				{
#ifdef HAVE_LIBSELINUX
					/* typedefs and pointers are pure evil */
					security_context_t con = NULL;
					if (getcon(&con))
						user->context = xstrdup(status[2]);
					else
						user->context = con;
#endif
				}
				break;
			default:
				/* something went very wrong here */
				err(EXIT_FAILURE, "fatal: unknown error");
		}
	}
	/* check if we have the info needed to sort */
	if (lslogins_flag & F_SORT) { /* sorting by username */
		if (!user->login)
			user->login = xstrdup(pwd->pw_name);
	}
	else /* sorting by UID */
		user->uid = pwd->pw_uid;

	return user;
}
/* some UNIX implementations set errno iff a passwd/grp/...
 * entry was not found. The original UNIX logins(1) utility always
 * ignores invalid login/group names, so we're going to as well.*/
#define IS_REAL_ERRNO(e) !((e) == ENOENT || (e) == ESRCH || \
		(e) == EBADF || (e) == EPERM || (e) == EAGAIN)

/*
static void *user_in_tree(void **rootp, struct lslogins_user *u)
{
	void *rc;
	rc = tfind(u, rootp, ctl->cmp_fn);
	if (!rc)
		tdelete(u, rootp, ctl->cmp_fn);
	return rc;
}
*/

/* get a definitive list of users we want info about... */

static int str_to_uint(char *s, unsigned int *ul)
{
	char *end;
	if (!s || !*s)
		return -1;
	*ul = strtoul(s, &end, 0);
	if (!*end)
		return 0;
	return 1;
}
static int get_ulist(struct lslogins_control *ctl, char *logins, char *groups)
{
	char *u, *g;
	size_t i = 0, n = 0, *arsiz;
	struct group *grp;
	struct passwd *pwd;
	char ***ar;
	uid_t uid;
	gid_t gid;

	ar = &ctl->ulist;
	arsiz = &ctl->ulsiz;

	/* an arbitrary starting value */
	*arsiz = 32;
	*ar = xcalloc(1, sizeof(char *) * (*arsiz));

	while ((u = strtok(logins, ","))) {
		logins = NULL;

		/* user specified by UID? */
		if (!str_to_uint(u, &uid)) {
			pwd = getpwuid(uid);
			if (!pwd)
				continue;
			u = pwd->pw_name;
		}
		(*ar)[i++] = xstrdup(u);

		if (i == *arsiz)
			*ar = xrealloc(*ar, sizeof(char *) * (*arsiz += 32));
	}
	/* FIXME: this might lead to duplicit entries, although not visible
	 * in output, crunching a user's info multiple times is very redundant */
	while ((g = strtok(groups, ","))) {
		groups = NULL;

		/* user specified by GID? */
		if (!str_to_uint(g, &gid))
			grp = getgrgid(gid);
		else
			grp = getgrnam(g);

		if (!grp)
			continue;

		while ((u = grp->gr_mem[n++])) {
			(*ar)[i++] = xstrdup(u);

			if (i == *arsiz)
				*ar = xrealloc(*ar, sizeof(char *) * (*arsiz += 32));
		}
	}
	*arsiz = i;
	return 0;
}

static void free_ctl(struct lslogins_control *ctl)
{
	size_t n = 0;

	free(ctl->wtmp);
	free(ctl->btmp);

	while (n < ctl->ulsiz)
		free(ctl->ulist[n++]);

	free(ctl->ulist);
	free(ctl);
}

static struct lslogins_user *get_next_user(struct lslogins_control *ctl)
{
	struct lslogins_user *u;
	errno = 0;
	while (!(u = get_user_info(ctl, NULL))) {
		/* no "false" errno-s here, iff we're unable to
		 * get a valid user entry for any reason, quit */
		if (errno == EAGAIN)
			continue;
		return NULL;
	}
	return u;
}

static int get_user(struct lslogins_control *ctl, struct lslogins_user **user, const char *username)
{
	*user = get_user_info(ctl, username);
	if (!*user && errno)
		if (IS_REAL_ERRNO(errno))
			return -1;
	return 0;
}
static int create_usertree(struct lslogins_control *ctl)
{
	struct lslogins_user *user = NULL;
	size_t n = 0;

	if (*ctl->ulist) {
		while (n < ctl->ulsiz) {
			if (get_user(ctl, &user, ctl->ulist[n]))
				return -1;
			if (user) /* otherwise an invalid user name has probably been given */
				tsearch(user, &ctl->usertree, ctl->cmp_fn);
			++n;
		}
	}
	else {
		while ((user = get_next_user(ctl)))
			tsearch(user, &ctl->usertree, ctl->cmp_fn);
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

static struct libscols_table *setup_table(void)
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
		case out_pretty:
			scols_table_enable_noheadings(tb, 1);
		default:
			break;
	}

	while (n < ncolumns) {
		if (!scols_table_new_column(tb, coldescs[columns[n]].name,
					    coldescs[columns[n]].whint, coldescs[columns[n]].flag))
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

	if (which == preorder || which == endorder)
		return;

	ln = scols_table_new_line(tb, NULL);
	while (n < ncolumns) {
		int rc = 0;

		switch (columns[n]) {
			case COL_LOGIN:
				rc = scols_line_set_data(ln, n, user->login);
				break;
			case COL_UID:
				rc = scols_line_refer_data(ln, n, uidtostr(user->uid));
				break;
			case COL_NOPASSWD:
				rc = scols_line_set_data(ln, n, status[user->nopasswd]);
				break;
			case COL_NOLOGIN:
				rc = scols_line_set_data(ln, n, status[user->nologin]);
				break;
			case COL_LOCKED:
				rc = scols_line_set_data(ln, n, status[user->locked]);
				break;
			case COL_PGRP:
				rc = scols_line_set_data(ln, n, user->group);
				break;
			case COL_PGID:
				rc = scols_line_refer_data(ln, n, gidtostr(user->gid));
				break;
			case COL_SGRPS:
				rc = scols_line_set_data(ln, n, user->sgroups);
				break;
			case COL_HOME:
				rc = scols_line_set_data(ln, n, user->homedir);
				break;
			case COL_SHELL:
				rc = scols_line_set_data(ln, n, user->shell);
				break;
			case COL_GECOS:
				rc = scols_line_set_data(ln, n, user->gecos);
				break;
			case COL_LAST_LOGIN:
				rc = scols_line_set_data(ln, n, user->last_login);
				break;
			case COL_LAST_TTY:
				rc = scols_line_set_data(ln, n, user->last_tty);
				break;
			case COL_LAST_HOSTNAME:
				rc = scols_line_set_data(ln, n, user->last_hostname);
				break;
			case COL_FAILED_LOGIN:
				rc = scols_line_set_data(ln, n, user->failed_login);
				break;
			case COL_FAILED_TTY:
				rc = scols_line_set_data(ln, n, user->failed_tty);
				break;
			case COL_HUSH_STATUS:
				rc= scols_line_set_data(ln, n, status[user->hushed]);
				break;
			case COL_PWD_WARN:
				rc = scols_line_set_data(ln, n, user->pwd_warn);
				break;
			case COL_PWD_EXPIR:
				rc = scols_line_set_data(ln, n, user->pwd_expire);
				break;
			case COL_PWD_CTIME:
				rc = scols_line_set_data(ln, n, user->pwd_ctime);
				break;
			case COL_PWD_CTIME_MIN:
				rc = scols_line_set_data(ln, n, user->pwd_ctime_min);
				break;
			case COL_PWD_CTIME_MAX:
				rc = scols_line_set_data(ln, n, user->pwd_ctime_max);
				break;
#ifdef HAVE_LIBSELINUX
			case COL_SELINUX:
				rc = scols_line_set_data(ln, n, user->context);
				break;
#endif
			default:
				/* something went very wrong here */
				err(EXIT_FAILURE, _("internal error: unknown column"));
		}

		if (rc != 0)
			err(EXIT_FAILURE, _("failed to set data"));
		++n;
	}
	return;
}
static int print_pretty(struct libscols_table *tb)
{
	struct libscols_iter *itr = scols_new_iter(SCOLS_ITER_FORWARD);
	struct libscols_column *col;
	struct libscols_cell *data;
	struct libscols_line *ln;
	const char *hstr, *dstr;
	int n = 0;

	ln = scols_table_get_line(tb, 0);
	while (!scols_table_next_column(tb, itr, &col)) {

		data = scols_line_get_cell(ln, n);

		hstr = coldescs[columns[n]].pretty_name;
		dstr = scols_cell_get_data(data);

		printf("%s:%*c%-36s\n", hstr, 26 - (int)strlen(hstr), ' ', dstr);
		++n;
	}

	scols_free_iter(itr);
	return 0;

}
static int print_user_table(struct lslogins_control *ctl)
{
	tb = setup_table();
	if (!tb)
		return -1;

	twalk(ctl->usertree, fill_table);
	if (outmode == out_pretty)
		print_pretty(tb);
	else
		scols_print_table(tb);
	return 0;
}

static void free_user(void *f)
{
	struct lslogins_user *u = f;
	free(u->login);
	free(u->group);
	free(u->gecos);
	free(u->sgroups);
	free(u->pwd_ctime);
	free(u->pwd_warn);
	free(u->pwd_ctime_min);
	free(u->pwd_ctime_max);
	free(u->last_login);
	free(u->last_tty);
	free(u->last_hostname);
	free(u->failed_login);
	free(u->failed_tty);
	free(u->homedir);
	free(u->shell);
	free(u->pwd_status);
#ifdef HAVE_LIBSELINUX
	freecon(u->context);
#endif
	free(u);
}
struct lslogins_timefmt {
	const char *name;
	int val;
};
static struct lslogins_timefmt timefmts[] = {
	{ "short", TIME_SHORT_RELATIVE },
	{ "full", TIME_FULL },
	{ "iso", TIME_ISO },
};

int main(int argc, char *argv[])
{
	int c, want_wtmp = 0, want_btmp = 0;
	char *logins = NULL, *groups = NULL;
	char *path_wtmp = _PATH_WTMP, *path_btmp = _PATH_BTMP;
	struct lslogins_control *ctl = xcalloc(1, sizeof(struct lslogins_control));

	/* long only options. */
	enum {
		OPT_LAST = CHAR_MAX + 1,
		OPT_VER,
		OPT_WTMP,
		OPT_BTMP,
		OPT_NOTRUNC,
		OPT_FULLT,
		OPT_TIME_FMT,
	};

	static const struct option longopts[] = {
		{ "acc-expiration", no_argument,	0, 'a' },
		{ "colon",          no_argument,	0, 'c' },
		{ "export",         no_argument,	0, 'e' },
		{ "failed",         no_argument,	0, 'f' },
		{ "fulltimes",      no_argument,	0, OPT_FULLT },
		{ "groups",         required_argument,	0, 'g' },
		{ "help",           no_argument,	0, 'h' },
		{ "iso",            no_argument,	0, 'i' },
		{ "logins",         required_argument,	0, 'l' },
		{ "supp-groups",    no_argument,	0, 'm' },
		{ "newline",        no_argument,	0, 'n' },
		{ "notruncate",     no_argument,	0, OPT_NOTRUNC },
		{ "output",         required_argument,	0, 'o' },
		{ "last",           no_argument,	0, OPT_LAST },
		{ "raw",            no_argument,	0, 'r' },
		{ "system-accs",    no_argument,	0, 's' },
		{ "sort-by-name",   no_argument,	0, 't' },
		{ "time-format",    required_argument,	0, OPT_TIME_FMT },
		{ "user-accs",      no_argument,	0, 'u' },
		{ "version",        no_argument,	0, OPT_VER },
		{ "extra",          no_argument,	0, 'x' },
		{ "print0",         no_argument,	0, 'z' },
		/* TODO: find a reasonable way to do this for passwd/group/shadow,
		 * as libc itself doesn't supply any way to get a specific
		 * entry from a user-specified file */
		{ "wtmp-file",      required_argument,	0, OPT_WTMP },
		{ "btmp-file",      required_argument,	0, OPT_BTMP },
#ifdef HAVE_LIBSELINUX
		{ "context",        no_argument,	0, 'Z' },
#endif
		{ NULL,             0, 			0,  0  }
	};

	static const ul_excl_t excl[] = {	/* rows and cols in ASCII order */
		{ 'c','e','n','r','z' },
		{ 'i', OPT_FULLT, OPT_TIME_FMT  },
		{ 0 }
	};
	int excl_st[ARRAY_SIZE(excl)] = UL_EXCL_STATUS_INIT;

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	ctl->cmp_fn = cmp_uid;
	ctl->time_mode = TIME_SHORT_RELATIVE;

	while ((c = getopt_long(argc, argv, "acefg:hil:mno:rstuxzZ",
				longopts, NULL)) != -1) {

		err_exclusive_options(c, longopts, excl, excl_st);

		switch (c) {
			case 'a':
				lslogins_flag |= F_EXPIR;
				break;
			case 'c':
				outmode = out_colon;
				break;
			case 'e':
				outmode = out_export;
				break;
			case 'f':
				lslogins_flag |= F_FAIL;
				break;
			case 'g':
				groups = optarg;
				break;
			case 'h':
				usage(stdout);
			case 'i':
				ctl->time_mode = TIME_ISO;
				break;
			case 'l':
				logins = optarg;
				break;
			case 'm':
				lslogins_flag |= F_MORE;
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
				lslogins_flag |= F_LAST;
				break;
			case 's':
				ctl->SYS_UID_MIN = getlogindefs_num("SYS_UID_MIN", UL_SYS_UID_MIN);
				ctl->SYS_UID_MAX = getlogindefs_num("SYS_UID_MAX", UL_SYS_UID_MAX);
				lslogins_flag |= F_SYSAC;
				break;
			case 't':
				ctl->cmp_fn = cmp_uname;
				lslogins_flag |= F_SORT;
				break;
			case 'u':
				ctl->UID_MIN = getlogindefs_num("UID_MIN", UL_UID_MIN);
				ctl->UID_MAX = getlogindefs_num("UID_MAX", UL_UID_MAX);
				lslogins_flag |= F_USRAC;
				break;
			case OPT_VER:
				printf(_("%s from %s\n"), program_invocation_short_name,
				       PACKAGE_STRING);
				return EXIT_SUCCESS;
			case 'x':
				lslogins_flag |= F_EXTRA;
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
			case OPT_NOTRUNC:
				coldescs[COL_GECOS].flag = 0;
				break;
			case OPT_FULLT:
				ctl->time_mode = TIME_FULL;
				break;
			case OPT_TIME_FMT:
				{
					size_t i;

					for (i = 0; i < ARRAY_SIZE(timefmts); i++) {
						if (strcmp(timefmts[i].name, optarg) == 0) {
							ctl->time_mode = timefmts[i].val;
							break;
						}
					}
					if (ctl->time_mode == TIME_INVALID)
						usage(stderr);
				}
				break;
			case 'Z':
#ifdef HAVE_LIBSELINUX
				lslogins_flag |= F_SELINUX;
				ctl->sel_enabled = is_selinux_enabled();
				if (ctl->sel_enabled == -1)
					exit(1);
#endif
				break;
			default:
				usage(stderr);
		}
	}

	if (argc - optind == 1) {
		if (strchr(argv[optind], ','))
			err(EXIT_FAILURE, "%s", "Only one user may be specified. Use -l for multiple users");
		logins = argv[optind];
		outmode = out_pretty;
	}
	else if (argc != optind)
		usage(stderr);

	/* lslogins -u -s == lslogins */
	if (lslogins_flag & F_USRAC && lslogins_flag & F_SYSAC)
		lslogins_flag &= ~(F_USRAC | F_SYSAC);

	if (!ncolumns) {
		if (lslogins_flag & F_SORT) {
			columns[ncolumns++] = COL_LOGIN;
			columns[ncolumns++] = COL_UID;
		}
		else {
			columns[ncolumns++] = COL_UID;
			columns[ncolumns++] = COL_LOGIN;
		}
		columns[ncolumns++] = COL_PGRP;
		columns[ncolumns++] = COL_PGID;
		columns[ncolumns++] = COL_LAST_LOGIN;

		want_wtmp = 1;

		if (lslogins_flag & F_NOPWD) {
			columns[ncolumns++] = COL_NOPASSWD;
		}
		if (lslogins_flag & F_MORE) {
			columns[ncolumns++] = COL_SGRPS;
		}
		if (lslogins_flag & F_EXPIR) {
			columns[ncolumns++] = COL_PWD_CTIME;
			columns[ncolumns++] = COL_PWD_EXPIR;
		}
		if (lslogins_flag & F_LAST) {
			columns[ncolumns++] = COL_LAST_TTY;
			columns[ncolumns++] = COL_LAST_HOSTNAME;
		}
		if (lslogins_flag & F_FAIL) {
			columns[ncolumns++] = COL_FAILED_LOGIN;
			columns[ncolumns++] = COL_FAILED_TTY;
			want_btmp = 1;
		}
		if (lslogins_flag & F_EXTRA) {
			columns[ncolumns++] = COL_HOME;
			columns[ncolumns++] = COL_SHELL;
			columns[ncolumns++] = COL_GECOS;
			columns[ncolumns++] = COL_NOPASSWD;
			columns[ncolumns++] = COL_NOLOGIN;
			columns[ncolumns++] = COL_LOCKED;
			columns[ncolumns++] = COL_HUSH_STATUS;
			columns[ncolumns++] = COL_PWD_WARN;
			columns[ncolumns++] = COL_PWD_CTIME_MIN; /*?*/
			columns[ncolumns++] = COL_PWD_CTIME_MAX; /*?*/
		}
		if (lslogins_flag & F_SELINUX)
			columns[ncolumns++] = COL_SELINUX;
	}
	else {
		int n = 0, i;
		while (n < ncolumns) {
			i = columns[n++];
			if (i <= COL_LAST_HOSTNAME && i >= COL_LAST_LOGIN)
				want_wtmp = 1;
			if (i == COL_FAILED_TTY && i >= COL_FAILED_LOGIN)
				want_btmp = 1;
		}
	}

	if (want_wtmp)
		parse_wtmp(ctl, path_wtmp);
	if (want_btmp)
		parse_btmp(ctl, path_btmp);

	get_ulist(ctl, logins, groups);

	if (create_usertree(ctl))
		return EXIT_FAILURE;

	print_user_table(ctl);

	scols_unref_table(tb);
	tdestroy(ctl->usertree, free_user);
	free_ctl(ctl);


	return EXIT_SUCCESS;
}
