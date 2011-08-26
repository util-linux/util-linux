/*
 * login(1)
 *
 * This program is derived from 4.3 BSD software and is subject to the
 * copyright notice below.
 *
 * Michael Glad (glad@daimi.dk)
 * Computer Science Department, Aarhus University, Denmark
 * 1990-07-04
 *
 * Copyright (c) 1980, 1987, 1988 The Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that the above copyright notice and this paragraph are
 * duplicated in all such forms and that any documentation,
 * advertising materials, and other materials related to such
 * distribution and use acknowledge that the software was developed
 * by the University of California, Berkeley.  The name of the
 * University may not be used to endorse or promote products derived
 * from this software without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */
#include <sys/param.h>
#include <stdio.h>
#include <ctype.h>
#include <unistd.h>
#include <getopt.h>
#include <memory.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/file.h>
#include <termios.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <utmp.h>
#include <setjmp.h>
#include <stdlib.h>
#include <sys/syslog.h>
#include <sys/sysmacros.h>
#include <linux/major.h>
#include <netdb.h>
#include <lastlog.h>
#include <security/pam_appl.h>
#include <security/pam_misc.h>

#ifdef HAVE_LIBAUDIT
# include <libaudit.h>
#endif
#ifdef HAVE_CRYPT_H
# include <crypt.h>
#endif

#include "c.h"
#include "setproctitle.h"
#include "pathnames.h"
#include "login.h"
#include "strutils.h"
#include "nls.h"
#include "xalloc.h"
#include "writeall.h"

#define is_pam_failure(_rc)	((_rc) != PAM_SUCCESS)

#define LOGIN_MAX_TRIES        3
#define LOGIN_EXIT_TIMEOUT     5
#define LOGIN_TIMEOUT          60

#ifdef USE_TTY_GROUP
# define TTY_MODE 0620
#else
# define TTY_MODE 0600
#endif

#define	TTYGRPNAME	"tty"	/* name of group to own ttys */
#define VCS_PATH_MAX	64

/*
 * Login control struct
 */
struct login_context {
	const char	*tty_path;	/* ttyname() return value */
	const char	*tty_name;	/* tty_path without /dev prefix */
	const char	*tty_number;	/* end of the tty_path */

	char		*username;	/* from command line or PAM */

#ifdef LOGIN_CHOWN_VCS
	char		vcsn[VCS_PATH_MAX];	/* virtual console name */
	char		vcsan[VCS_PATH_MAX];
#endif

	char		*hostname;
	char		hostaddress[16];

	pid_t		pid;
	int		quiet;		/* 1 is hush file exists */
};

/*
 * This bounds the time given to login.  Not a define so it can
 * be patched on machines where it's too small.
 */
int timeout = LOGIN_TIMEOUT;

struct passwd *pwd;

static struct passwd pwdcopy;

static void timedout(int);
static void sigint(int);
static void motd(void);

/*
 * Nice and simple code provided by Linus Torvalds 16-Feb-93
 * Nonblocking stuff by Maciej W. Rozycki, macro@ds2.pg.gda.pl, 1999.
 *
 * He writes: "Login performs open() on a tty in a blocking mode.
 * In some cases it may make login wait in open() for carrier infinitely,
 * for example if the line is a simplistic case of a three-wire serial
 * connection. I believe login should open the line in the non-blocking mode
 * leaving the decision to make a connection to getty (where it actually
 * belongs).
 */
static void open_tty(const char *tty)
{
	int i, fd, flags;

	fd = open(tty, O_RDWR | O_NONBLOCK);
	if (fd == -1) {
		syslog(LOG_ERR, _("FATAL: can't reopen tty: %m"));
		sleepexit(EXIT_FAILURE);
	}

	if (!isatty(fd)) {
		close(fd);
		syslog(LOG_ERR, _("FATAL: %s is not a terminal"), tty);
		sleepexit(EXIT_FAILURE);
	}

	flags = fcntl(fd, F_GETFL);
	flags &= ~O_NONBLOCK;
	fcntl(fd, F_SETFL, flags);

	for (i = 0; i < fd; i++)
		close(i);
	for (i = 0; i < 3; i++)
		if (fd != i)
			dup2(fd, i);
	if (fd >= 3)
		close(fd);
}

/*
 * Reads the currect terminal path and initialize cxt->tty_* variables.
 */
