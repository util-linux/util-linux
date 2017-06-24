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

#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/poll.h>
#include <sys/time.h>

#include <linux/rfkill.h>

#include "c.h"
#include "closestream.h"
#include "nls.h"

static void rfkill_event(void)
{
	struct rfkill_event event;
	struct timeval tv;
	struct pollfd p;
	ssize_t len;
	int fd, n;

	fd = open("/dev/rfkill", O_RDONLY);
	if (fd < 0) {
		perror("Can't open RFKILL control device");
		return;
	}

	memset(&p, 0, sizeof(p));
	p.fd = fd;
	p.events = POLLIN | POLLHUP;

	while (1) {
		n = poll(&p, 1, -1);
		if (n < 0) {
			perror("Failed to poll RFKILL control device");
			break;
		}

		if (n == 0)
			continue;

		len = read(fd, &event, sizeof(event));
		if (len < 0) {
			perror("Reading of RFKILL events failed");
			break;
		}

		if (len != RFKILL_EVENT_SIZE_V1) {
			fprintf(stderr, "Wrong size of RFKILL event\n");
			continue;
		}

		gettimeofday(&tv, NULL);
		printf("%ld.%06u: idx %u type %u op %u soft %u hard %u\n",
			(long) tv.tv_sec, (unsigned int) tv.tv_usec,
			event.idx, event.type, event.op, event.soft, event.hard);
		fflush(stdout);
	}

	close(fd);
}

static const char *get_name(__u32 idx)
{
	static char name[128] = {};
	char *pos, filename[64];
	int fd;

	snprintf(filename, sizeof(filename) - 1,
				"/sys/class/rfkill/rfkill%u/name", idx);

	fd = open(filename, O_RDONLY);
	if (fd < 0)
		return NULL;

	memset(name, 0, sizeof(name));
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
	}

	return NULL;
}

struct rfkill_type_str {
	enum rfkill_type type;
	const char *name;
};
static const struct rfkill_type_str rfkill_type_strings[] = {
	{	.type = RFKILL_TYPE_ALL,		.name = "all"	},
	{	.type = RFKILL_TYPE_WLAN,		.name = "wifi"	},
	{	.type = RFKILL_TYPE_WLAN,		.name = "wlan"	}, /* alias */
	{	.type = RFKILL_TYPE_BLUETOOTH,	.name = "bluetooth"	},
	{	.type = RFKILL_TYPE_UWB,		.name = "uwb"	},
	{	.type = RFKILL_TYPE_UWB,		.name = "ultrawideband"	}, /* alias */
	{	.type = RFKILL_TYPE_WIMAX,		.name = "wimax"	},
	{	.type = RFKILL_TYPE_WWAN,		.name = "wwan"	},
	{	.type = RFKILL_TYPE_GPS,		.name = "gps"	},
	{	.type = RFKILL_TYPE_FM,			.name = "fm"	},
	{	.type = RFKILL_TYPE_NFC,		.name = "nfc"	},
	{	.name = NULL }
};

struct rfkill_id {
	union {
		enum rfkill_type type;
		__u32 index;
	};
	enum {
		RFKILL_IS_INVALID,
		RFKILL_IS_TYPE,
		RFKILL_IS_INDEX,
	} result;
};

static struct rfkill_id rfkill_id_to_type(const char *s)
{
	const struct rfkill_type_str *p;
	struct rfkill_id ret;

