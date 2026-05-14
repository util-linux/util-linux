#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

#include "all-io.h"
#include "agetty.h"
#include "c.h"
#include "cctype.h"
#include "nls.h"
#include "ttyutils.h"

#ifdef USE_PLYMOUTH_SUPPORT
# include "plymouth-ctrl.h"
#endif

#ifdef __linux__
# include <sys/kd.h>
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

struct Speedtab {
	long speed;
	speed_t code;
};

#define	FIRST_SPEED	0

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

speed_t agetty_bcode(char *s)
{
	const struct Speedtab *sp;
	char *end = NULL;
	long speed;

	errno = 0;
	speed = strtol(s, &end, 10);

	if (errno || !end || end == s)
		return 0;

	for (sp = speedtab; sp->speed; sp++)
		if (sp->speed == speed)
			return sp->code;
	return 0;
}

void agetty_list_speeds(void)
{
	const struct Speedtab *sp;

	for (sp = speedtab; sp->speed; sp++)
		printf("%10ld\n", sp->speed);
}

void agetty_fprint_speed(FILE *out, speed_t speed)
{
	int i;

	for (i = 0; speedtab[i].speed; i++) {
		if (speedtab[i].code == speed) {
			fprintf(out, "%ld", speedtab[i].speed);
			break;
		}
	}
}

void agetty_termio_clear(int fd)
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

void agetty_reset_vc(const struct agetty_options *op, struct termios *tp, int canon)
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

void agetty_open_tty(const char *tty, struct termios *tp, struct agetty_options *op)
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

void agetty_termio_init(struct agetty_options *op, struct termios *tp)
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
		agetty_reset_vc(op, tp, 0);

		if ((tp->c_cflag & (CS8|PARODD|PARENB)) == CS8)
			op->flags |= F_EIGHTBITS;

		if ((op->flags & F_NOCLEAR) == 0)
			agetty_termio_clear(STDOUT_FILENO);
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

	/* The default is set c_iflag in agetty_termio_final() according to
	 * chardata. Unfortunately, the chardata are not set according to the
	 * serial line if --autolog is enabled. In this case we do not read
	 * from the line at all. The best what we can do in this case is to
	 * keep c_iflag unmodified for --autolog.
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

void agetty_termio_final(struct agetty_options *op, struct termios *tp,
			 struct chardata *cp)
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

void agetty_auto_baud(struct termios *tp)
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

void agetty_next_speed(struct agetty_options *op, struct termios *tp)
{
	static int baud_index = -1;

	if (baud_index == -1)
		/*
		 * If the F_KEEPSPEED flags is set then the FIRST_SPEED is not
		 * tested yet (see agetty_termio_init()).
		 */
		baud_index =
		    (op->flags & F_KEEPSPEED) ? FIRST_SPEED : 1 % op->numspeed;
	else
		baud_index = (baud_index + 1) % op->numspeed;

	cfsetispeed(tp, op->speeds[baud_index]);
	cfsetospeed(tp, op->speeds[baud_index]);
	tcsetattr(STDIN_FILENO, TCSANOW, tp);
}

void agetty_erase_char(int visual_count, struct chardata *cp)
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
