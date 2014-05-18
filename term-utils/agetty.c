/*
 * Alternate Getty (agetty) 'agetty' is a versatile, portable, easy to use
 * replacement for getty on SunOS 4.1.x or the SAC ttymon/ttyadm/sacadm/pmadm
 * suite on Solaris and other SVR4 systems. 'agetty' was written by Wietse
 * Venema, enhanced by John DiMarco, and further enhanced by Dennis Cronin.
 *
 * Ported to Linux by Peter Orbaek <poe@daimi.aau.dk>
 * Adopt the mingetty features for a better support
 * of virtual consoles by Werner Fink <werner@suse.de>
 *
 * This program is freely distributable.
 */
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <signal.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <stdarg.h>
#include <ctype.h>
#include <utmp.h>
#include <getopt.h>
#include <time.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <langinfo.h>
#include <grp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <ifaddrs.h>
#include <net/if.h>

#include "strutils.h"
#include "all-io.h"
#include "nls.h"
#include "pathnames.h"
#include "c.h"
#include "widechar.h"
#include "ttyutils.h"

#ifdef __linux__
#  include <sys/kd.h>
#  include <sys/param.h>
#  define USE_SYSLOG
#  ifndef DEFAULT_VCTERM
#    define DEFAULT_VCTERM "linux"
#  endif
#  if defined (__s390__) || defined (__s390x__)
#    define DEFAULT_TTYS0  "dumb"
#    define DEFAULT_TTY32  "ibm327x"
#    define DEFAULT_TTYS1  "vt220"
#  endif
#  ifndef DEFAULT_STERM
#    define DEFAULT_STERM  "vt102"
#  endif
#elif defined(__GNU__)
#  define USE_SYSLOG
#  ifndef DEFAULT_VCTERM
#    define DEFAULT_VCTERM "hurd"
#  endif
#  ifndef DEFAULT_STERM
#    define DEFAULT_STERM  "vt102"
#  endif
#else
#  ifndef DEFAULT_VCTERM
#    define DEFAULT_VCTERM "vt100"
#  endif
#  ifndef DEFAULT_STERM
#    define DEFAULT_STERM  "vt100"
#  endif
#endif

/* If USE_SYSLOG is undefined all diagnostics go to /dev/console. */
#ifdef	USE_SYSLOG
#  include <syslog.h>
#endif

/*
 * Some heuristics to find out what environment we are in: if it is not
 * System V, assume it is SunOS 4. The LOGIN_PROCESS is defined in System V
 * utmp.h, which will select System V style getty.
 */
#ifdef LOGIN_PROCESS
#  define SYSV_STYLE
#endif

/*
 * Things you may want to modify.
 *
 * If ISSUE is not defined, agetty will never display the contents of the
 * /etc/issue file. You will not want to spit out large "issue" files at the
 * wrong baud rate. Relevant for System V only.
 *
 * You may disagree with the default line-editing etc. characters defined
 * below. Note, however, that DEL cannot be used for interrupt generation
 * and for line editing at the same time.
 */

/* Displayed before the login prompt. */
#ifdef	SYSV_STYLE
#  define ISSUE _PATH_ISSUE
#  include <sys/utsname.h>
#endif

/* Login prompt. */
#define LOGIN		"login: "
#define LOGIN_ARGV_MAX	16		/* Numbers of args for login */

/*
 * When multiple baud rates are specified on the command line, the first one
 * we will try is the first one specified.
 */
#define	FIRST_SPEED	0

/* Storage for command-line options. */
#define	MAX_SPEED	10	/* max. nr. of baud rates */

struct options {
	int flags;			/* toggle switches, see below */
	int timeout;			/* time-out period */
	char *autolog;			/* login the user automatically */
	char *chdir;			/* Chdir before the login */
	char *chroot;			/* Chroot before the login */
	char *login;			/* login program */
	char *logopt;			/* options for login program */
	char *tty;			/* name of tty */
	char *vcline;			/* line of virtual console */
	char *term;			/* terminal type */
	char *initstring;		/* modem init string */
	char *issue;			/* alternative issue file */
	char *erasechars;		/* string with erase chars */
	char *killchars;		/* string with kill chars */
	char *osrelease;		/* /etc/os-release data */
	int delay;			/* Sleep seconds before prompt */
	int nice;			/* Run login with this priority */
	int numspeed;			/* number of baud rates to try */
	int clocal;			/* CLOCAL_MODE_* */
	int kbmode;			/* Keyboard mode if virtual console */
	speed_t speeds[MAX_SPEED];	/* baud rates to be tried */
};

enum {
	CLOCAL_MODE_AUTO = 0,
	CLOCAL_MODE_ALWAYS,
	CLOCAL_MODE_NEVER
};

#define	F_PARSE		(1<<0)	/* process modem status messages */
#define	F_ISSUE		(1<<1)	/* display /etc/issue */
#define	F_RTSCTS	(1<<2)	/* enable RTS/CTS flow control */

#define F_INITSTRING    (1<<4)	/* initstring is set */
#define F_WAITCRLF	(1<<5)	/* wait for CR or LF */
#define F_CUSTISSUE	(1<<6)	/* give alternative issue file */
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

#define serial_tty_option(opt, flag)	\
	(((opt)->flags & (F_VCONSOLE|(flag))) == (flag))

struct Speedtab {
	long speed;
	speed_t code;
};

static const struct Speedtab speedtab[] = {
	{50, B50},
	{75, B75},
	{110, B110},
	{134, B134},
	{150, B150},
	{200, B200},
	{300, B300},
	{600, B600},
	{1200, B1200},
	{1800, B1800},
	{2400, B2400},
	{4800, B4800},
	{9600, B9600},
#ifdef B19200
	{19200, B19200},
#elif defined(EXTA)
	{19200, EXTA},
#endif
#ifdef B38400
	{38400, B38400},
#elif defined(EXTB)
	{38400, EXTB},
#endif
#ifdef B57600
	{57600, B57600},
#endif
#ifdef B115200
	{115200, B115200},
#endif
#ifdef B230400
	{230400, B230400},
#endif
#ifdef B460800
	{460800, B460800},
#endif
#ifdef B500000
	{500000, B500000},
#endif
#ifdef B576000
	{576000, B576000},
#endif
#ifdef B921600
	{921600, B921600},
#endif
#ifdef B1000000
	{1000000, B1000000},
#endif
#ifdef B1152000
	{1152000, B1152000},
#endif
#ifdef B1500000
	{1500000, B1500000},
#endif
#ifdef B2000000
	{2000000, B2000000},
#endif
#ifdef B2500000
	{2500000, B2500000},
#endif
#ifdef B3000000
	{3000000, B3000000},
#endif
#ifdef B3500000
	{3500000, B3500000},
#endif
#ifdef B4000000
	{4000000, B4000000},
#endif
	{0, 0},
};

static void init_special_char(char* arg, struct options *op);
static void parse_args(int argc, char **argv, struct options *op);
static void parse_speeds(struct options *op, char *arg);
static void update_utmp(struct options *op);
static void open_tty(char *tty, struct termios *tp, struct options *op);
static void termio_init(struct options *op, struct termios *tp);
static void reset_vc (const struct options *op, struct termios *tp);
static void auto_baud(struct termios *tp);
static void output_special_char (unsigned char c, struct options *op,
		struct termios *tp, FILE *fp);
static void do_prompt(struct options *op, struct termios *tp);
static void next_speed(struct options *op, struct termios *tp);
static char *get_logname(struct options *op,
			 struct termios *tp, struct chardata *cp);
static void termio_final(struct options *op,
			 struct termios *tp, struct chardata *cp);
static int caps_lock(char *s);
static speed_t bcode(char *s);
static void usage(FILE * out) __attribute__((__noreturn__));
static void log_err(const char *, ...) __attribute__((__noreturn__))
			       __attribute__((__format__(printf, 1, 2)));
static void log_warn (const char *, ...)
				__attribute__((__format__(printf, 1, 2)));
static ssize_t append(char *dest, size_t len, const char  *sep, const char *src);
static void check_username (const char* nm);
static void login_options_to_argv(char *argv[], int *argc, char *str, char *username);
static int plymouth_command(const char* arg);

/* Fake hostname for ut_host specified on command line. */
static char *fakehost;

#ifdef DEBUGGING
# include "closestream.h"
# ifndef DEBUG_OUTPUT
#  define DEBUG_OUTPUT "/dev/tty10"
# endif
# define debug(s) do { fprintf(dbf,s); fflush(dbf); } while (0)
FILE *dbf;
#else
# define debug(s) do { ; } while (0)
#endif