static void init_tty(struct login_context *cxt)
{
	const char *p;
	struct stat st;
	struct termios tt, ttt;

	cxt->tty_path = ttyname(0);		/* libc calls istty() here */

	/*
	 * In case login is suid it was possible to use a hardlink as stdin
	 * and exploit races for a local root exploit. (Wojciech Purczynski).
	 *
	 * More precisely, the problem is  ttyn := ttyname(0); ...; chown(ttyn);
	 * here ttyname() might return "/tmp/x", a hardlink to a pseudotty.
	 * All of this is a problem only when login is suid, which it isnt.
	 */
	if (!cxt->tty_path || !*cxt->tty_path ||
	    lstat(cxt->tty_path, &st) != 0 || !S_ISCHR(st.st_mode) ||
	    (st.st_nlink > 1 && strncmp(cxt->tty_path, "/dev/", 5)) ||
	    access(cxt->tty_path, R_OK | W_OK) != 0) {

		syslog(LOG_ERR, _("FATAL: bad tty"));
		sleepexit(EXIT_FAILURE);
	}

	if (strncmp(cxt->tty_path, "/dev/", 5) == 0)
		cxt->tty_name = cxt->tty_path + 5;
	else
		cxt->tty_name = cxt->tty_path;

	for (p = cxt->tty_name; p && *p; p++) {
		if (isdigit(*p)) {
			cxt->tty_number = p;
			break;
		}
	}

#ifdef LOGIN_CHOWN_VCS
	/* find names of Virtual Console devices, for later mode change */
	snprintf(cxt->vcsn, sizeof(cxt->vcsn), "/dev/vcs%s", cxt->tty_number);
	snprintf(cxt->vcsan, sizeof(cxt->vcsan), "/dev/vcsa%s", cxt->tty_number);
#endif

	tcgetattr(0, &tt);
	ttt = tt;
	ttt.c_cflag &= ~HUPCL;

	if ((fchown(0, 0, 0) || fchmod(0, TTY_MODE)) && errno != EROFS) {

		syslog(LOG_ERR, _("FATAL: %s: change permissions failed: %m"),
				cxt->tty_path);
		sleepexit(EXIT_FAILURE);
	}

	/* Kill processes left on this tty */
	tcsetattr(0, TCSAFLUSH, &ttt);

	signal(SIGHUP, SIG_IGN);	/* so vhangup() wont kill us */
	vhangup();
	signal(SIGHUP, SIG_DFL);

	/* open stdin,stdout,stderr to the tty */
	open_tty(cxt->tty_path);

	/* restore tty modes */
	tcsetattr(0, TCSAFLUSH, &tt);
}


#ifdef LOGIN_CHOWN_VCS
/* true if the filedescriptor fd is a console tty, very Linux specific */
static int is_consoletty(int fd)
{
	struct stat stb;

	if ((fstat(fd, &stb) >= 0)
	    && (major(stb.st_rdev) == TTY_MAJOR)
	    && (minor(stb.st_rdev) < 64)) {
		return 1;
	}
	return 0;
}
#endif

/*
 * Log failed login attempts in _PATH_BTMP if that exists.
 * Must be called only with username the name of an actual user.
 * The most common login failure is to give password instead of username.
 */
static void logbtmp(struct login_context *cxt)
{
	struct utmp ut;
	struct timeval tv;

	memset(&ut, 0, sizeof(ut));

	strncpy(ut.ut_user,
		cxt->username ? cxt->username : "(unknown)",
		sizeof(ut.ut_user));

	if (cxt->tty_number)
		strncpy(ut.ut_id, cxt->tty_number, sizeof(ut.ut_id));
	if (cxt->tty_name)
		xstrncpy(ut.ut_line, cxt->tty_name, sizeof(ut.ut_line));

#if defined(_HAVE_UT_TV)	/* in <utmpbits.h> included by <utmp.h> */
	gettimeofday(&tv, NULL);
	ut.ut_tv.tv_sec = tv.tv_sec;
	ut.ut_tv.tv_usec = tv.tv_usec;
#else
	{
		time_t t;
		time(&t);
		ut.ut_time = t;	/* ut_time is not always a time_t */
	}
#endif

	ut.ut_type = LOGIN_PROCESS;	/* XXX doesn't matter */
	ut.ut_pid = cxt->pid;

	if (cxt->hostname) {
		xstrncpy(ut.ut_host, cxt->hostname, sizeof(ut.ut_host));
		if (cxt->hostaddress && *cxt->hostaddress)
			memcpy(&ut.ut_addr_v6, cxt->hostaddress,
			       sizeof(ut.ut_addr_v6));
	}

	updwtmp(_PATH_BTMP, &ut);
}

static int child_pid = 0;
static volatile int got_sig = 0;

/*
 * This handler allows to inform a shell about signals to login. If you have
 * (root) permissions you can kill all login childrent by one signal to login
 * process.
 *
 * Also, parent who is session leader is able (before setsid() in child) to
 * inform child when controlling tty goes away (e.g. modem hangup, SIGHUP).
 */
static void sig_handler(int signal)
{
	if (child_pid)
		kill(-child_pid, signal);
	else
		got_sig = 1;
	if (signal == SIGTERM)
		kill(-child_pid, SIGHUP);	/* because the shell often ignores SIGTERM */
}

