/*
 * /dev/rfkill userspace tool
 *
 * Copyright 2009 Johannes Berg <johannes@sipsolutions.net>
 * Copyright 2009 Marcel Holtmann <marcel@holtmann.org>
 * Copyright 2009 Tim Gardner <tim.gardner@canonical.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <ctype.h>
#include <getopt.h>
#include <libsmartcols.h>
#include <linux/rfkill.h>
#include <sys/poll.h>
#include <sys/syslog.h>
#include <sys/time.h>

#include "c.h"
#include "closestream.h"
#include "nls.h"
#include "optutils.h"
#include "pathnames.h"
#include "strutils.h"
#include "timeutils.h"
#include "widechar.h"
#include "xalloc.h"

struct rfkill_type_str {
	enum rfkill_type type;
	const char *name;
};

static const struct rfkill_type_str rfkill_type_strings[] = {
	{ .type = RFKILL_TYPE_ALL,       .name = "all"           },
	{ .type = RFKILL_TYPE_WLAN,      .name = "wifi"          },
	{ .type = RFKILL_TYPE_WLAN,      .name = "wlan"          }, /* alias */
	{ .type = RFKILL_TYPE_BLUETOOTH, .name = "bluetooth"     },
	{ .type = RFKILL_TYPE_UWB,       .name = "uwb"           },
	{ .type = RFKILL_TYPE_UWB,       .name = "ultrawideband" }, /* alias */
	{ .type = RFKILL_TYPE_WIMAX,     .name = "wimax"         },
	{ .type = RFKILL_TYPE_WWAN,      .name = "wwan"          },
	{ .type = RFKILL_TYPE_GPS,       .name = "gps"           },
	{ .type = RFKILL_TYPE_FM,        .name = "fm"            },
	{ .type = RFKILL_TYPE_NFC,       .name = "nfc"           },
	{ .type = NUM_RFKILL_TYPES,      .name = NULL            }
};

struct rfkill_id {
	union {
		enum rfkill_type type;
		uint32_t index;
	};
	enum {
		RFKILL_IS_INVALID,
		RFKILL_IS_TYPE,
		RFKILL_IS_INDEX,
		RFKILL_IS_ALL
	} result;
};

/* column IDs */
enum {
	COL_DEVICE,
	COL_ID,
	COL_TYPE,
	COL_SOFT,
	COL_HARD
};

/* column names */
struct colinfo {
	const char *name;	/* header */
	double whint;		/* width hint (N < 1 is in percent of termwidth) */
	int flags;		/* SCOLS_FL_* */
	const char *help;
};

/* columns descriptions */
static const struct colinfo infos[] = {
	[COL_DEVICE] = {"DEVICE", 0, 0, N_("kernel device name")},
	[COL_ID]     = {"ID",	  0, 0, N_("device identifier value")},
	[COL_TYPE]   = {"TYPE",	  0, 0, N_("device type name that can be used as identifier")},
	[COL_SOFT]   = {"SOFT",	  0, 0, N_("status of software block")},
	[COL_HARD]   = {"HARD",	  0, 0, N_("status of hardware block")}
};

static int columns[ARRAY_SIZE(infos) * 2];
static size_t ncolumns;

struct control {
	unsigned int
		json:1,
		no_headings:1,
		raw:1;
};

static int column_name_to_id(const char *name, size_t namesz)
{
	size_t i;

	assert(name);

	for (i = 0; i < ARRAY_SIZE(infos); i++) {
		const char *cn = infos[i].name;

		if (!strncasecmp(name, cn, namesz) && !*(cn + namesz))
			return i;
	}
	warnx(_("unknown column: %s"), name);
	return -1;
}

static int get_column_id(size_t num)
{
	assert(num < ncolumns);
	assert(columns[num] < (int)ARRAY_SIZE(infos));
	return columns[num];
}

static const struct colinfo *get_column_info(int num)
{
	return &infos[get_column_id(num)];
}

