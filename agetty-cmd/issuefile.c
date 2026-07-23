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
#include "nls.h"
#include "fileutils.h"
#include "pathnames.h"
#include "widechar.h"

#ifdef ISSUEDIR_SUPPORT
# include "configs.h"
# include <dirent.h>
# define ISSUEDIR_EXT	"issue"
# define ISSUEDIR_EXTSIZ	sizeof(ISSUEDIR_EXT)
#endif

#ifdef USE_SYSTEMD
# include "dl-systemd.h"
#endif

#ifdef USE_NETLINK
# include <net/if.h>
# include <arpa/inet.h>
static uint32_t netlink_groups;
#endif

static void output_special_char(struct agetty_issue *ie, unsigned char c,
				struct agetty_options *op, struct termios *tp, FILE *fp);

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


#ifdef ISSUEDIR_SUPPORT
static int issuedir_filter(const struct dirent *d)
{
	size_t namesz;

#ifdef _DIRENT_HAVE_D_TYPE
	if (d->d_type != DT_UNKNOWN && d->d_type != DT_REG &&
	    d->d_type != DT_LNK)
		return 0;
#endif
	if (*d->d_name == '.')
		return 0;

	namesz = strlen(d->d_name);
	if (!namesz || namesz < ISSUEDIR_EXTSIZ + 1 ||
	    strcmp(d->d_name + (namesz - ISSUEDIR_EXTSIZ), "." ISSUEDIR_EXT) != 0)
		return 0;

	/* Accept this */
	return 1;
}


static int issuefile_read_stream(struct agetty_issue *ie, FILE *f, struct agetty_options *op, struct termios *tp);

/* returns: 0 on success, 1 cannot open, <0 on error
 */
static int issuedir_read(struct agetty_issue *ie, const char *dirname,
			 struct agetty_options *op, struct termios *tp)
{
	int dd, nfiles, i;
	struct dirent **namelist = NULL;

	dd = open(dirname, O_RDONLY|O_CLOEXEC|O_DIRECTORY);
	if (dd < 0)
		return 1;

	nfiles = scandirat(dd, ".", &namelist, issuedir_filter, versionsort);
	if (nfiles <= 0)
		goto done;

	ie->do_tcsetattr = 1;

	for (i = 0; i < nfiles; i++) {
		struct dirent *d = namelist[i];
		FILE *f;

		f = fopen_at(dd, d->d_name, O_RDONLY|O_CLOEXEC, "r" UL_CLOEXECSTR);
		if (f) {
			issuefile_read_stream(ie, f, op, tp);
			fclose(f);
		}
	}

	for (i = 0; i < nfiles; i++)
		free(namelist[i]);
	free(namelist);
done:
	close(dd);
	return 0;
}

#else /* !ISSUEDIR_SUPPORT */
static int issuedir_read(struct agetty_issue *ie __attribute__((__unused__)),
			const char *dirname __attribute__((__unused__)),
			struct agetty_options *op __attribute__((__unused__)),
			struct termios *tp __attribute__((__unused__)))
{
	return 1;
}
#endif /* ISSUEDIR_SUPPORT */

#ifndef ISSUE_SUPPORT
void agetty_print_issue_file(struct agetty_issue *ie __attribute__((__unused__)),
			     struct agetty_options *op,
			     struct termios *tp __attribute__((__unused__)))
{
	if ((op->flags & F_NONL) == 0) {
		/* Issue not in use, start with a new line. */
		ul_write_all(STDOUT_FILENO, "\r\n", 2);
	}
}

void agetty_eval_issue_file(struct agetty_issue *ie __attribute__((__unused__)),
			    struct agetty_options *op __attribute__((__unused__)),
			    struct termios *tp __attribute__((__unused__)))
{
}

void agetty_show_issue(struct agetty_options *op __attribute__((__unused__)))
{
}

#else /* ISSUE_SUPPORT */

static int issuefile_read_stream(
		struct agetty_issue *ie, FILE *f,
		struct agetty_options *op, struct termios *tp)
{
	struct stat st;
	int c;

