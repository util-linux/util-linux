/*
 * sulogin
 *
 * This program gives Linux machines a reasonable secure way to boot single
 * user. It forces the user to supply the root password before a shell is
 * started. If there is a shadow password file and the encrypted root password
 * is "x" the shadow password will be used.
 *
 * Copyright (C) 1998-2003 Miquel van Smoorenburg.
 * Copyright (C) 2012 Karel Zak <kzak@redhat.com>
 * Copyright (C) 2012 Werner Fink <werner@suse.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <pwd.h>
#include <shadow.h>
#include <termios.h>
#include <errno.h>
#include <getopt.h>
#include <sys/ioctl.h>
#ifdef HAVE_CRYPT_H
# include <crypt.h>
#endif

#ifdef HAVE_LIBSELINUX
# include <selinux/selinux.h>
# include <selinux/get_context_list.h>
#endif

#ifdef __linux__
# include <sys/kd.h>
# include <sys/param.h>
#endif

#include "c.h"
#include "closestream.h"
#include "nls.h"
#include "pathnames.h"
#include "strutils.h"
#include "ttyutils.h"
#include "sulogin-consoles.h"
#define CONMAX		16

static unsigned int timeout;
static int profile;
static volatile uint32_t openfd;		/* Remember higher file descriptors */
static volatile uint32_t *usemask;

struct sigaction saved_sigint;
struct sigaction saved_sigtstp;
struct sigaction saved_sigquit;
struct sigaction saved_sighup;
struct sigaction saved_sigchld;

static volatile sig_atomic_t alarm_rised;
static volatile sig_atomic_t sigchild;

#ifndef IUCLC
# define IUCLC		0
#endif

#ifdef TIOCGLCKTRMIOS
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
		if (fd < 0)
			exit(127);
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
#endif

/*
 * Fix the tty modes and set reasonable defaults.
 */
static void tcinit(struct console *con)
{
	int mode = 0, flags = 0;
	struct termios *tio = &con->tio;
	struct termios lock;
	int fd = con->fd;
#ifdef TIOCGLCKTRMIOS
	int i = (plymouth_command("--ping")) ? 20 : 0;

	while (i-- > 0) {
		/*
		 * With plymouth the termios flags become changed after this
		 * function had changed the termios.
		 */
		memset(&lock, 0, sizeof(struct termios));
		if (ioctl(fd, TIOCGLCKTRMIOS, &lock) < 0)
			break;
		if (!lock.c_iflag && !lock.c_oflag && !lock.c_cflag && !lock.c_lflag)
			break;
		if (i == 15 && plymouth_command("quit") != 0)
			break;
		sleep(1);
	}
	memset(&lock, 0, sizeof(struct termios));
	ioctl(fd, TIOCSLCKTRMIOS, &lock);
#endif

	errno = 0;

	if (tcgetattr(fd, tio) < 0) {
		warn(_("tcgetattr failed"));
		con->flags |= CON_NOTTY;
		return;
	}

	/* Handle lines other than virtual consoles here */
#if defined(KDGKBMODE)
	if (ioctl(fd, KDGKBMODE, &mode) < 0)
#endif
	{
		speed_t ispeed, ospeed;
		struct winsize ws;
		errno = 0;

		/* this is a modem line */
		con->flags |= CON_SERIAL;

		/* Flush input and output queues on modem lines */
		tcflush(fd, TCIOFLUSH);

		ispeed = cfgetispeed(tio);
		ospeed = cfgetospeed(tio);

		if (!ispeed) ispeed = TTYDEF_SPEED;
		if (!ospeed) ospeed = TTYDEF_SPEED;

		tio->c_cflag = CREAD | CS8 | HUPCL | (tio->c_cflag & CLOCAL);
		tio->c_iflag = 0;
		tio->c_lflag = 0;
		tio->c_oflag &= OPOST | ONLCR;

		cfsetispeed(tio, ispeed);
		cfsetospeed(tio, ospeed);

#ifdef HAVE_STRUCT_TERMIOS_C_LINE
		tio->c_line         = 0;
#endif
		tio->c_cc[VTIME]    = 0;
		tio->c_cc[VMIN]     = 1;

		if (ioctl(fd, TIOCGWINSZ, &ws) == 0) {
			int set = 0;
			if (ws.ws_row == 0) {
				ws.ws_row = 24;
				set++;
			}
			if (ws.ws_col == 0) {
				ws.ws_col = 80;
				set++;
			}
			if (set)
				ignore_result( ioctl(fd, TIOCSWINSZ, &ws) );
		}

		setlocale(LC_CTYPE, "POSIX");
		goto setattr;
	}
#if defined(IUTF8) && defined(KDGKBMODE)
	/* Handle mode of current keyboard setup, e.g. for UTF-8 */
	switch(mode) {
	case K_UNICODE:
		setlocale(LC_CTYPE, "C.UTF-8");
		flags |= UL_TTY_UTF8;
		break;
	case K_RAW:
	case K_MEDIUMRAW:
	case K_XLATE:
	default:
		setlocale(LC_CTYPE, "POSIX");
		break;
	}
#else
	setlocale(LC_CTYPE, "POSIX");
#endif
	reset_virtual_console(tio, flags);
setattr:
	if (tcsetattr(fd, TCSANOW, tio))
		warn(_("tcsetattr failed"));

	/* Enable blocking mode for read and write */
	if ((flags = fcntl(fd, F_GETFL, 0)) != -1)
		ignore_result( fcntl(fd, F_SETFL, flags & ~O_NONBLOCK) );
}