	if (islower(*s)) {
		for (p = rfkill_type_strings; p->name != NULL; p++) {
			if ((strlen(s) == strlen(p->name)) && (!strcmp(s,p->name))) {
				ret.type = p->type;
				ret.result = RFKILL_IS_TYPE;
				return ret;
			}
		}
	} else if (isdigit(*s)) {
		/* assume a numeric character implies an index. */
		ret.index = atoi(s);
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
			fprintf(stderr, "Bogus %s argument '%s'.\n", "list", param);
			return 2;
		}
		/* don't filter "all" */
		if (id.result == RFKILL_IS_TYPE && id.type == RFKILL_TYPE_ALL)
			id.result = RFKILL_IS_INVALID;
	}

	fd = open("/dev/rfkill", O_RDONLY);
	if (fd < 0) {
		perror("Can't open RFKILL control device");
		return 1;
	}

	if (fcntl(fd, F_SETFL, O_NONBLOCK) < 0) {
		perror("Can't set RFKILL control device to non-blocking");
		close(fd);
		return 1;
	}

	while (1) {
		len = read(fd, &event, sizeof(event));
		if (len < 0) {
			if (errno == EAGAIN)
				break;
			perror("Reading of RFKILL events failed");
			break;
		}

		if (len != RFKILL_EVENT_SIZE_V1) {
			fprintf(stderr, "Wrong size of RFKILL event\n");
			continue;
		}

		if (event.op != RFKILL_OP_ADD)
			continue;

		/* filter out unwanted results */
		switch (id.result)
		{
		case RFKILL_IS_TYPE:
			if (event.type != id.type)
				continue;
			break;
		case RFKILL_IS_INDEX:
			if (event.idx != id.index)
				continue;
			break;
		case RFKILL_IS_INVALID:; /* must be last */
		}

		name = get_name(event.idx);

		printf("%u: %s: %s\n", event.idx, name,
						type2string(event.type));
		printf("\tSoft blocked: %s\n", event.soft ? "yes" : "no");
		printf("\tHard blocked: %s\n", event.hard ? "yes" : "no");
	}

	close(fd);
	return 0;
}

static int rfkill_block(__u8 block, const char *param)
{
	struct rfkill_id id;
	struct rfkill_event event;
	ssize_t len;
	int fd;

	id = rfkill_id_to_type(param);
	if (id.result == RFKILL_IS_INVALID) {
		fprintf(stderr, "Bogus %s argument '%s'.\n", block ? "block" : "unblock", param);
		return 2;
	}

	fd = open("/dev/rfkill", O_RDWR);
	if (fd < 0) {
		perror("Can't open RFKILL control device");
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
	case RFKILL_IS_INVALID:; /* must be last */
	}
	event.soft = block;

	len = write(fd, &event, sizeof(event));
	if (len < 0)
		perror("Failed to change RFKILL state");

	close(fd);
	return 0;
}

static const char *argv0;

static void usage(void)
{
	const struct rfkill_type_str *p;

	fprintf(stderr, "Usage:\t%s [options] command\n", argv0);
	fprintf(stderr, "Options:\n");
	fprintf(stderr, "\t--version\tshow version (%s)\n", PACKAGE_VERSION);
	fprintf(stderr, "Commands:\n");
	fprintf(stderr, "\thelp\n");
	fprintf(stderr, "\tevent\n");
	fprintf(stderr, "\tlist [IDENTIFIER]\n");
	fprintf(stderr, "\tblock IDENTIFIER\n");
	fprintf(stderr, "\tunblock IDENTIFIER\n");
	fprintf(stderr, "where IDENTIFIER is the index no. of an rfkill switch or one of:\n");
	fprintf(stderr, "\t<idx>");
	for (p = rfkill_type_strings; p->name != NULL; p++)
		fprintf(stderr, " %s", p->name);
	fprintf(stderr, "\n");
}

static void version(void)
{
	printf("rfkill %s\n", PACKAGE_VERSION);
}

int main(int argc, char **argv)
{
	int ret = 0;

	/* strip off self */
	argc--;
	argv0 = *argv++;

	if (argc > 0 && strcmp(*argv, "--version") == 0) {
		version();
		return 0;
	}

	if (argc == 0 || strcmp(*argv, "help") == 0) {
		usage();
		return 0;
	}

	if (strcmp(*argv, "event") == 0) {
		rfkill_event();
	} else if (strcmp(*argv, "list") == 0) {
		argc--;
		argv++;
		rfkill_list(*argv); /* NULL is acceptable */
	} else if (strcmp(*argv, "block") == 0 && argc > 1) {
		argc--;
		argv++;
		ret = rfkill_block(1,*argv);
	} else if (strcmp(*argv, "unblock") == 0 && argc > 1) {
		argc--;
		argv++;
		ret = rfkill_block(0,*argv);
	} else {
		usage();
		return 1;
	}

	return ret;
}
