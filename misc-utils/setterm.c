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
 * 1999-02-22 Arkadiusz Mi¶kiewicz <misiek@pld.ORG.PL>
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
 *   [ -blank [0-60] ]
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
 *
 *
 * Semantics:
 *
 * Setterm writes to standard output a character string that will
 * invoke the specified terminal capabilities.  Where possibile
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

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <ctype.h>
#include <unistd.h>
#include <termios.h>
#include <string.h>
#include <fcntl.h>
#ifndef NCURSES_CONST
#define NCURSES_CONST const	/* define before including term.h */
#endif
#include <term.h>
 
#ifdef HAVE_NCURSES_H
#include <ncurses.h>
#elif defined(HAVE_NCURSES_NCURSES_H) 
#include <ncurses/ncurses.h>
#endif

#include <sys/param.h>		/* for MAXPATHLEN */
#include <sys/ioctl.h>
#include <sys/time.h>
#include "nls.h"

#ifndef TCGETS
/* TCGETS is either defined in termios.h, or here: */
#include <asm/ioctls.h>
#endif

#if __GNU_LIBRARY__ < 5
#ifndef __alpha__
# include <linux/unistd.h>
#define __NR_klogctl __NR_syslog
_syscall3(int, klogctl, int, type, char*, buf, int, len);
#else /* __alpha__ */
#define klogctl syslog
#endif
#endif
extern int klogctl(int type, char *buf, int len);

/* Constants. */

/* General constants. */
#ifndef TRUE
#define TRUE  1
#define FALSE 0
#endif

/* Keyboard types. */
#define PC	 0
#define OLIVETTI 1
#define DUTCH    2
#define EXTENDED 3

/* Colors. */
#define BLACK   0
#define RED     1
#define GREEN   2
#define YELLOW  3
#define BLUE    4
#define MAGENTA 5
#define CYAN    6
#define WHITE   7
#define GREY	8
#define DEFAULT 9

/* Control sequences. */
#define ESC "\033"
#define DCS "\033P"
#define ST  "\033\\"

/* Static variables. */

/* Option flags.  Set if the option is to be invoked. */
int opt_term, opt_reset, opt_initialize, opt_cursor;
int opt_linewrap, opt_snow, opt_softscroll, opt_default, opt_foreground;
int opt_background, opt_bold, opt_blink, opt_reverse, opt_underline;
int opt_store, opt_clear, opt_blank, opt_snap, opt_snapfile, opt_standout;
int opt_append, opt_ulcolor, opt_hbcolor, opt_halfbright, opt_repeat;
int opt_tabs, opt_clrtabs, opt_regtabs, opt_appcursorkeys, opt_inversescreen;
int opt_msg, opt_msglevel, opt_powersave, opt_powerdown;
int opt_blength, opt_bfreq;

/* Option controls.  The variable names have been contracted to ensure
 * uniqueness.
 */
char *opt_te_terminal_name;	/* Terminal name. */
int opt_cu_on, opt_li_on, opt_sn_on, opt_so_on, opt_bo_on, opt_hb_on, opt_bl_on;
int opt_re_on, opt_un_on, opt_rep_on, opt_appck_on, opt_invsc_on;
int opt_msg_on;			/* Boolean switches. */
int opt_ke_type;		/* Keyboard type. */
int opt_fo_color, opt_ba_color;	/* Colors. */
int opt_ul_color, opt_hb_color;
int opt_cl_all;			/* Clear all or rest. */
int opt_bl_min;			/* Blank screen. */
int opt_blength_l;
int opt_bfreq_f;
int opt_sn_num;			/* Snap screen. */
int opt_st_attr;
int opt_rt_len;			/* regular tab length */
int opt_tb_array[161];		/* Array for tab list */
int opt_msglevel_num;
int opt_ps_mode, opt_pd_min;	/* powersave mode/powerdown time */

char opt_sn_name[200] = "screen.dump";

static void screendump(int vcnum, FILE *F);

/* Command line parsing routines.
 *
 * Note that it is an error for a given option to be invoked more than once.
 */

static void
parse_term(int argc, char **argv, int *option, char **opt_term, int *bad_arg) {
	/* argc: Number of arguments for this option. */
	/* argv: Arguments for this option. */
	/* option: Term flag to set. */
	/* opt_term: Terminal name to set. */
	/* bad_arg: Set to true if an error is detected. */

/* Parse a -term specification. */

	if (argc != 1 || *option)
		*bad_arg = TRUE;
	*option = TRUE;
	if (argc == 1)
		*opt_term = argv[0];
}

