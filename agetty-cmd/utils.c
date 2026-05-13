#include <stdarg.h>
#include <stdlib.h>
#include <syslog.h>
#include <unistd.h>

#include "agetty.h"

static void dolog(int priority, const char *fmt, va_list ap)
{
	openlog("agetty", LOG_PID, LOG_AUTHPRIV);
	vsyslog(priority, fmt, ap);
	closelog();
}

void agetty_exit_slowly(int code)
{
	/* Be kind to init(8). */
	sleep(10);
	exit(code);
}

void agetty_log_err(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	dolog(LOG_ERR, fmt, ap);
	va_end(ap);

	agetty_exit_slowly(EXIT_FAILURE);
}

void agetty_log_warn(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	dolog(LOG_WARNING, fmt, ap);
	va_end(ap);
}