/*
 * Finalize the tty modes on modem lines.
 */
static void tcfinal(struct console *con)
{
	struct termios *tio;
	int fd;

	if ((con->flags & CON_SERIAL) == 0) {
		setenv("TERM", "linux", 1);
		return;
	}
	if (con->flags & CON_NOTTY) {
		setenv("TERM", "dumb", 1);
		return;
	}

#if defined (__s390__) || defined (__s390x__)
	setenv("TERM", "dumb", 1);
#else
	setenv("TERM", "vt102", 1);
#endif
	tio = &con->tio;
	fd = con->fd;

	tio->c_iflag |= (IXON | IXOFF);
	tio->c_lflag |= (ICANON | ISIG | ECHO|ECHOE|ECHOK|ECHOKE);
	tio->c_oflag |= OPOST;

	tio->c_cc[VINTR]    = CINTR;
	tio->c_cc[VQUIT]    = CQUIT;
	tio->c_cc[VERASE]   = con->cp.erase;
	tio->c_cc[VKILL]    = con->cp.kill;
	tio->c_cc[VEOF]     = CEOF;
#ifdef VSWTC
	tio->c_cc[VSWTC]    = _POSIX_VDISABLE;
#elif defined(VSWTCH)
	tio->c_cc[VSWTCH]   = _POSIX_VDISABLE;
#endif
	tio->c_cc[VSTART]   = CSTART;
	tio->c_cc[VSTOP]    = CSTOP;
	tio->c_cc[VSUSP]    = CSUSP;
	tio->c_cc[VEOL]     = _POSIX_VDISABLE;

	if (con->cp.eol == CR) {
		tio->c_iflag |= ICRNL;
		tio->c_iflag &= ~(INLCR|IGNCR);
		tio->c_oflag |= ONLCR;
		tio->c_oflag &= ~(OCRNL|ONLRET);
	}

	switch (con->cp.parity) {
	default:
	case 0:
		tio->c_cflag &= ~(PARODD | PARENB);
		tio->c_iflag &= ~(INPCK | ISTRIP);
		break;
	case 1:				/* odd parity */
		tio->c_cflag |= PARODD;
		/* fall through */
	case 2:				/* even parity */
		tio->c_cflag |= PARENB;
		tio->c_iflag |= (INPCK | ISTRIP);
		/* fall through */
	case (1 | 2):			/* no parity bit */
		tio->c_cflag &= ~CSIZE;
		tio->c_cflag |= CS7;
		break;
	}

	/* Set line attributes */
	tcsetattr(fd, TCSANOW, tio);
}

/*
 * Called at timeout.
 */
static void alrm_handler(int sig __attribute__((unused)))
{
	/* Timeout expired */
	alarm_rised++;
}

static void chld_handler(int sig __attribute__((unused)))
{
	sigchild++;
}

static void mask_signal(int signal, void (*handler)(int),
		struct sigaction *origaction)
{
	struct sigaction newaction;