static void
parse_none(int argc, char **argv, int *option, int *bad_arg) {
	/* argc: Number of arguments for this option. */
	/* argv: Arguments for this option. */
	/* option: Term flag to set. */
	/* bad_arg: Set to true if an error is detected. */

/* Parse a parameterless specification. */

	if (argc != 0 || *option)
		*bad_arg = TRUE;
	*option = TRUE;
}

static void
parse_switch(int argc, char **argv, int *option, int *opt_on, int *bad_arg) {
	/* argc: Number of arguments for this option. */
	/* argv: Arguments for this option. */
	/* option: Option flag to set. */
	/* opt_on: Boolean option switch to set or reset. */
	/* bad_arg: Set to true if an error is detected. */

/* Parse a boolean (on/off) specification. */

	if (argc > 1 || *option)
		*bad_arg = TRUE;
	*option = TRUE;
	if (argc == 1) {
		if (strcmp(argv[0], "on") == 0)
			*opt_on = TRUE;
		else if (strcmp(argv[0], "off") == 0)
			*opt_on = FALSE;
		else
			*bad_arg = TRUE;
	} else {
		*opt_on = TRUE;
	}
}

static void
par_color(int argc, char **argv, int *option, int *opt_color, int *bad_arg) {
	/* argc: Number of arguments for this option. */
	/* argv: Arguments for this option. */
	/* option: Color flag to set. */
	/* opt_color: Color to set. */
	/* bad_arg: Set to true if an error is detected. */

/* Parse a -foreground or -background specification. */

	if (argc != 1 || *option)
		*bad_arg = TRUE;
	*option = TRUE;
	if (argc == 1) {
		if (strcmp(argv[0], "black") == 0)
			*opt_color = BLACK;
		else if (strcmp(argv[0], "red") == 0)
			*opt_color = RED;
		else if (strcmp(argv[0], "green") == 0)
			*opt_color = GREEN;
		else if (strcmp(argv[0], "yellow") == 0)
			*opt_color = YELLOW;
		else if (strcmp(argv[0], "blue") == 0)
			*opt_color = BLUE;
		else if (strcmp(argv[0], "magenta") == 0)
			*opt_color = MAGENTA;
		else if (strcmp(argv[0], "cyan") == 0)
			*opt_color = CYAN;
		else if (strcmp(argv[0], "white") == 0)
			*opt_color = WHITE;
		else if (strcmp(argv[0], "default") == 0)
			*opt_color = DEFAULT;
		else if (isdigit(argv[0][0]))
			*opt_color = atoi(argv[0]);
		else 
			*bad_arg = TRUE;
    
		if(*opt_color < 0 || *opt_color > 9 || *opt_color == 8)
			*bad_arg = TRUE;
	}
}

static void
par_color2(int argc, char **argv, int *option, int *opt_color, int *bad_arg) {
	/* argc: Number of arguments for this option. */
	/* argv: Arguments for this option. */
	/* option: Color flag to set. */
	/* opt_color: Color to set. */
	/* bad_arg: Set to true if an error is detected. */

/* Parse a -ulcolor or -hbcolor specification. */

	if (!argc || argc > 2 || *option)
		*bad_arg = TRUE;
	*option = TRUE;
	*opt_color = 0;
	if (argc == 2) {
		if (strcmp(argv[0], "bright") == 0)
			*opt_color = 8;
		else {
			*bad_arg = TRUE;
			return;
		}
	}
	if (argc) {
		if (strcmp(argv[argc-1], "black") == 0) {
			if(*opt_color)
				*bad_arg = TRUE;
			else
				*opt_color = BLACK;
		} else if (strcmp(argv[argc-1], "grey") == 0) {
			if(*opt_color)
				*bad_arg = TRUE;
			else
				*opt_color = GREY;
		} else if (strcmp(argv[argc-1], "red") == 0)
			*opt_color |= RED;
		else if (strcmp(argv[argc-1], "green") == 0)
			*opt_color |= GREEN;
		else if (strcmp(argv[argc-1], "yellow") == 0)
			*opt_color |= YELLOW;
		else if (strcmp(argv[argc-1], "blue") == 0)
			*opt_color |= BLUE;
		else if (strcmp(argv[argc-1], "magenta") == 0)
			*opt_color |= MAGENTA;
		else if (strcmp(argv[argc-1], "cyan") == 0)
			*opt_color |= CYAN;
		else if (strcmp(argv[argc-1], "white") == 0)
			*opt_color |= WHITE;
		else if (isdigit(argv[argc-1][0]))
			*opt_color = atoi(argv[argc-1]);
		else    
			*bad_arg = TRUE;
		if(*opt_color < 0 || *opt_color > 15)
			*bad_arg = TRUE;
	}
}

