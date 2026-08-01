/*
 * No copyright is claimed.  This code is in the public domain; do with
 * it what you wish.
 */
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "all-io.h"
#include "agetty.h"
#include "c.h"
#include "color-names.h"
#include "configs.h"
#include "nls.h"
#include "fileutils.h"
#include "pathnames.h"
#include "widechar.h"

/* The default issue file (e.g. /etc/issue) is read via ul_configs_file_list()
 * whenever ISSUE_SUPPORT is enabled. The drop-in directory (issue.d) scanning
 * performed by that function additionally requires ISSUEDIR_SUPPORT
 * (i.e. scandirat()/openat()); when it is unavailable ul_configs_file_list()
 * still returns the main issue file. */

#ifdef USE_SYSTEMD
# include <systemd/sd-daemon.h>
# include <systemd/sd-login.h>
#endif


static char *read_os_release(struct agetty_options *op, const char *varname)
{
	int fd = -1;
	struct stat st;
	size_t varsz = strlen(varname);
	char *p, *buf = NULL, *ret = NULL;

	/* read the file only once */
	if (!op->osrelease) {
		fd = open(_PATH_OS_RELEASE_ETC, O_RDONLY);
		if (fd == -1) {
			fd = open(_PATH_OS_RELEASE_USR, O_RDONLY);
			if (fd == -1) {
				agetty_log_warn(_("cannot open os-release file"));
				return NULL;
			}
		}

		if (fstat(fd, &st) < 0 || st.st_size > 4 * 1024 * 1024)
			goto done;

		op->osrelease = malloc(st.st_size + 1);
		if (!op->osrelease)
			agetty_log_err(_("failed to allocate memory: %m"));
		if (ul_read_all(fd, op->osrelease, st.st_size) != (ssize_t) st.st_size) {
			free(op->osrelease);
			op->osrelease = NULL;
			goto done;
		}
		op->osrelease[st.st_size] = 0;
	}
	buf = strdup(op->osrelease);
	if (!buf)
		agetty_log_err(_("failed to allocate memory: %m"));
	p = buf;

	for (;;) {
		char *eol, *eon;

		p += strspn(p, "\n\r");
		p += strspn(p, " \t\n\r");
		if (!*p)
			break;
		if (strspn(p, "#;\n") != 0) {
			p += strcspn(p, "\n\r");
			continue;
		}
		if (strncmp(p, varname, varsz) != 0) {
			p += strcspn(p, "\n\r");
			continue;
		}
		p += varsz;
		p += strspn(p, " \t\n\r");

		if (*p != '=')
			continue;

		p += strspn(p, " \t\n\r=\"");
		eol = p + strcspn(p, "\n\r");
		*eol = '\0';
		eon = eol-1;
		while (eon > p) {
			if (*eon == '\t' || *eon == ' ') {
				eon--;
				continue;
			}
			if (*eon == '"') {
				*eon = '\0';
				break;
			}
			break;
		}
		free(ret);
		ret = strdup(p);
		if (!ret)
			agetty_log_err(_("failed to allocate memory: %m"));
		p = eol + 1;
	}
done:
	free(buf);
	if (fd >= 0)
		close(fd);
	return ret;
}


#ifndef ISSUE_SUPPORT
void agetty_issue_print(struct agetty_issue *ie __attribute__((__unused__)),
			     struct agetty_options *op,
			     struct termios *tp __attribute__((__unused__)))
{
	if ((op->flags & F_NONL) == 0) {
		/* Issue not in use, start with a new line. */
		ul_write_all(STDOUT_FILENO, "\r\n", 2);
	}
}

void agetty_issue_eval(struct agetty_issue *ie __attribute__((__unused__)),
			    struct agetty_options *op __attribute__((__unused__)),
			    struct termios *tp __attribute__((__unused__)))
{
}

void agetty_issue_show(struct agetty_options *op __attribute__((__unused__)))
{
}

#else /* ISSUE_SUPPORT */

static int print_uname(struct agetty_iitem *item,
		       struct agetty_issue *ie,
		       struct agetty_ihandler *handler __attribute__((__unused__)))
{
	struct utsname uts;

