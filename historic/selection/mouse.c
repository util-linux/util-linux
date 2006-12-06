/* simple driver for serial mouse */
/* Andrew Haylett, 17th June 1993 */

#include <unistd.h>
#include <stdlib.h>
#include <termios.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>

#include "mouse.h"

#define DEF_MDEV	"/dev/mouse"
#define DEF_MTYPE	P_MS
#define DEF_MBAUD	1200
#define DEF_MSAMPLE	100
#define DEF_MDELTA	25
#define DEF_MACCEL	2
#define DEF_SLACK	-1

/* thse settings may be altered by the user */
static char *mdev = DEF_MDEV;		/* mouse device */
static mouse_type mtype = DEF_MTYPE;	/* mouse type */
static int mbaud = DEF_MBAUD;		/* mouse device baud rate */
static int msample = DEF_MSAMPLE;	/* sample rate for Logitech mice */
static int mdelta = DEF_MDELTA;		/* x+y movements more than mdelta pixels..*/
static int maccel = DEF_MACCEL;		/* ..are multiplied by maccel. */
static int slack = DEF_SLACK;		/* < 0 ? no wraparound : amount of slack */
int ms_copy_button = MS_BUTLEFT,
    ms_paste_button = MS_BUTRIGHT;

static char *progname;

static void
ms_usage()
{
    printf(
	"Selection version 1.5, 17th June 1993\n"
	"Usage: %s [-a accel] [-b baud-rate] [-c l|m|r] [-d delta]\n"
	"       [-m mouse-device] [-p l|m|r] [-s sample-rate] [-t mouse-type]\n"
	"       [-w slack]\n\n", progname);
    printf(
	"    -a accel         sets the acceleration (default %d)\n"
	"    -b baud-rate     sets the baud rate (default %d)\n"
	"    -c l|m|r         sets the copy button (default `l')\n"
	"    -d delta         sets the delta value (default %d)\n"
	"    -m mouse-device  sets mouse device (default `%s')\n"
	"    -p l|m|r         sets the paste button (default `r')\n"
	"    -s sample-rate   sets the sample rate (default %d)\n"
	"    -t mouse-type    sets mouse type (default `ms')\n"
	"                     Microsoft = `ms', Mouse Systems Corp = `msc',\n"
	"                     MM Series = `mm', Logitech = `logi', BusMouse = `bm',\n"
	"                     MSC 3-bytes = `sun', PS/2 = `ps2')\n"
	"    -w slack         turns on wrap-around and specifies slack (default off)\n",
	DEF_MACCEL, DEF_MBAUD, DEF_MDELTA, DEF_MDEV, DEF_MSAMPLE);
    exit(1);
}

extern int optind;
extern char *optarg;

void
ms_params(int argc, char *argv[])
{
    int opt;

    progname = (rindex(argv[0], '/')) ? rindex(argv[0], '/') + 1 : argv[0];
    while ((opt = getopt(argc, argv, "a:b:c:d:m:p:s:t:w:")) != -1)
    {
	switch (opt)
	{
	    case 'a':
		maccel = atoi(optarg);
		if (maccel < 2)
		    ms_usage();
		break;
	    case 'b':
		mbaud = atoi(optarg);
		break;
	    case 'c':
	        switch (*optarg)
	        {
	            case 'l':	ms_copy_button = MS_BUTLEFT; break;
	            case 'm':	ms_copy_button = MS_BUTMIDDLE; break;
	            case 'r':	ms_copy_button = MS_BUTRIGHT; break;
	            default:	ms_usage(); break;
	        }
		break;
	    case 'd':
		mdelta = atoi(optarg);
		if (mdelta < 2)
		    ms_usage();
		break;
	    case 'm':
		mdev = optarg;
		break;
	    case 'p':
	        switch (*optarg)
	        {
	            case 'l':	ms_paste_button = MS_BUTLEFT; break;
	            case 'm':	ms_paste_button = MS_BUTMIDDLE; break;
	            case 'r':	ms_paste_button = MS_BUTRIGHT; break;
	            default:	ms_usage(); break;
	        }
		break;
	    case 's':
		msample = atoi(optarg);
		break;
	    case 't':
		if (!strcmp(optarg, "ms"))
		    mtype = P_MS;
		else if (!strcmp(optarg, "sun"))
		    mtype = P_SUN;
		else if (!strcmp(optarg, "msc"))
		    mtype = P_MSC;
		else if (!strcmp(optarg, "mm"))
		    mtype = P_MM;
		else if (!strcmp(optarg, "logi"))
		    mtype = P_LOGI;
		else if (!strcmp(optarg, "bm"))
		    mtype = P_BM;
		else if (!strcmp(optarg, "ps2"))
		    mtype = P_PS2;
		else
		    ms_usage();
		break;
	    case 'w':
		slack = atoi (optarg);
		break;
	    default:
		ms_usage();
		break;
	}
    }
}