static void
parse_clear(int argc, char **argv, int *option, int *opt_all, int *bad_arg) {
	/* argc: Number of arguments for this option. */
	/* argv: Arguments for this option. */
	/* option: Clear flag to set. */
	/* opt_all: Clear all switch to set or reset. */
	/* bad_arg: Set to true if an error is detected. */

/* Parse a -clear specification. */

	if (argc > 1 || *option)
		*bad_arg = TRUE;
	*option = TRUE;
	if (argc == 1) {
		if (strcmp(argv[0], "all") == 0)
			*opt_all = TRUE;
		else if (strcmp(argv[0], "rest") == 0)
			*opt_all = FALSE;
		else
			*bad_arg = TRUE;
	} else {
		*opt_all = TRUE;
	}
}

static void
parse_blank(int argc, char **argv, int *option, int *opt_all, int *bad_arg) {
	/* argc: Number of arguments for this option. */
	/* argv: Arguments for this option. */
	/* option: Clear flag to set. */
	/* opt_all: Clear all switch to set or reset. */
	/* bad_arg: Set to true if an error is detected. */

/* Parse a -blank specification. */

	if (argc > 1 || *option)
		*bad_arg = TRUE;
	*option = TRUE;
	if (argc == 1) {
		*opt_all = atoi(argv[0]);
		if ((*opt_all > 60) || (*opt_all < 0))
			*bad_arg = TRUE;
	} else {
		*opt_all = 0;
	}
}

static void
parse_powersave(int argc, char **argv, int *option, int *opt_mode, int *bad_arg) {
	/* argc: Number of arguments for this option. */
	/* argv: Arguments for this option. */
	/* option: powersave flag to set. */
	/* opt_mode: Powersaving mode, defined in vesa_blank.c */
	/* bad_arg: Set to true if an error is detected. */

/* Parse a -powersave mode specification. */

	if (argc > 1 || *option)
		*bad_arg = TRUE;
	*option = TRUE;
	if (argc == 1) {
		if (strcmp(argv[0], "on") == 0)
			*opt_mode = 1;
		else if (strcmp(argv[0], "vsync") == 0)
			*opt_mode = 1;
		else if (strcmp(argv[0], "hsync") == 0)
			*opt_mode = 2;
		else if (strcmp(argv[0], "powerdown") == 0)
			*opt_mode = 3;
		else if (strcmp(argv[0], "off") == 0)
			*opt_mode = 0;
		else
			*bad_arg = TRUE;
	} else {
		*opt_mode = 0;
	}
}

#if 0
static void
parse_standout(int argc, char *argv, int *option, int *opt_all, int *bad_arg) {
	/* argc: Number of arguments for this option. */
	/* argv: Arguments for this option. */
	/* option: Clear flag to set. */
	/* opt_all: Clear all switch to set or reset. */
	/* bad_arg: Set to true if an error is detected. */

/* Parse a -standout specification. */

	if (argc > 1 || *option)
		*bad_arg = TRUE;
	*option = TRUE;
	if (argc == 1)
		*opt_all = atoi(argv[0]);
	else
		*opt_all = -1;
}
#endif

static void
parse_msglevel(int argc, char **argv, int *option, int *opt_all, int *bad_arg) {
	/* argc: Number of arguments for this option. */
	/* argv: Arguments for this option. */
	/* option: Clear flag to set. */
	/* opt_all: Clear all switch to set or reset. */
	/* bad_arg: Set to true if an error is detected. */

	if (argc > 1 || *option)
		*bad_arg = TRUE;
	*option = TRUE;
	if (argc == 1) {
		*opt_all = atoi(argv[0]);
		if (*opt_all < 0 || *opt_all > 8)
			*bad_arg = TRUE;
	} else {
		*opt_all = -1;
	}
}

static void
parse_snap(int argc, char **argv, int *option, int *opt_all, int *bad_arg) {
	/* argc: Number of arguments for this option. */
	/* argv: Arguments for this option. */
	/* option: Clear flag to set. */
	/* opt_all: Clear all switch to set or reset. */
	/* bad_arg: Set to true if an error is detected. */

/* Parse a -dump or -append specification. */

	if (argc > 1 || *option)
		*bad_arg = TRUE;
	*option = TRUE;
	if (argc == 1) {
		*opt_all = atoi(argv[0]);
		if ((*opt_all <= 0))
			*bad_arg = TRUE;
	} else {
		*opt_all = 0;
	}
}

