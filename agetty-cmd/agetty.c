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
#include <utmpx.h>
#include <getopt.h>
#include <time.h>
#include <sys/socket.h>
#include <langinfo.h>
#include <grp.h>
#include <pwd.h>
#include <netdb.h>
#include <sys/utsname.h>
#include <syslog.h>

#include "agetty.h"
#include "strutils.h"
#include "all-io.h"
#include "nls.h"
#include "pathnames.h"
#include "c.h"
#include "cctype.h"
#include "widechar.h"
#include "ttyutils.h"
#include "color-names.h"
#include "env.h"
#include "path.h"
#include "fileutils.h"
#ifdef AGETTY_RELOAD
# include "netaddrq.h"
# if defined(RTMGRP_IPV4_IFADDR) && defined(RTMGRP_IPV6_IFADDR)
#  define USE_NETLINK
# endif
#endif

#include "logindefs.h"

#ifdef USE_PLYMOUTH_SUPPORT
# include "plymouth-ctrl.h"
#endif

#ifdef HAVE_SYS_PARAM_H
# include <sys/param.h>
#endif

#ifdef HAVE_GETTTYNAM
# include <ttyent.h>
#endif

#if defined(__FreeBSD_kernel__)
# include <pty.h>
# ifdef HAVE_UTMP_H
#  include <utmp.h>
# endif
# ifdef HAVE_LIBUTIL_H
#  include <libutil.h>
# endif
#endif

#ifdef USE_SYSTEMD
# include <systemd/sd-daemon.h>
# include <systemd/sd-login.h>
#endif

#ifdef __linux__
#  include <sys/kd.h>
#endif


/*
 * Things you may want to modify.
 *
 * If ISSUE_SUPPORT is not defined, agetty will never display the contents of
 * the /etc/issue file. You will not want to spit out large "issue" files at
 * the wrong baud rate. Relevant for System V only.
 *
 * You may disagree with the default line-editing etc. characters defined
 * below. Note, however, that DEL cannot be used for interrupt generation
 * and for line editing at the same time.
 */

/* Displayed before the login prompt. */
#ifdef	SYSV_STYLE
#  define ISSUE_SUPPORT
#  if defined(HAVE_SCANDIRAT) && defined(HAVE_OPENAT)
#    include "configs.h"
#    include <dirent.h>
#    define ISSUEDIR_SUPPORT
#    define ISSUEDIR_EXT	"issue"
#    define ISSUEDIR_EXTSIZ	sizeof(ISSUEDIR_EXT)
#  endif
#endif

/* Login prompt. */
#define LOGIN_PROMPT		"login: "

/* Numbers of args for login(1) */
#define LOGIN_ARGV_MAX	16

/*
 * agetty --reload
 */
#ifdef AGETTY_RELOAD
# include <sys/inotify.h>
# define AGETTY_RELOAD_FILENAME "/run/agetty.reload"	/* trigger file */
# define AGETTY_RELOAD_FDNONE	-2			/* uninitialized fd */
static int inotify_fd = AGETTY_RELOAD_FDNONE;
#endif
#ifdef USE_NETLINK
static uint32_t netlink_groups;
#endif

struct issue {
	FILE *output;
	char *mem;
	size_t mem_sz;

#ifdef USE_NETLINK
	struct ul_nl_data nl;
#endif
#ifdef AGETTY_RELOAD
	char *mem_old;
#endif
	unsigned int do_tcsetattr : 1,
		     do_tcrestore : 1;
};

enum {
	CLOCAL_MODE_AUTO = 0,
	CLOCAL_MODE_ALWAYS,
	CLOCAL_MODE_NEVER
};

#define serial_tty_option(opt, flag)	\
	(((opt)->flags & (F_VCONSOLE|(flag))) == (flag))

static void init_special_char(char* arg, struct agetty_options *op);
static void parse_args(int argc, char **argv, struct agetty_options *op);
static void parse_speeds(struct agetty_options *op, char *arg);
static void open_tty(const char *tty, struct termios *tp, struct agetty_options *op);
static void termio_init(struct agetty_options *op, struct termios *tp);
static void reset_vc(const struct agetty_options *op, struct termios *tp, int canon);
static void auto_baud(struct termios *tp);
static void output_special_char (struct issue *ie, unsigned char c, struct agetty_options *op,
		struct termios *tp, FILE *fp);
static void do_prompt(struct issue *ie, struct agetty_options *op, struct termios *tp);
static void next_speed(struct agetty_options *op, struct termios *tp);
static char *get_logname(struct issue *ie, struct agetty_options *op,
			 struct termios *tp, struct chardata *cp);
static void termio_final(struct agetty_options *op,
			 struct termios *tp, struct chardata *cp);
