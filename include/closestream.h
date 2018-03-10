#ifndef UTIL_LINUX_CLOSESTREAM_H
#define UTIL_LINUX_CLOSESTREAM_H

#include <stdio.h>
#ifdef HAVE_STDIO_EXT_H
#include <stdio_ext.h>
#endif
#include <unistd.h>

#include "c.h"
#include "nls.h"

#ifndef CLOSE_EXIT_CODE
# define CLOSE_EXIT_CODE EXIT_FAILURE
#endif

static inline int
close_stream(FILE * stream)
{
#ifdef HAVE___FPENDING
	const int some_pending = (__fpending(stream) != 0);
#endif
	const int prev_fail = (ferror(stream) != 0);
	const int fclose_fail = (fclose(stream) != 0);

	if (prev_fail || (fclose_fail && (
#ifdef HAVE___FPENDING
					  some_pending ||
#endif
					  errno != EBADF))) {
		if (!fclose_fail && !(errno == EPIPE))
			errno = 0;
		return EOF;
	}
	return 0;
}

/* Meant to be used atexit(close_stdout); */
static inline void
close_stdout(void)
{
	if (close_stream(stdout) != 0 && !(errno == EPIPE)) {
		if (errno)
			warn(_("write error"));
		else
			warnx(_("write error"));
		_exit(CLOSE_EXIT_CODE);
	}

	if (close_stream(stderr) != 0)
		_exit(CLOSE_EXIT_CODE);
}

#ifndef HAVE_FSYNC
static inline int
fsync(int fd __attribute__((__unused__)))
{
	return 0;
}
#endif

static inline int
close_fd(int fd)
{
	const int fsync_fail = (fsync(fd) != 0);
	const int close_fail = (close(fd) != 0);

	if (fsync_fail || close_fail)
		return EOF;
	return 0;
}

#endif /* UTIL_LINUX_CLOSESTREAM_H */