	newaction.sa_handler = handler;
	sigemptyset(&newaction.sa_mask);
	newaction.sa_flags = 0;

	sigaction(signal, &newaction, origaction);
}

static void unmask_signal(int signal, struct sigaction *sa)
{
	sigaction(signal, sa, NULL);
}

/*
 * See if an encrypted password is valid. The encrypted password is checked for
 * traditional-style DES and FreeBSD-style MD5 encryption.
 */
static int valid(const char *pass)
{
	const char *s;
	char id[5];
	size_t len;
	off_t off;

	if (pass[0] == 0)
		return 1;
	if (pass[0] != '$')
		goto check_des;

	/*
	 * up to 4 bytes for the signature e.g. $1$
	 */
	for (s = pass+1; *s && *s != '$'; s++);

	if (*s++ != '$')
		return 0;

	if ((off = (off_t)(s-pass)) > 4 || off < 3)
		return 0;

	memset(id, '\0', sizeof(id));
	strncpy(id, pass, off);

	/*
	 * up to 16 bytes for the salt
	 */
	for (; *s && *s != '$'; s++);

	if (*s++ != '$')
		return 0;

	if ((off_t)(s-pass) > 16)
		return 0;

	len = strlen(s);

	/*
	 * the MD5 hash (128 bits or 16 bytes) encoded in base64 = 22 bytes
	 */
	if ((strcmp(id, "$1$") == 0) && (len < 22 || len > 24))
		return 0;

	/*
	 * the SHA-256 hash 43 bytes
	 */
	if ((strcmp(id, "$5$") == 0) && (len < 42 || len > 44))
		return 0;

	/*
	 * the SHA-512 hash 86 bytes
	 */
	if ((strcmp(id, "$6$") == 0) && (len < 85 || len > 87))
		return 0;

	/*
	 * e.g. Blowfish hash
	 */
	return 1;
check_des:
	if (strlen(pass) != 13)
		return 0;

	for (s = pass; *s; s++) {
		if ((*s < '0' || *s > '9') &&
		    (*s < 'a' || *s > 'z') &&
		    (*s < 'A' || *s > 'Z') &&
		    *s != '.' && *s != '/')
			return 0;
	}
	return 1;
}

/*
 * Set a variable if the value is not NULL.
 */
static inline void set(char **var, char *val)
{
	if (val)
		*var = val;
}

/*
 * Get the root password entry.
 */
static struct passwd *getrootpwent(int try_manually)
{
	static struct passwd pwd;
	struct passwd *pw;
	struct spwd *spw;
	FILE *fp;
	static char line[256];
	static char sline[256];
	char *p;

	/*
	 * First, we try to get the password the standard way using normal
	 * library calls.
	 */
	if ((pw = getpwnam("root")) &&
	    !strcmp(pw->pw_passwd, "x") &&
	    (spw = getspnam("root")))
		pw->pw_passwd = spw->sp_pwdp;

	if (pw || !try_manually)
		return pw;

	/*
	 * If we come here, we could not retrieve the root password through
	 * library calls and we try to read the password and shadow files
	 * manually.
	 */
	pwd.pw_name = "root";
	pwd.pw_passwd = "";
	pwd.pw_gecos = "Super User";
	pwd.pw_dir = "/";
	pwd.pw_shell = "";
	pwd.pw_uid = 0;
	pwd.pw_gid = 0;

	if ((fp = fopen(_PATH_PASSWD, "r")) == NULL) {
		warn(_("cannot open %s"), _PATH_PASSWD);
		return &pwd;
	}

	/*
	 * Find root in the password file.
	 */
	while ((p = fgets(line, 256, fp)) != NULL) {
		if (strncmp(line, "root:", 5) != 0)
			continue;
		p += 5;
		set(&pwd.pw_passwd, strsep(&p, ":"));
		strsep(&p, ":");
		strsep(&p, ":");
		set(&pwd.pw_gecos, strsep(&p, ":"));
		set(&pwd.pw_dir, strsep(&p, ":"));
		set(&pwd.pw_shell, strsep(&p, "\n"));
		p = line;
		break;
	}

	fclose(fp);

	/*
	 * If the encrypted password is valid or not found, return.
	 */
	if (p == NULL) {
		warnx(_("%s: no entry for root\n"), _PATH_PASSWD);
		return &pwd;
	}
	if (valid(pwd.pw_passwd))
		return &pwd;

