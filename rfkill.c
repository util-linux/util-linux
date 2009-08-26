/*
 * rfkill userspace tool
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
	case RFKILL_TYPE_GPS:
		return "GPS";
	case NUM_RFKILL_TYPES:
		return NULL;
	}

	return NULL;
}

static void rfkill_list(void)
{
	struct rfkill_event event;
	const char *name;
	ssize_t len;
	int fd;

	fd = open("/dev/rfkill", O_RDONLY);
	if (fd < 0) {
		perror("Can't open RFKILL control device");
		return;
	}

	if (fcntl(fd, F_SETFL, O_NONBLOCK) < 0) {
		perror("Can't set RFKILL control device to non-blocking");
		close(fd);
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

		name = get_name(event.idx);

		printf("%u: %s: %s\n", event.idx, name,
						type2string(event.type));
		printf("\tSoft blocked: %s\n", event.soft ? "yes" : "no");
		printf("\tHard blocked: %s\n", event.hard ? "yes" : "no");
	}

	close(fd);
}

static void rfkill_block(bool all, __u32 idx, __u8 block, __u8 type)
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
	if (!all) {
		event.idx = idx;
		event.op = RFKILL_OP_CHANGE;
	} else {
		event.op = RFKILL_OP_CHANGE_ALL;
		event.type = type;
	}
	event.soft = block;

	len = write(fd, &event, sizeof(event));
	if (len < 0)
		perror("Failed to change RFKILL state");

	close(fd);
}

struct rfkill_type_str {
	enum rfkill_type type;
	char *name;
};
static struct rfkill_type_str rfkill_type_strings[] = {
	{	.type = RFKILL_TYPE_ALL,		.name = "all"	},
	{	.type = RFKILL_TYPE_WLAN,		.name = "wifi"	},
	{	.type = RFKILL_TYPE_BLUETOOTH,	.name = "bluetooth"	},
	{	.type = RFKILL_TYPE_UWB,		.name = "uwb"	},
	{	.type = RFKILL_TYPE_WIMAX,		.name = "wimax"	},
	{	.type = RFKILL_TYPE_WWAN,		.name = "wwan"	},
	{	.type = RFKILL_TYPE_GPS,		.name = "gps"	},
	{	.name = NULL }
};

static enum rfkill_type rfkill_str_to_type(char *s)
{
	struct rfkill_type_str *p;

	for (p = rfkill_type_strings; p->name != NULL; p++) {
		if ((strlen(s) == strlen(p->name)) && (!strcmp(s,p->name)))
			return p->type;
	}
	return NUM_RFKILL_TYPES;
}

static const char *argv0;

#define BLOCK_PARAMS "{<idx>,all,wifi,bluetooth,uwb,wimax,wwan,gps}"

static void usage(void)
{
	fprintf(stderr, "Usage:\t%s [options] command\n", argv0);
	fprintf(stderr, "Options:\n");
	fprintf(stderr, "\t--version\tshow version (%s)\n", rfkill_version);
	fprintf(stderr, "Commands:\n");
	fprintf(stderr, "\thelp\n");
	fprintf(stderr, "\tevent\n");
	fprintf(stderr, "\tlist\n");
	fprintf(stderr, "\tblock "BLOCK_PARAMS"\n");
	fprintf(stderr, "\tunblock "BLOCK_PARAMS"\n");
}

static void version(void)
{
	printf("rfkill %s\n", rfkill_version);
}

static void do_block_unblock(__u8 block, char *param)
{
	enum rfkill_type t;
	__u32 idx;

	if (islower(*param)) {
		/* assume alphabetic characters imply a wireless type name */
		t = rfkill_str_to_type(param);
		if (t < NUM_RFKILL_TYPES)
			rfkill_block(true, 0, block, t);
		else
			goto err;
	} else if (isdigit(*param)) {
		/* assume a numeric character implies an index. */
		idx = atoi(param);
		rfkill_block(false, idx, block, 0);
	} else
		goto err;

	return;
err:
	fprintf(stderr,"Bogus %sblock argument '%s'.\n",block?"":"un",param);
	exit(1);
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
		do_block_unblock(1,*argv);
	} else if (strcmp(*argv, "unblock") == 0 && argc > 1) {
		argc--;
		argv++;
		do_block_unblock(0,*argv);
	} else {
		usage();
		return 1;
	}

	return 0;
}
