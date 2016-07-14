/*
 * No copyright is claimed.  This code is in the public domain; do with
 * it what you wish.
 *
 * Written by Karel Zak <kzak@redhat.com>
 */
#include <ctype.h>
#include <unistd.h>

#include "c.h"
#include "ttyutils.h"

int get_terminal_width(int default_width)
{
	int width = 0;

#if defined(TIOCGWINSZ)
	struct winsize	w_win;
	if (ioctl (STDOUT_FILENO, TIOCGWINSZ, &w_win) == 0)
		width = w_win.ws_col;
#elif defined(TIOCGSIZE)
	struct ttysize	t_win;
	if (ioctl (STDOUT_FILENO, TIOCGSIZE, &t_win) == 0)
		width = t_win.ts_cols;
#endif

	if (width <= 0) {
		const char *cp = getenv("COLUMNS");

		if (cp) {
			char *end = NULL;
			long c;

			errno = 0;
			c = strtol(cp, &end, 10);

			if (errno == 0 && end && *end == '\0' && end > cp &&
			    c > 0 && c <= INT_MAX)
				width = c;
		}
	}

	return width > 0 ? width : default_width;
}

int get_terminal_name(const char **path,
		      const char **name,
		      const char **number)
{
	const char *tty;
	const char *p;
	int fd;


	if (name)
		*name = NULL;
	if (path)
		*path = NULL;
	if (number)
		*number = NULL;

	if (isatty(STDIN_FILENO))
		fd = STDIN_FILENO;
	else if (isatty(STDOUT_FILENO))
		fd = STDOUT_FILENO;
	else if (isatty(STDERR_FILENO))
		fd = STDERR_FILENO;
	else
		return -1;

	tty = ttyname(fd);
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
	fprintf(stderr,         "tty width:  %d\n", get_terminal_width(0));

	return EXIT_SUCCESS;
}
#endif