	uname(&uts);
	switch (agetty_iitem_get_id(item)) {
	case AGETTY_ESC_SYSNAME:
		fputs(uts.sysname, ie->output);
		break;
	case AGETTY_ESC_NODENAME:
		fputs(uts.nodename, ie->output);
		break;
	case AGETTY_ESC_RELEASE:
		fputs(uts.release, ie->output);
		break;
	case AGETTY_ESC_VERSION:
		fputs(uts.version, ie->output);
		break;
	case AGETTY_ESC_MACHINE:
		fputs(uts.machine, ie->output);
		break;
	}
	return 0;
}

static int print_nis_domain(struct agetty_iitem *item __attribute__((__unused__)),
			   struct agetty_issue *ie,
			   struct agetty_ihandler *handler __attribute__((__unused__)))
{
	char *dom = agetty_xgetdomainname();

	fputs(dom ? dom : "unknown_domain", ie->output);
	free(dom);
	return 0;
}

static int print_dns_domain(struct agetty_iitem *item __attribute__((__unused__)),
			    struct agetty_issue *ie,
			    struct agetty_ihandler *handler __attribute__((__unused__)))
{
	char *dom = NULL;
	char *host = agetty_xgethostname();
	struct addrinfo hints, *info = NULL;

	memset(&hints, 0, sizeof(hints));
	hints.ai_flags = AI_CANONNAME;

	if (host && getaddrinfo(host, NULL, &hints, &info) == 0 && info) {
		char *canon;

		if (info->ai_canonname &&
		    (canon = strchr(info->ai_canonname, '.')))
			dom = canon + 1;
	}
	fputs(dom ? dom : "unknown_domain", ie->output);
	if (info)
		freeaddrinfo(info);
	free(host);
	return 0;
}

static int print_time(struct agetty_iitem *item,
		      struct agetty_issue *ie,
		      struct agetty_ihandler *handler __attribute__((__unused__)))
{
	time_t now;
	struct tm tm;

	time(&now);
	localtime_r(&now, &tm);

	if (agetty_iitem_get_id(item) == AGETTY_ESC_DATE)
		fprintf(ie->output, "%s %s %2d  %d",
			nl_langinfo(ABDAY_1 + tm.tm_wday),
			nl_langinfo(ABMON_1 + tm.tm_mon),
			tm.tm_mday,
			tm.tm_year < 70 ? tm.tm_year + 2000 :
			tm.tm_year + 1900);
	else
		fprintf(ie->output, "%02d:%02d:%02d",
			tm.tm_hour, tm.tm_min, tm.tm_sec);
	return 0;
}

static int print_osrelease(struct agetty_iitem *item,
			   struct agetty_issue *ie,
			   struct agetty_ihandler *handler __attribute__((__unused__)))
{
	const char *varname = agetty_iitem_get_arg(item, "variable");
	char *var;

	if (!varname)
		varname = agetty_iitem_get_arg(item, NULL);

	if (varname) {
		var = read_os_release(ie->op, varname);
		if (var) {
			if (strcmp(varname, "ANSI_COLOR") == 0)
				fprintf(ie->output, "\033[%sm", var);
			else
				fputs(var, ie->output);
		}
	} else if ((var = read_os_release(ie->op, "PRETTY_NAME"))) {
		fputs(var, ie->output);
	} else {
		struct utsname uts;

		uname(&uts);
		fputs(uts.sysname, ie->output);
	}

	free(var);
	return 0;
}

static int print_users(struct agetty_iitem *item,
		       struct agetty_issue *ie,
		       struct agetty_ihandler *handler __attribute__((__unused__)))
{
	int users = 0;
#ifdef USE_SYSTEMD
	if (sd_booted() > 0) {
		users = sd_get_sessions(NULL);
		if (users < 0)
			users = 0;
	} else
#endif
	{
		struct utmpx *ut;

		setutxent();
		while ((ut = getutxent()))
			if (ut->ut_type == USER_PROCESS)
				users++;
		endutxent();
	}