	if (fstat(fileno(f), &st) || !S_ISREG(st.st_mode))
		return 1;

	if (!ie->output) {
		free(ie->mem);
		ie->mem_sz = 0;
		ie->mem = NULL;
		ie->output = open_memstream(&ie->mem, &ie->mem_sz);
	}

	while ((c = fgetc(f)) != EOF) {
		if (c == '\\')
			output_special_char(ie, fgetc(f), op, tp, f);
		else
			putc(c, ie->output);
	}

	return 0;
}

static int issuefile_read(
		struct agetty_issue *ie, const char *filename,
		struct agetty_options *op, struct termios *tp)
{
	FILE *f = fopen(filename, "r" UL_CLOEXECSTR);
	int rc = 1;

	if (f) {
		rc = issuefile_read_stream(ie, f, op, tp);
		fclose(f);
	}
	return rc;
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

void agetty_print_issue_file(struct agetty_issue *ie,
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

void agetty_eval_issue_file(struct agetty_issue *ie,
			    struct agetty_options *op,
			    struct termios *tp)
{
	if (!(op->flags & F_ISSUE))
		goto done;

#ifdef USE_NETLINK
/* TODO:
 * Two pass processing for agetty_eval_issue_file()
 * Implement pass 1: Just evaluate list of netlink_groups (IP protocols) and
 * interfaces to monitor.
 * That is why again label is here: netlink_groups will be re-evaluated and
 * dump will be performed again.
 */
	/* netlink_groups = 0; */
	netlink_groups = RTMGRP_IPV4_IFADDR | RTMGRP_IPV6_IFADDR;

	/* Already initialized? */
	if (ie->nl.fd >= 0)
		goto skip;
	/* Prepare netlink. */
	ul_nl_init(&(ie->nl));
	if ((ul_netaddrq_init(&(ie->nl), NULL, NULL, (void *)ie)))
		goto skip;

	/* Open netlink and create address list. */
	if (ul_nl_open(&(ie->nl),
		       RTMGRP_LINK | netlink_groups))
		goto skip;
	if (ul_nl_request_dump(&(ie->nl), RTM_GETADDR))
		goto error;
	if (ul_nl_process(&(ie->nl), UL_NL_SYNC, UL_NL_LOOP) != UL_NL_DONE)
		goto error;
	goto skip;
error:
	/* In case of any error, the addrq list is just empty, and we can use
	 * the code without any error checking. */
	ul_nl_close(&(ie->nl));
	ie->nl.fd = -1;
skip:
#endif /* USE_NETLINK */
	/*
	 * The custom issue file or directory list specified by:
	 *   agetty --issue-file <path[:path]...>
	 * Note that nothing is printed if the file/dir does not exist.
	 */
	if (op->issue) {
		char *list = strdup(op->issue);
		char *file;

		if (!list)
			agetty_log_err(_("failed to allocate memory: %m"));

		for (file = strtok(list, ":"); file; file = strtok(NULL, ":")) {
			struct stat st;

			if (stat(file, &st) < 0)
				continue;
			if (S_ISDIR(st.st_mode))
				issuedir_read(ie, file, op, tp);
			else
				issuefile_read(ie, file, op, tp);
		}
		free(list);
		goto done;
	}

#ifdef ISSUEDIR_SUPPORT
	struct list_head file_list;
	struct list_head *current = NULL;
	char *name = NULL;

	/* Reading all issue files and concatenating all contents to one content.
	 * The ordering rules are defineded in:
	 * https://github.com/uapi-group/specifications/blob/main/specs/configuration_files_specification.md
	 *
	 * Note that _PATH_RUNSTATEDIR (/run) is always read by ul_configs_file_list().
	 */
	ul_configs_file_list(&file_list,
			     NULL,
			     _PATH_SYSCONFDIR,
			     _PATH_RUNSTATEDIR,
			     _PATH_SYSCONFSTATICDIR,
			     "issue",
			     ISSUEDIR_EXT);

	while (ul_configs_next_filename(&file_list, &current, &name) == 0) {
		issuefile_read(ie, name, op, tp);
	}

	ul_configs_free_list(&file_list);
#endif

done:
	if (ie->output) {
		fclose(ie->output);
		ie->output = NULL;
	}
}

/* This is --show-issue backend, executed by normal user on the current
 * terminal.
 */
void agetty_show_issue(struct agetty_options *op)
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

	agetty_eval_issue_file(&ie, op, &tp);

	if (ie.mem_sz)
		ul_write_all(STDOUT_FILENO, ie.mem, ie.mem_sz);
	if (ie.output)
		fclose(ie.output);
	free(ie.mem);
}

#endif /* ISSUE_SUPPORT */

#ifdef USE_NETLINK
static void print_iface_best(struct agetty_issue *ie,
			     const char *ifname,
			     uint8_t ifa_family)
{
	struct ul_netaddrq_ip *best[__ULNETLINK_RATING_MAX];
	struct ul_netaddrq_iface *ifaceq;
	struct list_head *l;
	enum ul_netaddrq_ip_rating threshold;

	if (!ie->nl.data_addr)
		return; /* error: init failed */

	if ((ifaceq = ul_netaddrq_iface_by_name(&(ie->nl), ifname)))
	{
		memset(best, 0, sizeof(best));
		if (ifa_family == AF_INET)
			l = &(ifaceq->ip_quality_list_4);
		else
		/* if (ifa_family == AF_INET6) */
			l = &(ifaceq->ip_quality_list_6);

		threshold =
			ul_netaddrq_iface_bestaddr(l, &best);
		if (threshold != __ULNETLINK_RATING_MAX)
			fputs(ul_nl_addr_ntop_address(best[threshold]->addr),
			      ie->output);
	}
}

static void print_addrq_bestofall(struct agetty_issue *ie,
				  uint8_t ifa_family)
{
	struct ul_netaddrq_iface *best_ifaceq;
	enum ul_netaddrq_ip_rating threshold;
	const char *best_ipp;

	if (!ie->nl.data_addr)
		return; /* error: init failed */

	best_ipp = ul_netaddrq_get_best_ipp(&(ie->nl), ifa_family,
					    &threshold, &best_ifaceq);
	if (best_ipp)
		fputs(best_ipp, ie->output);
}

static void dump_iface_good(struct agetty_issue *ie,
			    struct ul_netaddrq_iface *ifaceq)
{
	struct ul_netaddrq_ip *best4[__ULNETLINK_RATING_MAX];
	struct ul_netaddrq_ip *best6[__ULNETLINK_RATING_MAX];
	struct list_head *li;
	enum ul_netaddrq_ip_rating threshold = __ULNETLINK_RATING_MAX - 1;
	enum ul_netaddrq_ip_rating fthreshold; /* per family threshold */
	bool first = true;