int main(int argc, char **argv)
{
	char *username = NULL;			/* login name, given to /bin/login */
	struct chardata chardata;		/* will be set by get_logname() */
	struct termios termios;			/* terminal mode bits */
	struct options options = {
		.flags  =  F_ISSUE,		/* show /etc/issue (SYSV_STYLE) */
		.login  =  _PATH_LOGIN,		/* default login program */
		.tty    = "tty1",		/* default tty line */
		.issue  =  ISSUE		/* default issue file */
	};
	char *login_argv[LOGIN_ARGV_MAX + 1];
	int login_argc = 0;
	struct sigaction sa, sa_hup, sa_quit, sa_int;
	sigset_t set;

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

	/* In case vhangup(2) has to called */
	sa.sa_handler = SIG_IGN;
	sa.sa_flags = SA_RESTART;
	sigemptyset (&sa.sa_mask);
	sigaction(SIGHUP, &sa, &sa_hup);
	sigaction(SIGQUIT, &sa, &sa_quit);
	sigaction(SIGINT, &sa, &sa_int);

#ifdef DEBUGGING
	dbf = fopen(DEBUG_OUTPUT, "w");
	for (int i = 1; i < argc; i++) {
		if (i > 1)
			debug(" ");
		debug(argv[i]);
	}
	debug("\n");
#endif				/* DEBUGGING */

	/* Parse command-line arguments. */
	parse_args(argc, argv, &options);

	login_argv[login_argc++] = options.login;	/* set login program name */

	/* Update the utmp file. */
#ifdef	SYSV_STYLE
	update_utmp(&options);
#endif
	if (options.delay)
	    sleep(options.delay);

	debug("calling open_tty\n");

	/* Open the tty as standard { input, output, error }. */
	open_tty(options.tty, &termios, &options);

	/* Unmask SIGHUP if inherited */
	sigemptyset(&set);
	sigaddset(&set, SIGHUP);
	sigprocmask(SIG_UNBLOCK, &set, NULL);
	sigaction(SIGHUP, &sa_hup, NULL);

	tcsetpgrp(STDIN_FILENO, getpid());
	/* Initialize the termios settings (raw mode, eight-bit, blocking i/o). */
	debug("calling termio_init\n");
	termio_init(&options, &termios);

	/* Write the modem init string and DO NOT flush the buffers. */
	if (serial_tty_option(&options, F_INITSTRING) &&
	    options.initstring && *options.initstring != '\0') {
		debug("writing init string\n");
		write_all(STDOUT_FILENO, options.initstring,
			   strlen(options.initstring));
	}

	if (options.flags & F_VCONSOLE || options.clocal != CLOCAL_MODE_ALWAYS)
		/* Go to blocking mode unless -L is specified, this change
		 * affects stdout, stdin and stderr as all the file descriptors
		 * are created by dup().   */
		fcntl(STDOUT_FILENO, F_SETFL,
		      fcntl(STDOUT_FILENO, F_GETFL, 0) & ~O_NONBLOCK);

	/* Optionally detect the baud rate from the modem status message. */
	debug("before autobaud\n");
	if (serial_tty_option(&options, F_PARSE))
		auto_baud(&termios);

	/* Set the optional timer. */
	if (options.timeout)
		alarm((unsigned) options.timeout);

	/* Optionally wait for CR or LF before writing /etc/issue */
	if (serial_tty_option(&options, F_WAITCRLF)) {
		char ch;

		debug("waiting for cr-lf\n");
		while (read(STDIN_FILENO, &ch, 1) == 1) {
			/* Strip "parity bit". */
			ch &= 0x7f;
#ifdef DEBUGGING
			fprintf(dbf, "read %c\n", ch);
#endif
			if (ch == '\n' || ch == '\r')
				break;
		}
	}

	INIT_CHARDATA(&chardata);

	if (options.autolog) {
		debug("doing auto login\n");
		username = options.autolog;
	}

	if ((options.flags & F_NOPROMPT) == 0) {
		if (options.autolog) {
			/* Autologin prompt */
			do_prompt(&options, &termios);
			printf(_("%s%s (automatic login)\n"), LOGIN, options.autolog);
		} else {
			/* Read the login name. */
			debug("reading login name\n");
			while ((username =
				get_logname(&options, &termios, &chardata)) == NULL)
				if ((options.flags & F_VCONSOLE) == 0)
					next_speed(&options, &termios);
		}
	}

	/* Disable timer. */
	if (options.timeout)
		alarm(0);

	if ((options.flags & F_VCONSOLE) == 0) {
		/* Finalize the termios settings. */
		termio_final(&options, &termios, &chardata);

		/* Now the newline character should be properly written. */
		write_all(STDOUT_FILENO, "\r\n", 2);
	}

	sigaction(SIGQUIT, &sa_quit, NULL);
	sigaction(SIGINT, &sa_int, NULL);

	if (username)
		check_username(username);

	if (options.logopt) {
		/*
		 * The --login-options completely overwrites the default
		 * way how agetty composes login(1) command line.
		 */
		login_options_to_argv(login_argv, &login_argc,
				      options.logopt, username);
	} else {
		if (fakehost && (options.flags & F_REMOTE)) {
			login_argv[login_argc++] = "-h";
			login_argv[login_argc++] = fakehost;
		}
		if (username) {
			if (options.autolog)
				login_argv[login_argc++] = "-f";
			else
				login_argv[login_argc++] = "--";
			login_argv[login_argc++] = username;
		}
	}

	login_argv[login_argc] = NULL;	/* last login argv */

	if (options.chroot) {
		if (chroot(options.chroot) < 0)
			log_err(_("%s: can't change root directory %s: %m"),
				options.tty, options.chroot);
	}
	if (options.chdir) {
		if (chdir(options.chdir) < 0)
			log_err(_("%s: can't change working directory %s: %m"),
				options.tty, options.chdir);
	}
	if (options.nice) {
		if (nice(options.nice) < 0)
			log_warn(_("%s: can't change process priority: %m"),
				options.tty);
	}
	free(options.osrelease);
#ifdef DEBUGGING
	if (close_stream(dbf) != 0)
		log_err("write failed: %s", DEBUG_OUTPUT);
#endif

	/* Let the login program take care of password validation. */
	execv(options.login, login_argv);
	log_err(_("%s: can't exec %s: %m"), options.tty, login_argv[0]);
}

/*
 * Returns : @str if \u not found
 *         : @username if @str equal to "\u"
 *         : newly allocated string if \u mixed with something other
 */
static char *replace_u(char *str, char *username)
{
	char *entry = NULL, *p = str;
	size_t usz = username ? strlen(username) : 0;

	while (*p) {
		size_t sz;
		char *tp, *old = entry;

		if (memcmp(p, "\\u", 2)) {
			p++;
			continue;	/* no \u */
		}
		sz = strlen(str);

		if (p == str && sz == 2) {
			/* 'str' contains only '\u' */
			free(old);
			return username;
		}

		tp = entry = malloc(sz + usz);
		if (!tp)
			log_err(_("failed to allocate memory: %m"));

		if (p != str) {
			/* copy chars befor \u */
			memcpy(tp, str, p - str);
			tp += p - str;
		}
		if (usz) {
			/* copy username */
			memcpy(tp, username, usz);
			tp += usz;
		}
		if (*(p + 2))
			/* copy chars after \u + \0 */
			memcpy(tp, p + 2, sz - (p - str) - 1);
		else
			*tp = '\0';

		p = tp;
		str = entry;
		free(old);
	}

	return entry ? entry : str;
}

static void login_options_to_argv(char *argv[], int *argc,
				  char *str, char *username)
{
	char *p;
	int i = *argc;

	while (str && isspace(*str))
		str++;
	p = str;

	while (p && *p && i < LOGIN_ARGV_MAX) {
		if (isspace(*p)) {
			*p = '\0';
			while (isspace(*++p))
				;
			if (*p) {
				argv[i++] = replace_u(str, username);
				str = p;
			}
		} else
			p++;
	}
	if (str && *str && i < LOGIN_ARGV_MAX)
		argv[i++] = replace_u(str, username);
	*argc = i;
}

#define is_speed(str) (strlen((str)) == strspn((str), "0123456789,"))

