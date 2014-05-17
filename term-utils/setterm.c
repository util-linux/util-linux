/* setterm.c, set terminal attributes.
 *
 * Copyright (C) 1990 Gordon Irlam (gordoni@cs.ua.oz.au).  Conditions of use,
 * modification, and redistribution are contained in the file COPYRIGHT that
 * forms part of this distribution.
 * 
 * Adaption to Linux by Peter MacDonald.
 *
 * Enhancements by Mika Liljeberg (liljeber@cs.Helsinki.FI)
 *
 * Beep modifications by Christophe Jolif (cjolif@storm.gatelink.fr.net)
 *
 * Sanity increases by Cafeine Addict [sic].
 *
 * Powersave features by todd j. derr <tjd@wordsmith.org>
 *
 * Converted to terminfo by Kars de Jong (jongk@cs.utwente.nl)
 *
 * 1999-02-22 Arkadiusz Miśkiewicz <misiek@pld.ORG.PL>
 * - added Native Language Support
 *
 *
 * Syntax:
 *
 * setterm
 *   [ -term terminal_name ]
 *   [ -reset ]
 *   [ -initialize ]
 *   [ -cursor [on|off] ]
 *   [ -repeat [on|off] ]
 *   [ -appcursorkeys [on|off] ]
 *   [ -linewrap [on|off] ]
 *   [ -snow [on|off] ]
 *   [ -softscroll [on|off] ]
 *   [ -defaults ]
 *   [ -foreground black|red|green|yellow|blue|magenta|cyan|white|default ]
 *   [ -background black|red|green|yellow|blue|magenta|cyan|white|default ]
 *   [ -ulcolor black|grey|red|green|yellow|blue|magenta|cyan|white ]
 *   [ -ulcolor bright red|green|yellow|blue|magenta|cyan|white ]
 *   [ -hbcolor black|grey|red|green|yellow|blue|magenta|cyan|white ]
 *   [ -hbcolor bright red|green|yellow|blue|magenta|cyan|white ]
 *   [ -inversescreen [on|off] ]
 *   [ -bold [on|off] ]
 *   [ -half-bright [on|off] ]
 *   [ -blink [on|off] ]
 *   [ -reverse [on|off] ]
 *   [ -underline [on|off] ]
 *   [ -store ]
 *   [ -clear [ all|rest ] ]
 *   [ -tabs [tab1 tab2 tab3 ... ] ]     (tabn = 1-160)
 *   [ -clrtabs [ tab1 tab2 tab3 ... ]   (tabn = 1-160)
 *   [ -regtabs [1-160] ]
 *   [ -blank [0-60|force|poke|] ]
 *   [ -dump   [1-NR_CONS ] ]
 *   [ -append [1-NR_CONS ] ]
 *   [ -file dumpfilename ]
 *   [ -standout [attr] ]
 *   [ -msg [on|off] ]
 *   [ -msglevel [0-8] ]
 *   [ -powersave [on|vsync|hsync|powerdown|off] ]
 *   [ -powerdown [0-60] ]
 *   [ -blength [0-2000] ]
 *   [ -bfreq freq ]
 *   [ -version ]
 *   [ -help ]
 *
 *
 * Semantics:
 *
 * Setterm writes to standard output a character string that will
 * invoke the specified terminal capabilities.  Where possible
 * terminfo is consulted to find the string to use.  Some options
 * however do not correspond to a terminfo capability.  In this case if
 * the terminal type is "con*", or "linux*" the string that invokes
 * the specified capabilities on the PC Linux virtual console driver
 * is output.  Options that are not implemented by the terminal are
 * ignored.
 *
 * The following options are non-obvious.
 *
 *   -term can be used to override the TERM environment variable.
 *
 *   -reset displays the terminal reset string, which typically resets the
 *      terminal to its power on state.
 *
 *   -initialize displays the terminal initialization string, which typically
 *      sets the terminal's rendering options, and other attributes to the
 *      default values.
 *
 *   -default sets the terminal's rendering options to the default values.
 *
 *   -store stores the terminal's current rendering options as the default
 *      values.  */

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/klog.h>
#include <sys/param.h>		/* for MAXPATHLEN */
#include <sys/time.h>
#include <termios.h>
#include <unistd.h>

#ifndef NCURSES_CONST
# define NCURSES_CONST const	/* define before including term.h */
#endif
#ifdef HAVE_NCURSES_H
# include <ncurses.h>
#elif defined(HAVE_NCURSES_NCURSES_H)
# include <ncurses/ncurses.h>
#endif
/* must include after ncurses.h */
#include <term.h>

#ifdef HAVE_LINUX_TIOCL_H
# include <linux/tiocl.h>
#endif

#include "c.h"
#include "closestream.h"
#include "nls.h"
#include "xalloc.h"

/* Constants. */

/* Non-standard return values. */
#define EXIT_DUMPFILE	-1

/* Colors. */
enum {
	BLACK = 0,
	RED,
	GREEN,
	YELLOW,
	BLUE,
	MAGENTA,
	CYAN,
	WHITE,
	GREY,
	DEFAULT
};