	memset(best4, 0, sizeof(best4));
	threshold = ul_netaddrq_iface_bestaddr(&(ifaceq->ip_quality_list_4),
					       &best4);
	memset(best6, 0, sizeof(best6));
	fthreshold = ul_netaddrq_iface_bestaddr(&(ifaceq->ip_quality_list_6),
						&best6);
	if (fthreshold < threshold)
		threshold = fthreshold;

	list_for_each(li, &(ifaceq->ip_quality_list_4))
	{
		struct ul_netaddrq_ip *ipq;

		ipq = list_entry(li, struct ul_netaddrq_ip, entry);
		if (threshold <= ULNETLINK_RATING_SCOPE_LINK &&
		    ( ipq->quality <= threshold ||
		      /* Consider site addresses equally good as global */
		      ipq->quality == ULNETLINK_RATING_SCOPE_SITE) &&
		    best4[threshold])
		{
			if (first)
			{
				fprintf(ie->output, "%s: ", ifaceq->ifname);
				first = false;
			}
			else
				fprintf(ie->output, " ");
			/* Write only the longest living temporary address */
			if (threshold == ULNETLINK_RATING_F_TEMPORARY)
			{
				fputs(ul_nl_addr_ntop_address(best4[ULNETLINK_RATING_F_TEMPORARY]->addr),
				      ie->output);
				goto temp_cont4;
			}
			else
				fputs(ul_nl_addr_ntop_address(ipq->addr),
				      ie->output);
		}
	temp_cont4:;
	}

