/* setserial.c - get/set Linux serial port info - rick sladkey */
/* modified to do work again and added setting fast serial speeds,
   Michael K. Johnson, johnsonm@stolaf.edu */
/*
 * Very heavily modified --- almost rewritten from scratch --- to have
 * a more flexible command structure.  Now able to set any of the
 * serial-specific options using the TIOCSSERIAL ioctl().
 * 			Theodore Ts'o, tytso@mit.edu, 1/1/93
 *
 * Last modified: [tytso:19940520.0036EDT]
 */

#include <stdio.h>
#include <fcntl.h>
#include <termios.h>
#include <string.h>
#include <errno.h>

#include <linux/fs.h>
#include <linux/serial.h>
#include <linux/tty.h>

#define VERSION_STR "2.10"

char *progname;

int	verbosity = 1;		/* 1 = normal, 0=boot-time, 2=everything */
int	verbose_flag = 0;	/* print results after setting a port */
int	quiet_flag = 0;

struct serial_type_struct {
	int id;
	char *name;
} serial_type_tbl[] = {
	PORT_UNKNOWN,	"unknown",
	PORT_8250,	"8250",
	PORT_16450,	"16450",
	PORT_16550,	"16550",
	PORT_16550A,	"16550A",
	PORT_UNKNOWN,	"none",
	-1,		NULL
};

#define CMD_FLAG	1
#define CMD_PORT	2
#define CMD_IRQ		3
#define CMD_DIVISOR	4
#define CMD_TYPE	5
#define CMD_BASE	6
#define CMD_DELAY	7
#define CMD_CONFIG	8

#define FLAG_CAN_INVERT	0x0001
#define FLAG_NEED_ARG	0x0002

struct flag_type_table {
	int	cmd;
	char	*name;
	int	bits;
	int	mask;
	int	level;
	int	flags;
} flag_type_tbl[] = {
	CMD_FLAG,	"spd_normal",	0,		ASYNC_SPD_MASK,	2, 0,
	CMD_FLAG,	"spd_hi",	ASYNC_SPD_HI, 	ASYNC_SPD_MASK, 0, 0,
	CMD_FLAG,	"spd_vhi",	ASYNC_SPD_VHI,	ASYNC_SPD_MASK,	0, 0,
	CMD_FLAG,	"spd_cust",	ASYNC_SPD_CUST,	ASYNC_SPD_MASK,	0, 0,
	
	CMD_FLAG, 	"SAK", 		ASYNC_SAK, 	ASYNC_SAK, 	0, FLAG_CAN_INVERT,
	CMD_FLAG,	"Fourport",	ASYNC_FOURPORT, ASYNC_FOURPORT,	0, FLAG_CAN_INVERT,
	CMD_FLAG,	"hup_notify",	ASYNC_HUP_NOTIFY, ASYNC_HUP_NOTIFY, 0, FLAG_CAN_INVERT,
	CMD_FLAG,	"skip_test",	ASYNC_SKIP_TEST,ASYNC_SKIP_TEST,2, FLAG_CAN_INVERT,
	CMD_FLAG,	"auto_irq",	ASYNC_AUTO_IRQ,	ASYNC_AUTO_IRQ,	2, FLAG_CAN_INVERT,
#ifdef ASYNC_SPLIT_TERMIOS
	CMD_FLAG,	"split_termios", ASYNC_SPLIT_TERMIOS, ASYNC_SPLIT_TERMIOS, 2, FLAG_CAN_INVERT,
#endif
	CMD_FLAG,	"session_lockout", ASYNC_SESSION_LOCKOUT, ASYNC_SESSION_LOCKOUT, 2, FLAG_CAN_INVERT,
	CMD_FLAG,	"pgrp_lockout", ASYNC_PGRP_LOCKOUT, ASYNC_PGRP_LOCKOUT, 2, FLAG_CAN_INVERT,
#ifdef ASYNC_CALLOUT_NOHUP
	CMD_FLAG,	"callout_nohup", ASYNC_CALLOUT_NOHUP, ASYNC_CALLOUT_NOHUP, 2, FLAG_CAN_INVERT,
#endif	
	
	CMD_PORT,	"port",		0,		0,		0, FLAG_NEED_ARG,
	CMD_IRQ,	"irq",		0,		0,		0, FLAG_NEED_ARG,
	CMD_DIVISOR,	"divisor",	0,		0,		0, FLAG_NEED_ARG,
	CMD_TYPE,	"uart",		0,		0,		0, FLAG_NEED_ARG,
	CMD_BASE,	"base",		0,		0,		0, FLAG_NEED_ARG,
	CMD_BASE,	"baud_base",	0,		0,		0, FLAG_NEED_ARG,
	CMD_DELAY,	"close_delay",	0,		0,		0, FLAG_NEED_ARG,
	CMD_CONFIG,	"autoconfig",	0,		0,		0, 0,
	0,		0,		0,		0,		0, 0,
};
	
