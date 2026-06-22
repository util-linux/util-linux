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
 * No copyright is claimed.  This code is in the public domain; do with
 * it what you wish.
 */

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <netdb.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>

#include "agetty.h"
#include "all-io.h"
#include "c.h"
#include "logindefs.h"
#include "nls.h"
#include "pathnames.h"
#include "strutils.h"
#include "widechar.h"


/* Login prompt. */
#define LOGIN_PROMPT		"login: "


#ifdef AGETTY_RELOAD
# include <sys/inotify.h>
static int inotify_fd = AGETTY_RELOAD_FDNONE;
#endif

#define serial_tty_option(opt, flag)	\
	(((opt)->flags & (F_VCONSOLE|(flag))) == (flag))

/* Default banner printed when the login program is not available. */
#define DEFAULT_NOLOGIN_MESSAGE	N_("This system does not permit logins.")

static int wait_for_term_input(struct agetty_issue *ie, int fd);
static void wait_for_login_program(struct agetty_options *op);
static void do_prompt(struct agetty_issue *ie, struct agetty_options *op, struct termios *tp);
static char *get_logname(struct agetty_issue *ie, struct agetty_options *op,
			 struct termios *tp, struct chardata *cp);
static int caps_lock(char *s);
#ifdef KDGKBLED
static ssize_t append(char *dest, size_t len, const char  *sep, const char *src);
#endif