	list_for_each(li, &(ifaceq->ip_quality_list_6))
	{
		struct ul_netaddrq_ip *ipq;

		ipq = list_entry(li, struct ul_netaddrq_ip, entry);
		if (threshold <= ULNETLINK_RATING_SCOPE_LINK &&
		    ( ipq->quality <= threshold ||
		      /* Consider site addresses equally good as global */
		      ipq->quality == ULNETLINK_RATING_SCOPE_SITE) &&
		    best6[threshold])
		{
			if (first)
			{
				fprintf(ie->output, "%s: ", ifaceq->ifname);
				first = false;
			}
			else
				fprintf(ie->output, " ");
			/* Write only the longest living temporary address */
			if (threshold == ULNETLINK_RATING_F_TEMPORARY)
			{
				fputs(ul_nl_addr_ntop_address(best6[ULNETLINK_RATING_F_TEMPORARY]->addr),
				      ie->output);
				goto temp_cont6;
			}
			else
				fputs(ul_nl_addr_ntop_address(ipq->addr),
				      ie->output);
		}
	temp_cont6:;
	}
	if (!first)
		fputs("\n", ie->output);
}

static void dump_iface_all(struct agetty_issue *ie,
			   struct ul_netaddrq_iface *ifaceq)
{
	struct list_head *li;
	struct ul_netaddrq_ip *ipq;
	bool first = true;