char *serial_type(int id)
{
	int i;

	for (i = 0; serial_type_tbl[i].id != -1; i++)
		if (id == serial_type_tbl[i].id)
			return serial_type_tbl[i].name;
	return "undefined";
}

int uart_type(char *name)
{
	int i;

	for (i = 0; serial_type_tbl[i].id != -1; i++)
		if (!strcasecmp(name, serial_type_tbl[i].name))
			return serial_type_tbl[i].id;
	return -1;
}


int atonum(char *s)
{
	int n;

	while (*s == ' ')
		s++;
	if (strncmp(s, "0x", 2) == 0 || strncmp(s, "0X", 2) == 0)
		sscanf(s + 2, "%x", &n);
	else if (s[0] == '0' && s[1])
		sscanf(s + 1, "%o", &n);
	else
		sscanf(s, "%d", &n);
	return n;
}

void print_flags(struct serial_struct *serinfo,
		 char *prefix, char *postfix)
{
	struct	flag_type_table	*p;
	int	flags;
	int	first = 1;

	flags = serinfo->flags;
	
	for (p = flag_type_tbl; p->name; p++) {
		if (p->cmd != CMD_FLAG)
			continue;
		if (verbosity < p->level)
			continue;
		if ((flags & p->mask) == p->bits) {
			if (first) {
				printf("%s", prefix);
				first = 0;
			} else
				printf(" ");
			printf("%s", p->name);
		}
	}
	
	if (!first)
		printf("%s", postfix);
}

void get_serial(char *device)
{
	struct serial_struct serinfo;
	int	fd;

	if ((fd = open(device, O_RDWR|O_NONBLOCK)) < 0) {
		perror(device);
		return;
	}
	if (ioctl(fd, TIOCGSERIAL, &serinfo) < 0) {
		perror("Cannot get serial info");
		close(fd);
		return;
	}
	if (serinfo.irq == 9)
		serinfo.irq = 2;	/* People understand 2 better than 9 */
	if (verbosity==2) {
		printf("%s, Line %d, UART: %s, Port: 0x%.4x, IRQ: %d\n",
		       device, serinfo.line, serial_type(serinfo.type),
		       serinfo.port, serinfo.irq);
		printf("\tBaud_base: %d, close_delay: %d, divisor: %d\n",
		       serinfo.baud_base, serinfo.close_delay,
		       serinfo.custom_divisor);
		print_flags(&serinfo, "\tFlags: ", "");
		printf("\n\n");
	} else if (verbosity==0) {
		if (serinfo.type) {
			printf("%s at 0x%.4x (irq = %d) is a %s",
			       device, serinfo.port, serinfo.irq,
			       serial_type(serinfo.type));
			print_flags(&serinfo, " (", ")");
			printf("\n");
		}
	} else {
		printf("%s, UART: %s, Port: 0x%.4x, IRQ: %d",
		       device, serial_type(serinfo.type),
		       serinfo.port, serinfo.irq);
		print_flags(&serinfo, ", Flags: ", "");
		printf("\n");
	}
	close(fd);
}