static int rfkill_event(void)
{
	struct rfkill_event event;
	struct timeval tv;
	char date_buf[ISO_8601_BUFSIZ];
	struct pollfd p;
	ssize_t len;
	int fd, n, ret = 0;

	fd = open(_PATH_DEV_RFKILL, O_RDONLY);
	if (fd < 0) {
		warn(_("cannot open %s"), _PATH_DEV_RFKILL);
		return 1;
	}

	memset(&p, 0, sizeof(p));
	p.fd = fd;
	p.events = POLLIN | POLLHUP;

	while (1) {
		n = poll(&p, 1, -1);
		if (n < 0) {
			warn(_("failed to poll %s"), _PATH_DEV_RFKILL);
			ret = 1;
			break;
		}

		if (n == 0)
			continue;

		len = read(fd, &event, sizeof(event));
		if (len < 0) {
			warn(_("cannot read %s"), _PATH_DEV_RFKILL);
			ret = 1;
			break;
		}

		if (len < RFKILL_EVENT_SIZE_V1) {
			warnx(_("wrong size of rfkill event: %zu < %d"), len, RFKILL_EVENT_SIZE_V1);
			ret = 1;
			continue;
		}
		gettimeofday(&tv, NULL);
		strtimeval_iso(&tv,
			       ISO_8601_DATE |
			       ISO_8601_TIME |
			       ISO_8601_COMMAUSEC |
			       ISO_8601_TIMEZONE |
			       ISO_8601_SPACE, date_buf, sizeof(date_buf));
		printf("%s: idx %u type %u op %u soft %u hard %u\n",
		       date_buf,
		       event.idx, event.type, event.op, event.soft, event.hard);
		fflush(stdout);
	}

	close(fd);
	return ret;
}

static const char *get_name_or_type(uint32_t idx, int type)
{
	static char name[128] = { 0 };
	char *pos, filename[64];
	int fd;

	if (type)
		pos = "type";
	else
		pos = "name";
	snprintf(filename, sizeof(filename) - 1,
				_PATH_SYS_RFKILL "/rfkill%u/%s", idx, pos);

	fd = open(filename, O_RDONLY);
	if (fd < 0) {
		warn(_("cannot open %s"), filename);
		return NULL;
	}

	if (read(fd, name, sizeof(name) - 1) < 0) {
		warn(_("cannot read %s"), filename);
		close(fd);
		return NULL;
	}

	pos = strchr(name, '\n');
	if (pos)
		*pos = '\0';

	close(fd);

	return name;
}

static struct rfkill_id rfkill_id_to_type(const char *s)
{
	const struct rfkill_type_str *p;
	struct rfkill_id ret;

	if (islower(*s)) {
		for (p = rfkill_type_strings; p->name != NULL; p++) {
			if (!strcmp(s, p->name)) {
				ret.type = p->type;
				if (!strcmp(s, "all"))
					ret.result = RFKILL_IS_ALL;
				else
					ret.result = RFKILL_IS_TYPE;
				return ret;
			}
		}
	} else if (isdigit(*s)) {
		/* assume a numeric character implies an index. */
		char filename[64];

		ret.index = strtou32_or_err(s, _("invalid identifier"));
		snprintf(filename, sizeof(filename) - 1,
			 _PATH_SYS_RFKILL "/rfkill%" PRIu32 "/name", ret.index);
		if (access(filename, F_OK) == 0)
			ret.result = RFKILL_IS_INDEX;
		else
			ret.result = RFKILL_IS_INVALID;
		return ret;
	}

	ret.result = RFKILL_IS_INVALID;
	return ret;
}

static void fill_table_row(struct libscols_table *tb, struct rfkill_event *event)
{
	static struct libscols_line *ln;
	size_t i;

	assert(tb);

	ln = scols_table_new_line(tb, NULL);
	if (!ln) {
		errno = ENOMEM;
		errx(EXIT_FAILURE, _("failed to allocate output line"));
	}

	for (i = 0; i < (size_t)ncolumns; i++) {
		char *str = NULL;
		switch (get_column_id(i)) {
		case COL_DEVICE:
			str = xstrdup(get_name_or_type(event->idx, 0));
			break;
		case COL_ID:
			xasprintf(&str, "%" PRIu32, event->idx);
			break;
		case COL_TYPE:
			str = xstrdup(get_name_or_type(event->idx, 1));
			break;
		case COL_SOFT:
			str = xstrdup(event->soft ? _("blocked") : _("unblocked"));
			break;
		case COL_HARD:
			str = xstrdup(event->hard ? _("blocked") : _("unblocked"));
			break;
		default:
			abort();
		}
		if (str && scols_line_refer_data(ln, i, str))
			errx(EXIT_FAILURE, _("failed to add output data"));
	}
}