#define limit(n,l,u,s)	n = ((s) < 0 ? \
	(((n) < (l) ? (l) : ((n) > (u) ? (u) : (n)))) : \
	(((n) < (l-s) ? (u) : ((n) > (u+s) ? (l) : (n)))))

static int mx = 32767;
static int my = 32767;
static int x, y;
static int mfd = -1;

static const unsigned short cflag[NR_TYPES] =
{
      (CS7                   | CREAD | CLOCAL | HUPCL ),   /* MicroSoft */
      (CS8 | CSTOPB          | CREAD | CLOCAL | HUPCL ),   /* MouseSystems 3 */
      (CS8 | CSTOPB          | CREAD | CLOCAL | HUPCL ),   /* MouseSystems 5 */
      (CS8 | PARENB | PARODD | CREAD | CLOCAL | HUPCL ),   /* MMSeries */
      (CS8 | CSTOPB          | CREAD | CLOCAL | HUPCL ),   /* Logitech */
      0,                                                   /* BusMouse */
      0                                                    /* PS/2 */
};

static const unsigned char proto[NR_TYPES][5] =
{
    /*  hd_mask hd_id   dp_mask dp_id   nobytes */
    { 	0x40,	0x40,	0x40,	0x00,	3 	},  /* MicroSoft */
    {	0xf8,	0x80,	0x00,	0x00,	3	},  /* MouseSystems 3 (Sun) */
    {	0xf8,	0x80,	0x00,	0x00,	5	},  /* MouseSystems 5 */
    {	0xe0,	0x80,	0x80,	0x00,	3	},  /* MMSeries */
    {	0xe0,	0x80,	0x80,	0x00,	3	},  /* Logitech */
    {	0xf8,	0x80,	0x00,	0x00,	5	},  /* BusMouse */
    {   0xcc,	0x00,	0x00,	0x00,	3	}   /* PS/2 */
};

static void
ms_setspeed(const int old, const int new,
            const unsigned short c_cflag)
{
    struct termios tty;
    char *c;

    tcgetattr(mfd, &tty);
    
    tty.c_iflag = IGNBRK | IGNPAR;
    tty.c_oflag = 0;
    tty.c_lflag = 0;
    tty.c_line = 0;
    tty.c_cc[VTIME] = 0;
    tty.c_cc[VMIN] = 1;

    switch (old)
    {
    	case 9600:	tty.c_cflag = c_cflag | B9600; break;
    	case 4800:	tty.c_cflag = c_cflag | B4800; break;
    	case 2400:	tty.c_cflag = c_cflag | B2400; break;
    	case 1200:
	default:	tty.c_cflag = c_cflag | B1200; break;
    }

    tcsetattr(mfd, TCSAFLUSH, &tty);

    switch (new)
    {
    	case 9600:	c = "*q";  tty.c_cflag = c_cflag | B9600; break;
    	case 4800:	c = "*p";  tty.c_cflag = c_cflag | B4800; break;
    	case 2400:	c = "*o";  tty.c_cflag = c_cflag | B2400; break;
    	case 1200:
	default:	c = "*n";  tty.c_cflag = c_cflag | B1200; break;
    }

    write(mfd, c, 2);
    usleep(100000);
    tcsetattr(mfd, TCSAFLUSH, &tty);
}

int
ms_init(const int maxx, const int maxy)
{
    if ((mfd = open(mdev, O_RDWR)) < 0)
    {
    	char buf[32];
	sprintf(buf, "ms_init: %s", mdev);
	perror(buf);
	return -1;
    }

    if (mtype != P_BM && mtype != P_PS2)
    {
	ms_setspeed(9600, mbaud, cflag[mtype]);
	ms_setspeed(4800, mbaud, cflag[mtype]);
	ms_setspeed(2400, mbaud, cflag[mtype]);
	ms_setspeed(1200, mbaud, cflag[mtype]);

	if (mtype == P_LOGI)
	{
	    write(mfd, "S", 1);
	    ms_setspeed(mbaud, mbaud, cflag[P_MM]);
	}

	if	(msample <= 0)		write(mfd, "O", 1);
	else if	(msample <= 15)		write(mfd, "J", 1);
	else if	(msample <= 27)		write(mfd, "K", 1);
	else if	(msample <= 42)		write(mfd, "L", 1);
	else if	(msample <= 60)		write(mfd, "R", 1);
	else if	(msample <= 85)		write(mfd, "M", 1);
	else if	(msample <= 125)	write(mfd, "Q", 1);
	else				write(mfd, "N", 1);
    }

    mx = maxx;
    my = maxy;
    x = mx / 2;
    y = my / 2;
    return 0;
}