/* Blank commands */
enum {
	BLANKSCREEN	= -1,
	UNBLANKSCREEN	= -2,
	BLANKEDSCREEN	= -3
};

/* <linux/tiocl.h> fallback */
#ifndef TIOCL_BLANKSCREEN
enum {
	TIOCL_UNBLANKSCREEN	=  4,	/* unblank screen */
	TIOCL_SETVESABLANK	= 10,	/* set vesa blanking mode */
	TIOCL_BLANKSCREEN	= 14,	/* keep screen blank even if a key is pressed */
	TIOCL_BLANKEDSCREEN	= 15	/* return which vt was blanked */
};
#endif

/* Powersave modes */
enum {
	VESA_BLANK_MODE_OFF = 0,
	VESA_BLANK_MODE_SUSPENDV,
	VESA_BLANK_MODE_SUSPENDH,
	VESA_BLANK_MODE_POWERDOWN
};

/* klogctl() actions */
enum {
	SYSLOG_ACTION_CONSOLE_OFF	= 6,
	SYSLOG_ACTION_CONSOLE_ON	= 7,
	SYSLOG_ACTION_CONSOLE_LEVEL	= 8
};

/* Console log levels */
enum {
	CONSOLE_LEVEL_MIN = 1,
	CONSOLE_LEVEL_MAX = 8
};

/* Various numbers  */
#define DEFAULT_TAB_LEN	8
#define	BLANK_MAX	60
#define	TABS_MAX	160
#define BLENGTH_MAX	2000

/* Option flags.  Set if the option is to be invoked. */
int opt_term, opt_reset, opt_initialize, opt_cursor;
int opt_linewrap, opt_default, opt_foreground;
int opt_background, opt_bold, opt_blink, opt_reverse, opt_underline;
int opt_store, opt_clear, opt_blank, opt_snap, opt_snapfile;
int opt_append, opt_ulcolor, opt_hbcolor, opt_halfbright, opt_repeat;
int opt_tabs, opt_clrtabs, opt_regtabs, opt_appcursorkeys, opt_inversescreen;
int opt_msg, opt_msglevel, opt_powersave, opt_powerdown;
int opt_blength, opt_bfreq;

/* Option controls.  The variable names have been contracted to ensure
 * uniqueness.
 */
char *opt_te_terminal_name;	/* Terminal name. */
int opt_cu_on, opt_li_on, opt_bo_on, opt_hb_on, opt_bl_on;
int opt_re_on, opt_un_on, opt_rep_on, opt_appck_on, opt_invsc_on;
int opt_msg_on;			/* Boolean switches. */
int opt_fo_color, opt_ba_color;	/* Colors. */
int opt_ul_color, opt_hb_color;
int opt_cl_all;			/* Clear all or rest. */
int opt_bl_min;			/* Blank screen. */
int opt_blength_l;
int opt_bfreq_f;
int opt_sn_num;			/* Snap screen. */
int opt_rt_len;			/* regular tab length */
int opt_tb_array[TABS_MAX + 1];	/* Array for tab list */
int opt_msglevel_num;
int opt_ps_mode, opt_pd_min;	/* powersave mode/powerdown time */

char opt_sn_name[PATH_MAX] = "screen.dump";

static void screendump(int vcnum, FILE *F);

/* Command line parsing routines.
 *
 * Note that it is an error for a given option to be invoked more than once.
 */

static int parse_switch(const char *arg, const char *t, const char *f)
{
	if (strcmp(arg, t) == 0)
		return 1;
	else if (strcmp(arg, f) == 0)
		return 0;
	errx(EXIT_FAILURE, _("argument error: %s"), arg);
}

static int parse_febg_color(const char *arg)
{
	if (strcmp(arg, "black") == 0)
		return BLACK;
	else if (strcmp(arg, "red") == 0)
		return RED;
	else if (strcmp(arg, "green") == 0)
		return GREEN;
	else if (strcmp(arg, "yellow") == 0)
		return YELLOW;
	else if (strcmp(arg, "blue") == 0)
		return BLUE;
	else if (strcmp(arg, "magenta") == 0)
		return MAGENTA;
	else if (strcmp(arg, "cyan") == 0)
		return CYAN;
	else if (strcmp(arg, "white") == 0)
		return WHITE;
	else if (strcmp(arg, "default") == 0)
		return DEFAULT;
	else if (isdigit(arg[0])) {
		int color;

		color = atoi(arg);
		if (color < BLACK || DEFAULT < color || color == GREY)
			errx(EXIT_FAILURE, _("argument error: %s"), arg);
		return color;
	}
	errx(EXIT_FAILURE, _("argument error: %s"), arg);
}