static int caps_lock(char *s);
static void usage(void) __attribute__((__noreturn__));
#ifdef KDGKBLED
static ssize_t append(char *dest, size_t len, const char  *sep, const char *src);
#endif
static void check_username (const char* nm);
static void login_options_to_argv(char *argv[], int *argc, char *str, char *username);
static void reload_agettys(void);
static void print_issue_file(struct issue *ie, struct agetty_options *op, struct termios *tp);
static void eval_issue_file(struct issue *ie, struct agetty_options *op, struct termios *tp);
static void show_issue(struct agetty_options *op);


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
	struct agetty_options options = {
		.flags  =  F_ISSUE,		/* show /etc/issue (SYSV_STYLE) */
		.login  =  _PATH_LOGIN,		/* default login program */
		.tty    = "tty1"		/* default tty line */
	};
	struct issue issue = {
		.mem = NULL,
#ifdef USE_NETLINK
		.nl.fd = -1
#endif
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
	{
		int i;

		dbf = fopen(DEBUG_OUTPUT, "w");
		for (i = 1; i < argc; i++) {
			if (i > 1)
				debug(" ");
			debug(argv[i]);
		}
		debug("\n");
	}
#endif				/* DEBUGGING */

	/* Load systemd credentials. */
	agetty_load_credentials(&options);

	/* Parse command-line arguments. */
	parse_args(argc, argv, &options);

	login_argv[login_argc++] = options.login;	/* set login program name */

	/* Update the utmp file. */
#ifdef	SYSV_STYLE
	agetty_update_utmp(&options, fakehost);
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

	/* Default is to follow the current line speed and then default to 9600 */
	if ((options.flags & F_VCONSOLE) == 0 && options.numspeed == 0) {
		options.speeds[options.numspeed++] = agetty_bcode("9600");
		options.flags |= F_KEEPSPEED;
	}

	/* Initialize the termios settings (raw mode, eight-bit, blocking i/o). */
	debug("calling termio_init\n");
	termio_init(&options, &termios);

	/* Write the modem init string and DO NOT flush the buffers. */
	if (options.flags & F_INITSTRING &&
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
		alarm(options.timeout);

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

	if (options.flags & F_NOPROMPT) {	/* --skip-login */
		eval_issue_file(&issue, &options, &termios);
		print_issue_file(&issue, &options, &termios);

	} else {				/* regular (auto)login */
		if ((options.flags & F_NOHOSTNAME) == 0 &&
		    getlogindefs_bool("LOGIN_PLAIN_PROMPT", 0) == 1)
			/* /etc/login.defs enables --nohostname too */
			options.flags |= F_NOHOSTNAME;

		if (options.autolog) {
			/* Autologin prompt */
			eval_issue_file(&issue, &options, &termios);
			do_prompt(&issue, &options, &termios);
			printf(_("%s%s (automatic login)\n"), LOGIN_PROMPT,
					options.autolog);
		} else {
			/* Read the login name. */
			debug("reading login name\n");
			while ((username =
				get_logname(&issue, &options, &termios, &chardata)) == NULL)
				if ((options.flags & F_VCONSOLE) == 0 && options.numspeed)
					next_speed(&options, &termios);
		}
	}

	/* Disable timer. */
	if (options.timeout)
		alarm(0);

	/* Finalize the termios settings. */
	if ((options.flags & F_VCONSOLE) == 0)
		termio_final(&options, &termios, &chardata);
	else
		reset_vc(&options, &termios, 1);

	/* Now the newline character should be properly written. */
	write_all(STDOUT_FILENO, "\r\n", 2);

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
		if (options.flags & F_REMOTE) {
			if (fakehost) {
				login_argv[login_argc++] = "-h";
				login_argv[login_argc++] = fakehost;
			} else if (options.flags & F_NOHOSTNAME)
				login_argv[login_argc++] = "-H";
		}
		if (username) {
			if (options.autolog)
				login_argv[login_argc++] = "-f";
			login_argv[login_argc++] = "--";
			login_argv[login_argc++] = username;
		}
	}

	login_argv[login_argc] = NULL;	/* last login argv */

	if (options.chroot) {
		if (chroot(options.chroot) < 0)
			agetty_log_err(_("%s: can't change root directory %s: %m"),
				options.tty, options.chroot);
		if (chdir("/") < 0)
			agetty_log_err(_("%s: can't change working directory %s: %m"),
				options.tty, "/");
	}
	if (options.chdir && chdir(options.chdir) < 0)
		agetty_log_err(_("%s: can't change working directory %s: %m"),
			options.tty, options.chdir);
	if (options.nice && nice(options.nice) < 0)
		agetty_log_warn(_("%s: can't change process priority: %m"),
			 options.tty);

#ifdef DEBUGGING
	if (close_stream(dbf) != 0)
		agetty_log_err("write failed: %s", DEBUG_OUTPUT);
#endif

	/* Let the login program take care of password validation. */
	execv(options.login, login_argv);

	free(options.osrelease);
	free(options.autolog);

	agetty_log_err(_("%s: can't exec %s: %m"), options.tty, login_argv[0]);
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

		if (memcmp(p, "\\u", 2) != 0) {
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
			agetty_log_err(_("failed to allocate memory: %m"));

		if (p != str)
			/* copy chars before \u */
			tp = mempcpy(tp, str, p - str);
		if (usz)
			/* copy username */
			tp = mempcpy(tp, username, usz);

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

static void output_version(void)
{
	static const char *const features[] = {
#ifdef DEBUGGING
		"debug",
#endif
#ifdef CRTSCTS
		"flow control",
#endif
#ifdef KDGKBLED
		"hints",
#endif
#ifdef ISSUE_SUPPORT
		"issue",
#endif
#ifdef ISSUEDIR_SUPPORT
		"issue.d",
#endif
#ifdef KDGKBMODE
		"keyboard mode",
#endif
#ifdef USE_PLYMOUTH_SUPPORT
		"plymouth",
#endif
#ifdef AGETTY_RELOAD
		"reload",
#endif
#ifdef USE_SYSTEMD
		"systemd",
#endif
#ifdef HAVE_WIDECHAR
		"widechar",
#endif
		NULL
	};
	unsigned int i;

	printf( _("%s from %s"), program_invocation_short_name, PACKAGE_STRING);
	fputs(" (", stdout);
	for (i = 0; features[i]; i++) {
		if (0 < i)
			fputs(", ", stdout);
		printf("%s", features[i]);
	}
	fputs(")\n", stdout);
}

#define is_speed(str) (strlen((str)) == strspn((str), "0123456789,"))

/* Parse command-line arguments. */
static void parse_args(int argc, char **argv, struct agetty_options *op)
{
	int c;
	int opt_show_issue = 0;

	enum {
		VERSION_OPTION = CHAR_MAX + 1,
		NOHINTS_OPTION,
		NOHOSTNAME_OPTION,
		LONGHOSTNAME_OPTION,
		HELP_OPTION,
		ERASE_CHARS_OPTION,
		KILL_CHARS_OPTION,
		RELOAD_OPTION,
		LIST_SPEEDS_OPTION,
		ISSUE_SHOW_OPTION,
	};
	const struct option longopts[] = {
		{  "8bits",	     no_argument,	 NULL,  '8'  },
		{  "autologin",	     required_argument,	 NULL,  'a'  },
		{  "noreset",	     no_argument,	 NULL,  'c'  },
		{  "chdir",	     required_argument,	 NULL,  'C'  },
		{  "delay",	     required_argument,	 NULL,  'd'  },
		{  "remote",         no_argument,        NULL,  'E'  },
		{  "issue-file",     required_argument,  NULL,  'f'  },
		{  "show-issue",     no_argument,        NULL,  ISSUE_SHOW_OPTION },
		{  "flow-control",   no_argument,	 NULL,  'h'  },
		{  "host",	     required_argument,  NULL,  'H'  },
		{  "noissue",	     no_argument,	 NULL,  'i'  },
		{  "init-string",    required_argument,  NULL,  'I'  },
		{  "noclear",	     no_argument,	 NULL,  'J'  },
		{  "login-program",  required_argument,  NULL,  'l'  },
		{  "local-line",     optional_argument,	 NULL,  'L'  },
		{  "extract-baud",   no_argument,	 NULL,  'm'  },
		{  "list-speeds",    no_argument,	 NULL,	LIST_SPEEDS_OPTION },
		{  "skip-login",     no_argument,	 NULL,  'n'  },
		{  "nonewline",	     no_argument,	 NULL,  'N'  },
		{  "login-options",  required_argument,  NULL,  'o'  },
		{  "login-pause",    no_argument,        NULL,  'p'  },
		{  "nice",	     required_argument,  NULL,  'P'  },
		{  "chroot",	     required_argument,	 NULL,  'r'  },
		{  "hangup",	     no_argument,	 NULL,  'R'  },
		{  "keep-baud",      no_argument,	 NULL,  's'  },
		{  "timeout",	     required_argument,  NULL,  't'  },
		{  "detect-case",    no_argument,	 NULL,  'U'  },
		{  "wait-cr",	     no_argument,	 NULL,  'w'  },
		{  "nohints",        no_argument,        NULL,  NOHINTS_OPTION },
		{  "nohostname",     no_argument,	 NULL,  NOHOSTNAME_OPTION },
		{  "long-hostname",  no_argument,	 NULL,  LONGHOSTNAME_OPTION },
		{  "reload",         no_argument,        NULL,  RELOAD_OPTION },
		{  "version",	     no_argument,	 NULL,  VERSION_OPTION  },
		{  "help",	     no_argument,	 NULL,  HELP_OPTION     },
		{  "erase-chars",    required_argument,  NULL,  ERASE_CHARS_OPTION },
		{  "kill-chars",     required_argument,  NULL,  KILL_CHARS_OPTION },
		{ NULL, 0, NULL, 0 }
	};

	while ((c = getopt_long(argc, argv,
			   "8a:cC:d:Ef:hH:iI:Jl:L::mnNo:pP:r:Rst:Uw", longopts,
			    NULL)) != -1) {
		switch (c) {
		case '8':
			op->flags |= F_EIGHTBITS;
			break;
		case 'a':
			free(op->autolog);
			op->autolog = strdup(optarg);
			if (!op->autolog)
				agetty_log_err(_("failed to allocate memory: %m"));
			break;
		case 'c':
			op->flags |= F_KEEPCFLAGS;
			break;
		case 'C':
			op->chdir = optarg;
			break;
		case 'd':
			op->delay = strtou32_or_err(optarg,  _("invalid delay argument"));
			break;
		case 'E':
			op->flags |= F_REMOTE;
			break;
		case 'f':
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
					agetty_log_err(_("invalid argument of --local-line"));
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
			op->nice = strtos32_or_err(optarg,  _("invalid nice argument"));
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
			op->timeout = strtou32_or_err(optarg,  _("invalid timeout"));
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
		case RELOAD_OPTION:
			reload_agettys();
			exit(EXIT_SUCCESS);
		case LIST_SPEEDS_OPTION:
			agetty_list_speeds();
			exit(EXIT_SUCCESS);
		case ISSUE_SHOW_OPTION:
			opt_show_issue = 1;
			break;
		case VERSION_OPTION:
			output_version();
			exit(EXIT_SUCCESS);
		case HELP_OPTION:
			usage();
		default:
			errtryhelp(EXIT_FAILURE);
		}
	}

	if (opt_show_issue) {
		show_issue(op);
		exit(EXIT_SUCCESS);
	}

	debug("after getopt loop\n");

	if (argc < optind + 1) {
		agetty_log_warn(_("not enough arguments"));
		errx(EXIT_FAILURE, _("not enough arguments"));
	}

	/* Accept "tty", "baudrate tty", and "tty baudrate". */
	if (is_speed(argv[optind])) {
		/* Assume BSD style speed. */
		parse_speeds(op, argv[optind++]);
		if (argc < optind + 1) {
			agetty_log_warn(_("not enough arguments"));
			errx(EXIT_FAILURE, _("not enough arguments"));
		}
		op->tty = argv[optind++];
	} else {
		op->tty = argv[optind++];
		if (argc > optind) {
			char *v = argv[optind];
			if (is_speed(v)) {
				parse_speeds(op, v);
				optind++;
			}
		}
	}

	/* resolve the tty path in case it was provided as stdin */
	if (strcmp(op->tty, "-") == 0) {
		int fd;
		const char *name = op->tty;

		op->tty_is_stdin = 1;
		fd = get_terminal_name(NULL, &name, NULL);
		if (fd >= 0)
			op->tty = name;	/* set real device name */
		else
			agetty_log_warn(_("could not get terminal name: %d"), fd);
	}

	/* On virtual console remember the line which is used for */
	if (strncmp(op->tty, "tty", 3) == 0 &&
	    strspn(op->tty + 3, "0123456789") == strlen(op->tty+3))
		op->vcline = op->tty+3;

	if (argc > optind && argv[optind])
		op->term = argv[optind];

	debug("exiting parseargs\n");
}

/* Parse alternate baud rates. */
static void parse_speeds(struct agetty_options *op, char *arg)
{
	char *cp;
	char *str = strdup(arg);

	if (!str)
		agetty_log_err(_("failed to allocate memory: %m"));

	debug("entered parse_speeds:\n");
	for (cp = strtok(str, ","); cp != NULL; cp = strtok((char *)0, ",")) {
		if ((op->speeds[op->numspeed++] = agetty_bcode(cp)) <= 0)
			agetty_log_err(_("bad speed: %s"), cp);
		if (op->numspeed >= MAX_SPEED)
			agetty_log_err(_("too many alternate speeds"));
	}
	debug("exiting parsespeeds\n");
	free(str);
}


/* Set up tty as stdin, stdout & stderr. */
static void open_tty(const char *tty, struct termios *tp, struct agetty_options *op)
{
	const pid_t pid = getpid();
	int closed = 0;
#ifndef KDGKBMODE
	int serial;
#endif

	/* Set up new standard input, unless we are given an already opened port. */

	if (!op->tty_is_stdin) {
		char buf[PATH_MAX+1];
		struct group *gr = NULL;
		struct stat st;
		int fd, len;
		pid_t tid;
		gid_t gid = 0;

		/* Use tty group if available */
		if ((gr = getgrnam("tty")))
			gid = gr->gr_gid;

		len = snprintf(buf, sizeof(buf), "/dev/%s", tty);
		if (len < 0 || (size_t)len >= sizeof(buf))
			agetty_log_err(_("/dev/%s: cannot open as standard input: %m"), tty);

		/* Open the tty as standard input. */
		if ((fd = open(buf, O_RDWR|O_NOCTTY|O_NONBLOCK, 0)) < 0)
			agetty_log_err(_("/dev/%s: cannot open as standard input: %m"), tty);

		/*
		 * There is always a race between this reset and the call to
		 * vhangup() that s.o. can use to get access to your tty.
		 * Linux login(1) will change tty permissions. Use root owner and group
		 * with permission -rw------- for the period between getty and login.
		 */
		if (fchown(fd, 0, gid) || fchmod(fd, (gid ? 0620 : 0600))) {
			if (errno == EROFS)
				agetty_log_warn("%s: %m", buf);
			else
				agetty_log_err("%s: %m", buf);
		}

		/* Sanity checks... */
		if (fstat(fd, &st) < 0)
			agetty_log_err("%s: %m", buf);
		if ((st.st_mode & S_IFMT) != S_IFCHR)
			agetty_log_err(_("/dev/%s: not a character device"), tty);
		if (!isatty(fd))
			agetty_log_err(_("/dev/%s: not a tty"), tty);

		if (((tid = tcgetsid(fd)) < 0) || (pid != tid)) {
			if (ioctl(fd, TIOCSCTTY, 1) == -1)
				agetty_log_warn(_("/dev/%s: cannot get controlling tty: %m"), tty);
		}

		close(STDIN_FILENO);
		errno = 0;

		if (op->flags & F_HANGUP) {

			if (ioctl(fd, TIOCNOTTY))
				debug("TIOCNOTTY ioctl failed\n");

			/*
			 * Let's close all file descriptors before vhangup
			 * https://lkml.org/lkml/2012/6/5/145
			 */
			close(fd);
			close(STDOUT_FILENO);
			close(STDERR_FILENO);
			errno = 0;
			closed = 1;

			if (vhangup())
				agetty_log_err(_("/dev/%s: vhangup() failed: %m"), tty);
		} else
			close(fd);

		debug("open(2)\n");
		if (open(buf, O_RDWR|O_NOCTTY|O_NONBLOCK, 0) != 0)
			agetty_log_err(_("/dev/%s: cannot open as standard input: %m"), tty);

		if (((tid = tcgetsid(STDIN_FILENO)) < 0) || (pid != tid)) {
			if (ioctl(STDIN_FILENO, TIOCSCTTY, 1) == -1)
				agetty_log_warn(_("/dev/%s: cannot get controlling tty: %m"), tty);
		}

	} else {

		/*
		 * Standard input should already be connected to an open port. Make
		 * sure it is open for read/write.
		 */

		if ((fcntl(STDIN_FILENO, F_GETFL, 0) & O_RDWR) != O_RDWR)
			agetty_log_err(_("%s: not open for read/write"), tty);

	}

	if (tcsetpgrp(STDIN_FILENO, pid))
		agetty_log_warn(_("/dev/%s: cannot set process group: %m"), tty);

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
		agetty_log_err(_("%s: dup problem: %m"), tty);

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
		agetty_log_err(_("%s: failed to get terminal attributes: %m"), tty);

#if defined(__FreeBSD_kernel__)
	login_tty (0);
#endif

	/*
	 * Detect if this is a virtual console or serial/modem line.
	 * In case of a virtual console the ioctl KDGKBMODE succeeds
	 * whereas on other lines it will fails.
	 */
#ifdef KDGKBMODE
	if (ioctl(STDIN_FILENO, KDGKBMODE, &op->kbmode) == 0)
#else
	if (ioctl(STDIN_FILENO, TIOCMGET, &serial) < 0 && (errno == EINVAL))
#endif
	{
		op->flags |= F_VCONSOLE;
	} else {
#ifdef K_RAW
		op->kbmode = K_RAW;
#endif
	}

	if (!op->term)
		op->term = get_terminal_default_type(op->tty, !(op->flags & F_VCONSOLE));
	if (!op->term)
		agetty_log_err(_("failed to allocate memory: %m"));

	if (setenv("TERM", op->term, 1) != 0)
		agetty_log_err(_("failed to set the %s environment variable"), "TERM");
}

/* Initialize termios settings. */
static void termio_clear(int fd)
{
	/*
	 * Do not write a full reset (ESC c) because this destroys
	 * the unicode mode again if the terminal was in unicode
	 * mode.  Also it clears the CONSOLE_MAGIC features which
	 * are required for some languages/console-fonts.
	 * Just put the cursor to the home position (ESC [ H),
	 * erase everything below the cursor (ESC [ J), and set the
	 * scrolling region to the full window (ESC [ r)
	 */
	write_all(fd, "\033[r\033[H\033[J", 9);
}

/* Initialize termios settings. */
static void termio_init(struct agetty_options *op, struct termios *tp)
{
	speed_t ispeed, ospeed;
	struct winsize ws;
#ifdef USE_PLYMOUTH_SUPPORT
	struct termios lock;
	int i =  (plymouth_command(MAGIC_PING) == 0) ? PLYMOUTH_TERMIOS_FLAGS_DELAY : 0;
	if (i)
		plymouth_command(MAGIC_QUIT);
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
		sleep(1);
	}
	memset(&lock, 0, sizeof(struct termios));
	ioctl(STDIN_FILENO, TIOCSLCKTRMIOS, &lock);
#endif

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
		reset_vc(op, tp, 0);

		if ((tp->c_cflag & (CS8|PARODD|PARENB)) == CS8)
			op->flags |= F_EIGHTBITS;

		if ((op->flags & F_NOCLEAR) == 0)
			termio_clear(STDOUT_FILENO);
		return;
	}

	/*
	 * Serial line
	 */

	if (op->flags & F_KEEPSPEED || !op->numspeed) {
		/* Save the original setting. */
		ispeed = cfgetispeed(tp);
		ospeed = cfgetospeed(tp);

		/* Save also the original speed to array of the speeds to make
		 * it possible to return the original after unexpected BREAKs.
		 */
		if (op->numspeed)
			op->speeds[op->numspeed++] = ispeed ? ispeed :
						     ospeed ? ospeed :
						     TTYDEF_SPEED;
		if (!ispeed)
			ispeed = TTYDEF_SPEED;
		if (!ospeed)
			ospeed = TTYDEF_SPEED;
	} else {
		ospeed = ispeed = op->speeds[FIRST_SPEED];
	}

	/*
	 * Initial termios settings: 8-bit characters, raw-mode, blocking i/o.
	 * Special characters are set after we have read the login name; all
	 * reads will be done in raw mode anyway. Errors will be dealt with
	 * later on.
	 */

	/* The default is set c_iflag in termio_final() according to chardata.
	 * Unfortunately, the chardata are not set according to the serial line
	 * if --autolog is enabled. In this case we do not read from the line
	 * at all. The best what we can do in this case is to keep c_iflag
	 * unmodified for --autolog.
	 */
	if (!op->autolog) {
#ifdef IUTF8
		tp->c_iflag = tp->c_iflag & IUTF8;
		if (tp->c_iflag & IUTF8)
			op->flags |= F_UTF8;
#else
		tp->c_iflag = 0;
#endif
	}

	tp->c_lflag = 0;
	tp->c_oflag &= OPOST | ONLCR;

	if ((op->flags & F_KEEPCFLAGS) == 0)
		tp->c_cflag = CS8 | HUPCL | CREAD | (tp->c_cflag & CLOCAL);

	/*
	 * Note that the speed is stored in the c_cflag termios field, so we have
	 * set the speed always when the cflag is reset.
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
		if (ws.ws_row == 0)
			ws.ws_row = 24;
		if (ws.ws_col == 0)
			ws.ws_col = 80;
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
		agetty_log_warn(_("setting terminal attributes failed: %m"));

	/* Go to blocking input even in local mode. */
	fcntl(STDIN_FILENO, F_SETFL,
	      fcntl(STDIN_FILENO, F_GETFL, 0) & ~O_NONBLOCK);

	debug("term_io 2\n");
}