/* Parse command-line arguments. */
static void parse_args(int argc, char **argv, struct options *op)
{
	int c;

	enum {
		VERSION_OPTION = CHAR_MAX + 1,
		NOHINTS_OPTION,
		NOHOSTNAME_OPTION,
		LONGHOSTNAME_OPTION,
		HELP_OPTION,
		ERASE_CHARS_OPTION,
		KILL_CHARS_OPTION,
	};
	const struct option longopts[] = {
		{  "8bits",	     no_argument,	 0,  '8'  },
		{  "autologin",	     required_argument,	 0,  'a'  },
		{  "noreset",	     no_argument,	 0,  'c'  },
		{  "chdir",	     required_argument,	 0,  'C'  },
		{  "delay",	     required_argument,	 0,  'd'  },
		{  "remote",         no_argument,        0,  'E'  },
		{  "issue-file",     required_argument,  0,  'f'  },
		{  "flow-control",   no_argument,	 0,  'h'  },
		{  "host",	     required_argument,  0,  'H'  },
		{  "noissue",	     no_argument,	 0,  'i'  },
		{  "init-string",    required_argument,  0,  'I'  },
		{  "noclear",	     no_argument,	 0,  'J'  },
		{  "login-program",  required_argument,  0,  'l'  },
		{  "local-line",     optional_argument,	 0,  'L'  },
		{  "extract-baud",   no_argument,	 0,  'm'  },
		{  "skip-login",     no_argument,	 0,  'n'  },
		{  "nonewline",	     no_argument,	 0,  'N'  },
		{  "login-options",  required_argument,  0,  'o'  },
		{  "login-pause",    no_argument,        0,  'p'  },
		{  "nice",	     required_argument,  0,  'P'  },
		{  "chroot",	     required_argument,	 0,  'r'  },
		{  "hangup",	     no_argument,	 0,  'R'  },
		{  "keep-baud",      no_argument,	 0,  's'  },
		{  "timeout",	     required_argument,  0,  't'  },
		{  "detect-case",    no_argument,	 0,  'U'  },
		{  "wait-cr",	     no_argument,	 0,  'w'  },
		{  "nohints",        no_argument,        0,  NOHINTS_OPTION },
		{  "nohostname",     no_argument,	 0,  NOHOSTNAME_OPTION },
		{  "long-hostname",  no_argument,	 0,  LONGHOSTNAME_OPTION },
		{  "version",	     no_argument,	 0,  VERSION_OPTION  },
		{  "help",	     no_argument,	 0,  HELP_OPTION     },
		{  "erase-chars",    required_argument,  0,  ERASE_CHARS_OPTION },
		{  "kill-chars",     required_argument,  0,  KILL_CHARS_OPTION },
		{ NULL, 0, 0, 0 }
	};

	while ((c = getopt_long(argc, argv,
			   "8a:cC:d:Ef:hH:iI:Jl:L::mnNo:pP:r:Rst:Uw", longopts,
			    NULL)) != -1) {
		switch (c) {
		case '8':
			op->flags |= F_EIGHTBITS;
			break;
		case 'a':
			op->autolog = optarg;
			break;
		case 'c':
			op->flags |= F_KEEPCFLAGS;
			break;
		case 'C':
			op->chdir = optarg;
			break;
		case 'd':
			op->delay = atoi(optarg);
			break;
		case 'E':
			op->flags |= F_REMOTE;
			break;
		case 'f':
			op->flags |= F_CUSTISSUE;
			op->issue = optarg;
			break;
		case 'h':
			op->flags |= F_RTSCTS;
			break;
		case 'H':
			fakehost = optarg;
			break;
		case 'i':
			op->flags &= ~F_ISSUE;
			break;
		case 'I':
			init_special_char(optarg, op);
			op->flags |= F_INITSTRING;
			break;
		case 'J':
			op->flags |= F_NOCLEAR;
			break;
		case 'l':
			op->login = optarg;
			break;
		case 'L':
			/* -L and -L=always have the same meaning */
			op->clocal = CLOCAL_MODE_ALWAYS;
			if (optarg) {
				if (strcmp(optarg, "=always") == 0)
					op->clocal = CLOCAL_MODE_ALWAYS;
				else if (strcmp(optarg, "=never") == 0)
					op->clocal = CLOCAL_MODE_NEVER;
				else if (strcmp(optarg, "=auto") == 0)
					op->clocal = CLOCAL_MODE_AUTO;
				else
					log_err(_("invalid argument of --local-line"));
			}
			break;
		case 'm':
			op->flags |= F_PARSE;
			break;
		case 'n':
			op->flags |= F_NOPROMPT;
			break;
		case 'N':
			op->flags |= F_NONL;
			break;
		case 'o':
			op->logopt = optarg;
			break;
		case 'p':
			op->flags |= F_LOGINPAUSE;
			break;
		case 'P':
			op->nice = atoi(optarg);
			break;
		case 'r':
			op->chroot = optarg;
			break;
		case 'R':
			op->flags |= F_HANGUP;
			break;
		case 's':
			op->flags |= F_KEEPSPEED;
			break;
		case 't':
			if ((op->timeout = atoi(optarg)) <= 0)
				log_err(_("bad timeout value: %s"), optarg);
			break;
		case 'U':
			op->flags |= F_LCUC;
			break;
		case 'w':
			op->flags |= F_WAITCRLF;
			break;
		case NOHINTS_OPTION:
			op->flags |= F_NOHINTS;
			break;
		case NOHOSTNAME_OPTION:
			op->flags |= F_NOHOSTNAME;
			break;
		case LONGHOSTNAME_OPTION:
			op->flags |= F_LONGHNAME;
			break;
		case ERASE_CHARS_OPTION:
			op->erasechars = optarg;
			break;
		case KILL_CHARS_OPTION:
			op->killchars = optarg;
			break;
		case VERSION_OPTION:
			printf(_("%s from %s\n"), program_invocation_short_name,
			       PACKAGE_STRING);
			exit(EXIT_SUCCESS);
		case HELP_OPTION:
			usage(stdout);
		default:
			usage(stderr);
		}
	}

	debug("after getopt loop\n");

	if (argc < optind + 1) {
		log_warn(_("not enough arguments"));
		usage(stderr);
	}

	/* Accept "tty", "baudrate tty", and "tty baudrate". */
	if (is_speed(argv[optind])) {
		/* Assume BSD style speed. */
		parse_speeds(op, argv[optind++]);
		if (argc < optind + 1) {
			warn(_("not enough arguments"));
			usage(stderr);
		}
		op->tty = argv[optind++];
	} else {
		op->tty = argv[optind++];
		if (argc > optind) {
			char *v = argv[optind++];
			if (is_speed(v))
				parse_speeds(op, v);
			else
				op->speeds[op->numspeed++] = bcode("9600");
		}
	}

	/* On virtual console remember the line which is used for */
	if (strncmp(op->tty, "tty", 3) == 0 &&
	    strspn(op->tty + 3, "0123456789") == strlen(op->tty+3))
		op->vcline = op->tty+3;

	if (argc > optind && argv[optind])
		op->term = argv[optind];

#ifdef DO_DEVFS_FIDDLING
	/*
	 * Some devfs junk, following Goswin Brederlow:
	 *   turn ttyS<n> into tts/<n>
	 *   turn tty<n> into vc/<n>
	 * http://bugs.debian.org/cgi-bin/bugreport.cgi?bug=72241
	 */
	if (op->tty && strlen(op->tty) < 90) {
		char dev_name[100];
		struct stat st;

		if (strncmp(op->tty, "ttyS", 4) == 0) {
			strcpy(dev_name, "/dev/");
			strcat(dev_name, op->tty);
			if (stat(dev_name, &st) < 0) {
				strcpy(dev_name, "/dev/tts/");
				strcat(dev_name, op->tty + 4);
				if (stat(dev_name, &st) == 0) {
					op->tty = strdup(dev_name + 5);
					if (!op->tty)
						log_err(_("failed to allocate memory: %m"));
				}
			}
		} else if (strncmp(op->tty, "tty", 3) == 0) {
			strcpy(dev_name, "/dev/");
			strncat(dev_name, op->tty, 90);
			if (stat(dev_name, &st) < 0) {
				strcpy(dev_name, "/dev/vc/");
				strcat(dev_name, op->tty + 3);
				if (stat(dev_name, &st) == 0) {
					op->tty = strdup(dev_name + 5);
					if (!op->tty)
						log_err(_("failed to allocate memory: %m"));
				}
			}
		}
	}
#endif				/* DO_DEVFS_FIDDLING */

	debug("exiting parseargs\n");
}

/* Parse alternate baud rates. */
static void parse_speeds(struct options *op, char *arg)
{
	char *cp;

	debug("entered parse_speeds\n");
	for (cp = strtok(arg, ","); cp != NULL; cp = strtok((char *)0, ",")) {
		if ((op->speeds[op->numspeed++] = bcode(cp)) <= 0)
			log_err(_("bad speed: %s"), cp);
		if (op->numspeed >= MAX_SPEED)
			log_err(_("too many alternate speeds"));
	}
	debug("exiting parsespeeds\n");
}

#ifdef	SYSV_STYLE

/* Update our utmp entry. */
static void update_utmp(struct options *op)
{
	struct utmp ut;
	time_t t;
	pid_t pid = getpid();
	pid_t sid = getsid(0);
	char *vcline = op->vcline;
	char *line   = op->tty;
	struct utmp *utp;

	/*
	 * The utmp file holds miscellaneous information about things started by
	 * /sbin/init and other system-related events. Our purpose is to update
	 * the utmp entry for the current process, in particular the process type
	 * and the tty line we are listening to. Return successfully only if the
	 * utmp file can be opened for update, and if we are able to find our
	 * entry in the utmp file.
	 */
	utmpname(_PATH_UTMP);
	setutent();

	/*
	 * Find my pid in utmp.
	 *
	 * FIXME: Earlier (when was that?) code here tested only utp->ut_type !=
	 * INIT_PROCESS, so maybe the >= here should be >.
	 *
	 * FIXME: The present code is taken from login.c, so if this is changed,
	 * maybe login has to be changed as well (is this true?).
	 */
	while ((utp = getutent()))
		if (utp->ut_pid == pid
				&& utp->ut_type >= INIT_PROCESS
				&& utp->ut_type <= DEAD_PROCESS)
			break;

	if (utp) {
		memcpy(&ut, utp, sizeof(ut));
	} else {
		/* Some inits do not initialize utmp. */
		memset(&ut, 0, sizeof(ut));
		if (vcline && *vcline)
			/* Standard virtual console devices */
			strncpy(ut.ut_id, vcline, sizeof(ut.ut_id));
		else {
			size_t len = strlen(line);
			char * ptr;
			if (len >= sizeof(ut.ut_id))
				ptr = line + len - sizeof(ut.ut_id);
			else
				ptr = line;
			strncpy(ut.ut_id, ptr, sizeof(ut.ut_id));
		}
	}

	strncpy(ut.ut_user, "LOGIN", sizeof(ut.ut_user));
	strncpy(ut.ut_line, line, sizeof(ut.ut_line));
	if (fakehost)
		strncpy(ut.ut_host, fakehost, sizeof(ut.ut_host));
	time(&t);
#if defined(_HAVE_UT_TV)
	ut.ut_tv.tv_sec = t;
#else
	ut.ut_time = t;
#endif
	ut.ut_type = LOGIN_PROCESS;
	ut.ut_pid = pid;
	ut.ut_session = sid;

	pututline(&ut);
	endutent();

	{
#ifdef HAVE_UPDWTMP
		updwtmp(_PATH_WTMP, &ut);
#else
		int ut_fd;
		int lf;

		if ((lf = open(_PATH_WTMPLOCK, O_CREAT | O_WRONLY, 0660)) >= 0) {
			flock(lf, LOCK_EX);
			if ((ut_fd =
			     open(_PATH_WTMP, O_APPEND | O_WRONLY)) >= 0) {
				write_all(ut_fd, &ut, sizeof(ut));
				close(ut_fd);
			}
			flock(lf, LOCK_UN);
			close(lf);
		}
#endif				/* HAVE_UPDWTMP */
	}
}