void set_serial(char *device, char ** arg)
{
	struct serial_struct old_serinfo, new_serinfo;
	struct	flag_type_table	*p;
	int	fd;
	int	do_invert = 0;
	char	*word;
	

	if ((fd = open(device, O_RDWR|O_NONBLOCK)) < 0) {
		if (verbosity==0 && errno==ENOENT)
			exit(201);
		perror(device);
		exit(201);
	}
	if (ioctl(fd, TIOCGSERIAL, &old_serinfo) < 0) {
		perror("Cannot get serial info");
		exit(1);
	}
	new_serinfo = old_serinfo;
	while (*arg) {
		do_invert = 0;
		word = *arg++;
		if (*word == '^') {
			do_invert++;
			word++;
		}
		for (p = flag_type_tbl; p->name; p++) {
			if (!strcasecmp(p->name, word))
				break;
		}
		if (!p->name) {
			fprintf(stderr, "Invalid flag: %s\n", word);
			exit(1);
		}
		if (do_invert && !(p->flags & FLAG_CAN_INVERT)) {
			fprintf(stderr, "This flag can not be inverted: %s\n", word);
			exit(1);
		}
		if ((p->flags & FLAG_NEED_ARG) && !*arg) {
			fprintf(stderr, "Missing argument for %s\n", word);
			exit(1);
		}
		switch (p->cmd) {
		case CMD_FLAG:
			new_serinfo.flags &= ~p->mask;
			if (!do_invert)
				new_serinfo.flags |= p->bits;
			break;
		case CMD_PORT:
			new_serinfo.port = atonum(*arg++);
			break;
		case CMD_IRQ:
			new_serinfo.irq = atonum(*arg++);
			break;
		case CMD_DIVISOR:
			new_serinfo.custom_divisor = atonum(*arg++);
			break;
		case CMD_TYPE:
			new_serinfo.type = uart_type(*arg++);
			if (new_serinfo.type < 0) {
				fprintf(stderr, "Illegal UART type: %s", *--arg);
				exit(1);
			}
			break;
		case CMD_BASE:
			new_serinfo.baud_base = atonum(*arg++);
			break;
		case CMD_DELAY:
			new_serinfo.close_delay = atonum(*arg++);
			break;
		case CMD_CONFIG:
			if (ioctl(fd, TIOCSSERIAL, &new_serinfo) < 0) {
				perror("Cannot set serial info");
				exit(1);
			}
			if (ioctl(fd, TIOCSERCONFIG) < 0) {
				perror("Cannot autoconfigure port");
				exit(1);
			}
			if (ioctl(fd, TIOCGSERIAL, &new_serinfo) < 0) {
				perror("Cannot get serial info");
				exit(1);
			}
			break;
		default:
			fprintf(stderr, "Internal error: unhandled cmd #%d\n", p->cmd);
			exit(1);
		}
	}
	if (ioctl(fd, TIOCSSERIAL, &new_serinfo) < 0) {
		perror("Cannot set serial info");
		exit(1);
	}
	close(fd);
	if (verbose_flag)
		get_serial(device);
}

void do_wild_intr(char *device)
{
	int	fd;
	int	i, mask;
	int	wild_mask = -1;
	
	if ((fd = open(device, O_RDWR|O_NONBLOCK)) < 0) {
		perror(device);
		exit(1);
	}
	if (ioctl(fd, TIOCSERSWILD, &wild_mask) < 0) {
		perror("Cannot scan for wild interrupts");
		exit(1);
	}
	if (ioctl(fd, TIOCSERGWILD, &wild_mask) < 0) {
		perror("Cannot get wild interrupt mask");
		exit(1);
	}
	close(fd);
	if (quiet_flag)
		return;
	if (wild_mask) {
		printf("Wild interrupts found: ");
		for (i=0, mask=1; mask <= wild_mask; i++, mask <<= 1)
			if (mask & wild_mask)
				printf(" %d", i);
		printf("\n");
	} else if (verbose_flag)
		printf("No wild interrupts found.\n");
	return;
}