static int parse_ulhb_color(char **argv, int *optind)
{
	char *color_name;
	int bright = 0;
	int color = -1;

	if (argv[*optind] && strcmp(argv[*optind - 1], "bright") == 0) {
		bright = 1;
		color_name = argv[*optind];
		(*optind)++;
	} else
		color_name = argv[*optind - 1];

	if (strcmp(color_name, "black") == 0)
		color = BLACK;
	else if (strcmp(color_name, "grey") == 0)
		color = GREY;
	else if (strcmp(color_name, "red") == 0)
		color = RED;
	else if (strcmp(color_name, "green") == 0)
		color = GREEN;
	else if (strcmp(color_name, "yellow") == 0)
		color = YELLOW;
	else if (strcmp(color_name, "blue") == 0)
		color = BLUE;
	else if (strcmp(color_name, "magenta") == 0)
		color = MAGENTA;
	else if (strcmp(color_name, "cyan") == 0)
		color = CYAN;
	else if (strcmp(color_name, "white") == 0)
		color = WHITE;
	else if (isdigit(color_name[0]))
		color = atoi(color_name);

	if (color < BLACK || DEFAULT < color)
		errx(EXIT_FAILURE, _("argument error: %s"), color_name);
	if (bright && (color == BLACK || color == GREY))
		errx(EXIT_FAILURE, _("argument error: bright %s is not supported"), color_name);

	return color;
}

static char *find_optional_arg(char **argv, char *optarg, int *optind)
{
	char *arg;
	if (optarg)
		return optarg;
	else {
		arg = argv[*optind];
		if (!arg || arg[0] == '-')
			return NULL;
	}
	(*optind)++;
	return arg;
}

static int parse_blank(char **argv, char *optarg, int *optind)
{
	char *arg;

	arg = find_optional_arg(argv, optarg, optind);
	if (!arg)
		return BLANKEDSCREEN;
	if (!strcmp(arg, "force"))
		return BLANKSCREEN;
	else if (!strcmp(arg, "poke"))
		return UNBLANKSCREEN;
	else {
		int ret = -1;

		if (isdigit(arg[0]))
			ret = atoi(arg);
		if (ret < 0 || BLANK_MAX < ret)
			errx(EXIT_FAILURE, _("argument error: %s"), arg);
		return ret;
	}
	/* should be impossible to reach */
	abort();
}

static int parse_powersave(const char *arg)
{
	if (strcmp(arg, "on") == 0)
		return VESA_BLANK_MODE_SUSPENDV;
	else if (strcmp(arg, "vsync") == 0)
		return VESA_BLANK_MODE_SUSPENDV;
	else if (strcmp(arg, "hsync") == 0)
		return VESA_BLANK_MODE_SUSPENDH;
	else if (strcmp(arg, "powerdown") == 0)
		return VESA_BLANK_MODE_POWERDOWN;
	else if (strcmp(arg, "off") == 0)
		return VESA_BLANK_MODE_OFF;
	errx(EXIT_FAILURE, _("argument error: %s"), arg);
}

static int parse_msglevel(const char *arg)
{
	int ret = CONSOLE_LEVEL_MIN - 1;

	if (isdigit(arg[0]))
		ret = atoi(arg);
	if (ret < CONSOLE_LEVEL_MIN || CONSOLE_LEVEL_MAX < ret)
		errx(EXIT_FAILURE, _("argument error: %s"), arg);
	return ret;
}

static int parse_snap(char **argv, char *optarg, int *optind)
{
	int ret = 0;
	char *arg;

	arg = find_optional_arg(argv, optarg, optind);
	if (!arg)
		return 0;
	if (isdigit(arg[0]))
		ret = atoi(arg);
	if (ret < 1)
		errx(EXIT_FAILURE, _("argument error: %s"), arg);
	return ret;
}

static void parse_tabs(char **argv, char *optarg, int *optind, int *tab_array)
{
	int i = 0;

	if (optarg) {
		tab_array[i] = atoi(optarg);
		i++;
	}
	while (argv[*optind]) {
		if (TABS_MAX < i)
			errx(EXIT_FAILURE, _("too many tabs"));
		if (argv[*optind][0] == '-')
			break;
		if (isdigit(argv[*optind][0]))
			tab_array[i] = atoi(argv[*optind]);
		else
			break;
		(*optind)++;
		i++;
	}
	tab_array[i] = -1;
}

static int parse_regtabs(char **argv, char *optarg, int *optind)
{
	int ret;
	char *arg;

	arg = find_optional_arg(argv, optarg, optind);
	if (!arg)
		return DEFAULT_TAB_LEN;
	ret = atoi(arg);
	if (ret < 1 || TABS_MAX < ret)
		errx(EXIT_FAILURE, _("argument error: %s"), arg);
	return ret;
}

static int parse_blength(char **argv, char *optarg, int *optind)
{
	int ret = -1;
	char *arg;

	arg = find_optional_arg(argv, optarg, optind);
	if (!arg)
		return 0;
	if (isdigit(arg[0]))
		ret = atoi(arg);
	if (ret < 0 || BLENGTH_MAX < ret)
		errx(EXIT_FAILURE, _("argument error: %s"), arg);
	return ret;
}

static int parse_bfreq(char **argv, char *optarg, int *optind)
{
	char *arg;

	arg = find_optional_arg(argv, optarg, optind);
	if (!arg)
		return 0;
	if (isdigit(arg[0]))
		return atoi(arg);
	return 0;
}

