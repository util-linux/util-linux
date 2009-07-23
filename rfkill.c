/*
 * rfkill userspace tool
 *
 * Copyright 2009	Johannes Berg <johannes@sipsolutions.net>
 */

#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/poll.h>

#include "rfkill.h"
#include "core.h"

static void rfkill_event(void)
{
	struct rfkill_event event;
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

		printf("RFKILL event: idx %u type %u op %u soft %u hard %u\n",
					event.idx, event.type, event.op,
					event.soft, event.hard);
	}

	close(fd);
}

static const char *get_name(__u32 idx)
{
	static char name[128];
	ssize_t len;
	char *pos, filename[64];
	int fd;

	snprintf(filename, sizeof(filename) - 1,
				"/sys/class/rfkill/rfkill%u/name", idx);

	fd = open(filename, O_RDONLY);
	if (fd < 0)
		return NULL;

	memset(name, 0, sizeof(name));
	len = read(fd, name, sizeof(name) - 1);

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
	case NUM_RFKILL_TYPES:
		return NULL;
	}

	return NULL;
}

static struct rfkill_event *rfkill_get_event_list(int *num_events)
{
	struct rfkill_event event;
	struct rfkill_event *events = NULL;
	ssize_t len;
	int fd;

	*num_events = 0;

	fd = open("/dev/rfkill", O_RDONLY);
	if (fd < 0) {
		perror("Can't open RFKILL control device");
		return NULL;
	}

	if (fcntl(fd, F_SETFL, O_NONBLOCK) < 0) {
		perror("Can't set RFKILL control device to non-blocking");
		close(fd);
		return NULL;
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

		events = realloc(events,(*num_events+1)*sizeof(struct rfkill_event));
		if (!events) {
			perror("Cannot realloc events");
			break;
		}

		events[*num_events] = event;
		*num_events += 1;
	}

	close(fd);
	return events;
}

static void rfkill_list(void)
{
	int num_events;
	struct rfkill_event *events;
	const char *name;
	int i;

	events = rfkill_get_event_list(&num_events);
	if (!events)
		return;

	for (i = 0; i < num_events; i++) {

		name = get_name(events[i].idx);

		printf("%u: %s: %s\n", events[i].idx, name,
						type2string(events[i].type));
		printf("\tSoft blocked: %s\n", events[i].soft ? "yes" : "no");
		printf("\tHard blocked: %s\n", events[i].hard ? "yes" : "no");
	}

	free(events);
}

static void rfkill_block(__u32 idx, __u8 block)
{
	struct rfkill_event event;
	ssize_t len;
	int fd;

	fd = open("/dev/rfkill", O_RDWR);
	if (fd < 0) {
		perror("Can't open RFKILL control device");
		return;
	}

	memset(&event, 0, sizeof(event));
	event.idx = idx;
	event.op = RFKILL_OP_CHANGE;
	event.soft = block;

	len = write(fd, &event, sizeof(event));
	if (len < 0)
		perror("Failed to change RFKILL state");

	close(fd);
}

static void rfkill_block_all(enum rfkill_type type, __u8 block)
{
	int num_events;
	struct rfkill_event *events;
	int i;

	events = rfkill_get_event_list(&num_events);
	if (!events)
		return;

	for (i = 0; i < num_events; i++) {
		if ((events[i].type == type) || (type == RFKILL_TYPE_ALL)) {
			rfkill_block(events[i].idx, block);
		}
	}

	free(events);
}

static const char *argv0;

static void usage(void)
{
	fprintf(stderr, "Usage:\t%s [options] command\n", argv0);
	fprintf(stderr, "Options:\n");
	fprintf(stderr, "\t--version\tshow version (%s)\n", rfkill_version);
	fprintf(stderr, "Commands:\n");
	fprintf(stderr, "\thelp\n");
	fprintf(stderr, "\tevent\n");
	fprintf(stderr, "\tlist\n");
	fprintf(stderr, "\tblock <idx>\n");
	fprintf(stderr, "\tunblock <idx>\n");
}

static void version(void)
{
	printf("rfkill %s\n", rfkill_version);
}

int main(int argc, char **argv)
{
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
		rfkill_list();
	} else if (strcmp(*argv, "block") == 0 && argc > 1) {
		argc--;
		argv++;
		__u32 idx = atoi(*argv);
		rfkill_block(idx, 1);
	} else if (strcmp(*argv, "unblock") == 0 && argc > 1) {
		argc--;
		argv++;
		__u32 idx = atoi(*argv);
		rfkill_block(idx, 0);
	} else {
		usage();
		return 1;
	}

	return 0;
}
