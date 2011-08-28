/*
 *  ipcmk.c - used to create ad-hoc IPC segments
 *
 *  Copyright (C) 2008 Hayden A. James (hayden.james@gmail.com)
 *  Copyright (C) 2008 Karel Zak <kzak@redhat.com>
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
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <getopt.h>

#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/msg.h>

#include "nls.h"
#include "c.h"

static const char *progname;

key_t createKey(void)
{
	srandom( time( NULL ) );
	return random();
}

int createShm(size_t size, int permission)
{
	int result = -1;
	int shmid;
	key_t key = createKey();

	if (-1 != (shmid = shmget(key, size, permission | IPC_CREAT)))
		result = shmid;

	return result;
}

int createMsg(int permission)
{
	int result = -1;
	int msgid;
	key_t key = createKey();

	if (-1 != (msgid = msgget(key, permission | IPC_CREAT)))
		result = msgid;

	return result;
}

int createSem(int nsems, int permission)
{
	int result = -1;
	int semid;
	key_t key = createKey();

	if (-1 != (semid = semget(key, nsems, permission | IPC_CREAT)))
		result = semid;

	return result;
}

static void __attribute__ ((__noreturn__)) usage(FILE * out)
{
	fprintf(out, USAGE_HEADER);
	fprintf(out, _(" %s [options]\n"), progname);
	fprintf(out, USAGE_OPTIONS);

	fputs(_(" -M, --shmem <size>       create shared memory segment of size <size>\n"), out);
	fputs(_(" -S, --semaphore <nsems>  create semaphore array with <nsems> elements\n"), out);
	fputs(_(" -Q, --queue              create message queue\n"), out);
	fputs(_(" -p, --mode <mode>        permission for the resource (default is 0644)\n"), out);

	fprintf(out, USAGE_HELP);
	fprintf(out, USAGE_VERSION);
	fprintf(out, USAGE_BEGIN_TAIL);
	fprintf(out, USAGE_MAN_TAIL, "ipcmk(1)");
	exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}

int main(int argc, char **argv)
{
	int permission = 0644;
	int opt;
	size_t size = 0;
	int nsems = 0;
	int doShm = 0, doMsg = 0, doSem = 0;
	static const struct option longopts[] = {
		{"shmem", required_argument, NULL, 'M'},
		{"semaphore", required_argument, NULL, 'S'},
		{"queue", no_argument, NULL, 'Q'},
		{"mode", required_argument, NULL, 'p'},
		{"version", no_argument, NULL, 'V'},
		{"help", no_argument, NULL, 'h'},
		{NULL, 0, NULL, 0}
	};

	progname = program_invocation_short_name;
	if (!progname)
		progname = "ipcmk";

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

	while((opt = getopt_long(argc, argv, "hM:QS:p:Vh", longopts, NULL)) != -1) {
		switch(opt) {
		case 'M':
			size = atoi(optarg);
			doShm = 1;
			break;
		case 'Q':
			doMsg = 1;
			break;
		case 'S':
			nsems = atoi(optarg);
			doSem = 1;
			break;
		case 'p':
			permission = strtoul(optarg, NULL, 8);
			break;
		case 'h':
			usage(stdout);
			break;
		case 'V':
			printf(UTIL_LINUX_VERSION);
			return EXIT_SUCCESS;
		default:
			doShm = doMsg = doSem = 0;
			break;
		}
	}

	if(!doShm && !doMsg && !doSem)
		usage(stderr);

	if (doShm) {
		int shmid;
		if (-1 == (shmid = createShm(size, permission)))
			err(EXIT_FAILURE, _("create share memory failed"));
		else
			printf(_("Shared memory id: %d\n"), shmid);
	}

	if (doMsg) {
		int msgid;
		if (-1 == (msgid = createMsg(permission)))
			err(EXIT_FAILURE, _("create message queue failed"));
		else
			printf(_("Message queue id: %d\n"), msgid);
	}

	if (doSem) {
		int semid;
		if (-1 == (semid = createSem(nsems, permission)))
			err(EXIT_FAILURE, _("create semaphore failed"));
		else
			printf(_("Semaphore id: %d\n"), semid);
	}

	return EXIT_SUCCESS;
}