static void
parse_snapfile(int argc, char **argv, int *option, int *opt_all, int *bad_arg) {
	/* argc: Number of arguments for this option. */
	/* argv: Arguments for this option. */
	/* option: Clear flag to set. */
	/* opt_all: Clear all switch to set or reset. */
	/* bad_arg: Set to true if an error is detected. */

/* Parse a -file specification. */

	if (argc != 1 || *option)
		*bad_arg = TRUE;
	*option = TRUE;
	if (argc == 1)
		strcpy((char *)opt_all, argv[0]);
}

static void
parse_tabs(int argc, char **argv, int *option, int *tab_array, int *bad_arg) {
	/* argc: Number of arguments for this option. */
	/* argv: Arguments for this option. */
	/* option: Clear flag to set. */
	/* tab_array: Array of tabs */
	/* bad_arg: Set to true if an error is detected. */

	if (*option || argc > 160)
		*bad_arg = TRUE;
	*option = TRUE;
	tab_array[argc] = -1;
	while(argc--) {
		tab_array[argc] = atoi(argv[argc]);
		if(tab_array[argc] < 1 || tab_array[argc] > 160) {
			*bad_arg = TRUE;
			return;
		}
	}
}

static void
parse_clrtabs(int argc, char **argv, int *option, int *tab_array, int *bad_arg) {
	/* argc: Number of arguments for this option. */
	/* argv: Arguments for this option. */
	/* option: Clear flag to set. */
	/* tab_array: Array of tabs */
	/* bad_arg: Set to true if an error is detected. */

	if (*option || argc > 160)
		*bad_arg = TRUE;
	*option = TRUE;
	if(argc == 0) {
		tab_array[0] = -1;
		return;
	}
	tab_array[argc] = -1;
	while(argc--) {
		tab_array[argc] = atoi(argv[argc]);
		if(tab_array[argc] < 1 || tab_array[argc] > 160) {
			*bad_arg = TRUE;
			return;
		}
	}
}

static void
parse_regtabs(int argc, char **argv, int *option, int *opt_len, int *bad_arg) {
	/* argc: Number of arguments for this option. */
	/* argv: Arguments for this option. */
	/* option: Clear flag to set. */
	/* opt_len: Regular tab length. */
	/* bad_arg: Set to true if an error is detected. */

	if (*option || argc > 1)
		*bad_arg = TRUE;
	*option = TRUE;
	if(argc == 0) {
		*opt_len = 8;
		return;
	}
	*opt_len = atoi(argv[0]);
	if(*opt_len < 1 || *opt_len > 160) {
		*bad_arg = TRUE;
		return;
	}
}


static void
parse_blength(int argc, char **argv, int *option, int *opt_all, int *bad_arg) {
	/* argc: Number of arguments for this option. */
	/* argv: Arguments for this option. */
	/* option: Clear flag to set. */
	/* opt_all */
	/* bad_arg: Set to true if an error is detected. */

/* Parse  -blength specification. */

	if (argc > 1 || *option)
		*bad_arg = TRUE;
	*option = TRUE;
	if (argc == 1) {
		*opt_all = atoi(argv[0]);
		if (*opt_all > 2000)
			*bad_arg = TRUE;
	} else {
		*opt_all = 0;
	}
}