	/*
	 * The password is invalid. If there is a shadow password, try it.
	 */
	strcpy(pwd.pw_passwd, "");
	if ((fp = fopen(_PATH_SHADOW_PASSWD, "r")) == NULL) {
		warn(_("cannot open %s"), _PATH_PASSWD);
		return &pwd;
	}
	while ((p = fgets(sline, 256, fp)) != NULL) {
		if (strncmp(sline, "root:", 5) != 0)
			continue;
		p += 5;
		set(&pwd.pw_passwd, strsep(&p, ":"));
		break;
	}
	fclose(fp);

	/*
	 * If the password is still invalid, NULL it, and return.
	 */
	if (p == NULL) {
		warnx(_("%s: no entry for root"), _PATH_SHADOW_PASSWD);
		strcpy(pwd.pw_passwd, "");
	}
	if (!valid(pwd.pw_passwd)) {
		warnx(_("%s: root password garbled"), _PATH_SHADOW_PASSWD);
		strcpy(pwd.pw_passwd, "");
	}
	return &pwd;
}

/*
 * Ask by prompt for the password.
 */
static void doprompt(const char *crypted, struct console *con)
{
	struct termios tty;

	if (con->flags & CON_SERIAL) {
		tty = con->tio;
		/*
		 * For prompting: map NL in output to CR-NL
		 * otherwise we may see stairs in the output.
		 */
		tty.c_oflag |= (ONLCR | OPOST);
		tcsetattr(con->fd, TCSADRAIN, &tty);
	}
	if (con->file == (FILE*)0) {
		if  ((con->file = fdopen(con->fd, "r+")) == (FILE*)0)
			goto err;
	}
#if defined(USE_ONELINE)
	if (crypted[0])
		fprintf(con->file, _("Give root password for login: "));
	else
		fprintf(con->file, _("Press Enter for login: "));
#else
	if (crypted[0])
		fprintf(con->file, _("Give root password for maintenance\n"));
	else
		fprintf(con->file, _("Press Enter for maintenance"));
	fprintf(con->file, _("(or press Control-D to continue): "));
#endif
	fflush(con->file);
err:
	if (con->flags & CON_SERIAL)
		tcsetattr(con->fd, TCSADRAIN, &con->tio);
}

/*
 * Make sure to have an own session and controlling terminal
 */
static void setup(struct console *con)
{
	pid_t pid, pgrp, ppgrp, ttypgrp;
	int fd;

	if (con->flags & CON_NOTTY)
		return;
	fd = con->fd;

	/*
	 * Only go through this trouble if the new
	 * tty doesn't fall in this process group.
	 */
	pid = getpid();
	pgrp = getpgid(0);
	ppgrp = getpgid(getppid());
	ttypgrp = tcgetpgrp(fd);

	if (pgrp != ttypgrp && ppgrp != ttypgrp) {
		if (pid != getsid(0)) {
			if (pid == getpgid(0))
				setpgid(0, getpgid(getppid()));
			setsid();
		}

		mask_signal(SIGHUP, SIG_IGN, &saved_sighup);
		if (ttypgrp > 0)
			ioctl(STDIN_FILENO, TIOCNOTTY, (char *)1);
		unmask_signal(SIGHUP, &saved_sighup);
		if (fd > STDIN_FILENO)  close(STDIN_FILENO);
		if (fd > STDOUT_FILENO) close(STDOUT_FILENO);
		if (fd > STDERR_FILENO) close(STDERR_FILENO);

		ioctl(fd, TIOCSCTTY, (char *)1);
		tcsetpgrp(fd, ppgrp);
	}
	dup2(fd, STDIN_FILENO);
	dup2(fd, STDOUT_FILENO);
	dup2(fd, STDERR_FILENO);
	con->fd = STDIN_FILENO;

	for (fd = STDERR_FILENO+1; fd < 32; fd++) {
		if (openfd & (1<<fd)) {
			close(fd);
			openfd &= ~(1<<fd);
		}
	}
}

/*
 * Ask for the password. Note that there is no default timeout as we normally
 * skip this during boot.
 */
