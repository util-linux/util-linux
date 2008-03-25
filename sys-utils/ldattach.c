/* line discipline loading daemon
 * open a serial device and attach a line discipline on it
 *
 * Usage:
 *	ldattach GIGASET_M101 /dev/ttyS0
 *
 * =====================================================================
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 * =====================================================================
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <getopt.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <asm/termbits.h>
#include <unistd.h>
#include <err.h>

#include "nls.h"

#define dbg(format, arg...) \
	do { if (debug) fprintf(stderr , "%s:" format "\n" , progname , ## arg); } while (0)

#ifndef N_GIGASET_M101
#define N_GIGASET_M101 16
#endif

#ifndef PACKAGE_STRING
#define PACKAGE_STRING "me"
#endif

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#endif

static const char *progname;
static int debug = 0;

/* currently supported line disciplines, plus some aliases */
static const struct ld_entry { const char *s; int v; }
ld_table[] = {
	{ "TTY",	N_TTY },
	{ "SLIP",	N_SLIP },
	{ "MOUSE",	N_MOUSE },
	{ "PPP",	N_PPP },
	{ "STRIP",	N_STRIP },
	{ "AX25",	N_AX25 },
	{ "X25",	N_X25 },
	{ "6PACK",	N_6PACK },
	{ "R3964",	N_R3964 },
	{ "IRDA",	N_IRDA },
	{ "HDLC",	N_HDLC },
	{ "SYNC_PPP",	N_SYNC_PPP },
	{ "SYNCPPP",	N_SYNC_PPP },
	{ "HCI",	N_HCI },
	{ "GIGASET_M101",	N_GIGASET_M101 },
	{ "GIGASET",	N_GIGASET_M101 },
	{ "M101",	N_GIGASET_M101 }
};

/* look up line discipline code */
static int lookup_ld(const char *s)
{
    size_t i;

    for (i = 0; i < ARRAY_SIZE(ld_table); i++)
	if (!strcasecmp(ld_table[i].s, s))
	    return ld_table[i].v;
    return -1;
}

/* replacement for tcsetattr(3) and friends supporting arbitrary speed values */

/* some archs don't have separate struct termios2 */
#ifndef TCGETS2
#define termios2 termios
#define TCGETS2 TCGETS
#define TCSETS2 TCSETS
#define TCSETSW2 TCSETSW
#define TCSETSF2 TCSETSF
#endif

static int tcgetattr2(int fd, struct termios2 *pts)
{
    return ioctl(fd, TCGETS2, pts);
}

static int tcsetattr2(int fd, int option, const struct termios2 *pts)
{
    int request;

    switch (option) {
    case TCSANOW:
	request = TCSETS2;
	break;
    case TCSADRAIN:
	request = TCSETSW2;
	break;
    case TCSAFLUSH:
	request = TCSETSF2;
	break;
    default:
	errno = -EINVAL;
	return -1;
    }
    return ioctl(fd, request, pts);
}

static void cfmakeraw2(struct termios2 *pts)
{
    pts->c_iflag &= ~(IGNBRK|BRKINT|PARMRK|ISTRIP|INLCR|IGNCR|ICRNL|IXON);
    pts->c_oflag &= ~OPOST;
    pts->c_lflag &= ~(ECHO|ECHONL|ICANON|ISIG|IEXTEN);
    pts->c_cflag &= ~(CSIZE|PARENB);
    pts->c_cflag |= CS8;
}

/* table of standard line speeds */
static const struct speed_entry { int s; speed_t v; }
speed_table[] = {
	{ 50,     B50 },
	{ 75,     B75 },
	{ 110,    B110 },
	{ 134,    B134 },
	{ 150,    B150 },
	{ 200,    B200 },
	{ 300,    B300 },
	{ 600,    B600 },
	{ 1200,   B1200 },
	{ 1800,   B1800 },
	{ 2400,   B2400 },
	{ 4800,   B4800 },
	{ 9600,   B9600 },
	{ 19200,  B19200 },
	{ 38400,  B38400 }
#ifdef B57600
	,{ 57600,  B57600 }
#endif
#ifdef B115200
	,{ 115200, B115200 }
#endif
#ifdef B230400
	,{ 230400, B230400 }
#endif
};

static int cfsetspeed2(struct termios2 *pts, int speed)
{
    size_t i;

    /* try POSIX method first */
    for (i = 0; i < ARRAY_SIZE(speed_table); i++)
	if (speed_table[i].s == speed) {
	    pts->c_cflag &= ~CBAUD;
	    pts->c_cflag |= speed_table[i].v;
	    return 0;
	}

#ifdef BOTHER
    /* new method available */
    pts->c_ospeed = pts->c_ispeed = speed;
    pts->c_cflag &= ~CBAUD;
    pts->c_cflag |= BOTHER;
    return 0;
#else
    /* new method not available */
    return -1;
#endif
}

