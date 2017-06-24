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
#include <linux/rfkill.h>
#include <sys/poll.h>
#include <sys/time.h>

#include "c.h"
#include "closestream.h"
#include "nls.h"
#include "pathnames.h"
#include "strutils.h"
#include "widechar.h"

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
	} result;
};

static int rfkill_event(void)
{
	struct rfkill_event event;
	struct timeval tv;
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

		if (len != RFKILL_EVENT_SIZE_V1) {
			warnx(_("wrong size of rfkill event: %zu != %d"), len, RFKILL_EVENT_SIZE_V1);
			ret = 1;
			continue;
		}

		gettimeofday(&tv, NULL);
		printf("%ld.%06u: idx %u type %u op %u soft %u hard %u\n",
			(long) tv.tv_sec, (unsigned int) tv.tv_usec,
			event.idx, event.type, event.op, event.soft, event.hard);
		fflush(stdout);
	}

	close(fd);
	return ret;
}

static const char *get_name(uint32_t idx)
{
	static char name[128] = { 0 };
	char *pos, filename[64];
	int fd;

	snprintf(filename, sizeof(filename) - 1,
				_PATH_SYS_RFKILL "/rfkill%u/name", idx);

	fd = open(filename, O_RDONLY);
	if (fd < 0) {
		warn(_("cannot open %s"), filename);
		return NULL;
	}

	read(fd, name, sizeof(name) - 1);

	pos = strchr(name, '\n');
	if (pos)
		*pos = '\0';

	close(fd);

	return name;
}

static const char *type2string(enum rfkill_type type)
{
	switch (type) {
	case RFKILL_TYPE_ALL:
		return "All";
	case RFKILL_TYPE_WLAN:
		return "Wireless LAN";
	case RFKILL_TYPE_BLUETOOTH:
		return "Bluetooth";
	case RFKILL_TYPE_UWB:
		return "Ultra-Wideband";
	case RFKILL_TYPE_WIMAX:
		return "WiMAX";
	case RFKILL_TYPE_WWAN:
		return "Wireless WAN";
	case RFKILL_TYPE_GPS:
		return "GPS";
	case RFKILL_TYPE_FM:
		return "FM";
	case RFKILL_TYPE_NFC:
		return "NFC";
	case NUM_RFKILL_TYPES:
		return NULL;
	default:
		abort();
	}
	return NULL;
}

static struct rfkill_id rfkill_id_to_type(const char *s)
{
	const struct rfkill_type_str *p;
	struct rfkill_id ret;

	if (islower(*s)) {
		for (p = rfkill_type_strings; p->name != NULL; p++) {
			if (!strcmp(s, p->name)) {
				ret.type = p->type;
				ret.result = RFKILL_IS_TYPE;
				return ret;
			}
		}
	} else if (isdigit(*s)) {
		/* assume a numeric character implies an index. */
		ret.index = strtou32_or_err(s, _("invalid identifier"));
		ret.result = RFKILL_IS_INDEX;
		return ret;
	}

	ret.result = RFKILL_IS_INVALID;
	return ret;
}

static int rfkill_list(const char *param)
{
	struct rfkill_id id = { .result = RFKILL_IS_INVALID };
	struct rfkill_event event;
	const char *name;
	ssize_t len;
	int fd;

	if (param) {
		id = rfkill_id_to_type(param);
		if (id.result == RFKILL_IS_INVALID) {
			warnx(_("invalid identifier: %s"), param);
			return 1;
		}
		/* don't filter "all" */
		if (id.result == RFKILL_IS_TYPE && id.type == RFKILL_TYPE_ALL)
			id.result = RFKILL_IS_INVALID;
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

		if (len != RFKILL_EVENT_SIZE_V1) {
			warnx(_("wrong size of rfkill event: %zu != %d"), len, RFKILL_EVENT_SIZE_V1);
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
		case RFKILL_IS_INVALID:
			break;
		default:
			abort();
		}
		name = get_name(event.idx);

		printf("%u: %s: %s\n", event.idx, name,
						type2string(event.type));
		printf("\t%s: %s\n", _("Soft blocked"), event.soft ? _("yes") : _("no"));
		printf("\t%s: %s\n", _("Hard blocked"), event.hard ? _("yes") : _("no"));
	}

	close(fd);
	return 0;
}

static int rfkill_block(uint8_t block, const char *param)
{
	struct rfkill_id id;
	struct rfkill_event event;
	ssize_t len;
	int fd;

	id = rfkill_id_to_type(param);
	if (id.result == RFKILL_IS_INVALID) {
		warnx(_("invalid identifier: %s"), param);
		return 1;
	}

	fd = open(_PATH_DEV_RFKILL, O_RDWR);
	if (fd < 0) {
		warn(_("cannot open %s"), _PATH_DEV_RFKILL);
		return 1;
	}

	memset(&event, 0, sizeof(event));
	switch (id.result) {
	case RFKILL_IS_TYPE:
		event.op = RFKILL_OP_CHANGE_ALL;
		event.type = id.type;
		break;
	case RFKILL_IS_INDEX:
		event.op = RFKILL_OP_CHANGE;
		event.idx = id.index;
		break;
	case RFKILL_IS_INVALID:
		break;
	default:
		abort();
	}
	event.soft = block;

	len = write(fd, &event, sizeof(event));
	if (len < 0)
		warn(_("write failed: %s"), _PATH_DEV_RFKILL);
	return close_fd(fd);
}

static void __attribute__((__noreturn__)) usage(void)
{
	const struct rfkill_type_str *p;

	fputs(USAGE_HEADER, stdout);
	fprintf(stdout, _(" %s command [identifier]\n"), program_invocation_short_name);

	fputs(USAGE_SEPARATOR, stdout);
	fputs(_("Tool for enabling and disabling wireless devices.\n"), stdout);

	fputs(USAGE_SEPARATOR, stdout);
	fputs(_("Commands:\n"), stdout);
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

	fputs(USAGE_SEPARATOR, stdout);
	fputs(_("Identifiers, that can be referred by id number or name:\n"), stdout);
	for (p = rfkill_type_strings; p->name != NULL; p++)
		printf(" %d %s\n", p->type, p->name);

	fprintf(stdout, USAGE_MAN_TAIL("rfkill(8)"));
	exit(EXIT_SUCCESS);
}

int main(int argc, char **argv)
{
	int c;
	static const struct option longopts[] = {
		{ "version", no_argument, NULL, 'V' },
		{ "help",    no_argument, NULL, 'h' },
		{ NULL, 0, NULL, 0 }
	};
	int ret;

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	while ((c = getopt_long(argc, argv, "Vh", longopts, NULL)) != -1)
		switch (c) {
		case 'V':
			printf(UTIL_LINUX_VERSION);
			return EXIT_SUCCESS;
		case 'h':
			usage();
		default:
			errtryhelp(EXIT_FAILURE);
		}

	/* Skip program name. */
	argv++;
	argc--;

	if (argc == 0 || strcmp(*argv, _("help")) == 0)
		usage();

	if (strcmp(*argv, "event") == 0) {
		ret = rfkill_event();
	} else if (strcmp(*argv, "list") == 0) {
		argc--;
		argv++;
		ret = rfkill_list(*argv); /* NULL is acceptable */
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
