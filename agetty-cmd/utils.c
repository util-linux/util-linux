#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <utmpx.h>

#include "agetty.h"
#include "nls.h"
#include "pathnames.h"
#include "strutils.h"
#include "ttyutils.h"

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

char *agetty_xgethostname(void)
{
	char *name;
	size_t sz = get_hostname_max() + 1;

	name = malloc(sizeof(char) * sz);
	if (!name)
		agetty_log_err(_("failed to allocate memory: %m"));

	if (gethostname(name, sz) != 0) {
		free(name);
		return NULL;
	}
	name[sz - 1] = '\0';
	return name;
}

char *agetty_xgetdomainname(void)
{
#ifdef HAVE_GETDOMAINNAME
	char *name;
	const size_t sz = get_hostname_max() + 1;

	name = malloc(sizeof(char) * sz);
	if (!name)
		agetty_log_err(_("failed to allocate memory: %m"));

	if (getdomainname(name, sz) != 0) {
		free(name);
		return NULL;
	}
	name[sz - 1] = '\0';
	return name;
#else
	return NULL;
#endif
}

#ifdef SYSV_STYLE
void agetty_update_utmp(struct agetty_options *op, const char *fakehost)
{
	struct utmpx ut;
	time_t t;
	pid_t pid = getpid();
	pid_t sid = getsid(0);
	const char *vcline = op->vcline;
	const char *line = op->tty;
	struct utmpx *utp;

	/*
	 * The utmp file holds miscellaneous information about things started by
	 * /sbin/init and other system-related events. Our purpose is to update
	 * the utmp entry for the current process, in particular the process type
	 * and the tty line we are listening to. Return successfully only if the
	 * utmp file can be opened for update, and if we are able to find our
	 * entry in the utmp file.
	 */
	utmpxname(_PATH_UTMP);
	setutxent();

	/*
	 * Find my pid in utmp.
	 *
	 * FIXME: Earlier (when was that?) code here tested only utp->ut_type !=
	 * INIT_PROCESS, so maybe the >= here should be >.
	 *
	 * FIXME: The present code is taken from login.c, so if this is changed,
	 * maybe login has to be changed as well (is this true?).
	 */
	while ((utp = getutxent()))
		if (utp->ut_pid == pid
				&& utp->ut_type >= INIT_PROCESS
				&& utp->ut_type <= DEAD_PROCESS)
			break;

	if (utp) {
		memcpy(&ut, utp, sizeof(ut));
	} else {
		/* Some inits do not initialize utmp. */
		memset(&ut, 0, sizeof(ut));
		if (vcline && *vcline)
			/* Standard virtual console devices */
			str2memcpy(ut.ut_id, vcline, sizeof(ut.ut_id));
		else {
			size_t len = strlen(line);
			const char * ptr;
			if (len >= sizeof(ut.ut_id))
				ptr = line + len - sizeof(ut.ut_id);
			else
				ptr = line;
			str2memcpy(ut.ut_id, ptr, sizeof(ut.ut_id));
		}
	}

	str2memcpy(ut.ut_user, "LOGIN", sizeof(ut.ut_user));
	str2memcpy(ut.ut_line, line, sizeof(ut.ut_line));
	if (fakehost)
		str2memcpy(ut.ut_host, fakehost, sizeof(ut.ut_host));
	time(&t);
	ut.ut_tv.tv_sec = t;
	ut.ut_type = LOGIN_PROCESS;
	ut.ut_pid = pid;
	ut.ut_session = sid;

	pututxline(&ut);
	endutxent();

	updwtmpx(_PATH_WTMP, &ut);
}
#endif

void agetty_parse_speeds(struct agetty_options *op, char *arg)
{
	char *cp;
	char *str = strdup(arg);

	if (!str)
		agetty_log_err(_("failed to allocate memory: %m"));

	for (cp = strtok(str, ","); cp != NULL; cp = strtok(NULL, ",")) {
		if ((op->speeds[op->numspeed++] = agetty_bcode(cp)) <= 0)
			agetty_log_err(_("bad speed: %s"), cp);
		if (op->numspeed >= MAX_SPEED)
			agetty_log_err(_("too many alternate speeds"));
	}
	free(str);
}

char *agetty_parse_initstring(const char *arg)
{
	char ch, *str, *p, *q;
	int i;

	str = malloc(strlen(arg) + 1);
	if (!str)
		agetty_log_err(_("failed to allocate memory: %m"));

	q = str;
	p = (char *) arg;
	while (*p) {
		if (*p == '\\') {
			p++;
			if (*p == '\\') {
				ch = '\\';
				p++;
			} else {
				ch = 0;
				for (i = 1; i <= 3; i++) {
					if (*p >= '0' && *p <= '7') {
						ch <<= 3;
						ch += *p - '0';
						p++;
					} else
						break;
				}
			}
			*q++ = ch;
		} else
			*q++ = *p++;
	}
	*q = '\0';
	return str;
}
