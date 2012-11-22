
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

#ifdef TEST_PROGRAM
# include <stdlib.h>

int main(void)
{
	fprintf(stderr, "tty width: %d\n", get_terminal_width());
	return EXIT_SUCCESS;
}
#endif