static void
parse_bfreq(int argc, char **argv, int *option, int *opt_all, int *bad_arg) {
	/* argc: Number of arguments for this option. */
	/* argv: Arguments for this option. */
	/* option: Clear flag to set. */
	/* opt_all */
	/* bad_arg: Set to true if an error is detected. */

/* Parse  -bfreq specification. */

	if (argc > 1 || *option)
		*bad_arg = TRUE;
	*option = TRUE;
	if (argc == 1) {
		*opt_all = atoi(argv[0]);
	} else {
		*opt_all = 0;
	}
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


#define STRCMP(str1,str2) strncmp(str1,str2,strlen(str1))

static void
parse_option(char *option, int argc, char **argv, int *bad_arg) {
	/* option: Option with leading '-' removed. */
	/* argc: Number of arguments for this option. */
	/* argv: Arguments for this option. */
	/* bad_arg: Set to true if an error is detected. */

/* Parse a single specification. */

	if (STRCMP(option, "term") == 0)
		parse_term(argc, argv, &opt_term, &opt_te_terminal_name, bad_arg);
	else if (STRCMP(option, "reset") == 0)
		parse_none(argc, argv, &opt_reset, bad_arg);
	else if (STRCMP(option, "initialize") == 0)
		parse_none(argc, argv, &opt_initialize, bad_arg);
	else if (STRCMP(option, "cursor") == 0)
		parse_switch(argc, argv, &opt_cursor, &opt_cu_on, bad_arg);
	else if (STRCMP(option, "repeat") == 0)
		parse_switch(argc, argv, &opt_repeat, &opt_rep_on, bad_arg);
	else if (STRCMP(option, "appcursorkeys") == 0)
		parse_switch(argc, argv, &opt_appcursorkeys, &opt_appck_on, bad_arg);
	else if (STRCMP(option, "linewrap") == 0)
		parse_switch(argc, argv, &opt_linewrap, &opt_li_on, bad_arg);
#if 0
	else if (STRCMP(option, "snow") == 0)
		parse_switch(argc, argv, &opt_snow, &opt_sn_on, bad_arg);
	else if (STRCMP(option, "softscroll") == 0)
		parse_switch(argc, argv, &opt_softscroll, &opt_so_on, bad_arg);
#endif
	else if (STRCMP(option, "default") == 0)
		parse_none(argc, argv, &opt_default, bad_arg);
	else if (STRCMP(option, "foreground") == 0)
		par_color(argc, argv, &opt_foreground, &opt_fo_color, bad_arg);
	else if (STRCMP(option, "background") == 0)
		par_color(argc, argv, &opt_background, &opt_ba_color, bad_arg);
	else if (STRCMP(option, "ulcolor") == 0)
		par_color2(argc, argv, &opt_ulcolor, &opt_ul_color, bad_arg);
	else if (STRCMP(option, "hbcolor") == 0)
		par_color2(argc, argv, &opt_hbcolor, &opt_hb_color, bad_arg);
	else if (STRCMP(option, "inversescreen") == 0)
		parse_switch(argc, argv, &opt_inversescreen, &opt_invsc_on, bad_arg);
	else if (STRCMP(option, "bold") == 0)
		parse_switch(argc, argv, &opt_bold, &opt_bo_on, bad_arg);
	else if (STRCMP(option, "half-bright") == 0)
		parse_switch(argc, argv, &opt_halfbright, &opt_hb_on, bad_arg);
	else if (STRCMP(option, "blink") == 0)
		parse_switch(argc, argv, &opt_blink, &opt_bl_on, bad_arg);
	else if (STRCMP(option, "reverse") == 0)
		parse_switch(argc, argv, &opt_reverse, &opt_re_on, bad_arg);
	else if (STRCMP(option, "underline") == 0)
		parse_switch(argc, argv, &opt_underline, &opt_un_on, bad_arg);
	else if (STRCMP(option, "store") == 0)
		parse_none(argc, argv, &opt_store, bad_arg);
	else if (STRCMP(option, "clear") == 0)
		parse_clear(argc, argv, &opt_clear, &opt_cl_all, bad_arg);
	else if (STRCMP(option, "tabs") == 0)
		parse_tabs(argc, argv, &opt_tabs, opt_tb_array, bad_arg);
	else if (STRCMP(option, "clrtabs") == 0)
		parse_clrtabs(argc, argv, &opt_clrtabs, opt_tb_array, bad_arg);
	else if (STRCMP(option, "regtabs") == 0)
		parse_regtabs(argc, argv, &opt_regtabs, &opt_rt_len, bad_arg);
	else if (STRCMP(option, "blank") == 0)
		parse_blank(argc, argv, &opt_blank, &opt_bl_min, bad_arg);
	else if (STRCMP(option, "dump") == 0)
		parse_snap(argc, argv, &opt_snap, &opt_sn_num, bad_arg);
	else if (STRCMP(option, "append") == 0)
		parse_snap(argc, argv, &opt_append, &opt_sn_num, bad_arg);
	else if (STRCMP(option, "file") == 0)
		parse_snapfile(argc, argv, &opt_snapfile, (int *)opt_sn_name, bad_arg);
	else if (STRCMP(option, "msg") == 0)
		parse_switch(argc, argv, &opt_msg, &opt_msg_on, bad_arg);
	else if (STRCMP(option, "msglevel") == 0)
		parse_msglevel(argc, argv, &opt_msglevel, &opt_msglevel_num, bad_arg);
	else if (STRCMP(option, "powersave") == 0)
		parse_powersave(argc, argv, &opt_powersave, &opt_ps_mode, bad_arg);
	else if (STRCMP(option, "powerdown") == 0)
		parse_blank(argc, argv, &opt_powerdown, &opt_pd_min, bad_arg);
	else if (STRCMP(option, "blength") == 0)
		parse_blength(argc, argv, &opt_blength, &opt_blength_l, bad_arg);
	else if (STRCMP(option, "bfreq") == 0)
		parse_bfreq(argc, argv, &opt_bfreq, &opt_bfreq_f, bad_arg);
#if 0
	else if (STRCMP(option, "standout") == 0)
		parse_standout(argc, argv, &opt_standout, &opt_st_attr, bad_arg);
#endif
	else
		*bad_arg = TRUE;
}

/* End of command line parsing routines. */

static void
usage(char *prog_name) {
/* Print error message about arguments, and the command's syntax. */

	fprintf(stderr, _("%s: Argument error, usage\n"), prog_name);
	fprintf(stderr, "\n");
	fprintf(stderr, "%s\n", prog_name);
	fprintf(stderr, _("  [ -term terminal_name ]\n"));
	fprintf(stderr, _("  [ -reset ]\n"));
	fprintf(stderr, _("  [ -initialize ]\n"));
	fprintf(stderr, _("  [ -cursor [on|off] ]\n"));
#if 0
	fprintf(stderr, _("  [ -snow [on|off] ]\n"));
	fprintf(stderr, _("  [ -softscroll [on|off] ]\n"));
#endif
	fprintf(stderr, _("  [ -repeat [on|off] ]\n"));
	fprintf(stderr, _("  [ -appcursorkeys [on|off] ]\n"));
	fprintf(stderr, _("  [ -linewrap [on|off] ]\n"));
	fprintf(stderr, _("  [ -default ]\n"));
	fprintf(stderr, _("  [ -foreground black|blue|green|cyan"));
	fprintf(stderr, _("|red|magenta|yellow|white|default ]\n"));
	fprintf(stderr, _("  [ -background black|blue|green|cyan"));
	fprintf(stderr, _("|red|magenta|yellow|white|default ]\n"));
	fprintf(stderr, _("  [ -ulcolor black|grey|blue|green|cyan"));
	fprintf(stderr, _("|red|magenta|yellow|white ]\n"));
	fprintf(stderr, _("  [ -ulcolor bright blue|green|cyan"));
	fprintf(stderr, _("|red|magenta|yellow|white ]\n"));
	fprintf(stderr, _("  [ -hbcolor black|grey|blue|green|cyan"));
	fprintf(stderr, _("|red|magenta|yellow|white ]\n"));
	fprintf(stderr, _("  [ -hbcolor bright blue|green|cyan"));
	fprintf(stderr, _("|red|magenta|yellow|white ]\n"));
#if 0
	fprintf(stderr, _("  [ -standout [ attr ] ]\n"));
#endif
	fprintf(stderr, _("  [ -inversescreen [on|off] ]\n"));
	fprintf(stderr, _("  [ -bold [on|off] ]\n"));
	fprintf(stderr, _("  [ -half-bright [on|off] ]\n"));
	fprintf(stderr, _("  [ -blink [on|off] ]\n"));
	fprintf(stderr, _("  [ -reverse [on|off] ]\n"));
	fprintf(stderr, _("  [ -underline [on|off] ]\n"));
	fprintf(stderr, _("  [ -store ]\n"));
	fprintf(stderr, _("  [ -clear [all|rest] ]\n"));
	fprintf(stderr, _("  [ -tabs [ tab1 tab2 tab3 ... ] ]      (tabn = 1-160)\n"));
	fprintf(stderr, _("  [ -clrtabs [ tab1 tab2 tab3 ... ] ]   (tabn = 1-160)\n"));
	fprintf(stderr, _("  [ -regtabs [1-160] ]\n"));
	fprintf(stderr, _("  [ -blank [0-60] ]\n"));
	fprintf(stderr, _("  [ -dump   [1-NR_CONSOLES] ]\n"));
	fprintf(stderr, _("  [ -append [1-NR_CONSOLES] ]\n"));
	fprintf(stderr, _("  [ -file dumpfilename ]\n"));
	fprintf(stderr, _("  [ -msg [on|off] ]\n"));
	fprintf(stderr, _("  [ -msglevel [0-8] ]\n"));
	fprintf(stderr, _("  [ -powersave [on|vsync|hsync|powerdown|off] ]\n"));
	fprintf(stderr, _("  [ -powerdown [0-60] ]\n"));
	fprintf(stderr, _("  [ -blength [0-2000] ]\n"));
	fprintf(stderr, _("  [ -bfreq freqnumber ]\n"));
}

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
	if (opt_linewrap && vcterm) {
		if (opt_li_on)
			printf("\033[?7h");
		else
			printf("\033[?7l");
	}

	/* -repeat [on|off]. Vc only (vt102) */
	if (opt_repeat && vcterm) {
		if (opt_rep_on)
			printf("\033[?8h");
		else
			printf("\033[?8l");
	}

	/* -appcursorkeys [on|off]. Vc only (vt102) */
	if (opt_appcursorkeys && vcterm) {
		if (opt_appck_on)
			printf("\033[?1h");
		else
			printf("\033[?1l");
	}

#if 0
	/* -snow [on|off].  Vc only. */
	if (opt_snow && vcterm) {
		if (opt_sn_on)
			printf("%s%s%s", DCS, "snow.on", ST);
		else
			printf("%s%s%s", DCS, "snow.off", ST);
	}

	/* -softscroll [on|off].  Vc only. */
	if (opt_softscroll && vcterm) {
		if (opt_so_on)
			printf("%s%s%s", DCS, "softscroll.on", ST);
		else
			printf("%s%s%s", DCS, "softscroll.off", ST);
	}
#endif

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
	if (opt_foreground && vcterm) {
		printf("%s%s%c%s", ESC, "[3", '0' + opt_fo_color, "m");
	}

	/* -background black|red|green|yellow|blue|magenta|cyan|white|default.
	 * Vc only (ANSI).
	 */
	if (opt_background && vcterm) {
		printf("%s%s%c%s", ESC, "[4", '0' + opt_ba_color, "m");
	}

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
	if (opt_inversescreen) {
		if (vcterm) {
			if (opt_invsc_on)
				printf("\033[?5h");
			else
				printf("\033[?5l");
		}
	}

	/* -bold [on|off].  Vc behaves as expected, otherwise off turns off
	 * all attributes.
	 */
	if (opt_bold) {
		if (opt_bo_on)
			putp(ti_entry("bold"));
		else {
			if (vcterm)
				printf("%s%s", ESC, "[22m");
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
				printf("%s%s", ESC, "[22m");
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
				printf("%s%s", ESC, "[25m");
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
				printf("%s%s", ESC, "[27m");
			else
				putp(ti_entry("sgr0"));
		}
	}

	/* -underline [on|off]. */
	if (opt_underline) {
		if (opt_un_on)
			putp(ti_entry("smul"));
		else
			putp(ti_entry("rmul"));
	}

	/* -store.  Vc only. */
	if (opt_store && vcterm) {
		printf("\033[8]");
	}

	/* -clear [all|rest]. */
	if (opt_clear) {
		if (opt_cl_all)
			putp(ti_entry("clear"));
		else
			putp(ti_entry("ed"));
	}

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
			printf("\033[3g");
		else
			for(i=0; opt_tb_array[i] > 0; i++)
				printf("\033[%dG\033[g", opt_tb_array[i]);
		putchar('\r');
	}

	/* -regtabs Vc only. */
	if (opt_regtabs && vcterm) {
		int i;

		printf("\033[3g\r");
		for(i=opt_rt_len+1; i<=160; i+=opt_rt_len)
			printf("\033[%dC\033H",opt_rt_len);
		putchar('\r');
	}

	/* -blank [0-60]. */
	if (opt_blank && vcterm) 
		printf("\033[9;%d]", opt_bl_min);
    
	/* -powersave [on|vsync|hsync|powerdown|off] (console) */
	if (opt_powersave) {
		char ioctlarg[2];
		ioctlarg[0] = 10;	/* powersave */
		ioctlarg[1] = opt_ps_mode;
		if (ioctl(0,TIOCLINUX,ioctlarg))
			fprintf(stderr,_("cannot (un)set powersave mode\n"));
	}

	/* -powerdown [0-60]. */
	if (opt_powerdown) {
		printf("\033[14;%d]", opt_pd_min);
	}

