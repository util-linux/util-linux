/*
 * No copyright is claimed.  This code is in the public domain; do with
 * it what you wish.
 */
#ifndef UTIL_LINUX_AGETTY_H
#define UTIL_LINUX_AGETTY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <termios.h>
#include <unistd.h>
#include <utmpx.h>

#include "ttyutils.h"

/*
 * Some heuristics to find out what environment we are in: if it is not
 * System V, assume it is SunOS 4. The LOGIN_PROCESS is defined in System V
 * utmp.h, which will select System V style getty.
 */
#ifdef LOGIN_PROCESS
#  define SYSV_STYLE
#endif

#ifdef	SYSV_STYLE
#  define ISSUE_SUPPORT
#  if defined(HAVE_SCANDIRAT) && defined(HAVE_OPENAT)
#    define ISSUEDIR_SUPPORT
#  endif
#endif

#ifdef AGETTY_RELOAD
# include "netaddrq.h"
# if defined(RTMGRP_IPV4_IFADDR) && defined(RTMGRP_IPV6_IFADDR)
#  define USE_NETLINK
# endif
# define AGETTY_RELOAD_FILENAME "/run/agetty.reload"	/* trigger file */
# define AGETTY_RELOAD_FDNONE	-2			/* uninitialized fd */
#endif

struct agetty_issue {
	FILE *output;
	char *mem;
	size_t mem_sz;

#ifdef USE_NETLINK
	struct ul_nl_data nl;
#endif
#ifdef AGETTY_RELOAD
	char *mem_old;
#endif
	bool do_tcsetattr;
	bool do_tcrestore;
};

/* Numbers of args for login(1) */
#define LOGIN_ARGV_MAX	16

/* Storage for command-line options. */
#define	MAX_SPEED	10	/* max. nr. of baud rates */

struct agetty_options {
	int flags;			/* toggle switches, see below */
	unsigned int timeout;		/* time-out period */
	char *autolog;			/* login the user automatically */
	char *chdir;			/* Chdir before the login */
	char *chroot;			/* Chroot before the login */
	char *login;			/* login program */
	char *logopt;			/* options for login program */
	const char *tty;		/* name of tty */
	const char *vcline;		/* line of virtual console */
	char *term;			/* terminal type */
	char *initstring;		/* modem init string */
	char *issue;			/* alternative issue file or directory */
	char *erasechars;		/* string with erase chars */
	char *killchars;		/* string with kill chars */
	char *username;			/* login name, given to /bin/login */
	char *fakehost;			/* fake hostname for ut_host */
	char *osrelease;		/* /etc/os-release data */
	unsigned int delay;		/* Sleep seconds before prompt */
	int nice;			/* Run login with this priority */
	int numspeed;			/* number of baud rates to try */
	int clocal;			/* CLOCAL_MODE_* */
	int kbmode;			/* Keyboard mode if virtual console */
	int tty_is_stdin;		/* is the tty the standard input stream */
	speed_t speeds[MAX_SPEED];	/* baud rates to be tried */
};

#define	F_PARSE		(1<<0)	/* process modem status messages */
#define	F_ISSUE		(1<<1)	/* display /etc/issue or /etc/issue.d */
#define	F_RTSCTS	(1<<2)	/* enable RTS/CTS flow control */

#define F_INITSTRING    (1<<4)	/* initstring is set */
#define F_WAITCRLF	(1<<5)	/* wait for CR or LF */

#define F_NOPROMPT	(1<<7)	/* do not ask for login name! */
#define F_LCUC		(1<<8)	/* support for *LCUC stty modes */
#define F_KEEPSPEED	(1<<9)	/* follow baud rate from kernel */
#define F_KEEPCFLAGS	(1<<10)	/* reuse c_cflags setup from kernel */
#define F_EIGHTBITS	(1<<11)	/* Assume 8bit-clean tty */
#define F_VCONSOLE	(1<<12)	/* This is a virtual console */
#define F_HANGUP	(1<<13)	/* Do call vhangup(2) */
#define F_UTF8		(1<<14)	/* We can do UTF8 */
#define F_LOGINPAUSE	(1<<15)	/* Wait for any key before dropping login prompt */
#define F_NOCLEAR	(1<<16) /* Do not clear the screen before prompting */
#define F_NONL		(1<<17) /* No newline before issue */
#define F_NOHOSTNAME	(1<<18) /* Do not show the hostname */
#define F_LONGHNAME	(1<<19) /* Show Full qualified hostname */
#define F_NOHINTS	(1<<20) /* Don't print hints */
#define F_REMOTE	(1<<21) /* Add '-h fakehost' to login(1) command line */

extern void agetty_exit_slowly(int code) __attribute__((__noreturn__));
extern void agetty_log_err(const char *, ...) __attribute__((__noreturn__))
				       __attribute__((__format__(printf, 1, 2)));
extern void agetty_log_warn(const char *, ...)
				__attribute__((__format__(printf, 1, 2)));

extern void agetty_load_credentials(struct agetty_options *op);

extern char *agetty_xgethostname(void);
extern char *agetty_xgetdomainname(void);
extern void agetty_update_utmp(struct agetty_options *op);
extern void agetty_parse_speeds(struct agetty_options *op, char *arg);
extern char *agetty_parse_initstring(const char *arg);
extern void agetty_init_login_argv(char *argv[], int *argc,
				   struct agetty_options *op);

enum {
	CLOCAL_MODE_AUTO = 0,
	CLOCAL_MODE_ALWAYS,
	CLOCAL_MODE_NEVER
};

#ifdef DEBUGGING
# define debug(s) do { fprintf(dbf,s); fflush(dbf); } while (0)
extern FILE *dbf;
#else
# define debug(s) do { ; } while (0)
#endif

extern speed_t agetty_bcode(char *s);
extern void agetty_list_speeds(void);
extern void agetty_fprint_speed(FILE *out, speed_t speed);

extern void agetty_termio_clear(int fd);
extern void agetty_reset_vc(const struct agetty_options *op, struct termios *tp, int canon);
extern void agetty_open_tty(const char *tty, struct termios *tp, struct agetty_options *op);
extern void agetty_termio_init(struct agetty_options *op, struct termios *tp);
extern void agetty_termio_final(struct agetty_options *op, struct termios *tp, struct chardata *cp);
extern void agetty_auto_baud(struct termios *tp);
extern void agetty_next_speed(struct agetty_options *op, struct termios *tp);
extern void agetty_erase_char(int visual_count, struct chardata *cp);

extern void agetty_print_issue_file(struct agetty_issue *ie, struct agetty_options *op, struct termios *tp);
extern void agetty_eval_issue_file(struct agetty_issue *ie, struct agetty_options *op, struct termios *tp);
extern int agetty_issue_is_changed(struct agetty_issue *ie);
extern void agetty_show_issue(struct agetty_options *op);
extern void agetty_reload(void);

#endif /* UTIL_LINUX_AGETTY_H */