#endif				/* SYSV_STYLE */

/* Set up tty as stdin, stdout & stderr. */
static void open_tty(char *tty, struct termios *tp, struct options *op)
{
	const pid_t pid = getpid();
	int closed = 0;

	/* Set up new standard input, unless we are given an already opened port. */

	if (strcmp(tty, "-") != 0) {
		char buf[PATH_MAX+1];
		struct group *gr = NULL;
		struct stat st;
		int fd, len;
		pid_t tid;
		gid_t gid = 0;

		/* Use tty group if available */
		if ((gr = getgrnam("tty")))
			gid = gr->gr_gid;

		if (((len = snprintf(buf, sizeof(buf), "/dev/%s", tty)) >=
		     (int)sizeof(buf)) || (len < 0))
			log_err(_("/dev/%s: cannot open as standard input: %m"), tty);

		/*
		 * There is always a race between this reset and the call to
		 * vhangup() that s.o. can use to get access to your tty.
		 * Linux login(1) will change tty permissions. Use root owner and group
		 * with permission -rw------- for the period between getty and login.
		 */
		if (chown(buf, 0, gid) || chmod(buf, (gid ? 0620 : 0600))) {
			if (errno == EROFS)
				log_warn("%s: %m", buf);
			else
				log_err("%s: %m", buf);
		}

		/* Open the tty as standard input. */
		if ((fd = open(buf, O_RDWR|O_NOCTTY|O_NONBLOCK, 0)) < 0)
			log_err(_("/dev/%s: cannot open as standard input: %m"), tty);

		/* Sanity checks... */
		if (fstat(fd, &st) < 0)
			log_err("%s: %m", buf);
		if ((st.st_mode & S_IFMT) != S_IFCHR)
			log_err(_("/dev/%s: not a character device"), tty);
		if (!isatty(fd))
			log_err(_("/dev/%s: not a tty"), tty);

		if (((tid = tcgetsid(fd)) < 0) || (pid != tid)) {
			if (ioctl(fd, TIOCSCTTY, 1) == -1)
				log_warn(_("/dev/%s: cannot get controlling tty: %m"), tty);
		}

		close(STDIN_FILENO);
		errno = 0;

		if (op->flags & F_HANGUP) {

			if (ioctl(fd, TIOCNOTTY))
				debug("TIOCNOTTY ioctl failed\n");

			/*
			 * Let's close all file decriptors before vhangup
			 * https://lkml.org/lkml/2012/6/5/145
			 */
			close(fd);
			close(STDOUT_FILENO);
			close(STDERR_FILENO);
			errno = 0;
			closed = 1;

			if (vhangup())
				log_err(_("/dev/%s: vhangup() failed: %m"), tty);
		} else
			close(fd);

		debug("open(2)\n");
		if (open(buf, O_RDWR|O_NOCTTY|O_NONBLOCK, 0) != 0)
			log_err(_("/dev/%s: cannot open as standard input: %m"), tty);

		if (((tid = tcgetsid(STDIN_FILENO)) < 0) || (pid != tid)) {
			if (ioctl(STDIN_FILENO, TIOCSCTTY, 1) == -1)
				log_warn(_("/dev/%s: cannot get controlling tty: %m"), tty);
		}

	} else {

		/*
		 * Standard input should already be connected to an open port. Make
		 * sure it is open for read/write.
		 */

		if ((fcntl(STDIN_FILENO, F_GETFL, 0) & O_RDWR) != O_RDWR)
			log_err(_("%s: not open for read/write"), tty);

	}

	if (tcsetpgrp(STDIN_FILENO, pid))
		log_warn(_("/dev/%s: cannot set process group: %m"), tty);

	/* Get rid of the present outputs. */
	if (!closed) {
		close(STDOUT_FILENO);
		close(STDERR_FILENO);
		errno = 0;
	}

	/* Set up standard output and standard error file descriptors. */
	debug("duping\n");

	/* set up stdout and stderr */
	if (dup(STDIN_FILENO) != 1 || dup(STDIN_FILENO) != 2)
		log_err(_("%s: dup problem: %m"), tty);

	/* make stdio unbuffered for slow modem lines */
	setvbuf(stdout, NULL, _IONBF, 0);

	/*
	 * The following ioctl will fail if stdin is not a tty, but also when
	 * there is noise on the modem control lines. In the latter case, the
	 * common course of action is (1) fix your cables (2) give the modem
	 * more time to properly reset after hanging up.
	 *
	 * SunOS users can achieve (2) by patching the SunOS kernel variable
	 * "zsadtrlow" to a larger value; 5 seconds seems to be a good value.
	 * http://www.sunmanagers.org/archives/1993/0574.html
	 */
	memset(tp, 0, sizeof(struct termios));
	if (tcgetattr(STDIN_FILENO, tp) < 0)
		log_err(_("%s: failed to get terminal attributes: %m"), tty);

#if defined (__s390__) || defined (__s390x__)
	if (!op->term) {
	        /*
		 * Special terminal on first serial line on a S/390(x) which
		 * is due legacy reasons a block terminal of type 3270 or
		 * higher.  Whereas the second serial line on a S/390(x) is
		 * a real character terminal which is compatible with VT220.
		 */
		if (strcmp(op->tty, "ttyS0") == 0)		/* linux/drivers/s390/char/con3215.c */
			op->term = DEFAULT_TTYS0;
		else if (strncmp(op->tty, "3270/tty", 8) == 0)	/* linux/drivers/s390/char/con3270.c */
			op->term = DEFAULT_TTY32;
		else if (strcmp(op->tty, "ttyS1") == 0)		/* linux/drivers/s390/char/sclp_vt220.c */
			op->term = DEFAULT_TTYS1;
	}
#endif
	/*
	 * Detect if this is a virtual console or serial/modem line.
	 * In case of a virtual console the ioctl KDGKBMODE succeeds
	 * whereas on other lines it will fails.
	 */
	if (ioctl(STDIN_FILENO, KDGKBMODE, &op->kbmode) == 0) {
		op->flags |= F_VCONSOLE;
		if (!op->term)
			op->term = DEFAULT_VCTERM;
	} else {
		op->kbmode = K_RAW;
		if (!op->term)
			op->term = DEFAULT_STERM;
	}

	setenv("TERM", op->term, 1);
}

