/*
 *  ipcmk.c - used to create ad-hoc IPC segments
 *
 *  Copyright (C) 2008 Hayden A. James (hayden.james@gmail.com)
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

#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/msg.h>

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

void usage(char *progname)
{
	   fprintf(stderr, "usage: %s [-M size] [-S nsems] [-Q] [-p permission]\n", progname);
}

int main(int argc, char **argv)
{
	int permission = 0644;
	int opt;
	size_t size = 0;
	int nsems = 0;

	int doShm = 0, doMsg = 0, doSem = 0;

	while((opt = getopt(argc, argv, "M:QS:p:")) != -1) {
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
		default:
			doShm = doMsg = doSem = 0;
			break;
		}
	}

	if (doShm) {
		int shmid;
		if (-1 == (shmid = createShm(size, permission)))
			fprintf(stderr, "%s\n", strerror(errno));
		else
			fprintf(stdout, "%s%d\n", "Shared memory id: ", shmid);
	}

	if (doMsg) {
		int msgid;
		if (-1 == (msgid = createMsg(permission)))
			fprintf(stderr, "%s\n", strerror(errno));
		else
			fprintf(stdout, "%s%d\n", "Message queue id: ", msgid);
	}

	if (doSem) {
		int semid;
		if (-1 == (semid = createSem(nsems, permission)))
			fprintf(stderr, "%s\n", strerror(errno));
		else
			fprintf(stdout, "%s%d\n", "Semaphore id: ", semid);
	}

	if(!doShm && !doMsg && !doSem)
		usage(argv[0]);

	return 0;
}
