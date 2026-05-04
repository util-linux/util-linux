/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <paths.h>

#include "c.h"
#include "default_shell.h"

const char *ul_default_shell(int flags, const struct passwd *pw)
{
	const char *shell = NULL;

	if (!(flags & UL_SHELL_NOENV)) {
		shell = getenv("SHELL");
		if (shell && *shell)
			return shell;
	}
	if (!(flags & UL_SHELL_NOPWD)) {
		if (!pw)
			pw = getpwuid(getuid());
		if (pw)
			shell = pw->pw_shell;
		if (shell && *shell)
			return shell;
	}

	return _PATH_BSHELL;
}
