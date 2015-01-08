/*
 * lslogins - List information about users on the system
 *
 * Copyright (C) 2014 Ondrej Oprala <ooprala@redhat.com>
 * Copyright (C) 2014 Karel Zak <kzak@redhat.com>
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
# include <selinux/selinux.h>
#endif

#ifdef HAVE_LIBSYSTEMD
# include <systemd/sd-journal.h>
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
#include "procutils.h"

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
	OUT_COLON = 1,
	OUT_EXPORT,
	OUT_NEWLINE,
	OUT_RAW,
	OUT_NUL,
	OUT_PRETTY
};

struct lslogins_user {
	char *login;
	uid_t uid;
	char *group;
	gid_t gid;
	char *gecos;

	int pwd_empty;
	int nologin;
	int pwd_lock;
	int pwd_deny;

	gid_t *sgroups;
	size_t nsgroups;

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
	char *nprocs;

};

/*
 * time modes
 * */
enum {
	TIME_INVALID = 0,
	TIME_SHORT,
	TIME_FULL,
	TIME_ISO,
	TIME_ISO_SHORT,
};

/*
 * flags
 */
enum {
	F_SYSAC	= (1 << 3),
	F_USRAC	= (1 << 4),
};

/*
 * IDs
 */
enum {
	COL_USER = 0,
	COL_UID,
	COL_GECOS,
	COL_HOME,
	COL_SHELL,
	COL_NOLOGIN,
	COL_PWDLOCK,
	COL_PWDEMPTY,
	COL_PWDDENY,
	COL_GROUP,
	COL_GID,
	COL_SGROUPS,
	COL_SGIDS,
	COL_LAST_LOGIN,
	COL_LAST_TTY,
	COL_LAST_HOSTNAME,
	COL_FAILED_LOGIN,
	COL_FAILED_TTY,
	COL_HUSH_STATUS,
	COL_PWD_WARN,
	COL_PWD_CTIME,
	COL_PWD_CTIME_MIN,
	COL_PWD_CTIME_MAX,
	COL_PWD_EXPIR,
	COL_SELINUX,
	COL_NPROCS,
};

#define is_wtmp_col(x)	((x) == COL_LAST_LOGIN     || \
			 (x) == COL_LAST_TTY       || \
			 (x) == COL_LAST_HOSTNAME)

#define is_btmp_col(x)	((x) == COL_FAILED_LOGIN   || \
			 (x) == COL_FAILED_TTY)

enum {
	STATUS_FALSE = 0,
	STATUS_TRUE,
	STATUS_UNKNOWN
};

static const char *const status[] = {
	[STATUS_FALSE]	= "0",
	[STATUS_TRUE]	= "1",
	[STATUS_UNKNOWN]= NULL
};

static const char *const pretty_status[] = {
	[STATUS_FALSE]	= N_("no"),
	[STATUS_TRUE]	= N_("yes"),
	[STATUS_UNKNOWN]= NULL
};

#define get_status(x)	(outmode == OUT_PRETTY ? pretty_status[(x)] : status[(x)])