static char *getpasswd(struct console *con)
{
	struct sigaction sa;
	struct termios tty;
	static char pass[128], *ptr;
	struct chardata *cp;
	char *ret = pass;
	unsigned char tc;
	char c, ascval;
	int eightbit;
	int fd;

	if (con->flags & CON_NOTTY)
		goto out;
	fd = con->fd;
	cp = &con->cp;
	tty = con->tio;

	tty.c_iflag &= ~(IUCLC|IXON|IXOFF|IXANY);
	tty.c_lflag &= ~(ECHO|ECHOE|ECHOK|ECHONL|TOSTOP|ISIG);
	tc = (tcsetattr(fd, TCSAFLUSH, &tty) == 0);

	sigemptyset(&sa.sa_mask);
	sa.sa_handler = alrm_handler;
	sa.sa_flags = 0;
	sigaction(SIGALRM, &sa, NULL);

	if (timeout)
		alarm(timeout);

	ptr = &pass[0];
	cp->eol = *ptr = '\0';

	eightbit = ((con->flags & CON_SERIAL) == 0 || (tty.c_cflag & (PARODD|PARENB)) == 0);
	while (cp->eol == '\0') {
		if (read(fd, &c, 1) < 1) {
			if (errno == EINTR || errno == EAGAIN) {
				xusleep(250000);
				continue;
			}
			ret = (char*)0;
			switch (errno) {
			case 0:
			case EIO:
			case ESRCH:
			case EINVAL:
			case ENOENT:
				break;
			default:
				warn(_("cannot read %s"), con->tty);
				break;
			}
			goto quit;
		}

		if (eightbit)
			ascval = c;
		else if (c != (ascval = (c & 0177))) {
			uint32_t bits, mask;
			for (bits = 1, mask = 1; mask & 0177; mask <<= 1) {
				if (mask & ascval)
					bits++;
			}
			cp->parity |= ((bits & 1) ? 1 : 2);
		}

		switch (ascval) {
		case 0:
			*ptr = '\0';
			goto quit;
		case CR:
		case NL:
			*ptr = '\0';
			cp->eol = ascval;
			break;
		case BS:
		case CERASE:
			cp->erase = ascval;
			if (ptr > &pass[0])
				ptr--;
			break;
		case CKILL:
			cp->kill = ascval;
			while (ptr > &pass[0])
				ptr--;
			break;
		case CEOF:
			goto quit;
		default:
			if ((size_t)(ptr - &pass[0]) >= (sizeof(pass) -1 )) {
				 fprintf(stderr, "sulogin: input overrun at %s\n\r", con->tty);
				 ret = (char*)0;
				 goto quit;
			}
			*ptr++ = ascval;
			break;
		}
	}
quit:
	alarm(0);
	if (tc)
		tcsetattr(fd, TCSAFLUSH, &con->tio);
	if (ret && *ret != '\0')
		tcfinal(con);
	printf("\r\n");
out:
	return ret;
}

/*
 * Password was OK, execute a shell.
 */
static void sushell(struct passwd *pwd)
{
	char shell[PATH_MAX];
	char home[PATH_MAX];
	char *p;
	char *su_shell;

	/*
	 * Set directory and shell.
	 */
	if (chdir(pwd->pw_dir) != 0) {
		warn(_("%s: change directory failed"), pwd->pw_dir);
		printf(_("Logging in with home = \"/\".\n"));

		if (chdir("/") != 0)
			warn(_("change directory to system root failed"));
	}

	if ((p = getenv("SUSHELL")) != NULL)
		su_shell = p;
	else if ((p = getenv("sushell")) != NULL)
		su_shell = p;
	else {
		if (pwd->pw_shell[0])
			su_shell = pwd->pw_shell;
		else
			su_shell = "/bin/sh";
	}
	if ((p = strrchr(su_shell, '/')) == NULL)
		p = su_shell;
	else
		p++;

	snprintf(shell, sizeof(shell), profile ? "-%s" : "%s", p);

	/*
	 * Set some important environment variables.
	 */
	if (getcwd(home, sizeof(home)) == NULL)
		strcpy(home, "/");

	setenv("HOME", home, 1);
	setenv("LOGNAME", "root", 1);
	setenv("USER", "root", 1);
	if (!profile)
		setenv("SHLVL","0",1);

	/*
	 * Try to execute a shell.
	 */
	setenv("SHELL", su_shell, 1);
	unmask_signal(SIGINT, &saved_sigint);
	unmask_signal(SIGTSTP, &saved_sigtstp);
	unmask_signal(SIGQUIT, &saved_sigquit);
	mask_signal(SIGHUP, SIG_DFL, NULL);

#ifdef HAVE_LIBSELINUX
	if (is_selinux_enabled() > 0) {
		security_context_t scon=NULL;
		char *seuser=NULL;
		char *level=NULL;
		if (getseuserbyname("root", &seuser, &level) == 0) {
			if (get_default_context_with_level(seuser, level, 0, &scon) == 0) {
				if (setexeccon(scon) != 0)
					warnx(_("setexeccon failed"));
				freecon(scon);
			}
		}
		free(seuser);
		free(level);
	}
#endif
	execl(su_shell, shell, NULL);
	warn(_("failed to execute %s"), su_shell);

	setenv("SHELL", "/bin/sh", 1);
	execl("/bin/sh", profile ? "-sh" : "sh", NULL);
	warn(_("failed to execute %s"), "/bin/sh");
}