	if (agetty_iitem_get_id(item) == AGETTY_ESC_USERS_TEXT)
		fprintf(ie->output, P_("%d user", "%d users", users), users);
	else
		fprintf(ie->output, "%d ", users);
	return 0;
}

static int print_escape_char(struct agetty_iitem *item,
			     struct agetty_issue *ie,
			     struct agetty_ihandler *handler __attribute__((__unused__)))
{
	const char *color = agetty_iitem_get_arg(item, "color");

	if (!color)
		color = agetty_iitem_get_arg(item, NULL);

	if (color) {
		char *esc = color_get_sequence(color);

		if (esc) {
			fputs(esc, ie->output);
			free(esc);
		}
	} else
		fputs("\033", ie->output);
	return 0;
}

static int print_ttyname(struct agetty_iitem *item __attribute__((__unused__)),
			 struct agetty_issue *ie,
			 struct agetty_ihandler *handler __attribute__((__unused__)))
{
	fprintf(ie->output, "%s", ie->op->tty);
	return 0;
}

static int print_baudrate(struct agetty_iitem *item __attribute__((__unused__)),
			  struct agetty_issue *ie,
			  struct agetty_ihandler *handler __attribute__((__unused__)))
{
	agetty_fprint_speed(ie->output, cfgetispeed(ie->tp));
	return 0;
}

static void register_handlers(struct agetty_issue *ie)
{
	struct agetty_ifile *ls = &ie->ifile;

	agetty_ifile_set_handler(ls, AGETTY_ESC_SYSNAME, print_uname, NULL, NULL);
	agetty_ifile_set_handler(ls, AGETTY_ESC_NODENAME, print_uname, NULL, NULL);
	agetty_ifile_set_handler(ls, AGETTY_ESC_RELEASE, print_uname, NULL, NULL);
	agetty_ifile_set_handler(ls, AGETTY_ESC_VERSION, print_uname, NULL, NULL);
	agetty_ifile_set_handler(ls, AGETTY_ESC_MACHINE, print_uname, NULL, NULL);

	agetty_ifile_set_handler(ls, AGETTY_ESC_NIS_DOMAIN, print_nis_domain, NULL, NULL);
	agetty_ifile_set_handler(ls, AGETTY_ESC_DNS_DOMAIN, print_dns_domain, NULL, NULL);

	agetty_ifile_set_handler(ls, AGETTY_ESC_DATE, print_time, NULL, NULL);
	agetty_ifile_set_handler(ls, AGETTY_ESC_TIME, print_time, NULL, NULL);

	agetty_ifile_set_handler(ls, AGETTY_ESC_TTYNAME, print_ttyname, NULL, NULL);
	agetty_ifile_set_handler(ls, AGETTY_ESC_BAUDRATE, print_baudrate, NULL, NULL);

	agetty_ifile_set_handler(ls, AGETTY_ESC_OSRELEASE, print_osrelease, NULL, NULL);

	agetty_ifile_set_handler(ls, AGETTY_ESC_USERS, print_users, NULL, NULL);
	agetty_ifile_set_handler(ls, AGETTY_ESC_USERS_TEXT, print_users, NULL, NULL);

	agetty_ifile_set_handler(ls, AGETTY_ESC_ESCAPE, print_escape_char, NULL, NULL);

	agetty_issue_register_netlink(ie);
}

#ifdef AGETTY_RELOAD
int agetty_issue_is_changed(struct agetty_issue *ie)
{
	if (ie->mem_old && ie->mem
	    && strcmp(ie->mem_old, ie->mem) == 0) {
		free(ie->mem_old);
		ie->mem_old = ie->mem;
		ie->mem = NULL;
		ie->mem_sz = 0;
		return 0;
	}

	return 1;
}
#endif

void agetty_issue_print(struct agetty_issue *ie,
			     struct agetty_options *op,
			     struct termios *tp)
{
	int oflag = tp->c_oflag;	    /* Save current setting. */

	if ((op->flags & F_NONL) == 0) {
		/* Issue not in use, start with a new line. */
		ul_write_all(STDOUT_FILENO, "\r\n", 2);
	}