static void
show_tabs(void) {
	int i, co = tigetnum("cols");

	if(co > 0) {
		printf("\r         ");
		for(i = 10; i < co-2; i+=10)
			printf("%-10d", i);
		putchar('\n');
		for(i = 1; i <= co; i++)
			putchar(i%10+'0');
		putchar('\n');
		for(i = 1; i < co; i++)
			printf("\tT\b");
		putchar('\n');
	}
}

static void __attribute__ ((__noreturn__))
usage(FILE *out) {
/* Print error message about arguments, and the command's syntax. */

	if (out == stderr)
		warnx(_("Argument error."));

	fputs(_("\nUsage:\n"), out);
	fprintf(out,
	      _(" %s [options]\n"), program_invocation_short_name);

	fputs(_("\nOptions:\n"), out);
	fputs(_(" -term <terminal_name>\n"), out);
	fputs(_(" -reset\n"), out);
	fputs(_(" -initialize\n"), out);
	fputs(_(" -cursor <on|off>\n"), out);
	fputs(_(" -repeat <on|off>\n"), out);
	fputs(_(" -appcursorkeys <on|off>\n"), out);
	fputs(_(" -linewrap <on|off>\n"), out);
	fputs(_(" -default\n"), out);
	fputs(_(" -foreground <default|black|blue|cyan|green|magenta|red|white|yellow>\n"), out);
	fputs(_(" -background <default|black|blue|cyan|green|magenta|red|white|yellow>\n"), out);
	fputs(_(" -ulcolor <black|blue|bright|cyan|green|grey|magenta|red|white|yellow>\n"), out);
	fputs(_(" -ulcolor <black|blue|bright|cyan|green|grey|magenta|red|white|yellow>\n"), out);
	fputs(_(" -hbcolor <black|blue|bright|cyan|green|grey|magenta|red|white|yellow>\n"), out);
	fputs(_(" -hbcolor <black|blue|bright|cyan|green|grey|magenta|red|white|yellow>\n"), out);
	fputs(_(" -inversescreen <on|off>\n"), out);
	fputs(_(" -bold <on|off>\n"), out);
	fputs(_(" -half-bright <on|off>\n"), out);
	fputs(_(" -blink <on|off>\n"), out);
	fputs(_(" -reverse <on|off>\n"), out);
	fputs(_(" -underline <on|off>\n"), out);
	fputs(_(" -store\n"), out);
	fputs(_(" -clear <all|rest>\n"), out);
	fputs(_(" -tabs <tab1 tab2 tab3 ...>      (tabn = 1-160)\n"), out);
	fputs(_(" -clrtabs <tab1 tab2 tab3 ...>   (tabn = 1-160)\n"), out);
	fputs(_(" -regtabs <1-160>\n"), out);
	fputs(_(" -blank <0-60|force|poke>\n"), out);
	fputs(_(" -dump   <1-NR_CONSOLES>\n"), out);
	fputs(_(" -append <1-NR_CONSOLES>\n"), out);
	fputs(_(" -file dumpfilename\n"), out);
	fputs(_(" -msg <on|off>\n"), out);
	fputs(_(" -msglevel <0-8>\n"), out);	/* FIXME: klogctl console_log range is 1-8 */
	fputs(_(" -powersave <on|vsync|hsync|powerdown|off>\n"), out);
	fputs(_(" -powerdown <0-60>\n"), out);
	fputs(_(" -blength <0-2000>\n"), out);
	fputs(_(" -bfreq freqnumber\n"), out);
	fputs(_(" -version\n"), out);
	fputs(_(" -help\n"), out);

	fprintf(out, USAGE_MAN_TAIL("setterm(1)"));

	exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}

static void set_opt_flag(int *opt)
{
	if (*opt)
		errx(EXIT_FAILURE, _("duplicate use of an option"));
	*opt = 1;
}