#ifdef HAVE_LIBAUDIT
static void log_audit(struct login_context *cxt, struct passwd *pwd, int status)
{
	int audit_fd;

	audit_fd = audit_open();
	if (audit_fd == -1)
		return;
	if (!pwd && cxt->username)
		pwd = getpwnam(cxt->username);

	audit_log_acct_message(audit_fd,
			       AUDIT_USER_LOGIN,
			       NULL,
			       "login",
			       cxt->username ? cxt->username : "(unknown)",
			       pwd ? pwd->pw_uid : (unsigned int) -1,
			       cxt->hostname,
			       NULL,
			       cxt->tty_name,
			       status);

	close(audit_fd);
}
#else				/* ! HAVE_LIBAUDIT */
#define log_audit(cxt, pwd, status)
#endif				/* HAVE_LIBAUDIT */

static void log_lastlog(struct login_context *cxt)
{
	struct lastlog ll;
	time_t t;
	int fd;

	fd = open(_PATH_LASTLOG, O_RDWR, 0);
	if (fd < 0)
		return;

	lseek(fd, (off_t) pwd->pw_uid * sizeof(ll), SEEK_SET);

	/*
	 * Print last log message
	 */
	if (!cxt->quiet) {
		if (read(fd, (char *)&ll, sizeof(ll)) == sizeof(ll) &&
							ll.ll_time != 0) {
			time_t ll_time = (time_t) ll.ll_time;

			printf(_("Last login: %.*s "), 24 - 5, ctime(&ll_time));
			if (*ll.ll_host != '\0')
				printf(_("from %.*s\n"),
				       (int)sizeof(ll.ll_host), ll.ll_host);
			else
				printf(_("on %.*s\n"),
				       (int)sizeof(ll.ll_line), ll.ll_line);
		}
		lseek(fd, (off_t) pwd->pw_uid * sizeof(ll), SEEK_SET);
	}

	memset((char *)&ll, 0, sizeof(ll));

	time(&t);
	ll.ll_time = t;		/* ll_time is always 32bit */

	if (cxt->tty_name)
		xstrncpy(ll.ll_line, cxt->tty_name, sizeof(ll.ll_line));
	if (cxt->hostname)
		xstrncpy(ll.ll_host, cxt->hostname, sizeof(ll.ll_host));

	if (write_all(fd, (char *)&ll, sizeof(ll)))
		warn(_("write lastlog failed"));

	close(fd);
}

/*
 * Update wtmp and utmp logs
 */
static void log_utmp(struct login_context *cxt)
{
	struct utmp ut;
	struct utmp *utp;
	struct timeval tv;

	utmpname(_PATH_UTMP);
	setutent();

	/* Find pid in utmp.
	 *
	 * login sometimes overwrites the runlevel entry in /var/run/utmp,
	 * confusing sysvinit. I added a test for the entry type, and the
	 * problem was gone. (In a runlevel entry, st_pid is not really a pid
	 * but some number calculated from the previous and current runlevel).
	 * -- Michael Riepe <michael@stud.uni-hannover.de>
	 */
	while ((utp = getutent()))
		if (utp->ut_pid == cxt->pid
		    && utp->ut_type >= INIT_PROCESS
		    && utp->ut_type <= DEAD_PROCESS)
			break;

	/* If we can't find a pre-existing entry by pid, try by line.
	 * BSD network daemons may rely on this.
	 */
	if (utp == NULL) {
		setutent();
		ut.ut_type = LOGIN_PROCESS;
		strncpy(ut.ut_line, cxt->tty_name, sizeof(ut.ut_line));
		utp = getutline(&ut);
	}

	if (utp)
		memcpy(&ut, utp, sizeof(ut));
	else
		/* some gettys/telnetds don't initialize utmp... */
		memset(&ut, 0, sizeof(ut));

	if (ut.ut_id[0] == 0)
		strncpy(ut.ut_id, cxt->tty_number, sizeof(ut.ut_id));

	strncpy(ut.ut_user, cxt->username, sizeof(ut.ut_user));
	xstrncpy(ut.ut_line, cxt->tty_name, sizeof(ut.ut_line));

#ifdef _HAVE_UT_TV		/* in <utmpbits.h> included by <utmp.h> */
	gettimeofday(&tv, NULL);
	ut.ut_tv.tv_sec = tv.tv_sec;
	ut.ut_tv.tv_usec = tv.tv_usec;
#else
	{
		time_t t;
		time(&t);
		ut.ut_time = t;	/* ut_time is not always a time_t */
				/* glibc2 #defines it as ut_tv.tv_sec */
	}
#endif
	ut.ut_type = USER_PROCESS;
	ut.ut_pid = cxt->pid;
	if (cxt->hostname) {
		xstrncpy(ut.ut_host, cxt->hostname, sizeof(ut.ut_host));
		if (cxt->hostaddress && *cxt->hostaddress)
			memcpy(&ut.ut_addr_v6, cxt->hostaddress,
			       sizeof(ut.ut_addr_v6));
	}

	pututline(&ut);
	endutent();

	updwtmp(_PATH_WTMP, &ut);
}