	list_for_each(li, &(ifaceq->ip_quality_list_4))
	{
		ipq = list_entry(li, struct ul_netaddrq_ip, entry);
		if (first)
		{
			fprintf(ie->output, "%s: ", ifaceq->ifname);
			first = false;
		}
		else
			fprintf(ie->output, " ");
		fputs(ul_nl_addr_ntop_address(ipq->addr), ie->output);
	}
	list_for_each(li, &(ifaceq->ip_quality_list_6))
	{
		ipq = list_entry(li, struct ul_netaddrq_ip, entry);
		if (first)
		{
			fprintf(ie->output, "%s: ", ifaceq->ifname);
			first = false;
		}
		else
			fprintf(ie->output, " ");
		fputs(ul_nl_addr_ntop_address(ipq->addr), ie->output);
	}
	if (!first)
		fputs("\n", ie->output);
}
#endif /* USE_NETLINK */

/*
 * parses \x{argument}, if not argument specified then returns NULL, the @fd
 * has to point to one char after the sequence (it means '{').
 */
static char *get_escape_argument(FILE *fd, char *buf, size_t bufsz)
{
	size_t i = 0;
	int c = fgetc(fd);

	if (c == EOF || (unsigned char) c != '{') {
		ungetc(c, fd);
		return NULL;
	}

	do {
		c = fgetc(fd);
		if (c == EOF)
			return NULL;
		if ((unsigned char) c != '}' && i < bufsz - 1)
			buf[i++] = (unsigned char) c;

	} while ((unsigned char) c != '}');

	buf[i] = '\0';
	return buf;
}

static void output_special_char(struct agetty_issue *ie,
				unsigned char c,
				struct agetty_options *op,
				struct termios *tp,
				FILE *fp)
{
	struct utsname uts;

	switch (c) {
	case 'e':
	{
		char escname[UL_COLORNAME_MAXSZ];

		if (get_escape_argument(fp, escname, sizeof(escname))) {
			char *esc = color_get_sequence(escname);

			if (esc) {
				fputs(esc, ie->output);
				free(esc);
			}
		} else
			fputs("\033", ie->output);
		break;
	}
	case 's':
		uname(&uts);
		fprintf(ie->output, "%s", uts.sysname);
		break;
	case 'n':
		uname(&uts);
		fprintf(ie->output, "%s", uts.nodename);
		break;
	case 'r':
		uname(&uts);
		fprintf(ie->output, "%s", uts.release);
		break;
	case 'v':
		uname(&uts);
		fprintf(ie->output, "%s", uts.version);
		break;
	case 'm':
		uname(&uts);
		fprintf(ie->output, "%s", uts.machine);
		break;
	case 'o':
	{
		char *dom = agetty_xgetdomainname();

		fputs(dom ? dom : "unknown_domain", ie->output);
		free(dom);
		break;
	}
	case 'O':
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
		break;
	}
	case 'd':
	case 't':
	{
		time_t now;
		struct tm tm;

		time(&now);
		localtime_r(&now, &tm);

		if (c == 'd') /* ISO 8601 */
			fprintf(ie->output, "%s %s %2d  %d",
				      nl_langinfo(ABDAY_1 + tm.tm_wday),
				      nl_langinfo(ABMON_1 + tm.tm_mon),
				      tm.tm_mday,
				      tm.tm_year < 70 ? tm.tm_year + 2000 :
				      tm.tm_year + 1900);
		else
			fprintf(ie->output, "%02d:%02d:%02d",
				      tm.tm_hour, tm.tm_min, tm.tm_sec);
		break;
	}
	case 'l':
		fprintf (ie->output, "%s", op->tty);
		break;
	case 'b':
		agetty_fprint_speed(ie->output, cfgetispeed(tp));
		break;
	case 'S':
	{
		char *var = NULL, varname[64];

		/* \S{varname} */
		if (get_escape_argument(fp, varname, sizeof(varname))) {
			var = read_os_release(op, varname);
			if (var) {
				if (strcmp(varname, "ANSI_COLOR") == 0)
					fprintf(ie->output, "\033[%sm", var);
				else
					fputs(var, ie->output);
			}
		/* \S */
		} else if ((var = read_os_release(op, "PRETTY_NAME"))) {
			fputs(var, ie->output);

		/* \S and PRETTY_NAME not found */
		} else {
			uname(&uts);
			fputs(uts.sysname, ie->output);
		}

		free(var);

		break;
	}
	case 'u':
	case 'U':
	{
		int users = 0;
#ifdef USE_SYSTEMD
		if (ul_dlopen_libsystemd() == 0 && systemd_call(sd_booted)() > 0) {
			users = systemd_call(sd_get_sessions)(NULL);
			if (users < 0)
				users = 0;
		} else
#endif
		{
			users = 0;
			struct utmpx *ut;
			setutxent();
			while ((ut = getutxent()))
				if (ut->ut_type == USER_PROCESS)
					users++;
			endutxent();
		}
		if (c == 'U')
			fprintf(ie->output, P_("%d user", "%d users", users), users);
		else
			fprintf (ie->output, "%d ", users);
		break;
	}
#ifdef USE_NETLINK
	case '4':
	case '6':
	{
		char iface[IF_NAMESIZE];
		uint8_t ifa_family = c == '4' ? AF_INET : AF_INET6;

		if (get_escape_argument(fp, iface, sizeof(iface)))
			print_iface_best(ie, iface, ifa_family);
		else
			print_addrq_bestofall(ie, ifa_family);

		/* TODO: Move to pass 1 */
		if (c == '4')
			netlink_groups |= RTMGRP_IPV4_IFADDR;
		else
			netlink_groups |= RTMGRP_IPV6_IFADDR;
		break;
	}
	case 'a':
	{
		struct list_head *li;
		struct ul_netaddrq_iface *ifaceq;

		list_for_each_netaddrq_iface(li, &(ie->nl))
		{
			ifaceq = list_entry(li, struct ul_netaddrq_iface, entry);

			dump_iface_good(ie, ifaceq);
		}
	}
	break;
	case 'A':
	{
		struct list_head *li;
		struct ul_netaddrq_iface *ifaceq;

		list_for_each_netaddrq_iface(li, &(ie->nl))
		{
			ifaceq = list_entry(li, struct ul_netaddrq_iface, entry);

			dump_iface_all(ie, ifaceq);
		}
	}
	break;
#endif /* USE_NETLINK */
	default:
		putc(c, ie->output);
		break;
	}
}


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