/* Reset virtual console on stdin to its defaults */
static void reset_vc(const struct agetty_options *op, struct termios *tp, int canon)
{
	int fl = 0;

	fl |= (op->flags & F_KEEPCFLAGS) == 0 ? 0 : UL_TTY_KEEPCFLAGS;
	fl |= (op->flags & F_UTF8)       == 0 ? 0 : UL_TTY_UTF8;

	reset_virtual_console(tp, fl);

#ifdef AGETTY_RELOAD
	/*
	 * Discard all the flags that makes the line go canonical with echoing.
	 * We need to know when the user starts typing.
	 */
	if (canon == 0)
		tp->c_lflag = 0;
#endif

	if (tcsetattr(STDIN_FILENO, TCSADRAIN, tp))
		agetty_log_warn(_("setting terminal attributes failed: %m"));

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
			if (c_isascii(*bp) && isdigit(*bp)) {
				if ((speed = agetty_bcode(bp))) {
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



static char *read_os_release(struct agetty_options *op, const char *varname)
{
	int fd = -1;
	struct stat st;
	size_t varsz = strlen(varname);
	char *p, *buf = NULL, *ret = NULL;

	/* read the file only once */
	if (!op->osrelease) {
		fd = open(_PATH_OS_RELEASE_ETC, O_RDONLY);
		if (fd == -1) {
			fd = open(_PATH_OS_RELEASE_USR, O_RDONLY);
			if (fd == -1) {
				agetty_log_warn(_("cannot open os-release file"));
				return NULL;
			}
		}

		if (fstat(fd, &st) < 0 || st.st_size > 4 * 1024 * 1024)
			goto done;

		op->osrelease = malloc(st.st_size + 1);
		if (!op->osrelease)
			agetty_log_err(_("failed to allocate memory: %m"));
		if (read_all(fd, op->osrelease, st.st_size) != (ssize_t) st.st_size) {
			free(op->osrelease);
			op->osrelease = NULL;
			goto done;
		}
		op->osrelease[st.st_size] = 0;
	}
	buf = strdup(op->osrelease);
	if (!buf)
		agetty_log_err(_("failed to allocate memory: %m"));
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
		p += strspn(p, " \t\n\r");

		if (*p != '=')
			continue;

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
			agetty_log_err(_("failed to allocate memory: %m"));
		p = eol + 1;
	}
done:
	free(buf);
	if (fd >= 0)
		close(fd);
	return ret;
}

#ifdef AGETTY_RELOAD
static int wait_for_term_input(struct issue *ie, int fd)
{
	char buffer[sizeof(struct inotify_event) + NAME_MAX + 1];
	fd_set rfds;

	if (inotify_fd == AGETTY_RELOAD_FDNONE) {
		/* make sure the reload trigger file exists */
		int reload_fd = open(AGETTY_RELOAD_FILENAME,
					O_CREAT|O_CLOEXEC|O_RDONLY,
					S_IRUSR|S_IWUSR);

		/* initialize reload trigger inotify stuff */
		if (reload_fd >= 0) {
			inotify_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
			if (inotify_fd > 0)
				inotify_add_watch(inotify_fd, AGETTY_RELOAD_FILENAME,
					  IN_ATTRIB | IN_MODIFY);

			close(reload_fd);
		} else
			agetty_log_warn(_("failed to create reload file: %s: %m"),
					AGETTY_RELOAD_FILENAME);
	}

	while (1) {
		int nfds = fd;

		FD_ZERO(&rfds);
		FD_SET(fd, &rfds);

		if (inotify_fd >= 0) {
			FD_SET(inotify_fd, &rfds);
			nfds = max(nfds, inotify_fd);
		}

#ifdef USE_NETLINK
		if (ie->nl.fd >= 0) {
			FD_SET(ie->nl.fd, &rfds);
			nfds = max(nfds, ie->nl.fd);
		}
#endif
		/* If waiting fails, just fall through, presumably reading input will fail */
		if (select(nfds + 1, &rfds, NULL, NULL, NULL) < 0)
			return 1;

		if (FD_ISSET(fd, &rfds)) {
			return 1;

		}

#ifdef USE_NETLINK
		if (ie->nl.fd >= 0 && FD_ISSET(ie->nl.fd, &rfds)) {
			int rc;

			/* We are looping until it returns UL_NL_WOULDBLOCK.
			 * To prevent infinite loop, we are leaving on any other
			 * error except UL_NL_SOFT_ERROR. To prevent unability
			 * of further processing, we never exit. */
			do {
				rc = ul_nl_process(&(ie->nl), UL_NL_ASYNC,
						   UL_NL_ONESHOT);
			}
			while (!rc || rc == UL_NL_SOFT_ERROR);

		/* Just drain the inotify buffer */
		} else
#endif /* USE_NETLINK */
		if (inotify_fd >= 0 && FD_ISSET(inotify_fd, &rfds)) {
			while (read(inotify_fd, buffer, sizeof (buffer)) > 0);
		}

		return 0;
	}
}
#endif  /* AGETTY_RELOAD */

#ifdef ISSUEDIR_SUPPORT
static int issuedir_filter(const struct dirent *d)
{
	size_t namesz;

#ifdef _DIRENT_HAVE_D_TYPE
	if (d->d_type != DT_UNKNOWN && d->d_type != DT_REG &&
	    d->d_type != DT_LNK)
		return 0;
#endif
	if (*d->d_name == '.')
		return 0;

	namesz = strlen(d->d_name);
	if (!namesz || namesz < ISSUEDIR_EXTSIZ + 1 ||
	    strcmp(d->d_name + (namesz - ISSUEDIR_EXTSIZ), "." ISSUEDIR_EXT) != 0)
		return 0;

	/* Accept this */
	return 1;
}


static int issuefile_read_stream(struct issue *ie, FILE *f, struct agetty_options *op, struct termios *tp);

/* returns: 0 on success, 1 cannot open, <0 on error
 */
static int issuedir_read(struct issue *ie, const char *dirname,
			 struct agetty_options *op, struct termios *tp)
{
	int dd, nfiles, i;
	struct dirent **namelist = NULL;

	dd = open(dirname, O_RDONLY|O_CLOEXEC|O_DIRECTORY);
	if (dd < 0)
		return 1;

	nfiles = scandirat(dd, ".", &namelist, issuedir_filter, versionsort);
	if (nfiles <= 0)
		goto done;

	ie->do_tcsetattr = 1;

	for (i = 0; i < nfiles; i++) {
		struct dirent *d = namelist[i];
		FILE *f;

		f = fopen_at(dd, d->d_name, O_RDONLY|O_CLOEXEC, "r" UL_CLOEXECSTR);
		if (f) {
			issuefile_read_stream(ie, f, op, tp);
			fclose(f);
		}
	}

	for (i = 0; i < nfiles; i++)
		free(namelist[i]);
	free(namelist);
done:
	close(dd);
	return 0;
}

#else /* !ISSUEDIR_SUPPORT */
static int issuedir_read(struct issue *ie __attribute__((__unused__)),
			const char *dirname __attribute__((__unused__)),
			struct agetty_options *op __attribute__((__unused__)),
			struct termios *tp __attribute__((__unused__)))
{
	return 1;
}
#endif /* ISSUEDIR_SUPPORT */

#ifndef ISSUE_SUPPORT
static void print_issue_file(struct issue *ie __attribute__((__unused__)),
			     struct agetty_options *op,
			     struct termios *tp __attribute__((__unused__)))
{
	if ((op->flags & F_NONL) == 0) {
		/* Issue not in use, start with a new line. */
		write_all(STDOUT_FILENO, "\r\n", 2);
	}
}

static void eval_issue_file(struct issue *ie __attribute__((__unused__)),
			    struct agetty_options *op __attribute__((__unused__)),
			    struct termios *tp __attribute__((__unused__)))
{
}

static void show_issue(struct agetty_options *op __attribute__((__unused__)))
{
}

#else /* ISSUE_SUPPORT */

static int issuefile_read_stream(
		struct issue *ie, FILE *f,
		struct agetty_options *op, struct termios *tp)
{
	struct stat st;
	int c;

	if (fstat(fileno(f), &st) || !S_ISREG(st.st_mode))
		return 1;

	if (!ie->output) {
		free(ie->mem);
		ie->mem_sz = 0;
		ie->mem = NULL;
		ie->output = open_memstream(&ie->mem, &ie->mem_sz);
	}

	while ((c = fgetc(f)) != EOF) {
		if (c == '\\')
			output_special_char(ie, fgetc(f), op, tp, f);
		else
			putc(c, ie->output);
	}

	return 0;
}

static int issuefile_read(
		struct issue *ie, const char *filename,
		struct agetty_options *op, struct termios *tp)
{
	FILE *f = fopen(filename, "r" UL_CLOEXECSTR);
	int rc = 1;

	if (f) {
		rc = issuefile_read_stream(ie, f, op, tp);
		fclose(f);
	}
	return rc;
}


#ifdef AGETTY_RELOAD
static int issue_is_changed(struct issue *ie)
{
	if (ie->mem_old && ie->mem
	    && strcmp(ie->mem_old, ie->mem) == 0) {
		free(ie->mem_old);
		ie->mem_old = ie->mem;
		ie->mem = NULL;
		ie->mem_sz = 0;
		return 0;
	}

	return 1;
}
#endif

static void print_issue_file(struct issue *ie,
			     struct agetty_options *op,
			     struct termios *tp)
{
	int oflag = tp->c_oflag;	    /* Save current setting. */

	if ((op->flags & F_NONL) == 0) {
		/* Issue not in use, start with a new line. */
		write_all(STDOUT_FILENO, "\r\n", 2);
	}

	if (ie->do_tcsetattr) {
		if ((op->flags & F_VCONSOLE) == 0) {
			/* Map new line in output to carriage return & new line. */
			tp->c_oflag |= (ONLCR | OPOST);
			tcsetattr(STDIN_FILENO, TCSADRAIN, tp);
		}
	}

	if (ie->mem_sz && ie->mem)
		write_all(STDOUT_FILENO, ie->mem, ie->mem_sz);

	if (ie->do_tcrestore) {
		/* Restore settings. */
		tp->c_oflag = oflag;
		/* Wait till output is gone. */
		tcsetattr(STDIN_FILENO, TCSADRAIN, tp);
	}

#ifdef AGETTY_RELOAD
	free(ie->mem_old);
	ie->mem_old = ie->mem;
	ie->mem = NULL;
	ie->mem_sz = 0;
#else
	free(ie->mem);
	ie->mem = NULL;
	ie->mem_sz = 0;
#endif
}

static void eval_issue_file(struct issue *ie,
			    struct agetty_options *op,
			    struct termios *tp)
{
	if (!(op->flags & F_ISSUE))
		goto done;

#ifdef USE_NETLINK
/* TODO:
 * Two pass processing for eval_issue_file()
 * Implement pass 1: Just evaluate list of netlink_groups (IP protocols) and
 * interfaces to monitor.
 * That is why again label is here: netlink_groups will be re-evaluated and
 * dump will be performed again.
 */
	/* netlink_groups = 0; */
	netlink_groups = RTMGRP_IPV4_IFADDR | RTMGRP_IPV6_IFADDR;

	/* Already initialized? */
	if (ie->nl.fd >= 0)
		goto skip;
	/* Prepare netlink. */
	ul_nl_init(&(ie->nl));
	if ((ul_netaddrq_init(&(ie->nl), NULL, NULL, (void *)ie)))
		goto skip;

	/* Open netlink and create address list. */
	if (ul_nl_open(&(ie->nl),
		       RTMGRP_LINK | netlink_groups))
		goto skip;
	if (ul_nl_request_dump(&(ie->nl), RTM_GETADDR))
		goto error;
	if (ul_nl_process(&(ie->nl), UL_NL_SYNC, UL_NL_LOOP) != UL_NL_DONE)
		goto error;
	goto skip;
error:
	/* In case of any error, the addrq list is just empty, and we can use
	 * the code without any error checking. */
	ul_nl_close(&(ie->nl));
	ie->nl.fd = -1;
skip:
#endif /* USE_NETLINK */
	/*
	 * The custom issue file or directory list specified by:
	 *   agetty --issue-file <path[:path]...>
	 * Note that nothing is printed if the file/dir does not exist.
	 */
	if (op->issue) {
		char *list = strdup(op->issue);
		char *file;

		if (!list)
			agetty_log_err(_("failed to allocate memory: %m"));

		for (file = strtok(list, ":"); file; file = strtok(NULL, ":")) {
			struct stat st;

			if (stat(file, &st) < 0)
				continue;
			if (S_ISDIR(st.st_mode))
				issuedir_read(ie, file, op, tp);
			else
				issuefile_read(ie, file, op, tp);
		}
		free(list);
		goto done;
	}

#ifdef ISSUEDIR_SUPPORT
	struct list_head file_list;
	struct list_head *current = NULL;
	char *name = NULL;

	/* Reading all issue files and concatenating all contents to one content.
	 * The ordering rules are defineded in:
	 * https://github.com/uapi-group/specifications/blob/main/specs/configuration_files_specification.md
	 *
	 * Note that _PATH_RUNSTATEDIR (/run) is always read by ul_configs_file_list().
	 */
	ul_configs_file_list(&file_list,
			     NULL,
			     _PATH_SYSCONFDIR,
			     _PATH_RUNSTATEDIR,
			     _PATH_SYSCONFSTATICDIR,
			     "issue",
			     ISSUEDIR_EXT);

	while (ul_configs_next_filename(&file_list, &current, &name) == 0) {
		issuefile_read(ie, name, op, tp);
	}

	ul_configs_free_list(&file_list);
#endif

done:
	if (ie->output) {
		fclose(ie->output);
		ie->output = NULL;
	}
}

/* This is --show-issue backend, executed by normal user on the current
 * terminal.
 */
static void show_issue(struct agetty_options *op)
{
	struct issue ie = {
		.output = NULL,
#ifdef USE_NETLINK
		.nl.fd = -1
#endif
	};
	struct termios tp;

	memset(&tp, 0, sizeof(struct termios));
	if (tcgetattr(STDIN_FILENO, &tp) < 0)
		err(EXIT_FAILURE, _("failed to get terminal attributes: %m"));

	eval_issue_file(&ie, op, &tp);

	if (ie.mem_sz)
		write_all(STDOUT_FILENO, ie.mem, ie.mem_sz);
	if (ie.output)
		fclose(ie.output);
	free(ie.mem);
}

#endif /* ISSUE_SUPPORT */

/* Show login prompt, optionally preceded by /etc/issue contents. */
static void do_prompt(struct issue *ie, struct agetty_options *op, struct termios *tp)
{
#ifdef AGETTY_RELOAD
again:
#endif
	print_issue_file(ie, op, tp);

	if (op->flags & F_LOGINPAUSE) {
		puts(_("[press ENTER to login]"));
#ifdef AGETTY_RELOAD
		/* reload issue */
		if (!wait_for_term_input(ie, STDIN_FILENO)) {
			eval_issue_file(ie, op, tp);
			if (issue_is_changed(ie)) {
				if ((op->flags & F_VCONSOLE)
				    && (op->flags & F_NOCLEAR) == 0)
					termio_clear(STDOUT_FILENO);
				{
					/* TODO: Close to set netlink_groups again using pass 1 */
					/* if (ie->nl.fd >= 0) ul_nl_close(&(ie->nl));
					 * ie->nl.fd = -1; */

					goto again;
				}
			}
		}
#endif
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
		char *hn = agetty_xgethostname();

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
		write_all(STDOUT_FILENO, LOGIN_PROMPT,
				sizeof(LOGIN_PROMPT) - 1);
	}
}

/* Select next baud rate. */
static void next_speed(struct agetty_options *op, struct termios *tp)
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

/* Erase visual characters for one stored character */
static void erase_char(int visual_count, struct chardata *cp)
{
	static const char *const erase[] = {	/* backspace-space-backspace */
		"\010\040\010",		/* space parity */
		"\010\040\010",		/* odd parity */
		"\210\240\210",		/* even parity */
		"\210\240\210",		/* no parity */
	};
	int i;
	for (i = 0; i < visual_count; i++)
		write_all(1, erase[cp->parity], 3);
}

/* Get user name, establish parity, speed, erase, kill & eol. */
static char *get_logname(struct issue *ie, struct agetty_options *op, struct termios *tp, struct chardata *cp)
{
	static char logname[BUFSIZ];
	static int visual_widths[BUFSIZ];	/* visual char count for each stored byte */
	char *bp;
	int *visual_bp;
	char c;			/* input character, full eight bits */
	char ascval;		/* low 7 bits of input character */
	int eightbit;

	/* Initialize kill, erase, parity etc. (also after switching speeds). */
	INIT_CHARDATA(cp);

	/*
	 * Flush pending input (especially important after parsing or switching
	 * the baud rate).
	 */
	if ((op->flags & F_VCONSOLE) == 0)
		sleep(1);
	tcflush(STDIN_FILENO, TCIFLUSH);

	eightbit = (op->flags & (F_EIGHTBITS|F_UTF8));

	/* Initialize buffer pointers. visual_widths tracks how many visual
	 * characters each stored byte represents (e.g. ESC = 2 for "^[", tab = 1-8 spaces) */
	bp = logname;
	visual_bp = visual_widths;
	*bp = '\0';

	eval_issue_file(ie, op, tp);
	while (*logname == '\0') {
		/* Write issue file and prompt */
		do_prompt(ie, op, tp);

	no_reload:
#ifdef AGETTY_RELOAD
		if (!wait_for_term_input(ie, STDIN_FILENO)) {
			/* refresh prompt -- discard input data, clear terminal
			 * and call do_prompt() again
			 */
			if ((op->flags & F_VCONSOLE) == 0)
				sleep(1);
			eval_issue_file(ie, op, tp);
			if (!issue_is_changed(ie))
				goto no_reload;
			/* if (ie->nl.fd >= 0) ul_nl_close(&(ie->nl));
			 * ie->nl.fd = -1; */
			tcflush(STDIN_FILENO, TCIFLUSH);
			if ((op->flags & F_VCONSOLE)
			    && (op->flags & F_NOCLEAR) == 0)
				termio_clear(STDOUT_FILENO);
			bp = logname;
			visual_bp = visual_widths;
			*bp = '\0';
			continue;
		}
#endif
		cp->eol = '\0';

		/* Read name, watch for break and end-of-line. */
		while (cp->eol == '\0') {

			char key;
			ssize_t readres;

			debug("read from FD\n");
			readres = read(STDIN_FILENO, &c, 1);
			if (readres < 0) {
				debug("read failed\n");

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
					agetty_exit_slowly(EXIT_SUCCESS);
				default:
					agetty_log_err(_("%s: read: %m"), op->tty);
				}
			}

			if (readres == 0)
				c = 0;

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
				if (op->numspeed > 1 && !(op->flags & F_VCONSOLE))
					return NULL;
				if (readres == 0)
					agetty_exit_slowly(EXIT_SUCCESS);
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
						erase_char(*(visual_bp - 1), cp);
					bp--;
					visual_bp--;
				}
				break;
			case CTL('U'):
				cp->kill = ascval;		/* set kill character */
				FALLTHROUGH;
			case CTL('C'):
				if (key == CTL('C') && !(op->flags & F_VCONSOLE))
					/* Ignore CTRL+C on serial line */
					break;
				while (bp > logname) {
					if ((tp->c_lflag & ECHO) == 0)
						erase_char(*(visual_bp - 1), cp);
					bp--;
					visual_bp--;
				}
				break;
			case CTL('D'):
				exit(EXIT_SUCCESS);
			default:
				if ((size_t)(bp - logname) >= sizeof(logname) - 1)
					agetty_log_err(_("%s: input overrun"), op->tty);
				if ((tp->c_lflag & ECHO) == 0) {
					/* Visualize escape sequence instead of its execution */
					if (ascval == CTL('[')) {
						/* Ideally it should be "\xe2\x90\x9b"
						 * if (op->flags & (F_UTF8)),
						 * but only some fonts contain it */
						write_all(1, "^[", 2);
						*visual_bp = 2;		/* ESC shows as ^[ (2 chars) */
					} else if (ascval == '\t') {
						/* Tab expands to spaces */
						int pos = bp - logname;
						int spaces = 8 - (pos % 8);
						int i;
						for (i = 0; i < spaces; i++)
							write_all(1, " ", 1);
						*visual_bp = spaces;
					} else {
						write_all(1, &c, 1);	/* echo the character */
						*visual_bp = 1;		/* normal char shows as 1 */
					}
				} else {
					*visual_bp = 1;		/* when echo is on, assume 1 char */
				}
				*bp++ = ascval;			/* and store it */
				visual_bp++;
				break;
			}
			/* Everything was erased. */
			if (bp == logname && cp->eol == '\0')
				goto no_reload;
		}
	}