#ifdef DEBUGGING
# include "closestream.h"
# ifndef DEBUG_OUTPUT
#  define DEBUG_OUTPUT "/dev/tty10"
# endif
FILE *dbf;
#endif

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
	fputs(_("     --nologin-message <msg> message shown when login program is missing\n"), out);
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
		NOLOGIN_MESSAGE_OPTION,
	};
	static const struct option longopts[] = {
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
		{  "nologin-message", required_argument, NULL,  NOLOGIN_MESSAGE_OPTION },
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
			op->fakehost = optarg;
			break;
		case 'i':
			op->flags &= ~F_ISSUE;
			break;
		case 'I':
			op->initstring = agetty_parse_initstring(optarg);
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
		case NOLOGIN_MESSAGE_OPTION:
			op->nologin_message = optarg;
			break;
		case RELOAD_OPTION:
			agetty_reload();
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
		agetty_show_issue(op);
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
		agetty_parse_speeds(op, argv[optind++]);
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
				agetty_parse_speeds(op, v);
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


/* Show login prompt, optionally preceded by /etc/issue contents. */

int main(int argc, char **argv)
{
	struct chardata chardata;		/* will be set by get_logname() */
	struct termios termios;			/* terminal mode bits */
	struct agetty_options options = {
		.flags  =  F_ISSUE,		/* show /etc/issue (SYSV_STYLE) */
		.login  =  _PATH_LOGIN,		/* default login program */
		.tty    = "tty1"		/* default tty line */
	};
	struct agetty_issue issue = {
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

	/* Update the utmp file. */
#ifdef	SYSV_STYLE
	agetty_update_utmp(&options);
#endif
	if (options.delay)
	    sleep(options.delay);

	debug("calling open_tty\n");

	/* Open the tty as standard { input, output, error }. */
	agetty_open_tty(options.tty, &termios, &options);

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
	agetty_termio_init(&options, &termios);

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
		agetty_auto_baud(&termios);

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

	/*
	 * If the login program is missing or not executable there is no way to
	 * log in.  Show a banner and wait until it becomes available instead of
	 * prompting for a username that can never be used.
	 */
	wait_for_login_program(&options);

	if (options.autolog) {
		debug("doing auto login\n");
		options.username = options.autolog;
	}

	if (options.flags & F_NOPROMPT) {	/* --skip-login */
		agetty_eval_issue_file(&issue, &options, &termios);
		agetty_print_issue_file(&issue, &options, &termios);

	} else {				/* regular (auto)login */
		if ((options.flags & F_NOHOSTNAME) == 0 &&
		    getlogindefs_bool("LOGIN_PLAIN_PROMPT", 0) == 1)
			/* /etc/login.defs enables --nohostname too */
			options.flags |= F_NOHOSTNAME;

		if (options.autolog) {
			/* Autologin prompt */
			agetty_eval_issue_file(&issue, &options, &termios);
			do_prompt(&issue, &options, &termios);
			printf(_("%s%s (automatic login)\n"), LOGIN_PROMPT,
					options.autolog);
		} else {
			/* Read the login name. */
			debug("reading login name\n");
			while ((options.username =
				get_logname(&issue, &options, &termios, &chardata)) == NULL)
				if ((options.flags & F_VCONSOLE) == 0 && options.numspeed)
					agetty_next_speed(&options, &termios);
		}
	}

	/* Disable timer. */
	if (options.timeout)
		alarm(0);

	/* Finalize the termios settings. */
	if ((options.flags & F_VCONSOLE) == 0)
		agetty_termio_final(&options, &termios, &chardata);
	else
		agetty_reset_vc(&options, &termios, 1);

	/* Now the newline character should be properly written. */
	write_all(STDOUT_FILENO, "\r\n", 2);

	sigaction(SIGQUIT, &sa_quit, NULL);
	sigaction(SIGINT, &sa_int, NULL);

	agetty_init_login_argv(login_argv, &login_argc, &options);

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
 * Block until the login program is executable.
 *
 * On systems without a shell or /bin/login there is no point in showing the
 * issue file and prompting for a username, because the login program can never
 * be executed.  Instead of running into a confusing dead end, print a short
 * banner and wait for the user to press Enter, then re-check whether the login
 * program has become available (for example because an administrator installed
 * it at runtime).  As soon as the login program is executable we return and the
 * normal prompt flow continues.
 */
static void wait_for_login_program(struct agetty_options *op)
{
	const char *message = op->nologin_message ?
				op->nologin_message : _(DEFAULT_NOLOGIN_MESSAGE);

	while (access(op->login, X_OK) != 0) {
		printf("%s\n", message);
		fflush(stdout);

		/* Wait for Enter, then re-check the login program. */
		if (getc(stdin) == EOF)
			return;
	}
}

static void do_prompt(struct agetty_issue *ie, struct agetty_options *op, struct termios *tp)
{
#ifdef AGETTY_RELOAD
again:
#endif
	agetty_print_issue_file(ie, op, tp);

	if (op->flags & F_LOGINPAUSE) {
		puts(_("[press ENTER to login]"));
#ifdef AGETTY_RELOAD
		/* reload issue */
		if (!wait_for_term_input(ie, STDIN_FILENO)) {
			agetty_eval_issue_file(ie, op, tp);
			if (agetty_issue_is_changed(ie)) {
				if ((op->flags & F_VCONSOLE)
				    && (op->flags & F_NOCLEAR) == 0)
					agetty_termio_clear(STDOUT_FILENO);
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


/* Get user name, establish parity, speed, erase, kill & eol. */
static char *get_logname(struct agetty_issue *ie, struct agetty_options *op, struct termios *tp, struct chardata *cp)
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

	agetty_eval_issue_file(ie, op, tp);
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
			agetty_eval_issue_file(ie, op, tp);
			if (!agetty_issue_is_changed(ie))
				goto no_reload;
			/* if (ie->nl.fd >= 0) ul_nl_close(&(ie->nl));
			 * ie->nl.fd = -1; */
			tcflush(STDIN_FILENO, TCIFLUSH);
			if ((op->flags & F_VCONSOLE)
			    && (op->flags & F_NOCLEAR) == 0)
				agetty_termio_clear(STDOUT_FILENO);
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
						agetty_erase_char(*(visual_bp - 1), cp);
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
						agetty_erase_char(*(visual_bp - 1), cp);
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


#ifdef AGETTY_RELOAD
static int wait_for_term_input(struct agetty_issue *ie, int fd)
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