static void usage(FILE *out)
{
	fputs(USAGE_HEADER, out);
	fprintf(out, _(
		" %s [options] [tty device]\n"), program_invocation_short_name);

	fputs(USAGE_SEPARATOR, out);
	fputs(_("Single-user login.\n"), out);

	fputs(USAGE_OPTIONS, out);
	fputs(_(" -p, --login-shell        start a login shell\n"
		" -t, --timeout <seconds>  max time to wait for a password (default: no limit)\n"
		" -e, --force              examine password files directly if getpwnam(3) fails\n"),
		out);

	fputs(USAGE_SEPARATOR, out);
	fputs(USAGE_HELP, out);
	fputs(USAGE_VERSION, out);
	fprintf(out, USAGE_MAN_TAIL("sulogin(8)"));
}

int main(int argc, char **argv)
{
	LIST_HEAD(consoles);
	struct list_head *ptr;
	struct console *con;
	char *tty = NULL;
	struct passwd *pwd;
	int c, status = 0;
	int reconnect = 0;
	int opt_e = 0;
	pid_t pid;

	static const struct option longopts[] = {
		{ "login-shell",  0, 0, 'p' },
		{ "timeout",      1, 0, 't' },
		{ "force",        0, 0, 'e' },
		{ "help",         0, 0, 'h' },
		{ "version",      0, 0, 'V' },
		{ NULL,           0, 0, 0 }
	};

	/*
	 * If we are init we need to set up a own session.
	 */
	if ((pid = getpid()) == 1) {
		setsid();
		ignore_result( ioctl(STDIN_FILENO, TIOCSCTTY, (char *) 1) );
	}

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout); /* XXX */

	/*
	 * See if we have a timeout flag.
	 */
	while ((c = getopt_long(argc, argv, "ehpt:V", longopts, NULL)) != -1) {
		switch(c) {
		case 't':
			timeout = strtou32_or_err(optarg, _("invalid timeout argument"));
			break;
		case 'p':
			profile = 1;
			break;
		case 'e':
			opt_e = 1;
			break;
		case 'V':
			printf(UTIL_LINUX_VERSION);
			return EXIT_SUCCESS;
		case 'h':
			usage(stdout);
			return EXIT_SUCCESS;
		default:
			usage(stderr);
			/* Do not exit! */
			break;
		}
	}

	if (geteuid() != 0)
		errx(EXIT_FAILURE, _("only superuser can run this program"));

	mask_signal(SIGQUIT, SIG_IGN, &saved_sigquit);
	mask_signal(SIGTSTP, SIG_IGN, &saved_sigtstp);
	mask_signal(SIGINT,  SIG_IGN, &saved_sigint);
	mask_signal(SIGHUP,  SIG_IGN, &saved_sighup);


	emergency_do_mounts();
	atexit( emergency_do_umounts );

	/*
	 * See if we need to open an other tty device.
	 */
	if (optind < argc)
		tty = argv[optind];

	if (!tty || *tty == '\0')
		tty = getenv("CONSOLE");

	/*
	 * Detect possible consoles, use stdin as fallback.
	 * If an optional tty is given, reconnect it to stdin.
	 */
	reconnect = detect_consoles(tty, STDIN_FILENO, &consoles);

	/*
	 * If previous stdin was not the speified tty and therefore reconnected
	 * to the specified tty also reconnect stdout and stderr.
	 */
	if (reconnect) {
		if (isatty(STDOUT_FILENO) == 0)
			dup2(STDOUT_FILENO, STDIN_FILENO);
		if (isatty(STDERR_FILENO) == 0)
			dup2(STDOUT_FILENO, STDERR_FILENO);
	}

	/*
	 * Should not happen
	 */
	if (list_empty(&consoles)) {
		if (!errno)
			errno = ENOENT;
		err(EXIT_FAILURE, _("cannot open console"));
	}

	/*
	 * Get the root password.
	 */
	if ((pwd = getrootpwent(opt_e)) == NULL) {
		warnx(_("cannot open password database"));
		sleep(2);
		return EXIT_FAILURE;
	}

	/*
	 * Ask for the password on the consoles.
	 */
	list_for_each(ptr, &consoles) {
		con = list_entry(ptr, struct console, entry);
		if (con->id >= CONMAX)
			break;
		if (con->fd >= 0) {
			openfd |= (1 << con->fd);
			tcinit(con);
			continue;
		}
		if ((con->fd = open(con->tty, O_RDWR | O_NOCTTY | O_NONBLOCK)) < 0)
			continue;
		openfd |= (1 << con->fd);
		tcinit(con);
	}
	ptr = (&consoles)->next;
	usemask = (uint32_t*) mmap(NULL, sizeof(uint32_t),
					PROT_READ|PROT_WRITE,
					MAP_ANONYMOUS|MAP_SHARED, -1, 0);

	if (ptr->next == &consoles) {
		con = list_entry(ptr, struct console, entry);
		goto nofork;
	}

	mask_signal(SIGCHLD, chld_handler, &saved_sigchld);
	do {
		con = list_entry(ptr, struct console, entry);
		if (con->id >= CONMAX)
			break;

		switch ((con->pid = fork())) {
		case 0:
			mask_signal(SIGCHLD, SIG_DFL, NULL);
			/* fall through */
		nofork:
			setup(con);
			while (1) {
				const char *passwd = pwd->pw_passwd;
				const char *answer;
				int failed = 0, doshell = 0;

				doprompt(passwd, con);
				if ((answer = getpasswd(con)) == NULL)
					break;

				if (passwd[0] == '\0')
					doshell++;
				else {
					const char *cryptbuf;
					cryptbuf = crypt(answer, passwd);
					if (cryptbuf == NULL)
						warn(_("crypt failed"));
					else if (strcmp(cryptbuf, pwd->pw_passwd) == 0)
						doshell++;
				}

				if (doshell) {
					*usemask |= (1<<con->id);
					sushell(pwd);
					*usemask &= ~(1<<con->id);
					failed++;
				}

				mask_signal(SIGQUIT, SIG_IGN, &saved_sigquit);
				mask_signal(SIGTSTP, SIG_IGN, &saved_sigtstp);
				mask_signal(SIGINT,  SIG_IGN, &saved_sigint);

				if (failed) {
					fprintf(stderr, _("Can not execute su shell\n\n"));
					break;
				}
				fprintf(stderr, _("Login incorrect\n\n"));
			}
			if (alarm_rised) {
				tcfinal(con);
				warnx(_("Timed out\n\n"));
			}
			/*
			 * User pressed Control-D.
			 */
			exit(0);
		case -1:
			warn(_("fork failed"));
			/* fall through */
		default:
			break;
		}

		ptr = ptr->next;

	} while (ptr != &consoles);

	while ((pid = wait(&status))) {
		if (errno == ECHILD)
			break;
		if (pid < 0)
			continue;
		list_for_each(ptr, &consoles) {
			con = list_entry(ptr, struct console, entry);
			if (con->pid == pid) {
				*usemask &= ~(1<<con->id);
				continue;
			}
			if (kill(con->pid, 0) < 0) {
				*usemask &= ~(1<<con->id);
				continue;
			}
			if (*usemask & (1<<con->id))
				continue;
			kill(con->pid, SIGHUP);
			usleep(50000);
			kill(con->pid, SIGKILL);
		}
	}

	mask_signal(SIGCHLD, SIG_DFL, NULL);
	return EXIT_SUCCESS;
}