void usage()
{
	fprintf(stderr, "setserial Version %s\n\n", VERSION_STR);
	fprintf(stderr,
		"usage: %s serial-device [cmd1 [arg]] ... \n\n", progname);
	fprintf(stderr, "Available commands: (* = Takes an argument)\n");
	fprintf(stderr, "\t\t(^ = can be preceded by a '^' to turn off the option)\n");
fprintf(stderr, "\t* port\t\tset the I/O port\n");
	fprintf(stderr, "\t* irq\t\tset the interrupt\n");	
	fprintf(stderr, "\t* uart\t\tset UART type (none, 8250, 16450, 16550, 16550A\n");
	fprintf(stderr, "\t* baud_base\tset base baud rate (CLOCK_FREQ / 16)\n");
	fprintf(stderr, "\t* divisor\tset the custom divisor (see spd_custom)\n");
	fprintf(stderr, "\t* close_delay\tset the amount of time (in 1/100 of a\n");
	fprintf(stderr, "\t\t\t\tsecond) that DTR should be kept low\n");
	fprintf(stderr, "\t\t\t\twhile being closed\n");
	
	fprintf(stderr, "\t^ fourport\tconfigure the port as an AST Fourport\n");
	fprintf(stderr, "\t  autoconfigure\tautomatically configure the serial port\n");
	fprintf(stderr, "\t^ auto_irq\ttry to determine irq during autoconfiguration\n");
	fprintf(stderr, "\t^ skip_test\tskip UART test during autoconfiguration\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "\t^ sak\t\tset the break key as the Secure Attention Key\n");
	fprintf(stderr, "\t^ session_lockout Lock out callout port across different sessions\n");
	fprintf(stderr, "\t^ pgrp_lockout\tLock out callout port across different process groups\n");
#ifdef ASYNC_CALLOUT_NOHUP
	fprintf(stderr, "\t^ callout_nohup\tDon't hangup the tty when carrier detect drops\n");
#endif
	fprintf(stderr, "\t\t\t\t on the callout device\n");
#ifdef ASYNC_SPLIT_TERMIOS
	fprintf(stderr, "\t^ split_termios Use separate termios for callout and dailin lines\n");
#endif	
	fprintf(stderr, "\t^ hup_notify\tNotify a process blocked on opening a dial in line\n");
	fprintf(stderr, "\t\t\t\twhen a process has finished using a callout\n");
	fprintf(stderr, "\t\t\t\tline by returning EAGAIN to the open.\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "\t  spd_hi\tuse 56kb instead of 38.4kb\n");
	fprintf(stderr, "\t  spd_vhi\tuse 115kb instead of 38.4kb\n");
	fprintf(stderr, "\t  spd_cust\tuse the custom divisor to set the speed at 38.4kb\n");
	fprintf(stderr, "\t\t\t\t(baud rate = baud_base / custom_divisor)\n");
	fprintf(stderr, "\t  spd_normal\tuse 38.4kb when a buad rate of 38.4kb is selected\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "Use a leading '0x' for hex numbers.\n");
	fprintf(stderr, "CAUTION: Using an invalid port can lock up your machine!\n");
	exit(1);
}

main(int argc, char **argv)
{
	int	get_flag = 0, wild_intr_flag = 0;
	int	c;
	extern int optind;
	extern char *optarg;
	
	progname = argv[0];
	if (argc == 1)
		usage();
	while ((c = getopt(argc, argv, "abgqvVW")) != EOF) {
		switch (c) {
		case 'a':
			verbosity = 2;
			break;
		case 'b':
			verbosity = 0;
			break;
		case 'q':
			quiet_flag++;
			break;
		case 'v':
			verbose_flag++;
			break;
		case 'g':
			get_flag++;
			break;
		case 'V':
			fprintf(stderr, "setserial version %s\n", VERSION_STR);
			exit(0);
		case 'W':
			wild_intr_flag++;
			break;
		default:
			usage();
		}
	}
	if (get_flag) {
		argv += optind;
		while (*argv)
			get_serial(*argv++);
		exit(0);
	}
	if (argc == optind)
		usage();
	if (wild_intr_flag) {
		do_wild_intr(argv[optind]);
		exit(0);
	}
	if (argc-optind == 1)
		get_serial(argv[optind]);
	else
		set_serial(argv[optind], argv+optind+1);
	exit(0);
}