static void parse_option(int argc, char **argv)
{
	int c;
	enum {
		OPT_TERM = CHAR_MAX + 1,
		OPT_RESET,
		OPT_INITIALIZE,
		OPT_CURSOR,
		OPT_REPEAT,
		OPT_APPCURSORKEYS,
		OPT_LINEWRAP,
		OPT_DEFAULT,
		OPT_FOREGROUND,
		OPT_BACKGROUND,
		OPT_ULCOLOR,
		OPT_HBCOLOR,
		OPT_INVERSESCREEN,
		OPT_BOLD,
		OPT_HALF_BRIGHT,
		OPT_BLINK,
		OPT_REVERSE,
		OPT_UNDERLINE,
		OPT_STORE,
		OPT_CLEAR,
		OPT_TABS,
		OPT_CLRTABS,
		OPT_REGTABS,
		OPT_BLANK,
		OPT_DUMP,
		OPT_APPEND,
		OPT_FILE,
		OPT_MSG,
		OPT_MSGLEVEL,
		OPT_POWERSAVE,
		OPT_POWERDOWN,
		OPT_BLENGTH,
		OPT_BFREQ,
		OPT_VERSION,
		OPT_HELP
	};
	static const struct option longopts[] = {
		{"term", required_argument, NULL, OPT_TERM},
		{"reset", no_argument, NULL, OPT_RESET},
		{"initialize", no_argument, NULL, OPT_INITIALIZE},
		{"cursor", required_argument, NULL, OPT_CURSOR},
		{"repeat", required_argument, NULL, OPT_REPEAT},
		{"appcursorkeys", required_argument, NULL, OPT_APPCURSORKEYS},
		{"linewrap", required_argument, NULL, OPT_LINEWRAP},
		{"default", no_argument, NULL, OPT_DEFAULT},
		{"foreground", required_argument, NULL, OPT_FOREGROUND},
		{"background", required_argument, NULL, OPT_BACKGROUND},
		{"ulcolor", required_argument, NULL, OPT_ULCOLOR},
		{"ulcolor", required_argument, NULL, OPT_ULCOLOR},
		{"hbcolor", required_argument, NULL, OPT_HBCOLOR},
		{"hbcolor", required_argument, NULL, OPT_HBCOLOR},
		{"inversescreen", required_argument, NULL, OPT_INVERSESCREEN},
		{"bold", required_argument, NULL, OPT_BOLD},
		{"half-bright", required_argument, NULL, OPT_HALF_BRIGHT},
		{"blink", required_argument, NULL, OPT_BLINK},
		{"reverse", required_argument, NULL, OPT_REVERSE},
		{"underline", required_argument, NULL, OPT_UNDERLINE},
		{"store", no_argument, NULL, OPT_STORE},
		{"clear", required_argument, NULL, OPT_CLEAR},
		{"tabs", optional_argument, NULL, OPT_TABS},
		{"clrtabs", optional_argument, NULL, OPT_CLRTABS},
		{"regtabs", optional_argument, NULL, OPT_REGTABS},
		{"blank", optional_argument, NULL, OPT_BLANK},
		{"dump", optional_argument, NULL, OPT_DUMP},
		{"append", required_argument, NULL, OPT_APPEND},
		{"file", required_argument, NULL, OPT_FILE},
		{"msg", required_argument, NULL, OPT_MSG},
		{"msglevel", required_argument, NULL, OPT_MSGLEVEL},
		{"powersave", required_argument, NULL, OPT_POWERSAVE},
		{"powerdown", optional_argument, NULL, OPT_POWERDOWN},
		{"blength", optional_argument, NULL, OPT_BLENGTH},
		{"bfreq", optional_argument, NULL, OPT_BFREQ},
		{"version", no_argument, NULL, OPT_VERSION},
		{"help", no_argument, NULL, OPT_HELP},
		{NULL, 0, NULL, 0}
	};

	while ((c = getopt_long_only(argc, argv, "", longopts, NULL)) != -1) {
		switch (c) {
		case OPT_TERM:
			set_opt_flag(&opt_term);
			opt_te_terminal_name = optarg;
			break;
		case OPT_RESET:
			set_opt_flag(&opt_reset);
			break;
		case OPT_INITIALIZE:
			set_opt_flag(&opt_initialize);
			break;
		case OPT_CURSOR:
			set_opt_flag(&opt_cursor);
			opt_cu_on = parse_switch(optarg, "on", "off");
			break;
		case OPT_REPEAT:
			set_opt_flag(&opt_repeat);
			opt_rep_on = parse_switch(optarg, "on", "off");
			break;
		case OPT_APPCURSORKEYS:
			set_opt_flag(&opt_appcursorkeys);
			opt_appck_on = parse_switch(optarg, "on", "off");
			break;
		case OPT_LINEWRAP:
			set_opt_flag(&opt_linewrap);
			opt_li_on = parse_switch(optarg, "on", "off");
			break;
		case OPT_DEFAULT:
			set_opt_flag(&opt_default);
			break;
		case OPT_FOREGROUND:
			set_opt_flag(&opt_foreground);
			opt_fo_color = parse_febg_color(optarg);
			break;
		case OPT_BACKGROUND:
			set_opt_flag(&opt_background);
			opt_ba_color = parse_febg_color(optarg);
			break;
		case OPT_ULCOLOR:
			set_opt_flag(&opt_ulcolor);
			opt_ul_color = parse_ulhb_color(argv, &optind);
			break;
		case OPT_HBCOLOR:
			set_opt_flag(&opt_hbcolor);
			opt_hb_color = parse_ulhb_color(argv, &optind);
			break;
		case OPT_INVERSESCREEN:
			set_opt_flag(&opt_inversescreen);
			opt_invsc_on = parse_switch(optarg, "on", "off");
			break;
		case OPT_BOLD:
			set_opt_flag(&opt_bold);
			opt_bo_on = parse_switch(optarg, "on", "off");
			break;
		case OPT_HALF_BRIGHT:
			set_opt_flag(&opt_halfbright);
			opt_hb_on = parse_switch(optarg, "on", "off");
			break;
		case OPT_BLINK:
			set_opt_flag(&opt_blink);
			opt_bl_on = parse_switch(optarg, "on", "off");
			break;
		case OPT_REVERSE:
			set_opt_flag(&opt_reverse);
			opt_re_on = parse_switch(optarg, "on", "off");
			break;
		case OPT_UNDERLINE:
			set_opt_flag(&opt_underline);
			opt_un_on = parse_switch(optarg, "on", "off");
			break;
		case OPT_STORE:
			set_opt_flag(&opt_store);
			break;
		case OPT_CLEAR:
			set_opt_flag(&opt_clear);
			opt_cl_all = parse_switch(optarg, "all", "reset");
			break;
		case OPT_TABS:
			set_opt_flag(&opt_tabs);
			parse_tabs(argv, optarg, &optind, opt_tb_array);
			break;
		case OPT_CLRTABS:
			set_opt_flag(&opt_clrtabs);
			parse_tabs(argv, optarg, &optind, opt_tb_array);
			break;
		case OPT_REGTABS:
			set_opt_flag(&opt_regtabs);
			opt_rt_len = parse_regtabs(argv, optarg, &optind);
			break;
		case OPT_BLANK:
			set_opt_flag(&opt_blank);
			opt_bl_min = parse_blank(argv, optarg, &optind);
			break;
		case OPT_DUMP:
			set_opt_flag(&opt_snap);
			opt_sn_num = parse_snap(argv, optarg, &optind);
			break;
		case OPT_APPEND:
			set_opt_flag(&opt_append);
			opt_sn_num = parse_snap(argv, optarg, &optind);
			break;
		case OPT_FILE:
			set_opt_flag(&opt_snapfile);
			strncpy(opt_sn_name, optarg, PATH_MAX);	/* FIXME: should use xstrncpy() */
			opt_sn_name[PATH_MAX - 1] = 0;
			break;
		case OPT_MSG:
			set_opt_flag(&opt_msg);
			opt_msg_on = parse_switch(optarg, "on", "off");
			break;
		case OPT_MSGLEVEL:
			set_opt_flag(&opt_msglevel);
			opt_msglevel_num = parse_msglevel(optarg);
			break;
		case OPT_POWERSAVE:
			set_opt_flag(&opt_powersave);
			opt_ps_mode = parse_powersave(optarg);
			break;
		case OPT_POWERDOWN:
			set_opt_flag(&opt_powerdown);
			opt_pd_min = parse_blank(argv, optarg, &optind);
			break;
		case OPT_BLENGTH:
			set_opt_flag(&opt_blength);
			opt_blength_l = parse_blength(argv, optarg, &optind);
			break;
		case OPT_BFREQ:
			set_opt_flag(&opt_bfreq);
			opt_bfreq_f = parse_bfreq(argv, optarg, &optind);
			break;
		case OPT_VERSION:
			printf(UTIL_LINUX_VERSION);
			exit(EXIT_SUCCESS);
		case OPT_HELP:
			usage(stdout);
		default:
			usage(stderr);
		}
	}
}