/* Initialize termios settings. */
static void termio_init(struct options *op, struct termios *tp)
{
	speed_t ispeed, ospeed;
	struct winsize ws;
	struct termios lock;
	int i =  (plymouth_command("--ping") == 0) ? 30 : 0;

	while (i-- > 0) {
		/*
		 * Even with TTYReset=no it seems with systemd or plymouth
		 * the termios flags become changed from under the first
		 * agetty on a serial system console as the flags are locked.
		 */
		memset(&lock, 0, sizeof(struct termios));
		if (ioctl(STDIN_FILENO, TIOCGLCKTRMIOS, &lock) < 0)
			break;
		if (!lock.c_iflag && !lock.c_oflag && !lock.c_cflag && !lock.c_lflag)
			break;
		debug("termios locked\n");
		if (i == 15 && plymouth_command("quit") != 0)
			break;
		sleep(1);
	}
	memset(&lock, 0, sizeof(struct termios));
	ioctl(STDIN_FILENO, TIOCSLCKTRMIOS, &lock);

	if (op->flags & F_VCONSOLE) {
#if defined(IUTF8) && defined(KDGKBMODE)
		switch(op->kbmode) {
		case K_UNICODE:
			setlocale(LC_CTYPE, "C.UTF-8");
			op->flags |= F_UTF8;
			break;
		case K_RAW:
		case K_MEDIUMRAW:
		case K_XLATE:
		default:
			setlocale(LC_CTYPE, "POSIX");
			op->flags &= ~F_UTF8;
			break;
		}
#else
		setlocale(LC_CTYPE, "POSIX");
		op->flags &= ~F_UTF8;
#endif
		reset_vc(op, tp);

		if ((tp->c_cflag & (CS8|PARODD|PARENB)) == CS8)
			op->flags |= F_EIGHTBITS;

		if ((op->flags & F_NOCLEAR) == 0) {
			/*
			 * Do not write a full reset (ESC c) because this destroys
			 * the unicode mode again if the terminal was in unicode
			 * mode.  Also it clears the CONSOLE_MAGIC features which
			 * are required for some languages/console-fonts.
			 * Just put the cursor to the home position (ESC [ H),
			 * erase everything below the cursor (ESC [ J), and set the
			 * scrolling region to the full window (ESC [ r)
			 */
			write_all(STDOUT_FILENO, "\033[r\033[H\033[J", 9);
		}
		return;
	}

	if (op->flags & F_KEEPSPEED) {
		/* Save the original setting. */
		ispeed = cfgetispeed(tp);
		ospeed = cfgetospeed(tp);

		if (!ispeed) ispeed = TTYDEF_SPEED;
		if (!ospeed) ospeed = TTYDEF_SPEED;

	} else {
		ospeed = ispeed = op->speeds[FIRST_SPEED];
	}

	/*
	 * Initial termios settings: 8-bit characters, raw-mode, blocking i/o.
	 * Special characters are set after we have read the login name; all
	 * reads will be done in raw mode anyway. Errors will be dealt with
	 * later on.
	 */

#ifdef IUTF8
	tp->c_iflag = tp->c_iflag & IUTF8;
	if (tp->c_iflag & IUTF8)
		op->flags |= F_UTF8;
#else
	tp->c_iflag = 0;
#endif
	tp->c_lflag = 0;
	tp->c_oflag &= OPOST | ONLCR;

	if ((op->flags & F_KEEPCFLAGS) == 0)
		tp->c_cflag = CS8 | HUPCL | CREAD | (tp->c_cflag & CLOCAL);

	/*
	 * Note that the speed is stored in the c_cflag termios field, so we have
	 * set the speed always when the cflag se reseted.
	 */
	cfsetispeed(tp, ispeed);
	cfsetospeed(tp, ospeed);

	/* The default is to follow setting from kernel, but it's possible
	 * to explicitly remove/add CLOCAL flag by -L[=<mode>]*/
	switch (op->clocal) {
	case CLOCAL_MODE_ALWAYS:
		tp->c_cflag |= CLOCAL;		/* -L or -L=always */
		break;
	case CLOCAL_MODE_NEVER:
		tp->c_cflag &= ~CLOCAL;		/* -L=never */
		break;
	case CLOCAL_MODE_AUTO:			/* -L=auto */
		break;
	}

#ifdef HAVE_STRUCT_TERMIOS_C_LINE
	tp->c_line = 0;
#endif
	tp->c_cc[VMIN] = 1;
	tp->c_cc[VTIME] = 0;

	/* Check for terminal size and if not found set default */
	if (ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) == 0) {
		int set = 0;
		if (ws.ws_row == 0) {
			ws.ws_row = 24;
			set++;
		}
		if (ws.ws_col == 0) {
			ws.ws_col = 80;
			set++;
		}
		if (ioctl(STDIN_FILENO, TIOCSWINSZ, &ws))
			debug("TIOCSWINSZ ioctl failed\n");
	}

	/* Optionally enable hardware flow control. */
#ifdef	CRTSCTS
	if (op->flags & F_RTSCTS)
		tp->c_cflag |= CRTSCTS;
#endif
	 /* Flush input and output queues, important for modems! */
	tcflush(STDIN_FILENO, TCIOFLUSH);

	if (tcsetattr(STDIN_FILENO, TCSANOW, tp))
		log_warn(_("setting terminal attributes failed: %m"));

	/* Go to blocking input even in local mode. */
	fcntl(STDIN_FILENO, F_SETFL,
	      fcntl(STDIN_FILENO, F_GETFL, 0) & ~O_NONBLOCK);

	debug("term_io 2\n");
}

/* Reset virtual console on stdin to its defaults */
static void reset_vc(const struct options *op, struct termios *tp)
{
	int fl = 0;

	fl |= (op->flags & F_KEEPCFLAGS) == 0 ? 0 : UL_TTY_KEEPCFLAGS;
	fl |= (op->flags & F_UTF8)       == 0 ? 0 : UL_TTY_UTF8;

	reset_virtual_console(tp, fl);

	if (tcsetattr(STDIN_FILENO, TCSADRAIN, tp))
		log_warn(_("setting terminal attributes failed: %m"));

	/* Go to blocking input even in local mode. */
	fcntl(STDIN_FILENO, F_SETFL,
	      fcntl(STDIN_FILENO, F_GETFL, 0) & ~O_NONBLOCK);
}

/* Extract baud rate from modem status message. */
static void auto_baud(struct termios *tp)
{
	speed_t speed;
	int vmin;
	unsigned iflag;
	char buf[BUFSIZ];
	char *bp;
	int nread;

	/*
	 * This works only if the modem produces its status code AFTER raising
	 * the DCD line, and if the computer is fast enough to set the proper
	 * baud rate before the message has gone by. We expect a message of the
	 * following format:
	 *
	 * <junk><number><junk>
	 *
	 * The number is interpreted as the baud rate of the incoming call. If the
	 * modem does not tell us the baud rate within one second, we will keep
	 * using the current baud rate. It is advisable to enable BREAK
	 * processing (comma-separated list of baud rates) if the processing of
	 * modem status messages is enabled.
	 */

	/*
	 * Use 7-bit characters, don't block if input queue is empty. Errors will
	 * be dealt with later on.
	 */
	iflag = tp->c_iflag;
	/* Enable 8th-bit stripping. */
	tp->c_iflag |= ISTRIP;
	vmin = tp->c_cc[VMIN];
	/* Do not block when queue is empty. */
	tp->c_cc[VMIN] = 0;
	tcsetattr(STDIN_FILENO, TCSANOW, tp);

	/*
	 * Wait for a while, then read everything the modem has said so far and
	 * try to extract the speed of the dial-in call.
	 */
	sleep(1);
	if ((nread = read(STDIN_FILENO, buf, sizeof(buf) - 1)) > 0) {
		buf[nread] = '\0';
		for (bp = buf; bp < buf + nread; bp++)
			if (isascii(*bp) && isdigit(*bp)) {
				if ((speed = bcode(bp))) {
					cfsetispeed(tp, speed);
					cfsetospeed(tp, speed);
				}
				break;
			}
	}

	/* Restore terminal settings. Errors will be dealt with later on. */
	tp->c_iflag = iflag;
	tp->c_cc[VMIN] = vmin;
	tcsetattr(STDIN_FILENO, TCSANOW, tp);
}

static char *xgethostname(void)
{
	char *name;
	size_t sz = get_hostname_max() + 1;

	name = malloc(sizeof(char) * sz);
	if (!name)
		log_err(_("failed to allocate memory: %m"));

	if (gethostname(name, sz) != 0) {
		free(name);
		return NULL;
	}
	name[sz - 1] = '\0';
	return name;
}

static char *xgetdomainname(void)
{
#ifdef HAVE_GETDOMAINNAME
	char *name;
	size_t sz = get_hostname_max() + 1;

	name = malloc(sizeof(char) * sz);
	if (!name)
		log_err(_("failed to allocate memory: %m"));

	if (getdomainname(name, sz) != 0) {
		free(name);
		return NULL;
	}
	name[sz - 1] = '\0';
	return name;
#endif
	return NULL;
}

static char *read_os_release(struct options *op, const char *varname)
{
	int fd = -1;
	struct stat st;
	size_t varsz = strlen(varname);
	char *p, *buf = NULL, *ret = NULL;

	/* read the file only once */
	if (!op->osrelease) {
		fd = open(_PATH_OS_RELEASE, O_RDONLY);
		if (fd == -1) {
			log_warn(_("cannot open: %s: %m"), _PATH_OS_RELEASE);
			return NULL;
		}

		if (fstat(fd, &st) < 0 || st.st_size > 4 * 1024 * 1024)
			goto done;

		op->osrelease = malloc(st.st_size + 1);
		if (!op->osrelease)
			log_err(_("failed to allocate memory: %m"));
		if (read_all(fd, op->osrelease, st.st_size) != (ssize_t) st.st_size) {
			free(op->osrelease);
			op->osrelease = NULL;
			goto done;
		}
		op->osrelease[st.st_size] = 0;
	}
	buf = strdup(op->osrelease);
	if (!buf)
		log_err(_("failed to allocate memory: %m"));
	p = buf;

	for (;;) {
		char *eol, *eon;

		p += strspn(p, "\n\r");
		p += strspn(p, " \t\n\r");
		if (!*p)
			break;
		if (strspn(p, "#;\n") != 0) {
			p += strcspn(p, "\n\r");
			continue;
		}
		if (strncmp(p, varname, varsz) != 0) {
			p += strcspn(p, "\n\r");
			continue;
		}
		p += varsz;
		p += strspn(p, " \t\n\r=\"");
		eol = p + strcspn(p, "\n\r");
		*eol = '\0';
		eon = eol-1;
		while (eon > p) {
			if (*eon == '\t' || *eon == ' ') {
				eon--;
				continue;
			}
			if (*eon == '"') {
				*eon = '\0';
				break;
			}
			break;
		}
		free(ret);
		ret = strdup(p);
		if (!ret)
			log_err(_("failed to allocate memory: %m"));
		p = eol + 1;
	}
done:
	free(buf);
	if (fd >= 0)
		close(fd);
	return ret;
}

