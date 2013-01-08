/*
 * No copyright is claimed.  This code is in the public domain; do with
 * it what you wish.
 *
 * Written by Karel Zak <kzak@redhat.com>
 */
#include <ctype.h>

#include "c.h"
#include "ttyutils.h"

int get_terminal_width(void)
{
#ifdef TIOCGSIZE
	struct ttysize	t_win;
#endif
#ifdef TIOCGWINSZ
	struct winsize	w_win;
#endif
        const char	*cp;

#ifdef TIOCGSIZE
	if (ioctl (0, TIOCGSIZE, &t_win) == 0)
		return t_win.ts_cols;
#endif
#ifdef TIOCGWINSZ
	if (ioctl (0, TIOCGWINSZ, &w_win) == 0)
		return w_win.ws_col;
#endif
        cp = getenv("COLUMNS");
	if (cp) {
		char *end = NULL;
		long c;

		errno = 0;
		c = strtol(cp, &end, 10);

		if (errno == 0 && end && *end == '\0' && end > cp &&
		    c > 0 && c <= INT_MAX)
			return c;
	}
	return 0;
}

int get_terminal_name(const char **path,
		      const char **name,
		      const char **number)
{
	const char *tty;
	const char *p;

	if (name)
		*name = NULL;
	if (path)
		*path = NULL;
	if (number)
		*number = NULL;

	tty = ttyname(STDERR_FILENO);
	if (!tty)
		return -1;
	if (path)
		*path = tty;
	tty = strncmp(tty, "/dev/", 5) == 0 ? tty + 5 : tty;
	if (name)
		*name = tty;
	if (number) {
		for (p = tty; p && *p; p++) {
			if (isdigit(*p)) {
				*number = p;
				break;
			}
		}
	}
	return 0;
}


#ifdef TEST_PROGRAM
# include <stdlib.h>
int main(void)
{
	const char *path, *name, *num;

	if (get_terminal_name(&path, &name, &num) == 0) {
		fprintf(stderr, "tty path:   %s\n", path);
		fprintf(stderr, "tty name:   %s\n", name);
		fprintf(stderr, "tty number: %s\n", num);
	}
	fprintf(stderr,         "tty width:  %d\n", get_terminal_width());

	return EXIT_SUCCESS;
}
#endif