/* End of command line parsing routines. */

static char *ti_entry(const char *name) {
	/* name: Terminfo capability string to lookup. */

/* Return the specified terminfo string, or an empty string if no such terminfo
 * capability exists.
 */

	char *buf_ptr;

	if ((buf_ptr = tigetstr((char *)name)) == (char *)-1)
		buf_ptr = NULL;
	return buf_ptr;
}

static void
perform_sequence(int vcterm) {
	/* vcterm: Set if terminal is a virtual console. */

	int result;
/* Perform the selected options. */

	/* -reset. */
	if (opt_reset) {
		putp(ti_entry("rs1"));
	}

	/* -initialize. */
	if (opt_initialize) {
		putp(ti_entry("is2"));
	}

	/* -cursor [on|off]. */
	if (opt_cursor) {
		if (opt_cu_on)
			putp(ti_entry("cnorm"));
		else
			putp(ti_entry("civis"));
	}

	/* -linewrap [on|off]. Vc only (vt102) */
	if (opt_linewrap && vcterm)
		fputs(opt_li_on ? "\033[?7h" : "\033[?7l", stdout);

	/* -repeat [on|off]. Vc only (vt102) */
	if (opt_repeat && vcterm)
		fputs(opt_rep_on ? "\033[?8h" : "\033[?8l", stdout);

	/* -appcursorkeys [on|off]. Vc only (vt102) */
	if (opt_appcursorkeys && vcterm)
		fputs(opt_appck_on ? "\033[?1h" : "\033[?1l", stdout);

	/* -default.  Vc sets default rendition, otherwise clears all
	 * attributes.
	 */
	if (opt_default) {
		if (vcterm)
			printf("\033[0m");
		else
			putp(ti_entry("sgr0"));
	}

	/* -foreground black|red|green|yellow|blue|magenta|cyan|white|default.
	 * Vc only (ANSI).
	 */
	if (opt_foreground && vcterm)
		printf("\033[3%c%s", '0' + opt_fo_color, "m");

	/* -background black|red|green|yellow|blue|magenta|cyan|white|default.
	 * Vc only (ANSI).
	 */
	if (opt_background && vcterm)
		printf("\033[4%c%s", '0' + opt_ba_color, "m");

	/* -ulcolor black|red|green|yellow|blue|magenta|cyan|white|default.
	 * Vc only.
	 */
	if (opt_ulcolor && vcterm) {
		printf("\033[1;%d]", opt_ul_color);
	}

	/* -hbcolor black|red|green|yellow|blue|magenta|cyan|white|default.
	 * Vc only.
	 */
	if (opt_hbcolor && vcterm) {
		printf("\033[2;%d]", opt_hb_color);
	}

	/* -inversescreen [on|off].  Vc only (vt102).
	 */
	if (opt_inversescreen && vcterm)
		fputs(opt_invsc_on ? "\033[?5h" : "\033[?5l", stdout);

	/* -bold [on|off].  Vc behaves as expected, otherwise off turns off
	 * all attributes.
	 */
	if (opt_bold) {
		if (opt_bo_on)
			putp(ti_entry("bold"));
		else {
			if (vcterm)
				fputs("\033[22m", stdout);
			else
				putp(ti_entry("sgr0"));
		}
	}

	/* -half-bright [on|off].  Vc behaves as expected, otherwise off turns off
	 * all attributes.
	 */
	if (opt_halfbright) {
		if (opt_hb_on)
			putp(ti_entry("dim"));
		else {
			if (vcterm)
				fputs("\033[22m", stdout);
			else
				putp(ti_entry("sgr0"));
		}
	}

	/* -blink [on|off].  Vc behaves as expected, otherwise off turns off
	 * all attributes.
	 */
	if (opt_blink) {
		if (opt_bl_on)
			putp(ti_entry("blink"));
		else {
			if (vcterm)
				fputs("\033[25m", stdout);
			else
				putp(ti_entry("sgr0"));
		}
	}

	/* -reverse [on|off].  Vc behaves as expected, otherwise off turns
	 * off all attributes.
	 */
	if (opt_reverse) {
		if (opt_re_on)
			putp(ti_entry("rev"));
		else {
			if (vcterm)
				fputs("\033[27m", stdout);
			else
				putp(ti_entry("sgr0"));
		}
	}

	/* -underline [on|off]. */
	if (opt_underline)
		putp(ti_entry(opt_un_on ? "smul" : "rmul"));

	/* -store.  Vc only. */
	if (opt_store && vcterm)
		fputs("\033[8]", stdout);

	/* -clear [all|rest]. */
	if (opt_clear)
		putp(ti_entry(opt_cl_all ? "clear" : "ed"));

	/* -tabs Vc only. */
	if (opt_tabs && vcterm) {
		int i;

		if (opt_tb_array[0] == -1)
			show_tabs();
		else {
			for(i=0; opt_tb_array[i] > 0; i++)
				printf("\033[%dG\033H", opt_tb_array[i]);
			putchar('\r');
		}
	}

	/* -clrtabs Vc only. */
	if (opt_clrtabs && vcterm) {
		int i;

		if (opt_tb_array[0] == -1)
			fputs("\033[3g", stdout);
		else
			for(i=0; opt_tb_array[i] > 0; i++)
				printf("\033[%dG\033[g", opt_tb_array[i]);
		putchar('\r');
	}

	/* -regtabs Vc only. */
	if (opt_regtabs && vcterm) {
		int i;

		fputs("\033[3g\r", stdout);
		for(i=opt_rt_len+1; i<=TABS_MAX; i+=opt_rt_len)
			printf("\033[%dC\033H",opt_rt_len);
		putchar('\r');
	}

	/* -blank [0-60]. */
	if (opt_blank && vcterm) {
		if (opt_bl_min >= 0)
			printf("\033[9;%d]", opt_bl_min);
		else if (opt_bl_min == BLANKSCREEN) {
			char ioctlarg = TIOCL_BLANKSCREEN;
			if (ioctl(0,TIOCLINUX,&ioctlarg))
				warn(_("cannot force blank"));
		} else if (opt_bl_min == UNBLANKSCREEN) {
			char ioctlarg = TIOCL_UNBLANKSCREEN;
			if (ioctl(0,TIOCLINUX,&ioctlarg))
				warn(_("cannot force unblank"));
		} else if (opt_bl_min == BLANKEDSCREEN) {
			char ioctlarg = TIOCL_BLANKEDSCREEN;
			int ret;
			ret = ioctl(0,TIOCLINUX,&ioctlarg);
			if (ret < 0)
				warn(_("cannot get blank status"));
			else
				printf("%d\n",ret);
		}
	}

	/* -powersave [on|vsync|hsync|powerdown|off] (console) */
	if (opt_powersave) {
		char ioctlarg[2];
		ioctlarg[0] = TIOCL_SETVESABLANK;
		ioctlarg[1] = opt_ps_mode;
		if (ioctl(0,TIOCLINUX,ioctlarg))
			warn(_("cannot (un)set powersave mode"));
	}

	/* -powerdown [0-60]. */
	if (opt_powerdown) {
		printf("\033[14;%d]", opt_pd_min);
	}

	/* -snap [1-NR_CONS]. */
	if (opt_snap || opt_append) {
		FILE *F;

		F = fopen(opt_sn_name, opt_snap ? "w" : "a");
		if (!F)
			err(EXIT_DUMPFILE, _("can not open dump file %s for output"),
				opt_sn_name); 
		screendump(opt_sn_num, F);
		if (close_stream(F) != 0)
			errx(EXIT_FAILURE, _("write error"));
	}

	/* -msg [on|off]. */
	if (opt_msg && vcterm) {
		if (opt_msg_on)
			/* 7 -- Enable printk's to console */
			result = klogctl(SYSLOG_ACTION_CONSOLE_ON, NULL, 0);
		else
			/*  6 -- Disable printk's to console */
			result = klogctl(SYSLOG_ACTION_CONSOLE_OFF, NULL, 0);

		if (result != 0)
			warn(_("klogctl error"));
	}

	/* -msglevel [0-8] */
	if (opt_msglevel && vcterm) {
		/* 8 -- Set level of messages printed to console */
		result = klogctl(SYSLOG_ACTION_CONSOLE_LEVEL, NULL, opt_msglevel_num);
		if (result != 0)
			warn(_("klogctl error"));
	}

	/* -blength [0-2000] */
	if (opt_blength && vcterm) {
		printf("\033[11;%d]", opt_blength_l);
	}

	/* -bfreq freqnumber */
	if (opt_bfreq && vcterm) {
		printf("\033[10;%d]", opt_bfreq_f);
	}

}