static void __attribute__((__noreturn__)) usage(int exitcode)
{
    size_t i;

    fprintf(stderr,
	    _("\nUsage: %s [ -dhV78neo12 ] [ -s <speed> ] <ldisc> <device>\n"),
	    progname);
    fputs(_("\nKnown <ldisc> names:\n"), stderr);
    for (i = 0; i < ARRAY_SIZE(ld_table); i++)
	fprintf(stderr, "  %s\n", ld_table[i].s);
    exit(exitcode);
}

int main(int argc, char **argv)
{
    int tty_fd;
    struct termios2 ts;
    int speed = 0, bits = '-', parity = '-', stop = '-';
    int ldisc;
    int optc;
    char *end;
    char *dev;
    static const struct option opttbl[] = {
	{"speed", 1, 0, 's'},
	{"sevenbits", 0, 0, '7'},
	{"eightbits", 0, 0, '8'},
	{"noparity", 0, 0, 'n'},
	{"evenparity", 0, 0, 'e'},
	{"oddparity", 0, 0, 'o'},
	{"onestopbit", 0, 0, '1'},
	{"twostopbits", 0, 0, '2'},
	{"help", 0, 0, 'h'},
	{"version", 0, 0, 'V'},
	{"debug", 0, 0, 'd'},
	{0, 0, 0, 0}
    };


    setlocale(LC_ALL, "");
    bindtextdomain(PACKAGE, LOCALEDIR);
    textdomain(PACKAGE);

    /* parse options */
    progname = program_invocation_short_name;

    if (argc == 0)
	usage(EXIT_SUCCESS);
    while ((optc = getopt_long(argc, argv, "dhV78neo12s:", opttbl, NULL)) >= 0) {
	switch (optc) {
	case 'd':
	    debug++;
	    break;
	case '1':
	case '2':
	    stop = optc;
	    break;
	case '7':
	case '8':
	    bits = optc;
	    break;
	case 'n':
	case 'e':
	case 'o':
	    parity = optc;
	    break;
	case 's':
	    speed = strtol(optarg, &end, 10);
	    if (*end || speed <= 0)
		errx(EXIT_FAILURE, _("invalid speed: %s"), optarg);
	    break;
	case 'V':
	    printf(_("ldattach from %s\n"), PACKAGE_STRING);
	    break;
	case 'h':
	    usage(EXIT_SUCCESS);
	default:
	    warnx(_("invalid option"));
	    usage(EXIT_FAILURE);
	}
    }

    if (argc - optind != 2)
	usage(EXIT_FAILURE);

    /* parse line discipline specification */
    if ((ldisc = lookup_ld(argv[optind])) < 0) {
	ldisc = strtol(argv[optind], &end, 0);
	if (*end || ldisc < 0)
	    errx(EXIT_FAILURE, _("invalid line discipline: %s"), argv[optind]);
    }

    /* open device */
    dev = argv[optind+1];
    if ((tty_fd = open(dev, O_RDWR|O_NOCTTY)) < 0)
	err(EXIT_FAILURE, _("cannot open %s"), dev);
    if (!isatty(tty_fd))
	errx(EXIT_FAILURE, _("%s is not a serial line"), dev);

    dbg("opened %s", dev);

    /* set line speed and format */
    if (tcgetattr2(tty_fd, &ts) < 0)
	err(EXIT_FAILURE, _("cannot get terminal attributes for %s"), dev);
    cfmakeraw2(&ts);
    if (speed && cfsetspeed2(&ts, speed) < 0)
	errx(EXIT_FAILURE, _("speed %d unsupported"), speed);
    switch (stop) {
    case '1':
	ts.c_cflag &= ~CSTOPB;
	break;
    case '2':
	ts.c_cflag |= CSTOPB;
	break;
    }
    switch (bits) {
    case '7':
	ts.c_cflag = (ts.c_cflag & ~CSIZE) | CS7;
	break;
    case '8':
	ts.c_cflag = (ts.c_cflag & ~CSIZE) | CS8;
	break;
    }
    switch (parity) {
    case 'n':
	ts.c_cflag &= ~(PARENB|PARODD);
	break;
    case 'e':
	ts.c_cflag |= PARENB;
	ts.c_cflag &= ~PARODD;
	break;
    case 'o':
	ts.c_cflag |= (PARENB|PARODD);
	break;
    }
    ts.c_cflag |= CREAD;	/* just to be on the safe side */
    if (tcsetattr2(tty_fd, TCSAFLUSH, &ts) < 0)
	err(EXIT_FAILURE, _("cannot set terminal attributes for %s"), dev);

    dbg("set to raw %d %c%c%c: cflag=0x%x",
	speed, bits, parity, stop, ts.c_cflag);

    /* Attach the line discpline. */
    if (ioctl(tty_fd, TIOCSETD, &ldisc) < 0)
	err(EXIT_FAILURE, _("cannot set line discipline"));

    dbg("line discipline set to %d", ldisc);

    /* Go into background if not in debug mode. */
    if (!debug && daemon(0, 0) < 0)
	err(EXIT_FAILURE, _("cannot daemonize"));

    /* Sleep to keep the line discipline active. */
    pause();

    exit(EXIT_SUCCESS);
}