#ifdef HAVE_WIDECHAR
	if ((op->flags & (F_EIGHTBITS|F_UTF8)) == (F_EIGHTBITS|F_UTF8)) {
		/* Check out UTF-8 multibyte characters */
		ssize_t len;
		wchar_t *wcs, *wcp;

		len = mbstowcs((wchar_t *)0, logname, 0);
		if (len < 0)
			agetty_log_err(_("%s: invalid character conversion for login name"), op->tty);

		wcs = malloc((len + 1) * sizeof(wchar_t));
		if (!wcs)
			agetty_log_err(_("failed to allocate memory: %m"));

		len = mbstowcs(wcs, logname, len + 1);
		if (len < 0)
			agetty_log_err(_("%s: invalid character conversion for login name"), op->tty);

		wcp = wcs;
		while (*wcp) {
			const wint_t wc = *wcp++;
			if (!iswprint(wc))
				agetty_log_err(_("%s: invalid character 0x%x in login name"), op->tty, wc);
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
static void termio_final(struct agetty_options *op, struct termios *tp, struct chardata *cp)
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
		FALLTHROUGH;
	case 2:
		/* even parity */
		tp->c_cflag |= PARENB;
		tp->c_iflag |= INPCK | ISTRIP;
		FALLTHROUGH;
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
		agetty_log_err(_("%s: failed to set terminal attributes: %m"), op->tty);
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

static void __attribute__((__noreturn__)) usage(void)
{
	FILE *out = stdout;

	fputs(USAGE_HEADER, out);
	fprintf(out, _(" %1$s [options] <line> [<baud_rate>,...] [<termtype>]\n"
		       " %1$s [options] <baud_rate>,... <line> [<termtype>]\n"), program_invocation_short_name);

	fputs(USAGE_SEPARATOR, out);
	fputs(_("Open a terminal and set its mode.\n"), out);

	fputs(USAGE_OPTIONS, out);
	fputs(_(" -8, --8bits                assume 8-bit tty\n"), out);
	fputs(_(" -a, --autologin <user>     login the specified user automatically\n"), out);
	fputs(_(" -c, --noreset              do not reset control mode\n"), out);
	fputs(_(" -E, --remote               use -r <hostname> for login(1)\n"), out);
	fputs(_(" -f, --issue-file <list>    display issue files or directories\n"), out);
	fputs(_("     --show-issue           display issue file and exit\n"), out);
	fputs(_(" -h, --flow-control         enable hardware flow control\n"), out);
	fputs(_(" -H, --host <hostname>      specify login host\n"), out);
	fputs(_(" -i, --noissue              do not display issue file\n"), out);
	fputs(_(" -I, --init-string <string> set init string\n"), out);
	fputs(_(" -J, --noclear              do not clear the screen before prompt\n"), out);
	fputs(_(" -l, --login-program <file> specify login program\n"), out);
	fputs(_(" -L, --local-line[=<mode>]  control the local line flag\n"), out);
	fputs(_(" -m, --extract-baud         extract baud rate during connect\n"), out);
	fputs(_(" -n, --skip-login           do not prompt for login\n"), out);
	fputs(_(" -N, --nonewline            do not print a newline before issue\n"), out);
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
	fputs(_("     --reload               reload prompts on running agetty instances\n"), out);
	fputs(_("     --list-speeds          display supported baud rates\n"), out);
	fprintf(out, "     --help                 %s\n", USAGE_OPTSTR_HELP);
	fprintf(out, "     --version              %s\n", USAGE_OPTSTR_VERSION);
	fprintf(out, USAGE_MAN_TAIL("agetty(8)"));

	exit(EXIT_SUCCESS);
}


#ifdef USE_NETLINK
static void print_iface_best(struct issue *ie,
			     const char *ifname,
			     uint8_t ifa_family)
{
	struct ul_netaddrq_ip *best[__ULNETLINK_RATING_MAX];
	struct ul_netaddrq_iface *ifaceq;
	struct list_head *l;
	enum ul_netaddrq_ip_rating threshold;

	if (!ie->nl.data_addr)
		return; /* error: init failed */

	if ((ifaceq = ul_netaddrq_iface_by_name(&(ie->nl), ifname)))
	{
		memset(best, 0, sizeof(best));
		if (ifa_family == AF_INET)
			l = &(ifaceq->ip_quality_list_4);
		else
		/* if (ifa_family == AF_INET6) */
			l = &(ifaceq->ip_quality_list_6);

		threshold =
			ul_netaddrq_iface_bestaddr(l, &best);
		if (threshold != __ULNETLINK_RATING_MAX)
			fputs(ul_nl_addr_ntop_address(best[threshold]->addr),
			      ie->output);
	}
}

static void print_addrq_bestofall(struct issue *ie,
				  uint8_t ifa_family)
{
	struct ul_netaddrq_iface *best_ifaceq;
	enum ul_netaddrq_ip_rating threshold;
	const char *best_ipp;

	if (!ie->nl.data_addr)
		return; /* error: init failed */

	best_ipp = ul_netaddrq_get_best_ipp(&(ie->nl), ifa_family,
					    &threshold, &best_ifaceq);
	if (best_ipp)
		fputs(best_ipp, ie->output);
}

static void dump_iface_good(struct issue *ie,
			    struct ul_netaddrq_iface *ifaceq)
{
	struct ul_netaddrq_ip *best4[__ULNETLINK_RATING_MAX];
	struct ul_netaddrq_ip *best6[__ULNETLINK_RATING_MAX];
	struct list_head *li;
	enum ul_netaddrq_ip_rating threshold = __ULNETLINK_RATING_MAX - 1;
	enum ul_netaddrq_ip_rating fthreshold; /* per family threshold */
	bool first = true;

	memset(best4, 0, sizeof(best4));
	threshold = ul_netaddrq_iface_bestaddr(&(ifaceq->ip_quality_list_4),
					       &best4);
	memset(best6, 0, sizeof(best6));
	fthreshold = ul_netaddrq_iface_bestaddr(&(ifaceq->ip_quality_list_6),
						&best6);
	if (fthreshold < threshold)
		threshold = fthreshold;

	list_for_each(li, &(ifaceq->ip_quality_list_4))
	{
		struct ul_netaddrq_ip *ipq;

		ipq = list_entry(li, struct ul_netaddrq_ip, entry);
		if (threshold <= ULNETLINK_RATING_SCOPE_LINK &&
		    ( ipq->quality <= threshold ||
		      /* Consider site addresses equally good as global */
		      ipq->quality == ULNETLINK_RATING_SCOPE_SITE) &&
		    best4[threshold])
		{
			if (first)
			{
				fprintf(ie->output, "%s: ", ifaceq->ifname);
				first = false;
			}
			else
				fprintf(ie->output, " ");
			/* Write only the longest living temporary address */
			if (threshold == ULNETLINK_RATING_F_TEMPORARY)
			{
				fputs(ul_nl_addr_ntop_address(best4[ULNETLINK_RATING_F_TEMPORARY]->addr),
				      ie->output);
				goto temp_cont4;
			}
			else
				fputs(ul_nl_addr_ntop_address(ipq->addr),
				      ie->output);
		}
	temp_cont4:;
	}

	list_for_each(li, &(ifaceq->ip_quality_list_6))
	{
		struct ul_netaddrq_ip *ipq;

		ipq = list_entry(li, struct ul_netaddrq_ip, entry);
		if (threshold <= ULNETLINK_RATING_SCOPE_LINK &&
		    ( ipq->quality <= threshold ||
		      /* Consider site addresses equally good as global */
		      ipq->quality == ULNETLINK_RATING_SCOPE_SITE) &&
		    best6[threshold])
		{
			if (first)
			{
				fprintf(ie->output, "%s: ", ifaceq->ifname);
				first = false;
			}
			else
				fprintf(ie->output, " ");
			/* Write only the longest living temporary address */
			if (threshold == ULNETLINK_RATING_F_TEMPORARY)
			{
				fputs(ul_nl_addr_ntop_address(best6[ULNETLINK_RATING_F_TEMPORARY]->addr),
				      ie->output);
				goto temp_cont6;
			}
			else
				fputs(ul_nl_addr_ntop_address(ipq->addr),
				      ie->output);
		}
	temp_cont6:;
	}
	if (!first)
		fputs("\n", ie->output);
}

static void dump_iface_all(struct issue *ie,
			   struct ul_netaddrq_iface *ifaceq)
{
	struct list_head *li;
	struct ul_netaddrq_ip *ipq;
	bool first = true;

	list_for_each(li, &(ifaceq->ip_quality_list_4))
	{
		ipq = list_entry(li, struct ul_netaddrq_ip, entry);
		if (first)
		{
			fprintf(ie->output, "%s: ", ifaceq->ifname);
			first = false;
		}
		else
			fprintf(ie->output, " ");
		fputs(ul_nl_addr_ntop_address(ipq->addr), ie->output);
	}
	list_for_each(li, &(ifaceq->ip_quality_list_6))
	{
		ipq = list_entry(li, struct ul_netaddrq_ip, entry);
		if (first)
		{
			fprintf(ie->output, "%s: ", ifaceq->ifname);
			first = false;
		}
		else
			fprintf(ie->output, " ");
		fputs(ul_nl_addr_ntop_address(ipq->addr), ie->output);
	}
	if (!first)
		fputs("\n", ie->output);
}
#endif /* USE_NETLINK */

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

static void output_special_char(struct issue *ie,
				unsigned char c,
				struct agetty_options *op,
				struct termios *tp,
				FILE *fp)
{
	struct utsname uts;

	switch (c) {
	case 'e':
	{
		char escname[UL_COLORNAME_MAXSZ];

		if (get_escape_argument(fp, escname, sizeof(escname))) {
			char *esc = color_get_sequence(escname);

			if (esc) {
				fputs(esc, ie->output);
				free(esc);
			}
		} else
			fputs("\033", ie->output);
		break;
	}
	case 's':
		uname(&uts);
		fprintf(ie->output, "%s", uts.sysname);
		break;
	case 'n':
		uname(&uts);
		fprintf(ie->output, "%s", uts.nodename);
		break;
	case 'r':
		uname(&uts);
		fprintf(ie->output, "%s", uts.release);
		break;
	case 'v':
		uname(&uts);
		fprintf(ie->output, "%s", uts.version);
		break;
	case 'm':
		uname(&uts);
		fprintf(ie->output, "%s", uts.machine);
		break;
	case 'o':
	{
		char *dom = agetty_xgetdomainname();

		fputs(dom ? dom : "unknown_domain", ie->output);
		free(dom);
		break;
	}
	case 'O':
	{
		char *dom = NULL;
		char *host = agetty_xgethostname();
		struct addrinfo hints, *info = NULL;

		memset(&hints, 0, sizeof(hints));
		hints.ai_flags = AI_CANONNAME;

		if (host && getaddrinfo(host, NULL, &hints, &info) == 0 && info) {
			char *canon;

			if (info->ai_canonname &&
			    (canon = strchr(info->ai_canonname, '.')))
				dom = canon + 1;
		}
		fputs(dom ? dom : "unknown_domain", ie->output);
		if (info)
			freeaddrinfo(info);
		free(host);
		break;
	}
	case 'd':
	case 't':
	{
		time_t now;
		struct tm tm;

		time(&now);
		localtime_r(&now, &tm);

		if (c == 'd') /* ISO 8601 */
			fprintf(ie->output, "%s %s %2d  %d",
				      nl_langinfo(ABDAY_1 + tm.tm_wday),
				      nl_langinfo(ABMON_1 + tm.tm_mon),
				      tm.tm_mday,
				      tm.tm_year < 70 ? tm.tm_year + 2000 :
				      tm.tm_year + 1900);
		else
			fprintf(ie->output, "%02d:%02d:%02d",
				      tm.tm_hour, tm.tm_min, tm.tm_sec);
		break;
	}
	case 'l':
		fprintf (ie->output, "%s", op->tty);
		break;
	case 'b':
		agetty_fprint_speed(ie->output, cfgetispeed(tp));
		break;
	case 'S':
	{
		char *var = NULL, varname[64];

		/* \S{varname} */
		if (get_escape_argument(fp, varname, sizeof(varname))) {
			var = read_os_release(op, varname);
			if (var) {
				if (strcmp(varname, "ANSI_COLOR") == 0)
					fprintf(ie->output, "\033[%sm", var);
				else
					fputs(var, ie->output);
			}
		/* \S */
		} else if ((var = read_os_release(op, "PRETTY_NAME"))) {
			fputs(var, ie->output);

		/* \S and PRETTY_NAME not found */
		} else {
			uname(&uts);
			fputs(uts.sysname, ie->output);
		}

		free(var);

		break;
	}
	case 'u':
	case 'U':
	{
		int users = 0;
#ifdef USE_SYSTEMD
		if (sd_booted() > 0) {
			users = sd_get_sessions(NULL);
			if (users < 0)
				users = 0;
		} else
#endif
		{
			users = 0;
			struct utmpx *ut;
			setutxent();
			while ((ut = getutxent()))
				if (ut->ut_type == USER_PROCESS)
					users++;
			endutxent();
		}
		if (c == 'U')
			fprintf(ie->output, P_("%d user", "%d users", users), users);
		else
			fprintf (ie->output, "%d ", users);
		break;
	}
#ifdef USE_NETLINK
	case '4':
	case '6':
	{
		char iface[IF_NAMESIZE];
		uint8_t ifa_family = c == '4' ? AF_INET : AF_INET6;

		if (get_escape_argument(fp, iface, sizeof(iface)))
			print_iface_best(ie, iface, ifa_family);
		else
			print_addrq_bestofall(ie, ifa_family);

		/* TODO: Move to pass 1 */
		if (c == '4')
			netlink_groups |= RTMGRP_IPV4_IFADDR;
		else
			netlink_groups |= RTMGRP_IPV6_IFADDR;
		break;
	}
	case 'a':
	{
		struct list_head *li;
		struct ul_netaddrq_iface *ifaceq;

		list_for_each_netaddrq_iface(li, &(ie->nl))
		{
			ifaceq = list_entry(li, struct ul_netaddrq_iface, entry);

			dump_iface_good(ie, ifaceq);
		}
	}
	break;
	case 'A':
	{
		struct list_head *li;
		struct ul_netaddrq_iface *ifaceq;

		list_for_each_netaddrq_iface(li, &(ie->nl))
		{
			ifaceq = list_entry(li, struct ul_netaddrq_iface, entry);

			dump_iface_all(ie, ifaceq);
		}
	}
	break;
#endif /* USE_NETLINK */
	default:
		putc(c, ie->output);
		break;
	}
}

static void init_special_char(char* arg, struct agetty_options *op)
{
	char ch, *p, *q;
	int i;

	op->initstring = malloc(strlen(arg) + 1);
	if (!op->initstring)
		agetty_log_err(_("failed to allocate memory: %m"));

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

#ifdef KDGKBLED
/*
 * Appends @str to @dest and if @dest is not empty then use @sep as a
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
		p = mempcpy(p, sep, ssz);
	}
	memcpy(p, src, sz);
	*(p + sz) = '\0';

	return dsz + ssz + sz;
}
#endif /* KDGKBLED */

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
	agetty_log_err(_("checkname failed: %m"));
}

static void reload_agettys(void)
{
#ifdef AGETTY_RELOAD
	int fd = open(AGETTY_RELOAD_FILENAME, O_CREAT|O_CLOEXEC|O_WRONLY,
					      S_IRUSR|S_IWUSR);
	if (fd < 0)
		err(EXIT_FAILURE, _("cannot open %s"), AGETTY_RELOAD_FILENAME);

	if (futimens(fd, NULL) < 0 || close(fd) < 0)
		err(EXIT_FAILURE, _("cannot touch file %s"),
		    AGETTY_RELOAD_FILENAME);
#else
	/* very unusual */
	errx(EXIT_FAILURE, _("--reload is unsupported on your system"));
#endif
}