static void
screendump(int vcnum, FILE * F)
{
	char infile[MAXPATHLEN];
	unsigned char header[4];
	unsigned int rows, cols;
	int fd;
	size_t i, j;
	ssize_t rc;
	char *inbuf = NULL, *outbuf = NULL, *p, *q;

	sprintf(infile, "/dev/vcsa%d", vcnum);
	fd = open(infile, O_RDONLY);
	if (fd < 0 && vcnum == 0) {
		/* vcsa0 is often called vcsa */
		sprintf(infile, "/dev/vcsa");
		fd = open(infile, O_RDONLY);
	}
	if (fd < 0) {
		/* try devfs name - for zero vcnum just /dev/vcc/a */
		/* some gcc's warn for %.u - add 0 */
		sprintf(infile, "/dev/vcc/a%.0u", vcnum);
		fd = open(infile, O_RDONLY);
	}
	if (fd < 0) {
		sprintf(infile, "/dev/vcsa%d", vcnum);
		goto read_error;
	}
	if (read(fd, header, 4) != 4)
		goto read_error;
	rows = header[0];
	cols = header[1];
	if (rows * cols == 0)
		goto read_error;

	inbuf = xmalloc(rows * cols * 2);
	outbuf = xmalloc(rows * (cols + 1));

	rc = read(fd, inbuf, rows * cols * 2);
	if (rc < 0 || (size_t) rc != rows * cols * 2)
		goto read_error;
	p = inbuf;
	q = outbuf;
	for (i = 0; i < rows; i++) {
		for (j = 0; j < cols; j++) {
			*q++ = *p;
			p += 2;
		}
		while (j-- > 0 && q[-1] == ' ')
			q--;
		*q++ = '\n';
	}
	if (fwrite(outbuf, 1, q - outbuf, F) != (size_t) (q - outbuf)) {
		warnx(_("Error writing screendump"));
		goto error;
	}
	close(fd);
	free(inbuf);
	free(outbuf);
	return;

read_error:
	if (vcnum != 0)
		warnx(_("Couldn't read %s"), infile);
	else
		warnx(_("Couldn't read neither /dev/vcsa0 nor /dev/vcsa"));

error:
	if (fd >= 0)
		close(fd);
	free(inbuf);
	free(outbuf);
	exit(EXIT_FAILURE);
}

int
main(int argc, char **argv) {
	char *term;			/* Terminal type. */
	int vcterm;			/* Set if terminal is a virtual console. */
	int errret;

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	if (argc < 2)
		usage(stderr);

	/* Parse arguments. */
	parse_option(argc, argv);

	/* Find out terminal name. */

	if (opt_term) {
		term = opt_te_terminal_name;
	} else {
		term = getenv("TERM");
		if (term == NULL)
			errx(EXIT_FAILURE, _("$TERM is not defined."));
	}

	/* Find terminfo entry. */

	if (setupterm(term, 1, &errret))
		switch(errret) {
		case -1:
			errx(EXIT_FAILURE, _("terminfo database cannot be found"));
		case 0:
			errx(EXIT_FAILURE, _("%s: unknown terminal type"), term);
		case 1:
			errx(EXIT_FAILURE, _("terminal is hardcopy"));
		}

	/* See if the terminal is a virtual console terminal. */

	vcterm = (!strncmp(term, "con", 3) || !strncmp(term, "linux", 5));

	/* Perform the selected options. */

	perform_sequence(vcterm);

	return EXIT_SUCCESS;
}