static int rfkill_list(struct control const *const ctrl, const char *param)
{
	struct rfkill_id id = { .result = RFKILL_IS_ALL };
	struct rfkill_event event;
	ssize_t len;
	int fd;
	struct libscols_table *tb;

	scols_init_debug(0);
	tb = scols_new_table();
	if (!tb)
		err(EXIT_FAILURE, _("failed to allocate output table"));

	scols_table_enable_json(tb, ctrl->json);
	scols_table_enable_noheadings(tb, ctrl->no_headings);
	scols_table_enable_raw(tb, ctrl->raw);
	{
		size_t i;

		for (i = 0; i < (size_t)ncolumns; i++) {
			const struct colinfo *col = get_column_info(i);

			if (!scols_table_new_column(tb, col->name, col->whint, col->flags))
				err(EXIT_FAILURE, _("failed to initialize output column"));
		}
	}

	if (param) {
		id = rfkill_id_to_type(param);
		if (id.result == RFKILL_IS_INVALID) {
			warnx(_("invalid identifier: %s"), param);
			return 1;
		}
	}

	fd = open(_PATH_DEV_RFKILL, O_RDONLY);
	if (fd < 0) {
		warn(_("cannot open %s"), _PATH_DEV_RFKILL);
		return 1;
	}

	if (fcntl(fd, F_SETFL, O_NONBLOCK) < 0) {
		warn(_("cannot set non-blocking %s"), _PATH_DEV_RFKILL);
		close(fd);
		return 1;
	}

	while (1) {
		len = read(fd, &event, sizeof(event));
		if (len < 0) {
			if (errno == EAGAIN)
				break;
			warn(_("cannot read %s"), _PATH_DEV_RFKILL);
			break;
		}

		if (len < RFKILL_EVENT_SIZE_V1) {
			warnx(_("wrong size of rfkill event: %zu < %d"), len, RFKILL_EVENT_SIZE_V1);
			continue;
		}

		if (event.op != RFKILL_OP_ADD)
			continue;

		/* filter out unwanted results */
		switch (id.result) {
		case RFKILL_IS_TYPE:
			if (event.type != id.type)
				continue;
			break;
		case RFKILL_IS_INDEX:
			if (event.idx != id.index)
				continue;
			break;
		case RFKILL_IS_ALL:
			break;
		default:
			abort();
		}
		fill_table_row(tb, &event);
	}
	close(fd);
	scols_print_table(tb);
	scols_unref_table(tb);
	return 0;
}

static int rfkill_block(uint8_t block, const char *param)
{
	struct rfkill_id id;
	struct rfkill_event event = {
		.op = RFKILL_OP_CHANGE_ALL,
		.soft = block,
		0
	};
	ssize_t len;
	int fd;
	char *message = NULL;

	id = rfkill_id_to_type(param);

	switch (id.result) {
	case RFKILL_IS_INVALID:
		warnx(_("invalid identifier: %s"), param);
		return 1;
	case RFKILL_IS_TYPE:
		event.type = id.type;
		xasprintf(&message, "type %s", param);
		break;
	case RFKILL_IS_INDEX:
		event.op = RFKILL_OP_CHANGE;
		event.idx = id.index;
		xasprintf(&message, "id %d", id.index);
		break;
	case RFKILL_IS_ALL:
		message = xstrdup("all");
		break;
	default:
		abort();
	}

	fd = open(_PATH_DEV_RFKILL, O_RDWR);
	if (fd < 0) {
		warn(_("cannot open %s"), _PATH_DEV_RFKILL);
		free(message);
		return 1;
	}

	len = write(fd, &event, sizeof(event));
	if (len < 0)
		warn(_("write failed: %s"), _PATH_DEV_RFKILL);
	openlog("rfkill", 0, LOG_USER);
	syslog(LOG_NOTICE, "%s set for %s", block ? "block" : "unblock", message);
	free(message);
	closelog();
	return close_fd(fd);
}

