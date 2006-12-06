/*
 * krishna balasubramanian 1993
 *
 * 1999-02-22 Arkadiusz Mi¶kiewicz <misiek@misiek.eu.org>
 * - added Native Language Support
 *
 * 1999-04-02 frank zago
 * - can now remove several id's in the same call
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <sys/sem.h>
#include "nls.h"

#if defined (__GNU_LIBRARY__) && !defined(_SEM_SEMUN_UNDEFINED)
/* union semun is defined by including <sys/sem.h> */
#else
/* according to X/OPEN we have to define it ourselves */
union semun {
	int val;
	struct semid_ds *buf;
	unsigned short int *array;
	struct seminfo *__buf;
};
#endif

char *execname;

typedef enum type_id {
	SHM,
	SEM,
	MSG
} type_id;

static int
remove_ids(type_id type, int argc, char **argv) {
	int id;
	int ret = 0;		/* for gcc */
	char *end;
	int nb_errors = 0;
	union semun arg;

	arg.val = 0;

	while(argc) {

		id = strtoul(argv[0], &end, 10);

		if (*end != 0) {
			printf (_("invalid id: %s\n"), argv[0]);
			nb_errors ++;
		} else {
			switch(type) {
			case SEM:
				ret = semctl (id, 0, IPC_RMID, arg);
				break;

			case MSG:
				ret = msgctl (id, IPC_RMID, NULL);
				break;
				
			case SHM:
				ret = shmctl (id, IPC_RMID, NULL);
				break;
			}

			if (ret) {
				printf (_("cannot remove id %s (%s)\n"),
					argv[0], strerror(errno));
				nb_errors ++;
			}
		}
		argc--;
		argv++;
	}
	
	return(nb_errors);
}

static void display_usage(void)
{
	printf (_("usage: %s {shm | msg | sem} id ...\n"), execname);
}

int main(int argc, char **argv)
{
	execname = argv[0];

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

	if (argc < 3) {
		display_usage();
		exit(1);
	}
	
	if (!strcmp(argv[1], "shm")) {
		if (remove_ids(SHM, argc-2, &argv[2])) {
			exit(1);
		}
	}
	else if (!strcmp(argv[1], "msg")) {
		if (remove_ids(MSG, argc-2, &argv[2])) {
			exit(1);
		}
	} 
	else if (!strcmp(argv[1], "sem")) {
		if (remove_ids(SEM, argc-2, &argv[2])) {
			exit(1);
		}
	}
	else {
		display_usage();
		printf (_("unknown resource type: %s\n"), argv[1]);
		exit(1);
	}

	printf (_("resource(s) deleted\n"));
	return 0;
}
			