/* Show login prompt, optionally preceded by /etc/issue contents. */
static void do_prompt(struct options *op, struct termios *tp)
{
#ifdef	ISSUE
	FILE *fd;
#endif				/* ISSUE */

	if ((op->flags & F_NONL) == 0) {
		/* Issue not in use, start with a new line. */
		write_all(STDOUT_FILENO, "\r\n", 2);
	}

#ifdef	ISSUE
	if ((op->flags & F_ISSUE) && (fd = fopen(op->issue, "r"))) {
		int c, oflag = tp->c_oflag;	    /* Save current setting. */

		if ((op->flags & F_VCONSOLE) == 0) {
			/* Map new line in output to carriage return & new line. */
			tp->c_oflag |= (ONLCR | OPOST);
			tcsetattr(STDIN_FILENO, TCSADRAIN, tp);
		}

		while ((c = getc(fd)) != EOF) {
			if (c == '\\')
				output_special_char(getc(fd), op, tp, fd);
			else
				putchar(c);
		}
		fflush(stdout);

		if ((op->flags & F_VCONSOLE) == 0) {
			/* Restore settings. */
			tp->c_oflag = oflag;
			/* Wait till output is gone. */
			tcsetattr(STDIN_FILENO, TCSADRAIN, tp);
		}
		fclose(fd);
	}
#endif	/* ISSUE */
	if (op->flags & F_LOGINPAUSE) {
		puts(_("[press ENTER to login]"));
		getc(stdin);
	}
#ifdef KDGKBLED
	if (!(op->flags & F_NOHINTS) && !op->autolog &&
	    (op->flags & F_VCONSOLE)) {
		int kb = 0;

		if (ioctl(STDIN_FILENO, KDGKBLED, &kb) == 0) {
			char hint[256] = { '\0' };
			int nl = 0;

			if (access(_PATH_NUMLOCK_ON, F_OK) == 0)
				nl = 1;

			if (nl && (kb & 0x02) == 0)
				append(hint, sizeof(hint), NULL, _("Num Lock off"));

			else if (nl == 0 && (kb & 2) && (kb & 0x20) == 0)
				append(hint, sizeof(hint), NULL, _("Num Lock on"));

			if ((kb & 0x04) && (kb & 0x40) == 0)
				append(hint, sizeof(hint), ", ", _("Caps Lock on"));

			if ((kb & 0x01) && (kb & 0x10) == 0)
				append(hint, sizeof(hint), ", ",  _("Scroll Lock on"));

			if (*hint)
				printf(_("Hint: %s\n\n"), hint);
		}
	}
#endif /* KDGKBLED */
	if ((op->flags & F_NOHOSTNAME) == 0) {
		char *hn = xgethostname();

		if (hn) {
			char *dot = strchr(hn, '.');
			char *cn = hn;
			struct addrinfo *res = NULL;

			if ((op->flags & F_LONGHNAME) == 0) {
				if (dot)
					*dot = '\0';

			} else if (dot == NULL) {
				struct addrinfo hints;

				memset(&hints, 0, sizeof(hints));
				hints.ai_flags = AI_CANONNAME;

				if (!getaddrinfo(hn, NULL, &hints, &res)
				    && res && res->ai_canonname)
					cn = res->ai_canonname;
			}

			write_all(STDOUT_FILENO, cn, strlen(cn));
			write_all(STDOUT_FILENO, " ", 1);

			if (res)
				freeaddrinfo(res);
			free(hn);
		}
	}
	if (!op->autolog) {
		/* Always show login prompt. */
		write_all(STDOUT_FILENO, LOGIN, sizeof(LOGIN) - 1);
	}
}

/* Select next baud rate. */
static void next_speed(struct options *op, struct termios *tp)
{
	static int baud_index = -1;

	if (baud_index == -1)
		/*
		 * If the F_KEEPSPEED flags is set then the FIRST_SPEED is not
		 * tested yet (see termio_init()).
		 */
		baud_index =
		    (op->flags & F_KEEPSPEED) ? FIRST_SPEED : 1 % op->numspeed;
	else
		baud_index = (baud_index + 1) % op->numspeed;

	cfsetispeed(tp, op->speeds[baud_index]);
	cfsetospeed(tp, op->speeds[baud_index]);
	tcsetattr(STDIN_FILENO, TCSANOW, tp);
}

/* Get user name, establish parity, speed, erase, kill & eol. */
static char *get_logname(struct options *op, struct termios *tp, struct chardata *cp)
{
	static char logname[BUFSIZ];
	char *bp;
	char c;			/* input character, full eight bits */
	char ascval;		/* low 7 bits of input character */
	int eightbit;
	static char *erase[] = {	/* backspace-space-backspace */
		"\010\040\010",		/* space parity */
		"\010\040\010",		/* odd parity */
		"\210\240\210",		/* even parity */
		"\210\240\210",		/* no parity */
	};

	/* Initialize kill, erase, parity etc. (also after switching speeds). */
	INIT_CHARDATA(cp);

	/*
	 * Flush pending input (especially important after parsing or switching
	 * the baud rate).
	 */
	if ((op->flags & F_VCONSOLE) == 0)
		sleep(1);
	tcflush(STDIN_FILENO, TCIFLUSH);

	eightbit = (op->flags & F_EIGHTBITS);
	bp = logname;
	*bp = '\0';

	while (*logname == '\0') {

		/* Write issue file and prompt */
		do_prompt(op, tp);

		cp->eol = '\0';

		/* Read name, watch for break and end-of-line. */
		while (cp->eol == '\0') {

			char key;

			if (read(STDIN_FILENO, &c, 1) < 1) {

				/* The terminal could be open with O_NONBLOCK when
				 * -L (force CLOCAL) is specified...  */
				if (errno == EINTR || errno == EAGAIN) {
					xusleep(250000);
					continue;
				}
				switch (errno) {
				case 0:
				case EIO:
				case ESRCH:
				case EINVAL:
				case ENOENT:
					break;
				default:
					log_err(_("%s: read: %m"), op->tty);
				}
			}

			/* Do parity bit handling. */
			if (eightbit)
				ascval = c;
			else if (c != (ascval = (c & 0177))) {
				uint32_t bits;			/* # of "1" bits per character */
				uint32_t mask;			/* mask with 1 bit up */
				for (bits = 1, mask = 1; mask & 0177; mask <<= 1) {
					if (mask & ascval)
						bits++;
				}
				cp->parity |= ((bits & 1) ? 1 : 2);
			}

			if (op->killchars && strchr(op->killchars, ascval))
				key = CTL('U');
			else if (op->erasechars && strchr(op->erasechars, ascval))
				key = DEL;
			else
				key = ascval;

			/* Do erase, kill and end-of-line processing. */
			switch (key) {
			case 0:
				*bp = 0;
				if (op->numspeed > 1)
					return NULL;
				break;
			case CR:
			case NL:
				*bp = 0;			/* terminate logname */
				cp->eol = ascval;		/* set end-of-line char */
				break;
			case BS:
			case DEL:
				cp->erase = ascval; /* set erase character */
				if (bp > logname) {
					if ((tp->c_lflag & ECHO) == 0)
						write_all(1, erase[cp->parity], 3);
					bp--;
				}
				break;
			case CTL('U'):
				cp->kill = ascval;		/* set kill character */
				while (bp > logname) {
					if ((tp->c_lflag & ECHO) == 0)
						write_all(1, erase[cp->parity], 3);
					bp--;
				}
				break;
			case CTL('D'):
				exit(EXIT_SUCCESS);
			default:
				if (!isascii(ascval) || !isprint(ascval))
					break;
				if ((size_t)(bp - logname) >= sizeof(logname) - 1)
					log_err(_("%s: input overrun"), op->tty);
				if ((tp->c_lflag & ECHO) == 0)
					write_all(1, &c, 1);	/* echo the character */
				*bp++ = ascval;			/* and store it */
				break;
			}
		}
	}
#ifdef HAVE_WIDECHAR
	if ((op->flags & (F_EIGHTBITS|F_UTF8)) == (F_EIGHTBITS|F_UTF8)) {
		/* Check out UTF-8 multibyte characters */
		ssize_t len;
		wchar_t *wcs, *wcp;

		len = mbstowcs((wchar_t *)0, logname, 0);
		if (len < 0)
			log_err(_("%s: invalid character conversion for login name"), op->tty);

		wcs = (wchar_t *) malloc((len + 1) * sizeof(wchar_t));
		if (!wcs)
			log_err(_("failed to allocate memory: %m"));

		len = mbstowcs(wcs, logname, len + 1);
		if (len < 0)
			log_err(_("%s: invalid character conversion for login name"), op->tty);

		wcp = wcs;
		while (*wcp) {
			const wint_t wc = *wcp++;
			if (!iswprint(wc))
				log_err(_("%s: invalid character 0x%x in login name"), op->tty, wc);
		}
		free(wcs);
	} else
#endif
	if ((op->flags & F_LCUC) && (cp->capslock = caps_lock(logname))) {

		/* Handle names with upper case and no lower case. */
		for (bp = logname; *bp; bp++)
			if (isupper(*bp))
				*bp = tolower(*bp);		/* map name to lower case */
	}

	return logname;
}

