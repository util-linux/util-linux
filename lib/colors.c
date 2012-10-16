/*
 * Copyright (C) 2012 Ondrej Oprala <ooprala@redhat.com>
 *
 * This file may be distributed under the terms of the
 * GNU Lesser General Public License.
 */

#include "colors.h"

static int ul_color_term_ok;

int colors_init(void)
{
	ul_color_term_ok = isatty(STDOUT_FILENO);
	return ul_color_term_ok;
}

void color_enable(const char *color_scheme)
{
	if (ul_color_term_ok)
		fputs(color_scheme, stdout);
}

void color_disable(void)
{
	if (ul_color_term_ok)
		fputs(UL_COLOR_RESET, stdout);
}