int
get_ms_event(struct ms_event *ev)
{
    unsigned char buf[5];
    char dx, dy;
    int i, acc;

    if (mfd == -1)
	return -1;
    if (mtype != P_BM)
    {
	if (read(mfd, &buf[0], 1) != 1)
    	    return -1;
restart:
	/* find a header packet */
	while ((buf[0] & proto[mtype][0]) != proto[mtype][1])
	{
	    if (read(mfd, &buf[0], 1) != 1)
	    {
	    	perror("get_ms_event: read");
		return -1;
	    }
	}

	/* read in the rest of the packet */
	for (i = 1; i < proto[mtype][4]; ++i)
	{
	    if (read(mfd, &buf[i], 1) != 1)
	    {
	    	perror("get_ms_event: read");
		return -1;
	    }
	/* check whether it's a data packet */
	    if (mtype != P_PS2 && ((buf[i] & proto[mtype][2]) != proto[mtype][3]
		    || buf[i] == 0x80))
		goto restart;
	}
    }
    else	/* bus mouse */
    {
	while ((i = read(mfd, buf, 3)) != 3 && errno == EAGAIN)
	    usleep(40000);
	if (i != 3)
	{
	    perror("get_ms_event: read");
	    return -1;
	}
    }

/* construct the event */
    switch (mtype)
    {
	case P_MS:		/* Microsoft */
	default:
	    ev->ev_butstate = ((buf[0] & 0x20) >> 3) | ((buf[0] & 0x10) >> 4);
	    dx = (char)(((buf[0] & 0x03) << 6) | (buf[1] & 0x3F));
	    dy = (char)(((buf[0] & 0x0C) << 4) | (buf[2] & 0x3F));
	    break;
        case P_SUN:		/* Mouse Systems 3 byte as used in Sun workstations */
	    ev->ev_butstate = (~buf[0]) & 0x07;
	    dx =  (char)(buf[1]);
	    dy = -(char)(buf[2]);
	    break;
	case P_MSC:             /* Mouse Systems Corp (5 bytes, PC) */
	    ev->ev_butstate = (~buf[0]) & 0x07;
	    dx =    (char)(buf[1]) + (char)(buf[3]);
	    dy = - ((char)(buf[2]) + (char)(buf[4]));
	    break;
	case P_MM:              /* MM Series */
	case P_LOGI:            /* Logitech */
	    ev->ev_butstate = buf[0] & 0x07;
	    dx = (buf[0] & 0x10) ?   buf[1] : - buf[1];
	    dy = (buf[0] & 0x08) ? - buf[2] :   buf[2];
	    break;
	case P_BM:              /* BusMouse */
	    ev->ev_butstate = (~buf[0]) & 0x07;
	    dx =   (char)buf[1];
	    dy = - (char)buf[2];
	    break;
	case P_PS2:            /* PS/2 Mouse */
	    ev->ev_butstate = 0;
	    if (buf[0] & 0x01)
		ev->ev_butstate |= MS_BUTLEFT;
	    if (buf[0] & 0x02)
		ev->ev_butstate |= MS_BUTRIGHT;
	    dx =    (buf[0] & 0x10) ? buf[1]-256 : buf[1];
	    dy = - ((buf[0] & 0x20) ? buf[2]-256 : buf[2]);
	    break;
    }

    acc = (abs(ev->ev_dx) + abs(ev->ev_dy) > mdelta) ? maccel : 1;
    ev->ev_dx = dx * acc;
    ev->ev_dy = dy * acc;
    x += ev->ev_dx;
    y += ev->ev_dy;
    limit(x, 0, mx, (int) (slack * mx / my));
    limit(y, 0, my, slack);
    ev->ev_x = x;
    ev->ev_y = y;
    limit(ev->ev_x, 0, mx, -1);
    limit(ev->ev_y, 0, my, -1);
    if (dx || dy)
    {
	if (ev->ev_butstate)
	    ev->ev_code = MS_DRAG;
	else
	    ev->ev_code = MS_MOVE;
    }
    else
    {
	if (ev->ev_butstate)
	    ev->ev_code = MS_BUTDOWN;
	else
	    ev->ev_code = MS_BUTUP;
    }
    return 0;
}