/* Set the final tty mode bits. */
static void termio_final(struct options *op, struct termios *tp, struct chardata *cp)
{
	/* General terminal-independent stuff. */

	/* 2-way flow control */
	tp->c_iflag |= IXON | IXOFF;
	tp->c_lflag |= ICANON | ISIG | ECHO | ECHOE | ECHOK | ECHOKE;
	/* no longer| ECHOCTL | ECHOPRT */
	tp->c_oflag |= OPOST;
	/* tp->c_cflag = 0; */
	tp->c_cc[VINTR] = DEF_INTR;
	tp->c_cc[VQUIT] = DEF_QUIT;
	tp->c_cc[VEOF] = DEF_EOF;
	tp->c_cc[VEOL] = DEF_EOL;
#ifdef __linux__
	tp->c_cc[VSWTC] = DEF_SWITCH;
#elif defined(VSWTCH)
	tp->c_cc[VSWTCH] = DEF_SWITCH;
#endif				/* __linux__ */

	/* Account for special characters seen in input. */
	if (cp->eol == CR) {
		tp->c_iflag |= ICRNL;
		tp->c_oflag |= ONLCR;
	}
	tp->c_cc[VERASE] = cp->erase;
	tp->c_cc[VKILL] = cp->kill;

	/* Account for the presence or absence of parity bits in input. */
	switch (cp->parity) {
	case 0:
		/* space (always 0) parity */
		break;
	case 1:
		/* odd parity */
		tp->c_cflag |= PARODD;
		/* do not break */
	case 2:
		/* even parity */
		tp->c_cflag |= PARENB;
		tp->c_iflag |= INPCK | ISTRIP;
		/* do not break */
	case (1 | 2):
		/* no parity bit */
		tp->c_cflag &= ~CSIZE;
		tp->c_cflag |= CS7;
		break;
	}
	/* Account for upper case without lower case. */
	if (cp->capslock) {
#ifdef IUCLC
		tp->c_iflag |= IUCLC;
#endif
#ifdef XCASE
		tp->c_lflag |= XCASE;
#endif
#ifdef OLCUC
		tp->c_oflag |= OLCUC;
#endif
	}
	/* Optionally enable hardware flow control. */
#ifdef	CRTSCTS
	if (op->flags & F_RTSCTS)
		tp->c_cflag |= CRTSCTS;
#endif

	/* Finally, make the new settings effective. */
	if (tcsetattr(STDIN_FILENO, TCSANOW, tp) < 0)
		log_err(_("%s: failed to set terminal attributes: %m"), op->tty);
}

/*
 * String contains upper case without lower case.
 * http://bugs.debian.org/cgi-bin/bugreport.cgi?bug=52940
 * http://bugs.debian.org/cgi-bin/bugreport.cgi?bug=156242
 */
static int caps_lock(char *s)
{
	int capslock;

	for (capslock = 0; *s; s++) {
		if (islower(*s))
			return EXIT_SUCCESS;
		if (capslock == 0)
			capslock = isupper(*s);
	}
	return capslock;
}

/* Convert speed string to speed code; return 0 on failure. */
static speed_t bcode(char *s)
{
	const struct Speedtab *sp;
	long speed = atol(s);

	for (sp = speedtab; sp->speed; sp++)
		if (sp->speed == speed)
			return sp->code;
	return 0;
}

static void __attribute__ ((__noreturn__)) usage(FILE *out)
{
	fputs(USAGE_HEADER, out);
	fprintf(out, _(" %1$s [options] <line> [<baud_rate>,...] [<termtype>]\n"
		       " %1$s [options] <baud_rate>,... <line> [<termtype>]\n"), program_invocation_short_name);
	fputs(USAGE_OPTIONS, out);
	fputs(_(" -8, --8bits                assume 8-bit tty\n"), out);
	fputs(_(" -a, --autologin <user>     login the specified user automatically\n"), out);
	fputs(_(" -c, --noreset              do not reset control mode\n"), out);
	fputs(_(" -E, --remote               use -r <hostname> for login(1)\n"), out);
	fputs(_(" -f, --issue-file <file>    display issue file\n"), out);
	fputs(_(" -h, --flow-control         enable hardware flow control\n"), out);
	fputs(_(" -H, --host <hostname>      specify login host\n"), out);
	fputs(_(" -i, --noissue              do not display issue file\n"), out);
	fputs(_(" -I, --init-string <string> set init string\n"), out);
	fputs(_(" -J  --noclear              do not clear the screen before prompt\n"), out);
	fputs(_(" -l, --login-program <file> specify login program\n"), out);
	fputs(_(" -L, --local-line[=<mode>]  control the local line flag\n"), out);
	fputs(_(" -m, --extract-baud         extract baud rate during connect\n"), out);
	fputs(_(" -n, --skip-login           do not prompt for login\n"), out);
	fputs(_(" -N  --nonewline            do not print a newline before issue\n"), out);
	fputs(_(" -o, --login-options <opts> options that are passed to login\n"), out);
	fputs(_(" -p, --login-pause          wait for any key before the login\n"), out);
	fputs(_(" -r, --chroot <dir>         change root to the directory\n"), out);
	fputs(_(" -R, --hangup               do virtually hangup on the tty\n"), out);
	fputs(_(" -s, --keep-baud            try to keep baud rate after break\n"), out);
	fputs(_(" -t, --timeout <number>     login process timeout\n"), out);
	fputs(_(" -U, --detect-case          detect uppercase terminal\n"), out);
	fputs(_(" -w, --wait-cr              wait carriage-return\n"), out);
	fputs(_("     --nohints              do not print hints\n"), out);
	fputs(_("     --nohostname           no hostname at all will be shown\n"), out);
	fputs(_("     --long-hostname        show full qualified hostname\n"), out);
	fputs(_("     --erase-chars <string> additional backspace chars\n"), out);
	fputs(_("     --kill-chars <string>  additional kill chars\n"), out);
	fputs(_("     --chdir <directory>    chdir before the login\n"), out);
	fputs(_("     --delay <number>       sleep seconds before prompt\n"), out);
	fputs(_("     --nice <number>        run login with this priority\n"), out);
	fputs(_("     --help                 display this help and exit\n"), out);
	fputs(_("     --version              output version information and exit\n"), out);
	fprintf(out, USAGE_MAN_TAIL("agetty(8)"));

	exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}

/*
 * Helper function reports errors to console or syslog.
 * Will be used by log_err() and log_warn() therefore
 * it takes a format as well as va_list.
 */
#define	str2cpy(b,s1,s2)	strcat(strcpy(b,s1),s2)

static void dolog(int priority, const char *fmt, va_list ap)
{
#ifndef	USE_SYSLOG
	int fd;
#endif
	char buf[BUFSIZ];
	char *bp;

	/*
	 * If the diagnostic is reported via syslog(3), the process name is
	 * automatically prepended to the message. If we write directly to
	 * /dev/console, we must prepend the process name ourselves.
	 */
#ifdef USE_SYSLOG
	buf[0] = '\0';
	bp = buf;
#else
	str2cpy(buf, program_invocation_short_name, ": ");
	bp = buf + strlen(buf);
#endif				/* USE_SYSLOG */
	vsnprintf(bp, sizeof(buf)-strlen(buf), fmt, ap);

	/*
	 * Write the diagnostic directly to /dev/console if we do not use the
	 * syslog(3) facility.
	 */
#ifdef	USE_SYSLOG
	openlog(program_invocation_short_name, LOG_PID, LOG_AUTHPRIV);
	syslog(priority, "%s", buf);
	closelog();
#else
	/* Terminate with CR-LF since the console mode is unknown. */
	strcat(bp, "\r\n");
	if ((fd = open("/dev/console", 1)) >= 0) {
		write_all(fd, buf, strlen(buf));
		close(fd);
	}
#endif				/* USE_SYSLOG */
}

static void log_err(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	dolog(LOG_ERR, fmt, ap);
	va_end(ap);

	/* Be kind to init(8). */
	sleep(10);
	exit(EXIT_FAILURE);
}

static void log_warn(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	dolog(LOG_WARNING, fmt, ap);
	va_end(ap);
}

static void print_addr(sa_family_t family, void *addr)
{
	char buff[INET6_ADDRSTRLEN + 1];

	inet_ntop(family, addr, buff, sizeof(buff));
	printf("%s", buff);
}

/*
 * Prints IP for the specified interface (@iface), if the interface is not
 * specified then prints the "best" one (UP, RUNNING, non-LOOPBACK). If not
 * found the "best" interface then prints at least host IP.
 */