static void __attribute__((__noreturn__)) usage(void)
{
	size_t i;

	fputs(USAGE_HEADER, stdout);
	fprintf(stdout, _(" %s [options] command [identifier]\n"), program_invocation_short_name);

	fputs(USAGE_SEPARATOR, stdout);
	fputs(_("Tool for enabling and disabling wireless devices.\n"), stdout);

	fputs(USAGE_OPTIONS, stdout);
	fputs(_(" -J, --json             use JSON output format\n"), stdout);
	fputs(_(" -n, --noheadings       don't print headings\n"), stdout);
	fputs(_(" -o, --output <list>    define which output columns to use\n"), stdout);
	fputs(_(" -r, --raw              use the raw output format\n"), stdout);

	fputs(USAGE_SEPARATOR, stdout);
	printf(USAGE_HELP_OPTIONS(24));

	fputs(USAGE_COLUMNS, stdout);
	for (i = 0; i < ARRAY_SIZE(infos); i++)
		fprintf(stdout, " %-6s  %s\n", infos[i].name, _(infos[i].help));

	fputs(USAGE_COMMANDS, stdout);

	/*
	 * TRANSLATORS: command names should not be translated, explaining
	 * them as additional field after identifer is fine, for example
	 *
	 * list   [identifier]   (lista [tarkenne])
	 */
	fputs(_(" help\n"), stdout);
	fputs(_(" event\n"), stdout);
	fputs(_(" list   [identifier]\n"), stdout);
	fputs(_(" block   identifier\n"), stdout);
	fputs(_(" unblock identifier\n"), stdout);

	fprintf(stdout, USAGE_MAN_TAIL("rfkill(8)"));
	exit(EXIT_SUCCESS);
}

int main(int argc, char **argv)
{
	struct control ctrl = { 0 };
	int c;
	char *outarg = NULL;
	static const struct option longopts[] = {
		{ "json",	no_argument,	   NULL, 'J' },
		{ "noheadings", no_argument,	   NULL, 'n' },
		{ "output",	required_argument, NULL, 'o' },
		{ "raw",	no_argument,	   NULL, 'r' },
		{ "version",	no_argument,	   NULL, 'V' },
		{ "help",	no_argument,	   NULL, 'h' },
		{ NULL, 0, NULL, 0 }
	};
	static const ul_excl_t excl[] = {
		{'J', 'r'},
		{0}
	};
	int excl_st[ARRAY_SIZE(excl)] = UL_EXCL_STATUS_INIT;
	int ret;

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	while ((c = getopt_long(argc, argv, "Jno:rVh", longopts, NULL)) != -1) {
		err_exclusive_options(c, longopts, excl, excl_st);
		switch (c) {
		case 'J':
			ctrl.json = 1;
			break;
		case 'n':
			ctrl.no_headings = 1;
			break;
		case 'o':
			outarg = optarg;
			break;
		case 'r':
			ctrl.raw = 1;
			break;
		case 'V':
			printf(UTIL_LINUX_VERSION);
			return EXIT_SUCCESS;
		case 'h':
			usage();
		default:
			errtryhelp(EXIT_FAILURE);
		}
	}
	argc -= optind;
	argv += optind;

	if (argc == 0 || strcmp(*argv, "list") == 0) {
		columns[ncolumns++] = COL_DEVICE;
		columns[ncolumns++] = COL_ID;
		columns[ncolumns++] = COL_TYPE;
		columns[ncolumns++] = COL_SOFT;
		columns[ncolumns++] = COL_HARD;

		if (outarg
		    && string_add_to_idarray(outarg, columns,
					     ARRAY_SIZE(columns), &ncolumns,
					     column_name_to_id) < 0)
			return EXIT_FAILURE;
		if (argc) {
			argc--;
			argv++;
		}
		ret = rfkill_list(&ctrl, *argv);
	} else if (strcmp(*argv, "event") == 0) {
		ret = rfkill_event();
	} else if (strcmp(*argv, "help") == 0) {
		usage();
	} else if (strcmp(*argv, "block") == 0 && argc > 1) {
		argc--;
		argv++;
		ret = rfkill_block(1, *argv);
	} else if (strcmp(*argv, "unblock") == 0 && argc > 1) {
		argc--;
		argv++;
		ret = rfkill_block(0, *argv);
	} else
		errtryhelp(EXIT_FAILURE);

	return ret;
}