static const struct lslogins_coldesc coldescs[] =
{
	[COL_USER]          = { "USER",		N_("user name"), N_("Username"), 0.1, SCOLS_FL_NOEXTREMES },
	[COL_UID]           = { "UID",		N_("user ID"), "UID", 1, SCOLS_FL_RIGHT},
	[COL_PWDEMPTY]      = { "PWD-EMPTY",	N_("password not required"), N_("Password not required"), 1, SCOLS_FL_RIGHT },
	[COL_PWDDENY]       = { "PWD-DENY",	N_("login by password disabled"), N_("Login by password disabled"), 1, SCOLS_FL_RIGHT },
	[COL_PWDLOCK]       = { "PWD-LOCK",	N_("password defined, but locked"), N_("Password is locked"), 1, SCOLS_FL_RIGHT },
	[COL_NOLOGIN]       = { "NOLOGIN",	N_("log in disabled by nologin(8) or pam_nologin(8)"), N_("No login"), 1, SCOLS_FL_RIGHT },
	[COL_GROUP]         = { "GROUP",	N_("primary group name"), N_("Primary group"), 0.1 },
	[COL_GID]           = { "GID",		N_("primary group ID"), "GID", 1, SCOLS_FL_RIGHT },
	[COL_SGROUPS]       = { "SUPP-GROUPS",	N_("supplementary group names"), N_("Supplementary groups"), 0.1 },
	[COL_SGIDS]         = { "SUPP-GIDS",    N_("supplementary group IDs"), N_("Supplementary group IDs"), 0.1 },
	[COL_HOME]          = { "HOMEDIR",	N_("home directory"), N_("Home directory"), 0.1 },
	[COL_SHELL]         = { "SHELL",	N_("login shell"), N_("Shell"), 0.1 },
	[COL_GECOS]         = { "GECOS",	N_("full user name"), N_("Gecos field"), 0.1, SCOLS_FL_TRUNC },
	[COL_LAST_LOGIN]    = { "LAST-LOGIN",	N_("date of last login"), N_("Last login"), 0.1, SCOLS_FL_RIGHT },
	[COL_LAST_TTY]      = { "LAST-TTY",	N_("last tty used"), N_("Last terminal"), 0.05 },
	[COL_LAST_HOSTNAME] = { "LAST-HOSTNAME",N_("hostname during the last session"), N_("Last hostname"),  0.1},
	[COL_FAILED_LOGIN]  = { "FAILED-LOGIN",	N_("date of last failed login"), N_("Failed login"), 0.1 },
	[COL_FAILED_TTY]    = { "FAILED-TTY",	N_("where did the login fail?"), N_("Failed login terminal"), 0.05 },
	[COL_HUSH_STATUS]   = { "HUSHED",	N_("user's hush settings"), N_("Hushed"), 1, SCOLS_FL_RIGHT },
	[COL_PWD_WARN]      = { "PWD-WARN",	N_("days user is warned of password expiration"), N_("Password expiration warn interval"), 0.1, SCOLS_FL_RIGHT },
	[COL_PWD_EXPIR]     = { "PWD-EXPIR",	N_("password expiration date"), N_("Password expiration"), 0.1, SCOLS_FL_RIGHT },
	[COL_PWD_CTIME]     = { "PWD-CHANGE",	N_("date of last password change"), N_("Password changed"), 0.1, SCOLS_FL_RIGHT},
	[COL_PWD_CTIME_MIN] = { "PWD-MIN",	N_("number of days required between changes"), N_("Minimum change time"), 0.1, SCOLS_FL_RIGHT },
	[COL_PWD_CTIME_MAX] = { "PWD-MAX",	N_("max number of days a password may remain unchanged"), N_("Maximum change time"), 0.1, SCOLS_FL_RIGHT },
	[COL_SELINUX]       = { "CONTEXT",	N_("the user's security context"), N_("Selinux context"), 0.1 },
	[COL_NPROCS]        = { "PROC",         N_("number of processes run by the user"), N_("Running processes"), 1, SCOLS_FL_RIGHT },
};

struct lslogins_control {
	struct utmp *wtmp;
	size_t wtmp_size;

	struct utmp *btmp;
	size_t btmp_size;

	void *usertree;

	uid_t uid;
	uid_t UID_MIN;
	uid_t UID_MAX;

	uid_t SYS_UID_MIN;
	uid_t SYS_UID_MAX;

	char **ulist;
	size_t ulsiz;

	unsigned int time_mode;

	const char *journal_path;

	unsigned int selinux_enabled : 1,
		     ulist_on : 1,
		     noheadings : 1,
		     notrunc : 1;
};

/* these have to remain global since there's no other reasonable way to pass
 * them for each call of fill_table() via twalk() */
static struct libscols_table *tb;

/* columns[] array specifies all currently wanted output column. The columns
 * are defined by coldescs[] array and you can specify (on command line) each
 * column twice. That's enough, dynamically allocated array of the columns is
 * unnecessary overkill and over-engineering in this case */
static int columns[ARRAY_SIZE(coldescs) * 2];
static int ncolumns;

static inline size_t err_columns_index(size_t arysz, size_t idx)
{
	if (idx >= arysz)
		errx(EXIT_FAILURE, _("too many columns specified, "
				     "the limit is %zu columns"),
				arysz - 1);
	return idx;
}

#define add_column(ary, n, id)	\
		((ary)[ err_columns_index(ARRAY_SIZE(ary), (n)) ] = (id))

static struct timeval now;

static int date_is_today(time_t t)
{
	if (now.tv_sec == 0)
		gettimeofday(&now, NULL);
	return t / (3600 * 24) == now.tv_sec / (3600 * 24);
}

static int date_is_thisyear(time_t t)
{
	if (now.tv_sec == 0)
		gettimeofday(&now, NULL);
	return t / (3600 * 24 * 365) == now.tv_sec / (3600 * 24 * 365);
}

static int column_name_to_id(const char *name, size_t namesz)
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

