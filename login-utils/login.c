/* This program is derived from 4.3 BSD software and is
   subject to the copyright notice below.
   
   The port to HP-UX has been motivated by the incapability
   of 'rlogin'/'rlogind' as per HP-UX 6.5 (and 7.0) to transfer window sizes.
   
   Changes:
   
   - General HP-UX portation. Use of facilities not available
     in HP-UX (e.g. setpriority) has been eliminated.
     Utmp/wtmp handling has been ported.
   
   - The program uses BSD command line options to be used
     in connection with e.g. 'rlogind' i.e. 'new login'.
   
   - HP features left out:	    password expiry
				    '*' as login shell, add it if you need it

   - BSD features left out:         quota checks
   	                            password expiry
				    analysis of terminal type (tset feature)
   
   - BSD features thrown in:        Security logging to syslogd.
                                    This requires you to have a (ported) syslog
				    system -- 7.0 comes with syslog
   
				    'Lastlog' feature.
   
   - A lot of nitty gritty details have been adjusted in favour of
     HP-UX, e.g. /etc/securetty, default paths and the environment
     variables assigned by 'login'.
   
   - We do *nothing* to setup/alter tty state, under HP-UX this is
     to be done by getty/rlogind/telnetd/some one else.
   
   Michael Glad (glad@daimi.dk)
   Computer Science Department
   Aarhus University
   Denmark
   
   1990-07-04
   
   1991-09-24 glad@daimi.aau.dk: HP-UX 8.0 port:
   - now explictly sets non-blocking mode on descriptors
   - strcasecmp is now part of HP-UX

   1992-02-05 poe@daimi.aau.dk: Ported the stuff to Linux 0.12
   From 1992 till now (1997) this code for Linux has been maintained at
   ftp.daimi.aau.dk:/pub/linux/poe/

   1999-02-22 Arkadiusz Mi¶kiewicz <misiek@pld.ORG.PL>
    - added Native Language Support
   Sun Mar 21 1999 - Arnaldo Carvalho de Melo <acme@conectiva.com.br>
    - fixed strerr(errno) in gettext calls
 */

/*
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

/*
 * login [ name ]
 * login -h hostname	(for telnetd, etc.)
 * login -f name	(for pre-authenticated login: datakit, xterm, etc.)
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
#define index strchr
#define rindex strrchr
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <utmp.h>
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syslog.h>
#include <sys/sysmacros.h>
#include <netdb.h>
#include "pathnames.h"
#include "my_crypt.h"
#include "login.h"
#include "xstrncpy.h"
#include "nls.h"

#include <sys/sysmacros.h>
#include <linux/major.h>

#include <utmp.h>

#ifdef HAVE_SECURITY_PAM_MISC_H
#  include <security/pam_appl.h>
#  include <security/pam_misc.h>
#  define PAM_MAX_LOGIN_TRIES	3
#  define PAM_FAIL_CHECK if (retcode != PAM_SUCCESS) { \
       fprintf(stderr,"\n%s\n",pam_strerror(pamh, retcode)); \
       syslog(LOG_ERR,"%s",pam_strerror(pamh, retcode)); \
       pam_end(pamh, retcode); exit(1); \
   }
#  define PAM_END { \
	pam_setcred(pamh, PAM_DELETE_CRED); \
	retcode = pam_close_session(pamh,0); \
	pam_end(pamh,retcode); \
}
#endif

#include <lastlog.h>

#define SLEEP_EXIT_TIMEOUT 5

#include "setproctitle.h"

#ifndef HAVE_SECURITY_PAM_MISC_H
static void getloginname (void);
static void checknologin (void);
static int rootterm (char *ttyn);
#endif
static void timedout (int);
static void sigint (int);
static void motd (void);
static void dolastlog (int quiet);

#ifdef USE_TTY_GROUP
#  define TTY_MODE 0620
#else
#  define TTY_MODE 0600
#endif

#define	TTYGRPNAME	"tty"		/* name of group to own ttys */

#ifndef MAXPATHLEN
#  define MAXPATHLEN 1024
#endif

/*
 * This bounds the time given to login.  Not a define so it can
 * be patched on machines where it's too small.
 */
int     timeout = 60;

struct passwd *pwd;

#ifdef HAVE_SECURITY_PAM_MISC_H
static struct passwd pwdcopy;
#endif
char    hostaddress[4];		/* used in checktty.c */
char	*hostname;		/* idem */
static char	*username, *tty_name, *tty_number;
static char	thishost[100];
static int	failures = 1;
static pid_t	pid;

/* Nice and simple code provided by Linus Torvalds 16-Feb-93 */
/* Nonblocking stuff by Maciej W. Rozycki, macro@ds2.pg.gda.pl, 1999.
   He writes: "Login performs open() on a tty in a blocking mode.
   In some cases it may make login wait in open() for carrier infinitely,
   for example if the line is a simplistic case of a three-wire serial
   connection. I believe login should open the line in the non-blocking mode
   leaving the decision to make a connection to getty (where it actually
   belongs). */