/* encapsulate stupid "void **" pam_get_item() API */
static int loginpam_get_username(pam_handle_t * pamh, char **name)
{
	const void *item = (void *)*name;
	int rc;
	rc = pam_get_item(pamh, PAM_USER, &item);
	*name = (char *)item;
	return rc;
}

static int loginpam_err(pam_handle_t * pamh, int retcode)
{
	const char *msg = pam_strerror(pamh, retcode);

	if (msg) {
		fprintf(stderr, "\n%s\n", msg);
		syslog(LOG_ERR, "%s", msg);
	}
	pam_end(pamh, retcode);
	exit(EXIT_FAILURE);

}

/*
 * We need to check effective UID/GID. For example $HOME could be on root
 * squashed NFS or on NFS with UID mapping and access(2) uses real UID/GID.
 * The open(2) seems as the surest solution.
 * -- kzak@redhat.com (10-Apr-2009)
 */
int effective_access(const char *path, int mode)
{
	int fd = open(path, mode);
	if (fd != -1)
		close(fd);
	return fd == -1 ? -1 : 0;
}

int main(int argc, char **argv)
{
	extern int optind;
	extern char *optarg, **environ;
	struct group *gr;
	register int ch;
	register char *p;
	int fflag, hflag, pflag, cnt;
	int passwd_req;
	char *domain;
	char tbuf[PATH_MAX + 2];
	char *termenv;
	char *childArgv[10];
	char *buff;
	int childArgc = 0;
	int retcode;
	pam_handle_t *pamh = NULL;
	struct pam_conv conv = { misc_conv, NULL };
	struct sigaction sa, oldsa_hup, oldsa_term;

	struct login_context cxt = {
		.pid = getpid()
	};

	signal(SIGALRM, timedout);
	siginterrupt(SIGALRM, 1);	/* we have to interrupt syscalls like ioclt() */
	alarm((unsigned int)timeout);
	signal(SIGQUIT, SIG_IGN);
	signal(SIGINT, SIG_IGN);

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

	setpriority(PRIO_PROCESS, 0, 0);
	initproctitle(argc, argv);

	/*
	 * -p is used by getty to tell login not to destroy the environment
	 * -f is used to skip a second login authentication
	 * -h is used by other servers to pass the name of the remote
	 *    host to login so that it may be placed in utmp and wtmp
	 */
	gethostname(tbuf, sizeof(tbuf));
	domain = strchr(tbuf, '.');

	fflag = hflag = pflag = 0;
	passwd_req = 1;

	while ((ch = getopt(argc, argv, "fh:p")) != -1)
		switch (ch) {
		case 'f':
			fflag = 1;
			break;

		case 'h':
			if (getuid()) {
				fprintf(stderr,
					_("login: -h for super-user only.\n"));
				exit(EXIT_FAILURE);
			}
			hflag = 1;
			if (domain && (p = strchr(optarg, '.')) &&
			    strcasecmp(p, domain) == 0)
				*p = 0;

			cxt.hostname = strdup(optarg);
			{
				struct addrinfo hints, *info = NULL;

				memset(&hints, 0, sizeof(hints));
				hints.ai_flags = AI_ADDRCONFIG;

				cxt.hostaddress[0] = 0;

				if (getaddrinfo(cxt.hostname, NULL, &hints, &info)
				    == 0 && info) {
					if (info->ai_family == AF_INET) {
						struct sockaddr_in *sa =
						    (struct sockaddr_in *)info->
						    ai_addr;
						memcpy(cxt.hostaddress,
						       &(sa->sin_addr),
						       sizeof(sa->sin_addr));
					} else if (info->ai_family == AF_INET6) {
						struct sockaddr_in6 *sa =
						    (struct sockaddr_in6 *)
						    info->ai_addr;
						memcpy(cxt.hostaddress,
						       &(sa->sin6_addr),
						       sizeof(sa->sin6_addr));
					}
					freeaddrinfo(info);
				}
			}
			break;

		case 'p':
			pflag = 1;
			break;

		case '?':
		default:
			fprintf(stderr, _("usage: login [-fp] [username]\n"));
			exit(EXIT_FAILURE);
		}
	argc -= optind;
	argv += optind;

	if (*argv) {
		char *p = *argv;
		cxt.username = strdup(p);

		/* wipe name - some people mistype their password here */
		/* (of course we are too late, but perhaps this helps a little ..) */
		while (*p)
			*p++ = ' ';
	}

	for (cnt = getdtablesize(); cnt > 2; cnt--)
		close(cnt);

	setpgrp();	 /* set pgid to pid this means that setsid() will fail */

	openlog("login", LOG_ODELAY, LOG_AUTHPRIV);

	init_tty(&cxt);

	/*
	 * username is initialized to NULL
	 * and if specified on the command line it is set.
	 * Therefore, we are safe not setting it to anything
	 */

	retcode = pam_start(hflag ? "remote" : "login", cxt.username, &conv, &pamh);
	if (retcode != PAM_SUCCESS) {
		warnx(_("PAM failure, aborting: %s"),
		      pam_strerror(pamh, retcode));
		syslog(LOG_ERR, _("Couldn't initialize PAM: %s"),
		       pam_strerror(pamh, retcode));
		exit(EXIT_FAILURE);
	}

	/* hostname & tty are either set to NULL or their correct values,
	 * depending on how much we know
	 */
	retcode = pam_set_item(pamh, PAM_RHOST, cxt.hostname);
	if (is_pam_failure(retcode))
		loginpam_err(pamh, retcode);

	retcode = pam_set_item(pamh, PAM_TTY, cxt.tty_name);
	if (is_pam_failure(retcode))
		loginpam_err(pamh, retcode);

	/*
	 * Andrew.Taylor@cal.montage.ca: Provide a user prompt to PAM
	 * so that the "login: " prompt gets localized. Unfortunately,
	 * PAM doesn't have an interface to specify the "Password: " string
	 * (yet).
	 */
	retcode = pam_set_item(pamh, PAM_USER_PROMPT, _("login: "));
	if (is_pam_failure(retcode))
		loginpam_err(pamh, retcode);

	/* we need't the original username. We have to follow PAM. */
	free(cxt.username);
	cxt.username = NULL;

	/* if fflag == 1, then the user has already been authenticated */
	if (fflag && (getuid() == 0))
		passwd_req = 0;
	else
		passwd_req = 1;

	if (passwd_req == 1) {
		int failcount = 0;

		/* if we didn't get a user on the command line, set it to NULL */
		loginpam_get_username(pamh, &cxt.username);

		/* there may be better ways to deal with some of these
		   conditions, but at least this way I don't think we'll
		   be giving away information... */
		/* Perhaps someday we can trust that all PAM modules will
		   pay attention to failure count and get rid of MAX_LOGIN_TRIES? */

		retcode = pam_authenticate(pamh, 0);
		while ((failcount++ < LOGIN_MAX_TRIES) &&
		       ((retcode == PAM_AUTH_ERR) ||
			(retcode == PAM_USER_UNKNOWN) ||
			(retcode == PAM_CRED_INSUFFICIENT) ||
			(retcode == PAM_AUTHINFO_UNAVAIL))) {
			loginpam_get_username(pamh, &cxt.username);

			syslog(LOG_NOTICE,
			       _("FAILED LOGIN %d FROM %s FOR %s, %s"),
			       failcount, cxt.hostname, cxt.username, pam_strerror(pamh,
									   retcode));
			logbtmp(&cxt);
			log_audit(&cxt, NULL, 0);

			fprintf(stderr, _("Login incorrect\n\n"));
			pam_set_item(pamh, PAM_USER, NULL);
			retcode = pam_authenticate(pamh, 0);
		}

		if (is_pam_failure(retcode)) {
			loginpam_get_username(pamh, &cxt.username);

			if (retcode == PAM_MAXTRIES)
				syslog(LOG_NOTICE,
				       _
				       ("TOO MANY LOGIN TRIES (%d) FROM %s FOR "
					"%s, %s"), failcount, cxt.hostname,
				       cxt.username, pam_strerror(pamh, retcode));
			else
				syslog(LOG_NOTICE,
				       _
				       ("FAILED LOGIN SESSION FROM %s FOR %s, %s"),
				       cxt.hostname, cxt.username, pam_strerror(pamh,
									retcode));
			logbtmp(&cxt);
			log_audit(&cxt, NULL, 0);

			fprintf(stderr, _("\nLogin incorrect\n"));
			pam_end(pamh, retcode);
			exit(EXIT_SUCCESS);
		}
	}

	/*
	 * Authentication may be skipped (for example, during krlogin, rlogin, etc...),
	 * but it doesn't mean that we can skip other account checks. The account
	 * could be disabled or password expired (althought kerberos ticket is valid).
	 * -- kzak@redhat.com (22-Feb-2006)
	 */
	retcode = pam_acct_mgmt(pamh, 0);

	if (retcode == PAM_NEW_AUTHTOK_REQD)
		retcode = pam_chauthtok(pamh, PAM_CHANGE_EXPIRED_AUTHTOK);
	if (is_pam_failure(retcode))
		loginpam_err(pamh, retcode);

	/*
	 * Grab the user information out of the password file for future usage
	 * First get the username that we are actually using, though.
	 */
	retcode = loginpam_get_username(pamh, &cxt.username);
	if (is_pam_failure(retcode))
		loginpam_err(pamh, retcode);

	if (!cxt.username || !*cxt.username) {
		warnx(_("\nSession setup problem, abort."));
		syslog(LOG_ERR, _("NULL user name in %s:%d. Abort."),
		       __FUNCTION__, __LINE__);
		pam_end(pamh, PAM_SYSTEM_ERR);
		exit(EXIT_FAILURE);
	}
	if (!(pwd = getpwnam(cxt.username))) {
		warnx(_("\nSession setup problem, abort."));
		syslog(LOG_ERR, _("Invalid user name \"%s\" in %s:%d. Abort."),
		       cxt.username, __FUNCTION__, __LINE__);
		pam_end(pamh, PAM_SYSTEM_ERR);
		exit(EXIT_FAILURE);
	}

	/*
	 * Create a copy of the pwd struct - otherwise it may get
	 * clobbered by PAM
	 */
	memcpy(&pwdcopy, pwd, sizeof(*pwd));
	pwd = &pwdcopy;
	pwd->pw_name = strdup(pwd->pw_name);
	pwd->pw_passwd = strdup(pwd->pw_passwd);
	pwd->pw_gecos = strdup(pwd->pw_gecos);
	pwd->pw_dir = strdup(pwd->pw_dir);
	pwd->pw_shell = strdup(pwd->pw_shell);
	if (!pwd->pw_name || !pwd->pw_passwd || !pwd->pw_gecos ||
	    !pwd->pw_dir || !pwd->pw_shell) {
		warnx(_("out of memory"));
		syslog(LOG_ERR, "Out of memory");
		pam_end(pamh, PAM_SYSTEM_ERR);
		exit(EXIT_FAILURE);
	}
	cxt.username = pwd->pw_name;

	/*
	 * Initialize the supplementary group list.
	 * This should be done before pam_setcred because
	 * the PAM modules might add groups during pam_setcred.
	 */
	if (initgroups(cxt.username, pwd->pw_gid) < 0) {
		syslog(LOG_ERR, "initgroups: %m");
		warnx(_("\nSession setup problem, abort."));
		pam_end(pamh, PAM_SYSTEM_ERR);
		exit(EXIT_FAILURE);
	}

	retcode = pam_open_session(pamh, 0);
	if (is_pam_failure(retcode))
		loginpam_err(pamh, retcode);

	retcode = pam_setcred(pamh, PAM_ESTABLISH_CRED);
	if (is_pam_failure(retcode)) {
		pam_close_session(pamh, 0);
		loginpam_err(pamh, retcode);
	}

	/* committed to login -- turn off timeout */
	alarm((unsigned int)0);

	endpwent();

	{
		/*
		 * Check per accout setting.
		 *
		 * This requires some explanation: As root we may not be able to
		 * read the directory of the user if it is on an NFS mounted
		 * filesystem. We temporarily set our effective uid to the user-uid
		 * making sure that we keep root privs. in the real uid.
		 *
		 * A portable solution would require a fork(), but we rely on Linux
		 * having the BSD setreuid()
		 */
		char tmpstr[PATH_MAX];
		uid_t ruid = getuid();
		gid_t egid = getegid();

		/* avoid snprintf - old systems do not have it, or worse,
		   have a libc in which snprintf is the same as sprintf */
		if (strlen(pwd->pw_dir) + sizeof(_PATH_HUSHLOGIN) + 2 >
		    PATH_MAX)
			cxt.quiet = 0;
		else {
			sprintf(tmpstr, "%s/%s", pwd->pw_dir, _PATH_HUSHLOGIN);
			setregid(-1, pwd->pw_gid);
			setreuid(0, pwd->pw_uid);
			cxt.quiet = (effective_access(tmpstr, O_RDONLY) == 0);
			setuid(0);	/* setreuid doesn't do it alone! */
			setreuid(ruid, 0);
			setregid(-1, egid);
		}
	}

	log_utmp(&cxt);
	log_audit(&cxt, pwd, 1);
	log_lastlog(&cxt);

	if (fchown(0, pwd->pw_uid,
		   (gr = getgrnam(TTYGRPNAME)) ? gr->gr_gid : pwd->pw_gid))
		warn(_("change terminal owner failed"));

	fchmod(0, TTY_MODE);

#ifdef LOGIN_CHOWN_VCS
	/* if tty is one of the VC's then change owner and mode of the
	   special /dev/vcs devices as well */
	if (is_consoletty(0)) {

		if (chown(vcsn, pwd->pw_uid, (gr ? gr->gr_gid : pwd->pw_gid)))
			warn(_("change terminal owner failed"));
		if (chown(vcsan, pwd->pw_uid, (gr ? gr->gr_gid : pwd->pw_gid)))
			warn(_("change terminal owner failed"));

		chmod(vcsn, TTY_MODE);
		chmod(vcsan, TTY_MODE);
	}
#endif

	if (setgid(pwd->pw_gid) < 0 && pwd->pw_gid) {
		syslog(LOG_ALERT, _("setgid() failed"));
		exit(EXIT_FAILURE);
	}

	if (*pwd->pw_shell == '\0')
		pwd->pw_shell = _PATH_BSHELL;

	/* preserve TERM even without -p flag */
	{
		char *ep;

		if (!((ep = getenv("TERM")) && (termenv = strdup(ep))))
			termenv = "dumb";
	}

	/* destroy environment unless user has requested preservation */
	if (!pflag) {
		environ = (char **)malloc(sizeof(char *));
		memset(environ, 0, sizeof(char *));
	}

	setenv("HOME", pwd->pw_dir, 0);	/* legal to override */
	if (pwd->pw_uid)
		setenv("PATH", _PATH_DEFPATH, 1);
	else
		setenv("PATH", _PATH_DEFPATH_ROOT, 1);

	setenv("SHELL", pwd->pw_shell, 1);
	setenv("TERM", termenv, 1);

	/* mailx will give a funny error msg if you forget this one */
	{
		char tmp[PATH_MAX];
		/* avoid snprintf */
		if (sizeof(_PATH_MAILDIR) + strlen(pwd->pw_name) + 1 < PATH_MAX) {
			sprintf(tmp, "%s/%s", _PATH_MAILDIR, pwd->pw_name);
			setenv("MAIL", tmp, 0);
		}
	}

	/* LOGNAME is not documented in login(1) but
	   HP-UX 6.5 does it. We'll not allow modifying it.
	 */
	setenv("LOGNAME", pwd->pw_name, 1);

	{
		int i;
		char **env = pam_getenvlist(pamh);

		if (env != NULL) {
			for (i = 0; env[i]; i++) {
				putenv(env[i]);
				/* D(("env[%d] = %s", i,env[i])); */
			}
		}
	}

	setproctitle("login", cxt.username);

	if (!strncmp(cxt.tty_name, "ttyS", 4))
		syslog(LOG_INFO, _("DIALUP AT %s BY %s"), cxt.tty_name,
		       pwd->pw_name);

	/* allow tracking of good logins.
	   -steve philp (sphilp@mail.alliance.net) */

	if (pwd->pw_uid == 0) {
		if (cxt.hostname)
			syslog(LOG_NOTICE, _("ROOT LOGIN ON %s FROM %s"),
			       cxt.tty_name, cxt.hostname);
		else
			syslog(LOG_NOTICE, _("ROOT LOGIN ON %s"), cxt.tty_name);
	} else {
		if (cxt.hostname)
			syslog(LOG_INFO, _("LOGIN ON %s BY %s FROM %s"),
			       cxt.tty_name, pwd->pw_name, cxt.hostname);
		else
			syslog(LOG_INFO, _("LOGIN ON %s BY %s"), cxt.tty_name,
			       pwd->pw_name);
	}

	if (!cxt.quiet) {
		motd();

#ifdef LOGIN_STAT_MAIL
		/*
		 * This turns out to be a bad idea: when the mail spool
		 * is NFS mounted, and the NFS connection hangs, the
		 * login hangs, even root cannot login.
		 * Checking for mail should be done from the shell.
		 */
		{
			struct stat st;
			char *mail;

			mail = getenv("MAIL");
			if (mail && stat(mail, &st) == 0 && st.st_size != 0) {
				if (st.st_mtime > st.st_atime)
					printf(_("You have new mail.\n"));
				else
					printf(_("You have mail.\n"));
			}
		}
#endif
	}

	signal(SIGALRM, SIG_DFL);
	signal(SIGQUIT, SIG_DFL);
	signal(SIGTSTP, SIG_IGN);

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = SIG_IGN;
	sigaction(SIGINT, &sa, NULL);

	sigaction(SIGHUP, &sa, &oldsa_hup);	/* ignore when TIOCNOTTY */

	/*
	 * detach the controlling tty
	 * -- we needn't the tty in parent who waits for child only.
	 *    The child calls setsid() that detach from the tty as well.
	 */
	ioctl(0, TIOCNOTTY, NULL);

	/*
	 * We have care about SIGTERM, because leave PAM session without
	 * pam_close_session() is pretty bad thing.
	 */
	sa.sa_handler = sig_handler;
	sigaction(SIGHUP, &sa, NULL);
	sigaction(SIGTERM, &sa, &oldsa_term);

	closelog();

	/*
	 * We must fork before setuid() because we need to call
	 * pam_close_session() as root.
	 */

	child_pid = fork();
	if (child_pid < 0) {
		/* error in fork() */
		warn(_("failure forking"));
		pam_setcred(pamh, PAM_DELETE_CRED);
		pam_end(pamh, pam_close_session(pamh, 0));
		exit(EXIT_FAILURE);
	}

	if (child_pid) {
		/* parent - wait for child to finish, then cleanup session */
		close(0);
		close(1);
		close(2);
		sa.sa_handler = SIG_IGN;
		sigaction(SIGQUIT, &sa, NULL);
		sigaction(SIGINT, &sa, NULL);

		/* wait as long as any child is there */
		while (wait(NULL) == -1 && errno == EINTR) ;
		openlog("login", LOG_ODELAY, LOG_AUTHPRIV);
		pam_setcred(pamh, PAM_DELETE_CRED);
		pam_end(pamh, pam_close_session(pamh, 0));
		exit(EXIT_SUCCESS);
	}

	/* child */

	/* restore to old state */
	sigaction(SIGHUP, &oldsa_hup, NULL);
	sigaction(SIGTERM, &oldsa_term, NULL);
	if (got_sig)
		exit(EXIT_FAILURE);

	/*
	 * Problem: if the user's shell is a shell like ash that doesnt do
	 * setsid() or setpgrp(), then a ctrl-\, sending SIGQUIT to every
	 * process in the pgrp, will kill us.
	 */

	/* start new session */
	setsid();

	/* make sure we have a controlling tty */
	open_tty(cxt.tty_path);
	openlog("login", LOG_ODELAY, LOG_AUTHPRIV);	/* reopen */

	/*
	 * TIOCSCTTY: steal tty from other process group.
	 */
	if (ioctl(0, TIOCSCTTY, 1))
		syslog(LOG_ERR, _("TIOCSCTTY failed: %m"));
	signal(SIGINT, SIG_DFL);

	/* discard permissions last so can't get killed and drop core */
	if (setuid(pwd->pw_uid) < 0 && pwd->pw_uid) {
		syslog(LOG_ALERT, _("setuid() failed"));
		exit(EXIT_FAILURE);
	}

	/* wait until here to change directory! */
	if (chdir(pwd->pw_dir) < 0) {
		warn(_("%s: change directory failed"), pwd->pw_dir);
		if (chdir("/"))
			exit(EXIT_FAILURE);
		pwd->pw_dir = "/";
		printf(_("Logging in with home = \"/\".\n"));
	}

	/* if the shell field has a space: treat it like a shell script */
	if (strchr(pwd->pw_shell, ' ')) {
		buff = xmalloc(strlen(pwd->pw_shell) + 6);

		strcpy(buff, "exec ");
		strcat(buff, pwd->pw_shell);
		childArgv[childArgc++] = "/bin/sh";
		childArgv[childArgc++] = "-sh";
		childArgv[childArgc++] = "-c";
		childArgv[childArgc++] = buff;
	} else {
		tbuf[0] = '-';
		xstrncpy(tbuf + 1, ((p = strrchr(pwd->pw_shell, '/')) ?
				    p + 1 : pwd->pw_shell), sizeof(tbuf) - 1);

		childArgv[childArgc++] = pwd->pw_shell;
		childArgv[childArgc++] = tbuf;
	}

	childArgv[childArgc++] = NULL;

	execvp(childArgv[0], childArgv + 1);

	if (!strcmp(childArgv[0], "/bin/sh"))
		warn(_("couldn't exec shell script"));
	else
		warn(_("no shell"));

	exit(EXIT_SUCCESS);
}