static char *make_time(int mode, time_t time)
{
	char *s;
	struct tm tm;
	char buf[64] = {0};

	localtime_r(&time, &tm);

	switch(mode) {
	case TIME_FULL:
		asctime_r(&tm, buf);
		if (*(s = buf + strlen(buf) - 1) == '\n')
			*s = '\0';
		break;
	case TIME_SHORT:
		if (date_is_today(time))
			strftime(buf, sizeof(buf), "%H:%M:%S", &tm);
		else if (date_is_thisyear(time))
			strftime(buf, sizeof(buf), "%b%d/%H:%M", &tm);
		else
			strftime(buf, sizeof(buf), "%Y-%b%d", &tm);
		break;
	case TIME_ISO:
		strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S%z", &tm);
		break;
	case TIME_ISO_SHORT:
		strftime(buf, sizeof(buf), "%Y-%m-%d", &tm);
		break;
	default:
		errx(EXIT_FAILURE, _("unsupported time type"));
	}
	return xstrdup(buf);
}


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

static char *build_sgroups_string(gid_t *sgroups, size_t nsgroups, int want_names)
{
	size_t n = 0, maxlen, len;
	char *res, *p;

	if (!nsgroups)
		return NULL;

	len = maxlen = nsgroups * 10;
	res = p = xmalloc(maxlen);

	while (n < nsgroups) {
		int x;
again:
		if (!want_names)
			x = snprintf(p, len, "%u,", sgroups[n]);
		else {
			struct group *grp = getgrgid(sgroups[n]);
			if (!grp) {
				free(res);
				return NULL;
			}
			x = snprintf(p, len, "%s,", grp->gr_name);
		}

		if (x < 0 || (size_t) x + 1 > len) {
			size_t cur = p - res;

			maxlen *= 2;
			res = xrealloc(res, maxlen);
			p = res + cur;
			len = maxlen - cur;
			goto again;
		}

		len -= x;
		p += x;
		++n;
	}

	if (p > res)
		*(p - 1) = '\0';

	return res;
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

static int require_wtmp(void)
{
	size_t i;
	for (i = 0; i < (size_t) ncolumns; i++)
		if (is_wtmp_col(columns[i]))
			return 1;
	return 0;
}

static int require_btmp(void)
{
	size_t i;
	for (i = 0; i < (size_t) ncolumns; i++)
		if (is_btmp_col(columns[i]))
			return 1;
	return 0;
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

static int get_sgroups(gid_t **list, size_t *len, struct passwd *pwd)
{
	size_t n = 0;

	*len = 0;
	*list = NULL;

	/* first let's get a supp. group count */
	getgrouplist(pwd->pw_name, pwd->pw_gid, *list, (int *) len);
	if (!*len)
		return -1;

	*list = xcalloc(1, *len * sizeof(gid_t));

	/* now for the actual list of GIDs */
	if (-1 == getgrouplist(pwd->pw_name, pwd->pw_gid, *list, (int *) len))
		return -1;

	/* getgroups also returns the user's primary GID - dispose of it */
	while (n < *len) {
		if ((*list)[n] == pwd->pw_gid)
			break;
		++n;
	}

	if (*len)
		(*list)[n] = (*list)[--(*len)];
	return 0;
}

static int get_nprocs(const uid_t uid)
{
	int nprocs = 0;
	pid_t pid;
	struct proc_processes *proc = proc_open_processes();

	proc_processes_filter_by_uid(proc, uid);

	while (!proc_next_pid(proc, &pid))
		++nprocs;

	proc_close_processes(proc);
	return nprocs;
}

static int valid_pwd(const char *str)
{
	const char *p;

	for (p = str; p && *p; p++)
		if (!isalnum((unsigned int) *p))
			return 0;
	return p > str ? 1 : 0;
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

	pwd = username ? getpwnam(username) : getpwent();
	if (!pwd)
		return NULL;

	ctl->uid = uid = pwd->pw_uid;

	/* nfsnobody is an exception to the UID_MAX limit.  This is "nobody" on
	 * some systems; the decisive point is the UID - 65534 */
	if ((lslogins_flag & F_USRAC) &&
	    strcmp("nfsnobody", pwd->pw_name) != 0 &&
	    uid != 0) {
		if (uid < ctl->UID_MIN || uid > ctl->UID_MAX) {
			errno = EAGAIN;
			return NULL;
		}

	} else if ((lslogins_flag & F_SYSAC) &&
		   (uid < ctl->SYS_UID_MIN || uid > ctl->SYS_UID_MAX)) {
		errno = EAGAIN;
		return NULL;
	}

	user = xcalloc(1, sizeof(struct lslogins_user));

	grp = getgrgid(pwd->pw_gid);
	if (!grp)
		return NULL;

	if (ctl->wtmp)
		user_wtmp = get_last_wtmp(ctl, pwd->pw_name);
	if (ctl->btmp)
		user_btmp = get_last_btmp(ctl, pwd->pw_name);

	lckpwdf();
	shadow = getspnam(pwd->pw_name);
	ulckpwdf();

	/* required  by tseach() stuff */
	user->uid = pwd->pw_uid;

	while (n < ncolumns) {
		switch (columns[n++]) {
		case COL_USER:
			user->login = xstrdup(pwd->pw_name);
			break;
		case COL_UID:
			user->uid = pwd->pw_uid;
			break;
		case COL_GROUP:
			user->group = xstrdup(grp->gr_name);
			break;
		case COL_GID:
			user->gid = pwd->pw_gid;
			break;
		case COL_SGROUPS:
		case COL_SGIDS:
			if (get_sgroups(&user->sgroups, &user->nsgroups, pwd))
				err(EXIT_FAILURE, _("failed to get supplementary groups"));
			break;
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
			break;
		case COL_LAST_TTY:
			if (user_wtmp)
				user->last_tty = xstrdup(user_wtmp->ut_line);
			break;
		case COL_LAST_HOSTNAME:
			if (user_wtmp)
				user->last_hostname = xstrdup(user_wtmp->ut_host);
			break;
		case COL_FAILED_LOGIN:
			if (user_btmp) {
				time = user_btmp->ut_tv.tv_sec;
				user->failed_login = make_time(ctl->time_mode, time);
			}
			break;
		case COL_FAILED_TTY:
			if (user_btmp)
				user->failed_tty = xstrdup(user_btmp->ut_line);
			break;
		case COL_HUSH_STATUS:
			user->hushed = get_hushlogin_status(pwd, 0);
			if (user->hushed == -1)
				user->hushed = STATUS_UNKNOWN;
			break;
		case COL_PWDEMPTY:
			if (shadow) {
				if (!*shadow->sp_pwdp) /* '\0' */
					user->pwd_empty = STATUS_TRUE;
			} else
				user->pwd_empty = STATUS_UNKNOWN;
			break;
		case COL_PWDDENY:
			if (shadow) {
				if ((*shadow->sp_pwdp == '!' ||
				     *shadow->sp_pwdp == '*') &&
				    !valid_pwd(shadow->sp_pwdp + 1))
					user->pwd_deny = STATUS_TRUE;
			} else
				user->pwd_deny = STATUS_UNKNOWN;
			break;

		case COL_PWDLOCK:
			if (shadow) {
				if (*shadow->sp_pwdp == '!' && valid_pwd(shadow->sp_pwdp + 1))
					user->pwd_lock = STATUS_TRUE;
			} else
				user->pwd_lock = STATUS_UNKNOWN;
			break;
		case COL_NOLOGIN:
			if (strstr(pwd->pw_shell, "nologin"))
				user->nologin = 1;
			else if (pwd->pw_uid)
				user->nologin = access(_PATH_NOLOGIN, F_OK) == 0 ||
						access(_PATH_VAR_NOLOGIN, F_OK) == 0;
			break;
		case COL_PWD_WARN:
			if (shadow && shadow->sp_warn >= 0)
				xasprintf(&user->pwd_warn, "%ld", shadow->sp_warn);
			break;
		case COL_PWD_EXPIR:
			if (shadow && shadow->sp_expire >= 0)
				user->pwd_expire = make_time(ctl->time_mode == TIME_ISO ?
						TIME_ISO_SHORT : ctl->time_mode,
						shadow->sp_expire * 86400);
			break;
		case COL_PWD_CTIME:
			/* sp_lstchg is specified in days, showing hours
			 * (especially in non-GMT timezones) would only serve
			 * to confuse */
			if (shadow)
				user->pwd_ctime = make_time(ctl->time_mode == TIME_ISO ?
						TIME_ISO_SHORT : ctl->time_mode,
						shadow->sp_lstchg * 86400);
			break;
		case COL_PWD_CTIME_MIN:
			if (shadow && shadow->sp_min > 0)
				xasprintf(&user->pwd_ctime_min, "%ld", shadow->sp_min);
			break;
		case COL_PWD_CTIME_MAX:
			if (shadow && shadow->sp_max > 0)
				xasprintf(&user->pwd_ctime_max, "%ld", shadow->sp_max);
			break;
		case COL_SELINUX:
#ifdef HAVE_LIBSELINUX
			if (ctl->selinux_enabled) {
				/* typedefs and pointers are pure evil */
				security_context_t con = NULL;
				if (getcon(&con) == 0)
					user->context = con;
			}
#endif
			break;
		case COL_NPROCS:
			xasprintf(&user->nprocs, "%d", get_nprocs(pwd->pw_uid));
			break;
		default:
			/* something went very wrong here */
			err(EXIT_FAILURE, "fatal: unknown error");
			break;
		}
	}

	return user;
}

/* some UNIX implementations set errno iff a passwd/grp/...
 * entry was not found. The original UNIX logins(1) utility always
 * ignores invalid login/group names, so we're going to as well.*/
#define IS_REAL_ERRNO(e) !((e) == ENOENT || (e) == ESRCH || \
		(e) == EBADF || (e) == EPERM || (e) == EAGAIN)

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

	if (logins) {
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
		ctl->ulist_on = 1;
	}

	if (groups) {
		/* FIXME: this might lead to duplicit entries, although not visible
		 * in output, crunching a user's info multiple times is very redundant */
		while ((g = strtok(groups, ","))) {
			n = 0;
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
		ctl->ulist_on = 1;
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

static int get_user(struct lslogins_control *ctl, struct lslogins_user **user,
		    const char *username)
{
	*user = get_user_info(ctl, username);
	if (!*user)
		if (IS_REAL_ERRNO(errno))
			return -1;
	return 0;
}

static int cmp_uid(const void *a, const void *b)
{
	uid_t x = ((struct lslogins_user *)a)->uid;
	uid_t z = ((struct lslogins_user *)b)->uid;
	return x > z ? 1 : (x < z ? -1 : 0);
}

static int create_usertree(struct lslogins_control *ctl)
{
	struct lslogins_user *user = NULL;
	size_t n = 0;

	if (ctl->ulist_on) {
		while (n < ctl->ulsiz) {
			if (get_user(ctl, &user, ctl->ulist[n]))
				return -1;
			if (user) /* otherwise an invalid user name has probably been given */
				tsearch(user, &ctl->usertree, cmp_uid);
			++n;
		}
	} else {
		while ((user = get_next_user(ctl)))
			tsearch(user, &ctl->usertree, cmp_uid);
	}
	return 0;
}

static struct libscols_table *setup_table(struct lslogins_control *ctl)
{
	struct libscols_table *table = scols_new_table();
	int n = 0;

	if (!table)
		errx(EXIT_FAILURE, _("failed to initialize output table"));
	if (ctl->noheadings)
		scols_table_enable_noheadings(table, 1);

	switch(outmode) {
	case OUT_COLON:
		scols_table_enable_raw(table, 1);
		scols_table_set_column_separator(table, ":");
		break;
	case OUT_NEWLINE:
		scols_table_set_column_separator(table, "\n");
		/* fallthrough */
	case OUT_EXPORT:
		scols_table_enable_export(table, 1);
		break;
	case OUT_NUL:
		scols_table_set_line_separator(table, "\0");
		/* fallthrough */
	case OUT_RAW:
		scols_table_enable_raw(table, 1);
		break;
	case OUT_PRETTY:
		scols_table_enable_noheadings(table, 1);
	default:
		break;
	}

	while (n < ncolumns) {
		int flags = coldescs[columns[n]].flag;

		if (ctl->notrunc)
			flags &= ~SCOLS_FL_TRUNC;

		if (!scols_table_new_column(table,
				coldescs[columns[n]].name,
				coldescs[columns[n]].whint,
				flags))
			goto fail;
		++n;
	}

	return table;
fail:
	scols_unref_table(table);
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
		case COL_USER:
			rc = scols_line_set_data(ln, n, user->login);
			break;
		case COL_UID:
			rc = scols_line_refer_data(ln, n, uidtostr(user->uid));
			break;
		case COL_PWDEMPTY:
			rc = scols_line_set_data(ln, n, get_status(user->pwd_empty));
			break;
		case COL_NOLOGIN:
			rc = scols_line_set_data(ln, n, get_status(user->nologin));
			break;
		case COL_PWDLOCK:
			rc = scols_line_set_data(ln, n, get_status(user->pwd_lock));
			break;
		case COL_PWDDENY:
			rc = scols_line_set_data(ln, n, get_status(user->pwd_deny));
			break;
		case COL_GROUP:
			rc = scols_line_set_data(ln, n, user->group);
			break;
		case COL_GID:
			rc = scols_line_refer_data(ln, n, gidtostr(user->gid));
			break;
		case COL_SGROUPS:
			rc = scols_line_refer_data(ln, n,
				build_sgroups_string(user->sgroups,
						     user->nsgroups,
						     TRUE));
			break;
		case COL_SGIDS:
			rc = scols_line_refer_data(ln, n,
				build_sgroups_string(user->sgroups,
						     user->nsgroups,
						     FALSE));
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
			rc = scols_line_set_data(ln, n, get_status(user->hushed));
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
		case COL_SELINUX:
#ifdef HAVE_LIBSELINUX
			rc = scols_line_set_data(ln, n, user->context);
#endif
			break;
		case COL_NPROCS:
			rc = scols_line_set_data(ln, n, user->nprocs);
			break;
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
#ifdef HAVE_LIBSYSTEMD
static void print_journal_tail(const char *journal_path, uid_t uid, size_t len, int time_mode)
{
	sd_journal *j;
	char *match, *timestamp;
	uint64_t x;
	time_t t;
	const char *identifier, *pid, *message;
	size_t identifier_len, pid_len, message_len;

	if (journal_path)
		sd_journal_open_directory(&j, journal_path, 0);
	else
		sd_journal_open(&j, SD_JOURNAL_LOCAL_ONLY);

	xasprintf(&match, "_UID=%d", uid);

	sd_journal_add_match(j, match, 0);
	sd_journal_seek_tail(j);
	sd_journal_previous_skip(j, len);

	do {
		if (0 > sd_journal_get_data(j, "SYSLOG_IDENTIFIER",
				(const void **) &identifier, &identifier_len))
			goto done;
		if (0 > sd_journal_get_data(j, "_PID",
				(const void **) &pid, &pid_len))
			goto done;
		if (0 > sd_journal_get_data(j, "MESSAGE",
				(const void **) &message, &message_len))
			goto done;

		sd_journal_get_realtime_usec(j, &x);
		t = x / 1000000;
		timestamp = make_time(time_mode, t);
		/* Get rid of journal entry field identifiers */
		identifier = strchr(identifier, '=') + 1;
		pid = strchr(pid, '=') + 1;
		message = strchr(message, '=') + 1;

		fprintf(stdout, "%s %s[%s]: %s\n", timestamp, identifier, pid,
			message);
		free(timestamp);
	} while (sd_journal_next(j));

done:
	free(match);
	sd_journal_flush_matches(j);
	sd_journal_close(j);
}
#endif

static int print_pretty(struct libscols_table *table)
{
	struct libscols_iter *itr = scols_new_iter(SCOLS_ITER_FORWARD);
	struct libscols_column *col;
	struct libscols_cell *data;
	struct libscols_line *ln;
	const char *hstr, *dstr;
	int n = 0;

	ln = scols_table_get_line(table, 0);
	while (!scols_table_next_column(table, itr, &col)) {

		data = scols_line_get_cell(ln, n);

		hstr = _(coldescs[columns[n]].pretty_name);
		dstr = scols_cell_get_data(data);

		if (dstr)
			printf("%s:%*c%-36s\n", hstr, 35 - (int)strlen(hstr), ' ', dstr);
		++n;
	}

	scols_free_iter(itr);
	return 0;

}

static int print_user_table(struct lslogins_control *ctl)
{
	tb = setup_table(ctl);
	if (!tb)
		return -1;

	twalk(ctl->usertree, fill_table);
	if (outmode == OUT_PRETTY) {
		print_pretty(tb);
#ifdef HAVE_LIBSYSTEMD
		fprintf(stdout, _("\nLast logs:\n"));
		print_journal_tail(ctl->journal_path, ctl->uid, 3, ctl->time_mode);
		fputc('\n', stdout);
#endif
	} else
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

static int parse_time_mode(const char *optarg)
{
	struct lslogins_timefmt {
		const char *name;
		const int val;
	};
	static const struct lslogins_timefmt timefmts[] = {
		{"iso", TIME_ISO},
		{"full", TIME_FULL},
		{"short", TIME_SHORT},
	};
	size_t i;

	for (i = 0; i < ARRAY_SIZE(timefmts); i++) {
		if (strcmp(timefmts[i].name, optarg) == 0)
			return timefmts[i].val;
	}
	errx(EXIT_FAILURE, _("unknown time format: %s"), optarg);
}

static void __attribute__((__noreturn__)) usage(FILE *out)
{
	size_t i;

	fputs(USAGE_HEADER, out);
	fprintf(out, _(" %s [options]\n"), program_invocation_short_name);

	fputs(USAGE_SEPARATOR, out);
	fputs(_("Display information about known users in the system.\n"), out);

	fputs(USAGE_OPTIONS, out);
	fputs(_(" -a, --acc-expiration     display info about passwords expiration\n"), out);
	fputs(_(" -c, --colon-separate     display data in a format similar to /etc/passwd\n"), out);
	fputs(_(" -e, --export             display in an export-able output format\n"), out);
	fputs(_(" -f, --failed             display data about the users' last failed logins\n"), out);
	fputs(_(" -G, --supp-groups        display information about groups\n"), out);
	fputs(_(" -g, --groups=<groups>    display users belonging to a group in <groups>\n"), out);
	fputs(_(" -L, --last               show info about the users' last login sessions\n"), out);
	fputs(_(" -l, --logins=<logins>    display only users from <logins>\n"), out);
	fputs(_(" -n, --newline            display each piece of information on a new line\n"), out);
	fputs(_("     --noheadings         don't print headings\n"), out);
	fputs(_("     --notruncate         don't truncate output\n"), out);
	fputs(_(" -o, --output[=<list>]    define the columns to output\n"), out);
	fputs(_(" -p, --pwd                display information related to login by password.\n"), out);
	fputs(_(" -r, --raw                display in raw mode\n"), out);
	fputs(_(" -s, --system-accs        display system accounts\n"), out);
	fputs(_("     --time-format=<type> display dates in short, full or iso format\n"), out);
	fputs(_(" -u, --user-accs          display user accounts\n"), out);
	fputs(_(" -Z, --context            display SELinux contexts\n"), out);
	fputs(_(" -z, --print0             delimit user entries with a nul character\n"), out);
	fputs(_("     --wtmp-file <path>   set an alternate path for wtmp\n"), out);
	fputs(_("     --btmp-file <path>   set an alternate path for btmp\n"), out);
	fputs(USAGE_SEPARATOR, out);
	fputs(USAGE_HELP, out);
	fputs(USAGE_VERSION, out);

	fprintf(out, _("\nAvailable columns:\n"));

	for (i = 0; i < ARRAY_SIZE(coldescs); i++)
		fprintf(out, " %14s  %s\n", coldescs[i].name,
				_(coldescs[i].help));

	fprintf(out, USAGE_MAN_TAIL("lslogins(1)"));

	exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}

int main(int argc, char *argv[])
{
	int c, opt_o = 0;
	char *logins = NULL, *groups = NULL;
	char *path_wtmp = _PATH_WTMP, *path_btmp = _PATH_BTMP;
	struct lslogins_control *ctl = xcalloc(1, sizeof(struct lslogins_control));
	size_t i;

	/* long only options. */
	enum {
		OPT_WTMP = CHAR_MAX + 1,
		OPT_BTMP,
		OPT_NOTRUNC,
		OPT_NOHEAD,
		OPT_TIME_FMT,
	};

	static const struct option longopts[] = {
		{ "acc-expiration", no_argument,	0, 'a' },
		{ "colon-separate", no_argument,	0, 'c' },
		{ "export",         no_argument,	0, 'e' },
		{ "failed",         no_argument,	0, 'f' },
		{ "groups",         required_argument,	0, 'g' },
		{ "help",           no_argument,	0, 'h' },
		{ "logins",         required_argument,	0, 'l' },
		{ "supp-groups",    no_argument,	0, 'G' },
		{ "newline",        no_argument,	0, 'n' },
		{ "notruncate",     no_argument,	0, OPT_NOTRUNC },
		{ "noheadings",     no_argument,	0, OPT_NOHEAD },
		{ "output",         required_argument,	0, 'o' },
		{ "last",           no_argument,	0, 'L', },
		{ "raw",            no_argument,	0, 'r' },
		{ "system-accs",    no_argument,	0, 's' },
		{ "time-format",    required_argument,	0, OPT_TIME_FMT },
		{ "user-accs",      no_argument,	0, 'u' },
		{ "version",        no_argument,	0, 'V' },
		{ "pwd",            no_argument,	0, 'p' },
		{ "print0",         no_argument,	0, 'z' },
		{ "wtmp-file",      required_argument,	0, OPT_WTMP },
		{ "btmp-file",      required_argument,	0, OPT_BTMP },
#ifdef HAVE_LIBSELINUX
		{ "context",        no_argument,	0, 'Z' },
#endif
		{ NULL,             0, 			0,  0  }
	};

	static const ul_excl_t excl[] = {	/* rows and cols in ASCII order */
		{ 'G', 'o' },
		{ 'L', 'o' },
		{ 'Z', 'o' },
		{ 'a', 'o' },
		{ 'c','n','r','z' },
		{ 'o', 'p' },
		{ 0 }
	};
	int excl_st[ARRAY_SIZE(excl)] = UL_EXCL_STATUS_INIT;

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	ctl->time_mode = TIME_SHORT;

	/* very basic default */
	add_column(columns, ncolumns++, COL_UID);
	add_column(columns, ncolumns++, COL_USER);

	while ((c = getopt_long(argc, argv, "acefGg:hLl:no:prsuVzZ",
				longopts, NULL)) != -1) {

		err_exclusive_options(c, longopts, excl, excl_st);

		switch (c) {
		case 'a':
			add_column(columns, ncolumns++, COL_PWD_WARN);
			add_column(columns, ncolumns++, COL_PWD_CTIME_MIN);
			add_column(columns, ncolumns++, COL_PWD_CTIME_MAX);
			add_column(columns, ncolumns++, COL_PWD_CTIME);
			add_column(columns, ncolumns++, COL_PWD_EXPIR);
			break;
		case 'c':
			outmode = OUT_COLON;
			break;
		case 'e':
			outmode = OUT_EXPORT;
			break;
		case 'f':
			add_column(columns, ncolumns++, COL_FAILED_LOGIN);
			add_column(columns, ncolumns++, COL_FAILED_TTY);
			break;
		case 'G':
			add_column(columns, ncolumns++, COL_GID);
			add_column(columns, ncolumns++, COL_GROUP);
			add_column(columns, ncolumns++, COL_SGIDS);
			add_column(columns, ncolumns++, COL_SGROUPS);
			break;
		case 'g':
			groups = optarg;
			break;
		case 'h':
			usage(stdout);
			break;
		case 'L':
			add_column(columns, ncolumns++, COL_LAST_TTY);
			add_column(columns, ncolumns++, COL_LAST_HOSTNAME);
			add_column(columns, ncolumns++, COL_LAST_LOGIN);
			break;
		case 'l':
			logins = optarg;
			break;
		case 'n':
			outmode = OUT_NEWLINE;
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
			opt_o = 1;
			break;
		case 'r':
			outmode = OUT_RAW;
			break;
		case 's':
			ctl->SYS_UID_MIN = getlogindefs_num("SYS_UID_MIN", UL_SYS_UID_MIN);
			ctl->SYS_UID_MAX = getlogindefs_num("SYS_UID_MAX", UL_SYS_UID_MAX);
			lslogins_flag |= F_SYSAC;
			break;
		case 'u':
			ctl->UID_MIN = getlogindefs_num("UID_MIN", UL_UID_MIN);
			ctl->UID_MAX = getlogindefs_num("UID_MAX", UL_UID_MAX);
			lslogins_flag |= F_USRAC;
			break;
		case 'p':
			add_column(columns, ncolumns++, COL_PWDEMPTY);
			add_column(columns, ncolumns++, COL_PWDLOCK);
			add_column(columns, ncolumns++, COL_PWDDENY);
			add_column(columns, ncolumns++, COL_NOLOGIN);
			add_column(columns, ncolumns++, COL_HUSH_STATUS);
			break;
		case 'z':
			outmode = OUT_NUL;
			break;
		case OPT_WTMP:
			path_wtmp = optarg;
			break;
		case OPT_BTMP:
			path_btmp = optarg;
			break;
		case OPT_NOTRUNC:
			ctl->notrunc = 1;
			break;
		case OPT_NOHEAD:
			ctl->noheadings = 1;
			break;
		case OPT_TIME_FMT:
			ctl->time_mode = parse_time_mode(optarg);
			break;
		case 'V':
			printf(UTIL_LINUX_VERSION);
			return EXIT_SUCCESS;
		case 'Z':
		{
#ifdef HAVE_LIBSELINUX
			int sl = is_selinux_enabled();
			if (sl < 0)
				warn(_("failed to request selinux state"));
			else
				ctl->selinux_enabled = sl == 1;
#endif
			add_column(columns, ncolumns++, COL_SELINUX);
			break;
		}
		default:
			usage(stderr);
		}
	}

	if (argc - optind == 1) {
		if (strchr(argv[optind], ','))
			errx(EXIT_FAILURE, _("Only one user may be specified. Use -l for multiple users."));
		logins = argv[optind];
		outmode = OUT_PRETTY;
	} else if (argc != optind)
		errx(EXIT_FAILURE, _("Only one user may be specified. Use -l for multiple users."));

	scols_init_debug(0);

	/* lslogins -u -s == lslogins */
	if (lslogins_flag & F_USRAC && lslogins_flag & F_SYSAC)
		lslogins_flag &= ~(F_USRAC | F_SYSAC);

	if (outmode == OUT_PRETTY && !opt_o) {
		/* all columns for lslogins <username> */
		for (ncolumns = 0, i = 0; i < ARRAY_SIZE(coldescs); i++)
			 columns[ncolumns++] = i;

	} else if (ncolumns == 2 && !opt_o) {
		/* default colummns */
		add_column(columns, ncolumns++, COL_NPROCS);
		add_column(columns, ncolumns++, COL_PWDLOCK);
		add_column(columns, ncolumns++, COL_PWDDENY);
		add_column(columns, ncolumns++, COL_LAST_LOGIN);
		add_column(columns, ncolumns++, COL_GECOS);
	}

	if (require_wtmp())
		parse_wtmp(ctl, path_wtmp);
	if (require_btmp())
		parse_btmp(ctl, path_btmp);

	if (logins || groups)
		get_ulist(ctl, logins, groups);

	if (create_usertree(ctl))
		return EXIT_FAILURE;

	print_user_table(ctl);

	scols_unref_table(tb);
	tdestroy(ctl->usertree, free_user);
	free_ctl(ctl);

	return EXIT_SUCCESS;
}
