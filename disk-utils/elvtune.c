/*
 *  elvtune.c - I/O elevator tuner
 *
 *  Copyright (C) 2000 Andrea Arcangeli <andrea@suse.de> SuSE
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *  This file may be redistributed under the terms of the GNU General
 *  Public License, version 2.
 */

#include <getopt.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <stdlib.h>

#define BLKELVGET   _IO(0x12,106)/* elevator get */
#define BLKELVSET   _IO(0x12,107)/* elevator set */

/* this has to match with the kernel structure */
typedef struct blkelv_ioctl_arg_s {
	void * queue_ID;
	int read_latency;
	int write_latency;
	int max_bomb_segments;
} blkelv_ioctl_arg_t;

static void
usage(void) {
	fprintf(stderr, "usage:\n\telvtune [-r r_lat] [-w w_lat] [-b b_lat] /dev/blkdev1 [/dev/blkdev2...]\n");
	fprintf(stderr, "\telvtune -h\n");
	fprintf(stderr, "\telvtune -v\n");
}

static void
version(void) {
	fprintf(stderr, "elvtune: version 1.0\n");
}

int
main(int argc, char * argv[]) {
	int read_value = 0xbeefbeef, write_value = 0xbeefbeef, bomb_value = 0xbeefbeef;
	int read_set, write_set, bomb_set, set;
	char * devname;
	int fd;
	blkelv_ioctl_arg_t elevator;

	read_set = write_set = bomb_set = set = 0;

	for (;;) {
		int opt;

		opt = getopt(argc, argv, "r:w:b:hv");
		if (opt < 0)
			break;
		switch (opt) {
		case 'r':
			read_value = atoi(optarg);
			read_set = set = 1;
			break;
		case 'w':
			write_value = atoi(optarg);
			write_set = set = 1;
			break;
		case 'b':
			bomb_value = atoi(optarg);
			bomb_set = set = 1;
			break;

		case 'h':
			usage(), exit(0);
		case 'v':
			version(), exit(0);

		case '?':
		default:
		case ':':
			fprintf(stderr, "parse error\n");
			exit(1);
		}
	}

	if (optind >= argc)
		fprintf(stderr, "missing blockdevice, use -h for help\n"), exit(1);
		
	while (optind < argc) {
		devname = argv[optind++];

		fd = open(devname, O_RDONLY|O_NONBLOCK);
		if (fd < 0) {
			perror("open");
			break;
		}

		if (ioctl(fd, BLKELVGET, &elevator) < 0) {
			perror("ioctl get");
			break;
		}

		if (set) {
			if (read_set)
				elevator.read_latency = read_value;
			if (write_set)
				elevator.write_latency = write_value;
			if (bomb_set)
				elevator.max_bomb_segments = bomb_value;

			if (ioctl(fd, BLKELVSET, &elevator) < 0) {
				perror("ioctl set");
				break;
			}
			if (ioctl(fd, BLKELVGET, &elevator) < 0) {
				perror("ioctl reget");
				break;
			}
		}

		printf("\n%s elevator ID %p\n", devname, elevator.queue_ID);
		printf("\tread_latency:\t\t%d\n", elevator.read_latency);
		printf("\twrite_latency:\t\t%d\n", elevator.write_latency);
		printf("\tmax_bomb_segments:\t%d\n\n", elevator.max_bomb_segments);

		if (close(fd) < 0) {
			perror("close");
			break;
		}
	}

	return 0;
}