	if (ie->do_tcsetattr) {
		if ((op->flags & F_VCONSOLE) == 0) {
			/* Map new line in output to carriage return & new line. */
			tp->c_oflag |= (ONLCR | OPOST);
			tcsetattr(STDIN_FILENO, TCSADRAIN, tp);
		}
	}

	if (ie->mem_sz && ie->mem)
		ul_write_all(STDOUT_FILENO, ie->mem, ie->mem_sz);

	if (ie->do_tcrestore) {
		/* Restore settings. */
		tp->c_oflag = oflag;
		/* Wait till output is gone. */
		tcsetattr(STDIN_FILENO, TCSADRAIN, tp);
	}

#ifdef AGETTY_RELOAD
	free(ie->mem_old);
	ie->mem_old = ie->mem;
	ie->mem = NULL;
	ie->mem_sz = 0;
#else
	free(ie->mem);
	ie->mem = NULL;
	ie->mem_sz = 0;
#endif
}

void agetty_issue_reset(struct agetty_issue *ie)
{
	if (agetty_ifile_is_ready(&ie->ifile))
		agetty_ifile_free(&ie->ifile);
	ie->parsed = false;
}

void agetty_issue_eval(struct agetty_issue *ie,
			    struct agetty_options *op,
			    struct termios *tp)
{
	if (!(op->flags & F_ISSUE))
		goto done;

	ie->op = op;
	ie->tp = tp;

	if (!ie->parsed) {
		agetty_issue_reset(ie);
		agetty_ifile_init(&ie->ifile);

		if (op->issue)
			agetty_ifile_parse_spec(&ie->ifile, op->issue);
		else {
			struct list_head plist;
			struct list_head *pcur = NULL;
			char *pname = NULL;

			ul_configs_file_list(&plist,
					     NULL,
					     _PATH_SYSCONFDIR,
					     _PATH_RUNSTATEDIR,
					     _PATH_SYSCONFSTATICDIR,
					     "issue",
					     ISSUEDIR_EXT);

			while (ul_configs_next_filename(&plist, &pcur, &pname) == 0)
				agetty_ifile_parse_file(&ie->ifile, pname);

			ul_configs_free_list(&plist);
		}
		register_handlers(ie);
		ie->parsed = true;
	}

	if (agetty_ifile_is_empty(&ie->ifile))
		goto done;

	free(ie->mem);
	ie->mem = NULL;
	ie->mem_sz = 0;
	ie->output = open_memstream(&ie->mem, &ie->mem_sz);

	agetty_ifile_print(&ie->ifile, ie, ie->output);

done:
	if (ie->output) {
		fclose(ie->output);
		ie->output = NULL;
	}
}

/* This is --show-issue backend, executed by normal user on the current
 * terminal.
 */
void agetty_issue_show(struct agetty_options *op)
{
	struct agetty_issue ie = {
		.output = NULL,
#ifdef USE_NETLINK
		.nl.fd = -1
#endif
	};
	struct termios tp;

	memset(&tp, 0, sizeof(struct termios));
	if (tcgetattr(STDIN_FILENO, &tp) < 0)
		err(EXIT_FAILURE, _("failed to get terminal attributes: %m"));

	agetty_issue_eval(&ie, op, &tp);

	if (ie.mem_sz)
		ul_write_all(STDOUT_FILENO, ie.mem, ie.mem_sz);
	if (ie.output)
		fclose(ie.output);
	agetty_ifile_free(&ie.ifile);
	free(ie.mem);
}

#endif /* ISSUE_SUPPORT */


void agetty_reload(void)
{
#ifdef AGETTY_RELOAD
	int fd = open(AGETTY_RELOAD_FILENAME, O_CREAT|O_CLOEXEC|O_WRONLY,
					      S_IRUSR|S_IWUSR);
	if (fd < 0)
		err(EXIT_FAILURE, _("cannot open %s"), AGETTY_RELOAD_FILENAME);

	if (futimens(fd, NULL) < 0 || close(fd) < 0)
		err(EXIT_FAILURE, _("cannot touch file %s"),
		    AGETTY_RELOAD_FILENAME);
#else
	/* very unusual */
	errx(EXIT_FAILURE, _("--reload is unsupported on your system"));
#endif
}