#if 0
	/* -standout [num]. */
	if (opt_standout)
		/* nothing */;
#endif

	/* -snap [1-NR_CONS]. */
	if (opt_snap || opt_append) {
		FILE *F;

		F = fopen(opt_sn_name, opt_snap ? "w" : "a");
		if (!F) {
			perror(opt_sn_name);
			fprintf(stderr,("setterm: can not open dump file %s for output\n"),
				opt_sn_name); 
			exit(-1);
		}
		screendump(opt_sn_num, F);
		fclose(F);
	}

	/* -msg [on|off]. */
	if (opt_msg && vcterm) {
		if (opt_msg_on)
			/* 7 -- Enable printk's to console */
			result = klogctl(7, NULL, 0);
		else
			/*  6 -- Disable printk's to console */
			result = klogctl(6, NULL, 0);

		if (result != 0)
			printf(_("klogctl error: %s\n"), strerror(errno));
	}

	/* -msglevel [0-8] */
	if (opt_msglevel && vcterm) {
		/* 8 -- Set level of messages printed to console */
		result = klogctl(8, NULL, opt_msglevel_num);
		if (result != 0)
			printf(_("klogctl error: %s\n"), strerror(errno));
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
screendump(int vcnum, FILE *F) {
    char infile[MAXPATHLEN];
    unsigned char header[4];
    unsigned int rows, cols;
    int fd, i, j;
    char *inbuf, *outbuf, *p, *q;

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
	goto try_ioctl;
    }
    if (read(fd, header, 4) != 4)
	goto try_ioctl;
    rows = header[0];
    cols = header[1];
    if (rows * cols == 0)
        goto try_ioctl;
    inbuf = malloc(rows*cols*2);
    outbuf = malloc(rows*(cols+1));
    if(!inbuf || !outbuf) {
	fputs(_("Out of memory"), stderr);
	exit(1);
    }
    if (read(fd, inbuf, rows*cols*2) != rows*cols*2) {
	fprintf(stderr, _("Error reading %s\n"), infile);
	exit(1);
    }
    p = inbuf;
    q = outbuf;
    for(i=0; i<rows; i++) {
	for(j=0; j<cols; j++) {
	    *q++ = *p;
	    p += 2;
	}
	while(j-- > 0 && q[-1] == ' ')
	  q--;
	*q++ = '\n';
    }
    if (fwrite(outbuf, 1, q-outbuf, F) != q-outbuf) {
	fprintf(stderr, _("Error writing screendump\n"));
	exit(1);
    }
    return;

try_ioctl:
    {
#define NUM_COLS 160
#define NUM_ROWS 75
	char buf[NUM_COLS+1];
	unsigned char screenbuf[NUM_ROWS*NUM_COLS];
	screenbuf[0] = 0;
	screenbuf[1] = (unsigned char) vcnum;
	if (ioctl(0,TIOCLINUX,screenbuf) < 0) {
	    fprintf(stderr,_("couldn't read %s, and cannot ioctl dump\n"),
		    infile);
	    exit(1);
	}
	rows = screenbuf[0];
	cols = screenbuf[1];

	for (i=0; i<rows; i++) {
	    strncpy(buf, screenbuf+2+(cols*i), cols);
	    buf[cols] = '\0';
	    j = cols;
	    while (--j && (buf[j] == ' '))
	      buf[j] = '\0';
	    fputs(buf,F);
	    fputc('\n',F); 
	}
    }
}

