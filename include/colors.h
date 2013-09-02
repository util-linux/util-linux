/*
 * Copyright (C) 2012 Ondrej Oprala <ooprala@redhat.com>
 *
 * This file may be distributed under the terms of the
 * GNU Lesser General Public License.
 */
#ifndef UTIL_LINUX_COLORS_H
#define UTIL_LINUX_COLORS_H

#include <stdio.h>
#include <unistd.h>

#define UL_COLOR_RESET		"\033[0m"
#define UL_COLOR_BOLD		"\033[1m"
#define UL_COLOR_HALFBRIGHT	"\033[2m"
#define UL_COLOR_UNDERSCORE	"\033[4m"
#define UL_COLOR_BLINK		"\033[5m"
#define UL_COLOR_REVERSE	"\033[7m"

/* Standard colors */
#define UL_COLOR_BLACK		"\033[30m"
#define UL_COLOR_RED		"\033[31m"
#define UL_COLOR_GREEN		"\033[32m"
#define UL_COLOR_BROWN		"\033[33m"	/* well, brown */
#define UL_COLOR_BLUE		"\033[34m"
#define UL_COLOR_MAGENTA	"\033[35m"
#define UL_COLOR_CYAN		"\033[36m"
#define UL_COLOR_GRAY		"\033[37m"

/* Bold variants */
#define UL_COLOR_DARK_GRAY	"\033[1;30m"
#define UL_COLOR_BOLD_RED	"\033[1;31m"
#define UL_COLOR_BOLD_GREEN	"\033[1;32m"
#define UL_COLOR_BOLD_YELLOW	"\033[1;33m"
#define UL_COLOR_BOLD_BLUE	"\033[1;34m"
#define UL_COLOR_BOLD_MAGENTA	"\033[1;35m"
#define UL_COLOR_BOLD_CYAN	"\033[1;36m"

#define UL_COLOR_WHITE		"\033[1;37m"

/* --color[=WHEN] */
enum colortmode {
	UL_COLORMODE_AUTO = 0,
	UL_COLORMODE_NEVER,
	UL_COLORMODE_ALWAYS,

	__UL_NCOLORMODES	/* last */
};

extern int colormode_from_string(const char *str);
extern int colormode_or_err(const char *str, const char *errmsg);

/* Initialize the global variable OUT_IS_TERM */
extern int colors_init(int mode);

/* Returns 1 or 0 */
extern int colors_wanted(void);

/* Set the color to CLR_SCHEME */
extern void color_fenable(const char *clr_scheme, FILE *f);

static inline void color_enable(const char *clr_scheme)
{
	color_fenable(clr_scheme, stdout);
}

/* Reset colors to default */
extern void color_fdisable(FILE *f);

static inline void color_disable(void)
{
	color_fdisable(stdout);
}



#endif /* UTIL_LINUX_COLORS_H */