static void output_iface_ip(struct ifaddrs *addrs,
			    const char *iface,
			    sa_family_t family)
{
	struct ifaddrs *p;
	struct addrinfo hints, *info = NULL;
	char *host = NULL;
	void *addr = NULL;

	if (!addrs)
		return;

	for (p = addrs; p; p = p->ifa_next) {

		if (!p->ifa_name ||
		    !p->ifa_addr ||
		    p->ifa_addr->sa_family != family)
			continue;

		if (iface) {
			/* Filter out by interface name */
		       if (strcmp(p->ifa_name, iface) != 0)
				continue;
		} else {
			/* Select the "best" interface */
			if ((p->ifa_flags & IFF_LOOPBACK) ||
			    !(p->ifa_flags & IFF_UP) ||
			    !(p->ifa_flags & IFF_RUNNING))
				continue;
		}

		addr = NULL;
		switch (p->ifa_addr->sa_family) {
		case AF_INET:
			addr = &((struct sockaddr_in *)	p->ifa_addr)->sin_addr;
			break;
		case AF_INET6:
			addr = &((struct sockaddr_in6 *) p->ifa_addr)->sin6_addr;
			break;
		}

		if (addr) {
			print_addr(family, addr);
			return;
		}
	}

	if (iface)
		return;

	/* Hmm.. not found the best interface, print host IP at least */
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = family;
	if (family == AF_INET6)
		hints.ai_flags = AI_V4MAPPED;

	host = xgethostname();
	if (host && getaddrinfo(host, NULL, &hints, &info) == 0 && info) {
		switch (info->ai_family) {
		case AF_INET:
			addr = &((struct sockaddr_in *) info->ai_addr)->sin_addr;
			break;
		case AF_INET6:
			addr = &((struct sockaddr_in6 *) info->ai_addr)->sin6_addr;
			break;
		}
		if (addr)
			print_addr(family, addr);

		freeaddrinfo(info);
	}
	free(host);
}

/*
 * parses \x{argument}, if not argument specified then returns NULL, the @fd
 * has to point to one char after the sequence (it means '{').
 */
static char *get_escape_argument(FILE *fd, char *buf, size_t bufsz)
{
	size_t i = 0;
	int c = fgetc(fd);

	if (c == EOF || (unsigned char) c != '{') {
		ungetc(c, fd);
		return NULL;
	}

	do {
		c = fgetc(fd);
		if (c == EOF)
			return NULL;
		if ((unsigned char) c != '}' && i < bufsz - 1)
			buf[i++] = (unsigned char) c;

	} while ((unsigned char) c != '}');

	buf[i] = '\0';
	return buf;
}

static void output_special_char(unsigned char c, struct options *op,
				struct termios *tp, FILE *fp)
{
	struct utsname uts;

	uname(&uts);

	switch (c) {
	case 's':
		printf("%s", uts.sysname);
		break;
	case 'n':
		printf("%s", uts.nodename);
		break;
	case 'r':
		printf("%s", uts.release);
		break;
	case 'v':
		printf("%s", uts.version);
		break;
	case 'm':
		printf("%s", uts.machine);
		break;
	case 'o':
	{
		char *dom = xgetdomainname();

		fputs(dom ? dom : "unknown_domain", stdout);
		free(dom);
		break;
	}
	case 'O':
	{
		char *dom = NULL;
		char *host = xgethostname();
		struct addrinfo hints, *info = NULL;

		memset(&hints, 0, sizeof(hints));
		hints.ai_flags = AI_CANONNAME;

		if (host && getaddrinfo(host, NULL, &hints, &info) == 0 && info) {
			char *canon;

			if (info->ai_canonname &&
			    (canon = strchr(info->ai_canonname, '.')))
				dom = canon + 1;
		}
		fputs(dom ? dom : "unknown_domain", stdout);
		if (info)
			freeaddrinfo(info);
		free(host);
		break;
	}
	case 'd':
	case 't':
	{
		time_t now;
		struct tm *tm;

		time(&now);
		tm = localtime(&now);

		if (!tm)
			break;

		if (c == 'd') /* ISO 8601 */
			printf("%s %s %d  %d",
				      nl_langinfo(ABDAY_1 + tm->tm_wday),
				      nl_langinfo(ABMON_1 + tm->tm_mon),
				      tm->tm_mday,
				      tm->tm_year < 70 ? tm->tm_year + 2000 :
				      tm->tm_year + 1900);
		else
			printf("%02d:%02d:%02d",
				      tm->tm_hour, tm->tm_min, tm->tm_sec);
		break;
	}
	case 'l':
		printf ("%s", op->tty);
		break;
	case 'b':
	{
		const speed_t speed = cfgetispeed(tp);
		int i;

		for (i = 0; speedtab[i].speed; i++) {
			if (speedtab[i].code == speed) {
				printf("%ld", speedtab[i].speed);
				break;
			}
		}
		break;
	}
	case 'S':
	{
		char *var = NULL, varname[64];

		if (get_escape_argument(fp, varname, sizeof(varname)))
			var = read_os_release(op, varname);
		else if (!(var = read_os_release(op, "PRETTY_NAME")))
			var = uts.sysname;
		if (var) {
			if (strcmp(varname, "ANSI_COLOR") == 0)
				printf("\033[%sm", var);
			else
				printf("%s", var);
			if (var != uts.sysname)
				free(var);
		}
		break;
	}
	case 'u':
	case 'U':
	{
		int users = 0;
		struct utmp *ut;
		setutent();
		while ((ut = getutent()))
			if (ut->ut_type == USER_PROCESS)
				users++;
		endutent();
		if (c == 'U')
			printf(P_("%d user", "%d users", users), users);
		else
			printf ("%d ", users);
		break;
	}
	case '4':
	case '6':
	{
		sa_family_t family = c == '4' ? AF_INET : AF_INET6;
		struct ifaddrs *addrs = NULL;
		char iface[128];

		if (getifaddrs(&addrs))
			break;

		if (get_escape_argument(fp, iface, sizeof(iface)))
			output_iface_ip(addrs, iface, family);
		else
			output_iface_ip(addrs, NULL, family);

		freeifaddrs(addrs);
		break;
	}
	default:
		putchar(c);
		break;
	}
}

static void init_special_char(char* arg, struct options *op)
{
	char ch, *p, *q;
	int i;

	op->initstring = malloc(strlen(arg) + 1);
	if (!op->initstring)
		log_err(_("failed to allocate memory: %m"));

	/*
	 * Copy optarg into op->initstring decoding \ddd octal
	 * codes into chars.
	 */
	q = op->initstring;
	p = arg;
	while (*p) {
		/* The \\ is converted to \ */
		if (*p == '\\') {
			p++;
			if (*p == '\\') {
				ch = '\\';
				p++;
			} else {
				/* Handle \000 - \177. */
				ch = 0;
				for (i = 1; i <= 3; i++) {
					if (*p >= '0' && *p <= '7') {
						ch <<= 3;
						ch += *p - '0';
						p++;
					} else {
						break;
					}
				}
			}
			*q++ = ch;
		} else
			*q++ = *p++;
	}
	*q = '\0';
}

/*
 * Appends @str to @dest and if @dest is not empty then use use @sep as a
 * separator. The maximal final length of the @dest is @len.
 *
 * Returns the final @dest length or -1 in case of error.
 */
static ssize_t append(char *dest, size_t len, const char  *sep, const char *src)
{
	size_t dsz = 0, ssz = 0, sz;
	char *p;

	if (!dest || !len || !src)
		return -1;

	if (*dest)
		dsz = strlen(dest);
	if (dsz && sep)
		ssz = strlen(sep);
	sz = strlen(src);

	if (dsz + ssz + sz + 1 > len)
		return -1;

	p = dest + dsz;
	if (ssz) {
		memcpy(p, sep, ssz);
		p += ssz;
	}
	memcpy(p, src, sz);
	*(p + sz) = '\0';

	return dsz + ssz + sz;
}

/*
 * Do not allow the user to pass an option as a user name
 * To be more safe: Use `--' to make sure the rest is
 * interpreted as non-options by the program, if it supports it.
 */
static void check_username(const char* nm)
{
	const char *p = nm;
	if (!nm)
		goto err;
	if (strlen(nm) > 42)
		goto err;
	while (isspace(*p))
		p++;
	if (*p == '-')
		goto err;
	return;
err:
	errno = EPERM;
	log_err(_("checkname failed: %m"));
}

/*
 * For the case plymouth is found on this system
 */
static int plymouth_command(const char* arg)
{
	const char *cmd = "/usr/bin/plymouth";
	static int has_plymouth = 1;
	pid_t pid;

	if (!has_plymouth)
		return 127;

	pid = fork();
	if (!pid) {
		int fd = open("/dev/null", O_RDWR);
		dup2(fd, 0);
		dup2(fd, 1);
		dup2(fd, 2);
		close(fd);
		execl(cmd, cmd, arg, (char *) NULL);
		exit(127);
	} else if (pid > 0) {
		int status;
		waitpid(pid, &status, 0);
		if (status == 127)
			has_plymouth = 0;
		return status;
	}
	return 1;
}