int
main(int argc, char **argv) {
	int bad_arg = FALSE;		/* Set if error in arguments. */
	int arg, modifier;
	char *term;			/* Terminal type. */
	int vcterm;			/* Set if terminal is a virtual console. */

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
  
	if (argc < 2)
		bad_arg = TRUE;

	/* Parse arguments. */

	for (arg = 1; arg < argc;) {
		if (*argv[arg] == '-') {

			/* Parse a single option. */

			for (modifier = arg + 1; modifier < argc; modifier++) {
				if (*argv[modifier] == '-') break;
			}
			parse_option(argv[arg] + 1, modifier - arg - 1,
				     &argv[arg + 1], &bad_arg);
			arg = modifier;
		} else {
			bad_arg = TRUE;
			arg++;
		}
	}

	/* Display syntax message if error in arguments. */

	if (bad_arg) {
		usage(argv[0]);
		exit(1);
	}

	/* Find out terminal name. */

	if (opt_term) {
		term = opt_te_terminal_name;
	} else {
		term = getenv("TERM");
		if (term == NULL) {
			fprintf(stderr, _("%s: $TERM is not defined.\n"),
				argv[0]);
			exit(1);
		}
	}

	/* Find terminfo entry. */

	setupterm(term, 1, (int *)0);

	/* See if the terminal is a virtual console terminal. */

	vcterm = (!strncmp(term, "con", 3) || !strncmp(term, "linux", 5));

	/* Perform the selected options. */

	perform_sequence(vcterm);

	return 0;
}
