/*
 *  ipcmk.c - used to create ad-hoc IPC segments
 *
 *  Copyright (C) 2008 Hayden A. James (hayden.james@gmail.com)
 *  Copyright (C) 2008 Karel Zak <kzak@redhat.com>
 *  
 *  2025 Prasanna Paithankar <paithankarprasanna@gmail.com>
 * 	- Added POSIX IPC support
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
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <errno.h>
#include <getopt.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/time.h>

#ifdef HAVE_MQUEUE_H
# include <mqueue.h>
#endif

#ifdef HAVE_SEMAPHORE_H
# include <semaphore.h>
#endif

#ifdef HAVE_SYS_MMAN_H
# include <sys/stat.h>
# include <sys/mman.h>
#endif

#include "c.h"
#include "nls.h"
#include "randutils.h"
#include "strutils.h"
#include "closestream.h"

static int create_shm(size_t size, int permission)
{
	key_t key;

	ul_random_get_bytes(&key, sizeof(key));
	return shmget(key, size, permission | IPC_CREAT);
}

#ifndef HAVE_SYS_MMAN_H
static int create_posix_shm(const char *name __attribute__((__unused__)),
			size_t size __attribute__((__unused__)),
			int permission __attribute__((__unused__)))
{
	warnx(_("POSIX shared memory is not supported"));
	return -1;
}
#else
static int create_posix_shm(const char *name, size_t size, int permission)
{
	int shmfd;

	if (-1 == (shmfd = shm_open(name, O_RDWR | O_CREAT, permission)))
		return -1;

	if (-1 == ftruncate(shmfd, size)) {
		close(shmfd);
		return -1;
	}

	close(shmfd);
	printf(_("POSIX shared memory name: %s\n"), name);
	return 0;
}
#endif

static int create_msg(int permission)
{
	key_t key;

	ul_random_get_bytes(&key, sizeof(key));
	return msgget(key, permission | IPC_CREAT);
}

#ifndef HAVE_MQUEUE_H
static int create_posix_msg(const char *name __attribute__((__unused__)),
			int permission __attribute__((__unused__)))
{
	warnx(_("POSIX message queue is not supported"));
	return -1;
}
#else
static int create_posix_msg(const char *name, int permission)
{
	mqd_t mqd;

	if (-1 == (mqd = mq_open(name, O_RDWR | O_CREAT, permission, NULL)))
		return -1;

	mq_close(mqd);
	printf(_("POSIX message queue name: %s\n"), name);
	return 0;
}
#endif

static int create_sem(int nsems, int permission)
{
	key_t key;

	ul_random_get_bytes(&key, sizeof(key));
	return semget(key, nsems, permission | IPC_CREAT);
}

#ifndef HAVE_SEMAPHORE_H
static int create_posix_sem(const char *name __attribute__((__unused__)),
			int permission __attribute__((__unused__)))
{
	warnx(_("POSIX semaphore is not supported"));
	return -1;
}
#else
static int create_posix_sem(const char *name, int permission)
{
	sem_t *sem;

	if (SEM_FAILED == (sem = sem_open(name, O_CREAT, permission, 0)))
		return -1;

	sem_close(sem);
	printf(_("POSIX semaphore name: %s\n"), name);
	return 0;
}
#endif

static void __attribute__((__noreturn__)) usage(void)
{
	FILE *out = stdout;
	fputs(USAGE_HEADER, out);
	fprintf(out, _(" %s [options]\n"), program_invocation_short_name);

	fputs(USAGE_SEPARATOR, out);
	fputs(_("Create various IPC resources.\n"), out);

	fputs(USAGE_OPTIONS, out);
	fputs(_(" -M, --shmem <size>       create shared memory segment of size <size>\n"), out);
	fputs(_(" -m, --posix-shmem <size> create POSIX shared memory segment of size <size>\n"), out);
	fputs(_(" -S, --semaphore <number> create semaphore array with <number> elements\n"), out);
	fputs(_(" -s, --posix-semaphore    create POSIX semaphore\n"), out);
	fputs(_(" -Q, --queue              create message queue\n"), out);
	fputs(_(" -q, --posix-mqueue       create POSIX message queue\n"), out);
	fputs(_(" -p, --mode <mode>        permission for the resource (default is 0644)\n"), out);
	fputs(_(" -n, --name <name>        name of the POSIX resource\n"), out);

	fputs(USAGE_SEPARATOR, out);
	fprintf(out, USAGE_HELP_OPTIONS(26));

	fputs(USAGE_ARGUMENTS, out);
	fprintf(out, USAGE_ARG_SIZE(_("<size>")));

	fputs(USAGE_SEPARATOR, out);
	fputs(_(" -n, --name <name> option is required for POSIX IPC\n"), out);

	fprintf(out, USAGE_MAN_TAIL("ipcmk(1)"));

	exit(EXIT_SUCCESS);
}

int main(int argc, char **argv)
{
	int permission = 0644;
	char *name = NULL;
	int opt;
	size_t size = 0;
	int nsems = 0;
	int ask_shm = 0, ask_msg = 0, ask_sem = 0, ask_pshm = 0, ask_pmsg = 0, ask_psem = 0;
	static const struct option longopts[] = {
		{"shmem", required_argument, NULL, 'M'},
		{"posix-shmem", required_argument, NULL, 'm'},
		{"semaphore", required_argument, NULL, 'S'},
		{"posix-semaphore", no_argument, NULL, 's'},
		{"queue", no_argument, NULL, 'Q'},
		{"posix-mqueue", no_argument, NULL, 'q'},
		{"mode", required_argument, NULL, 'p'},
		{"name", required_argument, NULL, 'n'},
		{"version", no_argument, NULL, 'V'},
		{"help", no_argument, NULL, 'h'},
		{NULL, 0, NULL, 0}
	};

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	close_stdout_atexit();

	while((opt = getopt_long(argc, argv, "hM:m:QqS:sp:n:Vh", longopts, NULL)) != -1) {
		switch(opt) {
		case 'M':
			size = strtosize_or_err(optarg, _("failed to parse size"));
			ask_shm = 1;
			break;
		case 'm':
			size = strtosize_or_err(optarg, _("failed to parse size"));
			ask_pshm = 1;
			break;
		case 'Q':
			ask_msg = 1;
			break;
		case 'q':
			ask_pmsg = 1;
			break;
		case 'S':
			nsems = strtos32_or_err(optarg, _("failed to parse elements"));
			ask_sem = 1;
			break;
		case 's':
			ask_psem = 1;
			break;
		case 'p':
		{
			char *end = NULL;
			errno = 0;
			permission = strtoul(optarg, &end, 8);
			if (errno || optarg == end || (end && *end))
				err(EXIT_FAILURE, _("failed to parse mode"));
			break;
		}
		case 'n':
			name = optarg;
			break;
		case 'h':
			usage();
		case 'V':
			print_version(EXIT_SUCCESS);
		default:
			errtryhelp(EXIT_FAILURE);
		}
	}

	if(!ask_shm && !ask_msg && !ask_sem && !ask_pshm && !ask_pmsg && !ask_psem) {
		warnx(_("bad usage"));
		errtryhelp(EXIT_FAILURE);
	}

	if ((ask_pshm + ask_pmsg + ask_psem > 0) && name == NULL) {
		warnx(_("name is required for POSIX IPC"));
		errtryhelp(EXIT_FAILURE);
	}

	if (ask_shm) {
		int shmid;
		if (-1 == (shmid = create_shm(size, permission)))
			err(EXIT_FAILURE, _("create share memory failed"));
		else
			printf(_("Shared memory id: %d\n"), shmid);
	}

	if (ask_pshm) {
		if (-1 == create_posix_shm(name, size, permission))
			err(EXIT_FAILURE, _("create POSIX shared memory failed"));
	}

	if (ask_msg) {
		int msgid;
		if (-1 == (msgid = create_msg(permission)))
			err(EXIT_FAILURE, _("create message queue failed"));
		else
			printf(_("Message queue id: %d\n"), msgid);
	}

	if (ask_pmsg) {
		if (-1 == create_posix_msg(name, permission))
			err(EXIT_FAILURE, _("create POSIX message queue failed"));
	}

	if (ask_sem) {
		int semid;
		if (-1 == (semid = create_sem(nsems, permission)))
			err(EXIT_FAILURE, _("create semaphore failed"));
		else
			printf(_("Semaphore id: %d\n"), semid);
	}

	if (ask_psem) {
		if (-1 == create_posix_sem(name, permission))
			err(EXIT_FAILURE, _("create POSIX semaphore failed"));
	}

	return EXIT_SUCCESS;
}