/*
 * Robert Ambrose writes:
 * A couple of my users have a problem with login processes hanging around
 * soaking up pts's.  What they seem to hung up on is trying to write out the
 * message 'Login timed out after %d seconds' when the connection has already
 * been dropped.
 * What I did was add a second timeout while trying to write the message so
 * the process just exits if the second timeout expires.
 */

static void timedout2(int sig __attribute__ ((__unused__)))
{
	struct termios ti;

	/* reset echo */
	tcgetattr(0, &ti);
	ti.c_lflag |= ECHO;
	tcsetattr(0, TCSANOW, &ti);
	exit(EXIT_SUCCESS);	/* %% */
}

static void timedout(int sig __attribute__ ((__unused__)))
{
	signal(SIGALRM, timedout2);
	alarm(10);
	/* TRANSLATORS: The standard value for %d is 60. */
	warnx(_("timed out after %d seconds"), timeout);
	signal(SIGALRM, SIG_IGN);
	alarm(0);
	timedout2(0);
}

jmp_buf motdinterrupt;

void motd(void)
{
	int fd, nchars;
	void (*oldint) (int);
	char tbuf[8192];

	if ((fd = open(_PATH_MOTDFILE, O_RDONLY, 0)) < 0)
		return;
	oldint = signal(SIGINT, sigint);
	if (setjmp(motdinterrupt) == 0)
		while ((nchars = read(fd, tbuf, sizeof(tbuf))) > 0) {
			if (write(fileno(stdout), tbuf, nchars)) {
				;	/* glibc warn_unused_result */
			}
		}
	signal(SIGINT, oldint);
	close(fd);
}

void sigint(int sig __attribute__ ((__unused__)))
{
	longjmp(motdinterrupt, 1);
}


/* Should not be called from PAM code... */
void sleepexit(int eval)
{
	sleep(LOGIN_EXIT_TIMEOUT);
	exit(eval);
}