static void
opentty(const char * tty) {
	int i, fd, flags;

	fd = open(tty, O_RDWR | O_NONBLOCK);
	if (fd == -1) {
		syslog(LOG_ERR, _("FATAL: can't reopen tty: %s"),
		       strerror(errno));
		sleep(1);
		exit(1);
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

/* In case login is suid it was possible to use a hardlink as stdin
   and exploit races for a local root exploit. (Wojciech Purczynski). */
/* More precisely, the problem is  ttyn := ttyname(0); ...; chown(ttyn);
   here ttyname() might return "/tmp/x", a hardlink to a pseudotty. */
/* All of this is a problem only when login is suid, which it isnt. */
static void
check_ttyname(char *ttyn) {
	struct stat statbuf;

	if (lstat(ttyn, &statbuf)
	    || !S_ISCHR(statbuf.st_mode)
	    || (statbuf.st_nlink > 1 && strncmp(ttyn, "/dev/", 5))) {
		syslog(LOG_ERR, _("FATAL: bad tty"));
		sleep(1);
		exit(1);
	}
}

#ifdef LOGIN_CHOWN_VCS
/* true if the filedescriptor fd is a console tty, very Linux specific */
static int
consoletty(int fd) {
    struct stat stb;

    if ((fstat(fd, &stb) >= 0) 
	&& (major(stb.st_rdev) == TTY_MAJOR)
	&& (minor(stb.st_rdev) < 64)) {
	return 1;
    }
    return 0;
}
#endif

#ifdef HAVE_SECURITY_PAM_MISC_H
/*
 * Log failed login attempts in _PATH_BTMP if that exists.
 * Must be called only with username the name of an actual user.
 * The most common login failure is to give password instead of username.
 */
#define	_PATH_BTMP	"/var/log/btmp"
static void
logbtmp(const char *line, const char *username, const char *hostname) {
	struct utmp ut;

	memset(&ut, 0, sizeof(ut));

	strncpy(ut.ut_user, username ? username : "(unknown)",
		sizeof(ut.ut_user));

	strncpy(ut.ut_id, line + 3, sizeof(ut.ut_id));
	xstrncpy(ut.ut_line, line, sizeof(ut.ut_line));

#if defined(_HAVE_UT_TV)	    /* in <utmpbits.h> included by <utmp.h> */
	gettimeofday(&ut.ut_tv, NULL);
#else
	{
		time_t t;
		time(&t);
		ut.ut_time = t;	    /* ut_time is not always a time_t */
	}
#endif

	ut.ut_type = LOGIN_PROCESS; /* XXX doesn't matter */
	ut.ut_pid = pid;
	if (hostname) {
		xstrncpy(ut.ut_host, hostname, sizeof(ut.ut_host));
		if (hostaddress[0])
			memcpy(&ut.ut_addr, hostaddress, sizeof(ut.ut_addr));
	}
#if HAVE_UPDWTMP		/* bad luck for ancient systems */
	updwtmp(_PATH_BTMP, &ut);
#endif
}
#endif	/* HAVE_SECURITY_PAM_MISC_H */

int
main(int argc, char **argv)
{
    extern int optind;
    extern char *optarg, **environ;
    struct group *gr;
    register int ch;
    register char *p;
    int ask, fflag, hflag, pflag, cnt, errsv;
    int quietlog, passwd_req;
    char *domain, *ttyn;
    char tbuf[MAXPATHLEN + 2], tname[sizeof(_PATH_TTY) + 10];
    char *termenv;
    char *childArgv[10];
    char *buff;
    int childArgc = 0;
#ifdef HAVE_SECURITY_PAM_MISC_H
    int retcode;
    pam_handle_t *pamh = NULL;
    struct pam_conv conv = { misc_conv, NULL };
    pid_t childPid;
#else
    char *salt, *pp;
#endif
#ifdef LOGIN_CHOWN_VCS
    char vcsn[20], vcsan[20];
#endif

    pid = getpid();

    signal(SIGALRM, timedout);
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
    xstrncpy(thishost, tbuf, sizeof(thishost));
    domain = index(tbuf, '.');
    
    username = tty_name = hostname = NULL;
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
	      exit(1);
	  }
	  hflag = 1;
	  if (domain && (p = index(optarg, '.')) &&
	      strcasecmp(p, domain) == 0)
	    *p = 0;

	  hostname = strdup(optarg); 	/* strdup: Ambrose C. Li */
	  {
		  struct hostent *he = gethostbyname(hostname);

		  /* he points to static storage; copy the part we use */
		  hostaddress[0] = 0;
		  if (he && he->h_addr_list && he->h_addr_list[0])
			  memcpy(hostaddress, he->h_addr_list[0],
				 sizeof(hostaddress));
	  }
	  break;
	  
	case 'p':
	  pflag = 1;
	  break;

	case '?':
	default:
	  fprintf(stderr,
		  _("usage: login [-fp] [username]\n"));
	  exit(1);
      }
    argc -= optind;
    argv += optind;
    if (*argv) {
	char *p = *argv;
	username = strdup(p);
	ask = 0;
	/* wipe name - some people mistype their password here */
	/* (of course we are too late, but perhaps this helps a little ..) */
	while(*p)
	    *p++ = ' ';
    } else
        ask = 1;

    for (cnt = getdtablesize(); cnt > 2; cnt--)
      close(cnt);
    
    ttyn = ttyname(0);

    if (ttyn == NULL || *ttyn == '\0') {
	/* no snprintf required - see definition of tname */
	sprintf(tname, "%s??", _PATH_TTY);
	ttyn = tname;
    }

    check_ttyname(ttyn);

    if (strncmp(ttyn, "/dev/", 5) == 0)
	tty_name = ttyn+5;
    else
	tty_name = ttyn;

    if (strncmp(ttyn, "/dev/tty", 8) == 0)
	tty_number = ttyn+8;
    else {
	char *p = ttyn;
	while (*p && !isdigit(*p)) p++;
	tty_number = p;
    }

#ifdef LOGIN_CHOWN_VCS
    /* find names of Virtual Console devices, for later mode change */
    snprintf(vcsn, sizeof(vcsn), "/dev/vcs%s", tty_number);
    snprintf(vcsan, sizeof(vcsan), "/dev/vcsa%s", tty_number);
#endif

    /* set pgid to pid */
    setpgrp();
    /* this means that setsid() will fail */
    
    {
	struct termios tt, ttt;
	
	tcgetattr(0, &tt);
	ttt = tt;
	ttt.c_cflag &= ~HUPCL;

	/* These can fail, e.g. with ttyn on a read-only filesystem */
	chown(ttyn, 0, 0);
	chmod(ttyn, TTY_MODE);

	/* Kill processes left on this tty */
	tcsetattr(0,TCSAFLUSH,&ttt);
	signal(SIGHUP, SIG_IGN); /* so vhangup() wont kill us */
	vhangup();
	signal(SIGHUP, SIG_DFL);

	/* open stdin,stdout,stderr to the tty */
	opentty(ttyn);
	
	/* restore tty modes */
	tcsetattr(0,TCSAFLUSH,&tt);
    }

    openlog("login", LOG_ODELAY, LOG_AUTHPRIV);

#if 0
    /* other than iso-8859-1 */
    printf("\033(K");
    fprintf(stderr,"\033(K");
#endif

#ifdef HAVE_SECURITY_PAM_MISC_H
    /*
     * username is initialized to NULL
     * and if specified on the command line it is set.
     * Therefore, we are safe not setting it to anything
     */

    retcode = pam_start("login",username, &conv, &pamh);
    if(retcode != PAM_SUCCESS) {
	fprintf(stderr, _("login: PAM Failure, aborting: %s\n"),
		pam_strerror(pamh, retcode));
	syslog(LOG_ERR, _("Couldn't initialize PAM: %s"),
	       pam_strerror(pamh, retcode));
	exit(99);
    }
    /* hostname & tty are either set to NULL or their correct values,
       depending on how much we know */
    retcode = pam_set_item(pamh, PAM_RHOST, hostname);
    PAM_FAIL_CHECK;
    retcode = pam_set_item(pamh, PAM_TTY, tty_name);
    PAM_FAIL_CHECK;

    /*
     * Andrew.Taylor@cal.montage.ca: Provide a user prompt to PAM
     * so that the "login: " prompt gets localized. Unfortunately,
     * PAM doesn't have an interface to specify the "Password: " string
     * (yet).
     */
    retcode = pam_set_item(pamh, PAM_USER_PROMPT, _("login: "));
    PAM_FAIL_CHECK;

#if 0
    /*
     * other than iso-8859-1
     * one more time due to reset tty by PAM
     */
    printf("\033(K");
    fprintf(stderr,"\033(K");
#endif
	    
    /* if fflag == 1, then the user has already been authenticated */
    if (fflag && (getuid() == 0))
	passwd_req = 0;
    else
	passwd_req = 1;

    if(passwd_req == 1) {
	int failcount=0;

	/* if we didn't get a user on the command line, set it to NULL */
	pam_get_item(pamh,  PAM_USER, (const void **) &username);
	if (!username)
		pam_set_item(pamh, PAM_USER, NULL);

	/* there may be better ways to deal with some of these
	   conditions, but at least this way I don't think we'll
	   be giving away information... */
	/* Perhaps someday we can trust that all PAM modules will
	   pay attention to failure count and get rid of MAX_LOGIN_TRIES? */

	retcode = pam_authenticate(pamh, 0);
	while((failcount++ < PAM_MAX_LOGIN_TRIES) &&
	      ((retcode == PAM_AUTH_ERR) ||
	       (retcode == PAM_USER_UNKNOWN) ||
	       (retcode == PAM_CRED_INSUFFICIENT) ||
	       (retcode == PAM_AUTHINFO_UNAVAIL))) {
	    pam_get_item(pamh, PAM_USER, (const void **) &username);

	    syslog(LOG_NOTICE,_("FAILED LOGIN %d FROM %s FOR %s, %s"),
		   failcount, hostname, username, pam_strerror(pamh, retcode));
	    logbtmp(tty_name, username, hostname);

	    fprintf(stderr,_("Login incorrect\n\n"));
	    pam_set_item(pamh,PAM_USER,NULL);
	    retcode = pam_authenticate(pamh, 0);
	}

	if (retcode != PAM_SUCCESS) {
	    pam_get_item(pamh, PAM_USER, (const void **) &username);

	    if (retcode == PAM_MAXTRIES)
		syslog(LOG_NOTICE,_("TOO MANY LOGIN TRIES (%d) FROM %s FOR "
			"%s, %s"), failcount, hostname, username,
			 pam_strerror(pamh, retcode));
	    else
		syslog(LOG_NOTICE,_("FAILED LOGIN SESSION FROM %s FOR %s, %s"),
			hostname, username, pam_strerror(pamh, retcode));
	    logbtmp(tty_name, username, hostname);

	    fprintf(stderr,_("\nLogin incorrect\n"));
	    pam_end(pamh, retcode);
	    exit(0);
	}

	retcode = pam_acct_mgmt(pamh, 0);

	if(retcode == PAM_NEW_AUTHTOK_REQD) {
	    retcode = pam_chauthtok(pamh, PAM_CHANGE_EXPIRED_AUTHTOK);
	}

	PAM_FAIL_CHECK;
    }

    /*
     * Grab the user information out of the password file for future usage
     * First get the username that we are actually using, though.
     */
    retcode = pam_get_item(pamh, PAM_USER, (const void **) &username);
    PAM_FAIL_CHECK;

    if (!username || !*username) {
	    fprintf(stderr, _("\nSession setup problem, abort.\n"));
	    syslog(LOG_ERR, _("NULL user name in %s:%d. Abort."),
		   __FUNCTION__, __LINE__);
	    pam_end(pamh, PAM_SYSTEM_ERR);
	    exit(1);
    }
    if (!(pwd = getpwnam(username))) {
	    fprintf(stderr, _("\nSession setup problem, abort.\n"));
	    syslog(LOG_ERR, _("Invalid user name \"%s\" in %s:%d. Abort."),
		   username, __FUNCTION__, __LINE__);
	    pam_end(pamh, PAM_SYSTEM_ERR);
	    exit(1);
    }

    /*
     * Create a copy of the pwd struct - otherwise it may get
     * clobbered by PAM
     */
    memcpy(&pwdcopy, pwd, sizeof(*pwd));
    pwd = &pwdcopy;
    pwd->pw_name   = strdup(pwd->pw_name);
    pwd->pw_passwd = strdup(pwd->pw_passwd);
    pwd->pw_gecos  = strdup(pwd->pw_gecos);
    pwd->pw_dir    = strdup(pwd->pw_dir);
    pwd->pw_shell  = strdup(pwd->pw_shell);
    if (!pwd->pw_name || !pwd->pw_passwd || !pwd->pw_gecos ||
	!pwd->pw_dir || !pwd->pw_shell) {
	    fprintf(stderr, _("login: Out of memory\n"));
	    syslog(LOG_ERR, "Out of memory");
	    pam_end(pamh, PAM_SYSTEM_ERR);
	    exit(1);
    }
    username = pwd->pw_name;

    /*
     * Initialize the supplementary group list.
     * This should be done before pam_setcred because
     * the PAM modules might add groups during pam_setcred.
     */
    if (initgroups(username, pwd->pw_gid) < 0) {
	    syslog(LOG_ERR, "initgroups: %m");
	    fprintf(stderr, _("\nSession setup problem, abort.\n"));
	    pam_end(pamh, PAM_SYSTEM_ERR);
	    exit(1);
    }

    retcode = pam_open_session(pamh, 0);
    PAM_FAIL_CHECK;

    retcode = pam_setcred(pamh, PAM_ESTABLISH_CRED);
    PAM_FAIL_CHECK;

#else /* ! HAVE_SECURITY_PAM_MISC_H */

    for (cnt = 0;; ask = 1) {

	if (ask) {
	    fflag = 0;
	    getloginname();
	}

	/* Dirty patch to fix a gigantic security hole when using 
	   yellow pages. This problem should be solved by the
	   libraries, and not by programs, but this must be fixed
	   urgently! If the first char of the username is '+', we 
	   avoid login success.
	   Feb 95 <alvaro@etsit.upm.es> */
	
	if (username[0] == '+') {
	    puts(_("Illegal username"));
	    badlogin(username);
	    sleepexit(1);
	}
	
	/* (void)strcpy(tbuf, username); why was this here? */
	if ((pwd = getpwnam(username))) {
#  ifdef SHADOW_PWD
	    struct spwd *sp;
	    
	    if ((sp = getspnam(username)))
	      pwd->pw_passwd = sp->sp_pwdp;
#  endif
	    salt = pwd->pw_passwd;
	} else
	  salt = "xx";
	
	if (pwd) {
	    initgroups(username, pwd->pw_gid);
	    checktty(username, tty_name, pwd); /* in checktty.c */
	}
	
	/* if user not super-user, check for disabled logins */
	if (pwd == NULL || pwd->pw_uid)
	  checknologin();
	
	/*
	 * Disallow automatic login to root; if not invoked by
	 * root, disallow if the uid's differ.
	 */
	if (fflag && pwd) {
	    int uid = getuid();
	    
	    passwd_req = pwd->pw_uid == 0 ||
	      (uid && uid != pwd->pw_uid);
	}
	
	/*
	 * If trying to log in as root, but with insecure terminal,
	 * refuse the login attempt.
	 */
	if (pwd && pwd->pw_uid == 0 && !rootterm(tty_name)) {
	    fprintf(stderr,
		    _("%s login refused on this terminal.\n"),
		    pwd->pw_name);
	    
	    if (hostname)
	      syslog(LOG_NOTICE,
		     _("LOGIN %s REFUSED FROM %s ON TTY %s"),
		     pwd->pw_name, hostname, tty_name);
	    else
	      syslog(LOG_NOTICE,
		     _("LOGIN %s REFUSED ON TTY %s"),
		     pwd->pw_name, tty_name);
	    continue;
	}

	/*
	 * If no pre-authentication and a password exists
	 * for this user, prompt for one and verify it.
	 */
	if (!passwd_req || (pwd && !*pwd->pw_passwd))
	  break;
	
	setpriority(PRIO_PROCESS, 0, -4);
	pp = getpass(_("Password: "));
	
#  ifdef CRYPTOCARD
	if (strncmp(pp, "CRYPTO", 6) == 0) {
	    if (pwd && cryptocard()) break;
	}
#  endif /* CRYPTOCARD */
	
	p = crypt(pp, salt);
	setpriority(PRIO_PROCESS, 0, 0);

#  ifdef KERBEROS
	/*
	 * If not present in pw file, act as we normally would.
	 * If we aren't Kerberos-authenticated, try the normal
	 * pw file for a password.  If that's ok, log the user
	 * in without issueing any tickets.
	 */
	
	if (pwd && !krb_get_lrealm(realm,1)) {
	    /*
	     * get TGT for local realm; be careful about uid's
	     * here for ticket file ownership
	     */
	    setreuid(geteuid(),pwd->pw_uid);
	    kerror = krb_get_pw_in_tkt(pwd->pw_name, "", realm,
				       "krbtgt", realm, DEFAULT_TKT_LIFE, pp);
	    setuid(0);
	    if (kerror == INTK_OK) {
		memset(pp, 0, strlen(pp));
		notickets = 0;	/* user got ticket */
		break;
	    }
	}
#  endif /* KERBEROS */
	memset(pp, 0, strlen(pp));

	if (pwd && !strcmp(p, pwd->pw_passwd))
	  break;
	
	printf(_("Login incorrect\n"));
	badlogin(username); /* log ALL bad logins */
	failures++;
	
	/* we allow 10 tries, but after 3 we start backing off */
	if (++cnt > 3) {
	    if (cnt >= 10) {
		sleepexit(1);
	    }
	    sleep((unsigned int)((cnt - 3) * 5));
	}
    }
#endif /* !HAVE_SECURITY_PAM_MISC_H */
    
    /* committed to login -- turn off timeout */
    alarm((unsigned int)0);
    
    endpwent();
    
    /* This requires some explanation: As root we may not be able to
       read the directory of the user if it is on an NFS mounted
       filesystem. We temporarily set our effective uid to the user-uid
       making sure that we keep root privs. in the real uid. 
       
       A portable solution would require a fork(), but we rely on Linux
       having the BSD setreuid() */
    
    {
	char tmpstr[MAXPATHLEN];
	uid_t ruid = getuid();
	gid_t egid = getegid();

	/* avoid snprintf - old systems do not have it, or worse,
	   have a libc in which snprintf is the same as sprintf */
	if (strlen(pwd->pw_dir) + sizeof(_PATH_HUSHLOGIN) + 2 > MAXPATHLEN)
		quietlog = 0;
	else {
		sprintf(tmpstr, "%s/%s", pwd->pw_dir, _PATH_HUSHLOGIN);
		setregid(-1, pwd->pw_gid);
		setreuid(0, pwd->pw_uid);
		quietlog = (access(tmpstr, R_OK) == 0);
		setuid(0); /* setreuid doesn't do it alone! */
		setreuid(ruid, 0);
		setregid(-1, egid);
	}
    }
    
    /* for linux, write entries in utmp and wtmp */
    {
	struct utmp ut;
	struct utmp *utp;
	
	utmpname(_PATH_UTMP);
	setutent();

	/* Find pid in utmp.
login sometimes overwrites the runlevel entry in /var/run/utmp,
confusing sysvinit. I added a test for the entry type, and the problem
was gone. (In a runlevel entry, st_pid is not really a pid but some number
calculated from the previous and current runlevel).
Michael Riepe <michael@stud.uni-hannover.de>
	*/
	while ((utp = getutent()))
		if (utp->ut_pid == pid
		    && utp->ut_type >= INIT_PROCESS
		    && utp->ut_type <= DEAD_PROCESS)
			break;

	/* If we can't find a pre-existing entry by pid, try by line.
	   BSD network daemons may rely on this. (anonymous) */
	if (utp == NULL) {
	     setutent();
	     ut.ut_type = LOGIN_PROCESS;
	     strncpy(ut.ut_line, tty_name, sizeof(ut.ut_line));
	     utp = getutline(&ut);
	}
	
	if (utp) {
	    memcpy(&ut, utp, sizeof(ut));
	} else {
	    /* some gettys/telnetds don't initialize utmp... */
	    memset(&ut, 0, sizeof(ut));
	}
	
	if (ut.ut_id[0] == 0)
	  strncpy(ut.ut_id, tty_number, sizeof(ut.ut_id));
	
	strncpy(ut.ut_user, username, sizeof(ut.ut_user));
	xstrncpy(ut.ut_line, tty_name, sizeof(ut.ut_line));
#ifdef _HAVE_UT_TV		/* in <utmpbits.h> included by <utmp.h> */
	gettimeofday(&ut.ut_tv, NULL);
#else
	{
	    time_t t;
	    time(&t);
	    ut.ut_time = t;	/* ut_time is not always a time_t */
				/* glibc2 #defines it as ut_tv.tv_sec */
	}
#endif
	ut.ut_type = USER_PROCESS;
	ut.ut_pid = pid;
	if (hostname) {
		xstrncpy(ut.ut_host, hostname, sizeof(ut.ut_host));
		if (hostaddress[0])
			memcpy(&ut.ut_addr, hostaddress, sizeof(ut.ut_addr));
	}
	
	pututline(&ut);
	endutent();

#if HAVE_UPDWTMP
	updwtmp(_PATH_WTMP, &ut);
#else
#if 0
	/* The O_APPEND open() flag should be enough to guarantee
	   atomic writes at end of file. */
	{
	    int wtmp;

	    if((wtmp = open(_PATH_WTMP, O_APPEND|O_WRONLY)) >= 0) {
		write(wtmp, (char *)&ut, sizeof(ut));
		close(wtmp);
	    }
	}
#else
	/* Probably all this locking below is just nonsense,
	   and the short version is OK as well. */
	{ 
	    int lf, wtmp;
	    if ((lf = open(_PATH_WTMPLOCK, O_CREAT|O_WRONLY, 0660)) >= 0) {
		flock(lf, LOCK_EX);
		if ((wtmp = open(_PATH_WTMP, O_APPEND|O_WRONLY)) >= 0) {
		    write(wtmp, (char *)&ut, sizeof(ut));
		    close(wtmp);
		}
		flock(lf, LOCK_UN);
		close(lf);
	    }
	}
#endif
#endif
    }
    
    dolastlog(quietlog);
    
    chown(ttyn, pwd->pw_uid,
	  (gr = getgrnam(TTYGRPNAME)) ? gr->gr_gid : pwd->pw_gid);
    chmod(ttyn, TTY_MODE);

#ifdef LOGIN_CHOWN_VCS
    /* if tty is one of the VC's then change owner and mode of the 
       special /dev/vcs devices as well */
    if (consoletty(0)) {
	chown(vcsn, pwd->pw_uid, (gr ? gr->gr_gid : pwd->pw_gid));
	chown(vcsan, pwd->pw_uid, (gr ? gr->gr_gid : pwd->pw_gid));
	chmod(vcsn, TTY_MODE);
	chmod(vcsan, TTY_MODE);
    }
#endif

    setgid(pwd->pw_gid);
    
    if (*pwd->pw_shell == '\0')
      pwd->pw_shell = _PATH_BSHELL;
    
    /* preserve TERM even without -p flag */
    {
	char *ep;
	
	if(!((ep = getenv("TERM")) && (termenv = strdup(ep))))
	  termenv = "dumb";
    }
    
    /* destroy environment unless user has requested preservation */
    if (!pflag)
      {
          environ = (char**)malloc(sizeof(char*));
	  memset(environ, 0, sizeof(char*));
      }
    
    setenv("HOME", pwd->pw_dir, 0);      /* legal to override */
    if(pwd->pw_uid)
      setenv("PATH", _PATH_DEFPATH, 1);
    else
      setenv("PATH", _PATH_DEFPATH_ROOT, 1);
    
    setenv("SHELL", pwd->pw_shell, 1);
    setenv("TERM", termenv, 1);
    
    /* mailx will give a funny error msg if you forget this one */
    {
      char tmp[MAXPATHLEN];
      /* avoid snprintf */
      if (sizeof(_PATH_MAILDIR) + strlen(pwd->pw_name) + 1 < MAXPATHLEN) {
	      sprintf(tmp, "%s/%s", _PATH_MAILDIR, pwd->pw_name);
	      setenv("MAIL",tmp,0);
      }
    }
    
    /* LOGNAME is not documented in login(1) but
       HP-UX 6.5 does it. We'll not allow modifying it.
       */
    setenv("LOGNAME", pwd->pw_name, 1);

#ifdef HAVE_SECURITY_PAM_MISC_H
    {
	int i;
	char ** env = pam_getenvlist(pamh);

	if (env != NULL) {
	    for (i=0; env[i]; i++) {
		putenv(env[i]);
		/* D(("env[%d] = %s", i,env[i])); */
	    }
	}
    }
#endif

    setproctitle("login", username);
    
    if (!strncmp(tty_name, "ttyS", 4))
      syslog(LOG_INFO, _("DIALUP AT %s BY %s"), tty_name, pwd->pw_name);
    
    /* allow tracking of good logins.
       -steve philp (sphilp@mail.alliance.net) */
    
    if (pwd->pw_uid == 0) {
	if (hostname)
	  syslog(LOG_NOTICE, _("ROOT LOGIN ON %s FROM %s"),
		 tty_name, hostname);
	else
	  syslog(LOG_NOTICE, _("ROOT LOGIN ON %s"), tty_name);
    } else {
	if (hostname) 
	  syslog(LOG_INFO, _("LOGIN ON %s BY %s FROM %s"), tty_name, 
		 pwd->pw_name, hostname);
	else 
	  syslog(LOG_INFO, _("LOGIN ON %s BY %s"), tty_name, 
		 pwd->pw_name);
    }
    
    if (!quietlog) {
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

#ifdef HAVE_SECURITY_PAM_MISC_H
    /*
     * We must fork before setuid() because we need to call
     * pam_close_session() as root.
     */
    
    childPid = fork();
    if (childPid < 0) {
       int errsv = errno;
       /* error in fork() */
       fprintf(stderr, _("login: failure forking: %s"), strerror(errsv));
       PAM_END;
       exit(0);
    }

    if (childPid) {
       /* parent - wait for child to finish, then cleanup session */
       signal(SIGHUP, SIG_IGN);
       signal(SIGINT, SIG_IGN);
       signal(SIGQUIT, SIG_IGN);
       signal(SIGTSTP, SIG_IGN);
       signal(SIGTTIN, SIG_IGN);
       signal(SIGTTOU, SIG_IGN);

       wait(NULL);
       PAM_END;
       exit(0);
    }

    /* child */
    /*
     * Problem: if the user's shell is a shell like ash that doesnt do
     * setsid() or setpgrp(), then a ctrl-\, sending SIGQUIT to every
     * process in the pgrp, will kill us.
     */

    /* start new session */
    setsid();

    /* make sure we have a controlling tty */
    opentty(ttyn);
    openlog("login", LOG_ODELAY, LOG_AUTHPRIV);	/* reopen */

    /*
     * TIOCSCTTY: steal tty from other process group.
     */
    if (ioctl(0, TIOCSCTTY, 1))
	    syslog(LOG_ERR, _("TIOCSCTTY failed: %m"));
#endif
    signal(SIGINT, SIG_DFL);
    
    /* discard permissions last so can't get killed and drop core */
    if(setuid(pwd->pw_uid) < 0 && pwd->pw_uid) {
	syslog(LOG_ALERT, _("setuid() failed"));
	exit(1);
    }
    
    /* wait until here to change directory! */
    if (chdir(pwd->pw_dir) < 0) {
	printf(_("No directory %s!\n"), pwd->pw_dir);
	if (chdir("/"))
	  exit(0);
	pwd->pw_dir = "/";
	printf(_("Logging in with home = \"/\".\n"));
    }
    
    /* if the shell field has a space: treat it like a shell script */
    if (strchr(pwd->pw_shell, ' ')) {
	buff = malloc(strlen(pwd->pw_shell) + 6);

	if (!buff) {
	    fprintf(stderr, _("login: no memory for shell script.\n"));
	    exit(0);
	}

	strcpy(buff, "exec ");
	strcat(buff, pwd->pw_shell);
	childArgv[childArgc++] = "/bin/sh";
	childArgv[childArgc++] = "-sh";
	childArgv[childArgc++] = "-c";
	childArgv[childArgc++] = buff;
    } else {
	tbuf[0] = '-';
	xstrncpy(tbuf + 1, ((p = rindex(pwd->pw_shell, '/')) ?
			   p + 1 : pwd->pw_shell),
		sizeof(tbuf)-1);
	
	childArgv[childArgc++] = pwd->pw_shell;
	childArgv[childArgc++] = tbuf;
    }

    childArgv[childArgc++] = NULL;

    execvp(childArgv[0], childArgv + 1);

    errsv = errno;

    if (!strcmp(childArgv[0], "/bin/sh"))
	fprintf(stderr, _("login: couldn't exec shell script: %s.\n"),
		strerror(errsv));
    else
	fprintf(stderr, _("login: no shell: %s.\n"), strerror(errsv));

    exit(0);
}

#ifndef HAVE_SECURITY_PAM_MISC_H
static void
getloginname(void) {
    int ch, cnt, cnt2;
    char *p;
    static char nbuf[UT_NAMESIZE + 1];
    
    cnt2 = 0;
    for (;;) {
	cnt = 0;
	printf(_("\n%s login: "), thishost); fflush(stdout);
	for (p = nbuf; (ch = getchar()) != '\n'; ) {
	    if (ch == EOF) {
		badlogin("EOF");
		exit(0);
	    }
	    if (p < nbuf + UT_NAMESIZE)
	      *p++ = ch;
	    
	    cnt++;
	    if (cnt > UT_NAMESIZE + 20) {
		fprintf(stderr, _("login name much too long.\n"));
		badlogin(_("NAME too long"));
		exit(0);
	    }
	}
	if (p > nbuf) {
	  if (nbuf[0] == '-')
	    fprintf(stderr,
		    _("login names may not start with '-'.\n"));
	  else {
	      *p = '\0';
	      username = nbuf;
	      break;
	  }
	}
	
	cnt2++;
	if (cnt2 > 50) {
	    fprintf(stderr, _("too many bare linefeeds.\n"));
	    badlogin(_("EXCESSIVE linefeeds"));
	    exit(0);
	}
    }
}
#endif

/*
 * Robert Ambrose writes:
 * A couple of my users have a problem with login processes hanging around
 * soaking up pts's.  What they seem to hung up on is trying to write out the
 * message 'Login timed out after %d seconds' when the connection has already
 * been dropped.
 * What I did was add a second timeout while trying to write the message so
 * the process just exits if the second timeout expires.
 */

static void
timedout2(int sig) {
	struct termio ti;
    
	/* reset echo */
	ioctl(0, TCGETA, &ti);
	ti.c_lflag |= ECHO;
	ioctl(0, TCSETA, &ti);
	exit(0);			/* %% */
}

static void
timedout(int sig) {
	signal(SIGALRM, timedout2);
	alarm(10);
	fprintf(stderr, _("Login timed out after %d seconds\n"), timeout);
	signal(SIGALRM, SIG_IGN);
	alarm(0);
	timedout2(0);
}

#ifndef HAVE_SECURITY_PAM_MISC_H
int
rootterm(char * ttyn)
{ 
    int fd;
    char buf[100],*p;
    int cnt, more = 0;
    
    fd = open(SECURETTY, O_RDONLY);
    if(fd < 0) return 1;
    
    /* read each line in /etc/securetty, if a line matches our ttyline
       then root is allowed to login on this tty, and we should return
       true. */
    for(;;) {
	p = buf; cnt = 100;
	while(--cnt >= 0 && (more = read(fd, p, 1)) == 1 && *p != '\n') p++;
	if(more && *p == '\n') {
	    *p = '\0';
	    if(!strcmp(buf, ttyn)) {
		close(fd);
		return 1;
	    } else
	      continue;
  	} else {
	    close(fd);
	    return 0;
  	}
    }
}
#endif /* !HAVE_SECURITY_PAM_MISC_H */

jmp_buf motdinterrupt;

void
motd(void) {
    int fd, nchars;
    void (*oldint)(int);
    char tbuf[8192];
    
    if ((fd = open(_PATH_MOTDFILE, O_RDONLY, 0)) < 0)
      return;
    oldint = signal(SIGINT, sigint);
    if (setjmp(motdinterrupt) == 0)
      while ((nchars = read(fd, tbuf, sizeof(tbuf))) > 0)
	write(fileno(stdout), tbuf, nchars);
    signal(SIGINT, oldint);
    close(fd);
}

void
sigint(int sig) {
    longjmp(motdinterrupt, 1);
}

#ifndef HAVE_SECURITY_PAM_MISC_H			/* PAM takes care of this */
void
checknologin(void) {
    int fd, nchars;
    char tbuf[8192];
    
    if ((fd = open(_PATH_NOLOGIN, O_RDONLY, 0)) >= 0) {
	while ((nchars = read(fd, tbuf, sizeof(tbuf))) > 0)
	  write(fileno(stdout), tbuf, nchars);
	close(fd);
	sleepexit(0);
    }
}
#endif

void
dolastlog(int quiet) {
    struct lastlog ll;
    int fd;
    
    if ((fd = open(_PATH_LASTLOG, O_RDWR, 0)) >= 0) {
	lseek(fd, (off_t)pwd->pw_uid * sizeof(ll), SEEK_SET);
	if (!quiet) {
	    if (read(fd, (char *)&ll, sizeof(ll)) == sizeof(ll) &&
		ll.ll_time != 0) {
		    time_t ll_time = (time_t) ll.ll_time;

		    printf(_("Last login: %.*s "),
			   24-5, ctime(&ll_time));
		
		    if (*ll.ll_host != '\0')
			    printf(_("from %.*s\n"),
				   (int)sizeof(ll.ll_host), ll.ll_host);
		    else
			    printf(_("on %.*s\n"),
				   (int)sizeof(ll.ll_line), ll.ll_line);
	    }
	    lseek(fd, (off_t)pwd->pw_uid * sizeof(ll), SEEK_SET);
	}
	memset((char *)&ll, 0, sizeof(ll));
	time(&ll.ll_time);
	xstrncpy(ll.ll_line, tty_name, sizeof(ll.ll_line));
	if (hostname)
	    xstrncpy(ll.ll_host, hostname, sizeof(ll.ll_host));

	write(fd, (char *)&ll, sizeof(ll));
	close(fd);
    }
}

void
badlogin(const char *name) {
    if (failures == 1) {
	if (hostname)
	  syslog(LOG_NOTICE, _("LOGIN FAILURE FROM %s, %s"),
		 hostname, name);
	else
	  syslog(LOG_NOTICE, _("LOGIN FAILURE ON %s, %s"),
		 tty_name, name);
    } else {
	if (hostname)
	  syslog(LOG_NOTICE, _("%d LOGIN FAILURES FROM %s, %s"),
		 failures, hostname, name);
	else
	  syslog(LOG_NOTICE, _("%d LOGIN FAILURES ON %s, %s"),
		 failures, tty_name, name);
    }
}

/* Should not be called from PAM code... */
void
sleepexit(int eval) {
    sleep(SLEEP_EXIT_TIMEOUT);
    exit(eval);
}
