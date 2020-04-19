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


static int get_env_int(const char *name)
{
	const char *cp = getenv(name);

	if (cp) {
		char *end = NULL;
		long x;

		errno = 0;
		x = strtol(cp, &end, 10);

		if (errno == 0 && end && *end == '\0' && end > cp &&
		    x > 0 && x <= INT_MAX)
			return x;
	}

	return -1;
}

int get_terminal_dimension(int *cols, int *lines)
{
	int c = 0, l = 0;

#if defined(TIOCGWINSZ)
	struct winsize	w_win;
	if (ioctl (STDOUT_FILENO, TIOCGWINSZ, &w_win) == 0) {
		c = w_win.ws_col;
		l = w_win.ws_row;
	}
#elif defined(TIOCGSIZE)
	struct ttysize	t_win;
	if (ioctl (STDOUT_FILENO, TIOCGSIZE, &t_win) == 0) {
		c = t_win.ts_cols;
		l = t_win.ts_lines;
	}
#endif
	if (cols) {
		if (c <= 0)
			c = get_env_int("COLUMNS");
		*cols = c;
	}
	if (lines) {
		if (l <= 0)
			l = get_env_int("LINES");
		*lines = l;
	}
	return 0;
}

int get_terminal_width(int default_width)
{
	int width = 0;

	get_terminal_dimension(&width, NULL);

	return width > 0 ? width : default_width;
}

int get_terminal_stdfd(void)
{
	if (isatty(STDIN_FILENO))
		return STDIN_FILENO;
	if (isatty(STDOUT_FILENO))
		return STDOUT_FILENO;
	if (isatty(STDERR_FILENO))
		return STDERR_FILENO;

	return -EINVAL;
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

	fd = get_terminal_stdfd();
	if (fd < 0)
		return fd;	/* error */

	tty = ttyname(fd);
	if (!tty)
		return -1;

	if (path)
		*path = tty;
	if (name || number)
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

int get_terminal_type(const char **type)
{
	*type = getenv("TERM");
	if (*type)
		return -EINVAL;
	return 0;
}

#ifdef TEST_PROGRAM_TTYUTILS
# include <stdlib.h>
int main(void)
{
	const char *path, *name, *num;
	int c, l;

	if (get_terminal_name(&path, &name, &num) == 0) {
		fprintf(stderr, "tty path:   %s\n", path);
		fprintf(stderr, "tty name:   %s\n", name);
		fprintf(stderr, "tty number: %s\n", num);
	}
	get_terminal_dimension(&c, &l);
	fprintf(stderr,         "tty cols:   %d\n", c);
	fprintf(stderr,         "tty lines:  %d\n", l);


	return EXIT_SUCCESS;
}
#endif /* TEST_PROGRAM_TTYUTILS */
