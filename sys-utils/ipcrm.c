/*
 * krishna balasubramanian 1993
 *
 * 1999-02-22 Arkadiusz Mi¶kiewicz <misiek@pld.ORG.PL>
 * - added Native Language Support
 *
 * 1999-04-02 frank zago
 * - can now remove several id's in the same call
 *
 */

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <sys/sem.h>
#include "c.h"
#include "nls.h"

/* for getopt */
#include <unistd.h>
/* for tolower and isupper */
#include <ctype.h>

#ifndef HAVE_UNION_SEMUN
/* according to X/OPEN we have to define it ourselves */
union semun {
	int val;
	struct semid_ds *buf;
	unsigned short int *array;
	struct seminfo *__buf;
};
#endif

typedef enum type_id {
	SHM,
	SEM,
	MSG
} type_id;

/* print the new usage */
static void __attribute__ ((__noreturn__)) usage(FILE * out)
{
	fprintf(out, USAGE_HEADER);
	fprintf(out, " %s [options]\n", program_invocation_short_name);
	fprintf(out, " %s <shm|msg|sem> <id> [...]\n", program_invocation_short_name);
	fprintf(out, USAGE_OPTIONS);
	fputs(_(" -m, --shmem-id <id>        remove shared memory segment by shmid\n"), out);
	fputs(_(" -M, --shmem-key <key>      remove shared memory segment by key\n"), out);
	fputs(_(" -q, --queue-id <id>        remove message queue by id\n"), out);
	fputs(_(" -Q, --queue-key <key>      remove message queue by key\n"), out);
	fputs(_(" -s, --semaphore-id <id>    remove semaprhore by id\n"), out);
	fputs(_(" -S, --semaphore-key <key>  remove semaprhore by key\n"), out);
	fprintf(out, USAGE_HELP);
	fprintf(out, USAGE_VERSION);
	fprintf(out, USAGE_BEGIN_TAIL);
	fprintf(out, USAGE_MAN_TAIL, "ipcrm(1)");
	exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}

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
			warnx(_("invalid id: %s"), argv[0]);
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
				warn(_("cannot remove id %s"), argv[0]);
				nb_errors ++;
			}
		}
		argc--;
		argv++;
	}
	
	return(nb_errors);
}

static int deprecated_main(int argc, char **argv)
{
	if (argc < 3)
		usage(stderr);
	
	if (!strcmp(argv[1], "shm")) {
		if (remove_ids(SHM, argc-2, &argv[2]))
			exit(1);
	}
	else if (!strcmp(argv[1], "msg")) {
		if (remove_ids(MSG, argc-2, &argv[2]))
			exit(1);
	} 
	else if (!strcmp(argv[1], "sem")) {
		if (remove_ids(SEM, argc-2, &argv[2]))
			exit(1);
	}
	else {
		warnx(_("unknown resource type: %s"), argv[1]);
		usage(stderr);
	}

	printf (_("resource(s) deleted\n"));
	return 0;
}


int main(int argc, char **argv)
{
	int   c;
	int   error = 0;

	static const struct option longopts[] = {
		{"shmem-id", required_argument, NULL, 'm'},
		{"shmem-key", required_argument, NULL, 'M'},
		{"queue-id", required_argument, NULL, 'q'},
		{"queue-key", required_argument, NULL, 'Q'},
		{"semaphore-id", required_argument, NULL, 's'},
		{"semaphore-key", required_argument, NULL, 'S'},
		{"version", no_argument, NULL, 'V'},
		{"help", no_argument, NULL, 'h'},
		{NULL, 0, NULL, 0}
	};

	/* if the command is executed without parameters, do nothing */
	if (argc == 1)
		return 0;

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

	/* check to see if the command is being invoked in the old way if so
	   then run the old code */
	if (strcmp(argv[1], "shm") == 0 ||
	    strcmp(argv[1], "msg") == 0 ||
	    strcmp(argv[1], "sem") == 0)
		return deprecated_main(argc, argv);

	/* process new syntax to conform with SYSV ipcrm */
	while ((c = getopt_long(argc, argv, "q:m:s:Q:M:S:hV", longopts, NULL)) != -1) {
		int result;
		int id = 0;
		int iskey = isupper(c);

		/* needed to delete semaphores */
		union semun arg;
		arg.val = 0;

		/* --help & --version */
		if (c == 'h')
			usage(stdout);
		if (c == 'V') {
			printf(UTIL_LINUX_VERSION);
			return EXIT_SUCCESS;
		}

		/* we don't need case information any more */
		c = tolower(c);

		/* make sure the option is in range */
		if (c != 'q' && c != 'm' && c != 's') {
			usage(stderr);
		}

		if (iskey) {
			/* keys are in hex or decimal */
			key_t key = strtoul(optarg, NULL, 0);
			if (key == IPC_PRIVATE) {
				error++;
				warnx(_("illegal key (%s)"), optarg);
				continue;
			}

			/* convert key to id */
			id = ((c == 'q') ? msgget(key, 0) :
			      (c == 'm') ? shmget(key, 0, 0) :
			      semget(key, 0, 0));

			if (id < 0) {
				char *errmsg;
				error++;
				switch(errno) {
				case EACCES:
					errmsg = _("permission denied for key");
					break;
				case EIDRM:
					errmsg = _("already removed key");
					break;
				case ENOENT:
					errmsg = _("invalid key");
					break;
				default:
					err(EXIT_FAILURE, _("key failed"));
				}
				warnx("%s (%s)", errmsg, optarg);
				continue;
			}
		} else {
			/* ids are in decimal */
			id = strtoul(optarg, NULL, 10);
		}

		result = ((c == 'q') ? msgctl(id, IPC_RMID, NULL) :
			  (c == 'm') ? shmctl(id, IPC_RMID, NULL) : 
			  semctl(id, 0, IPC_RMID, arg));

		if (result < 0) {
			char *errmsg;
			error++;
			switch(errno) {
			case EACCES:
			case EPERM:
				errmsg = iskey
					? _("permission denied for key")
					: _("permission denied for id");
				break;
			case EINVAL:
				errmsg = iskey
					? _("invalid key")
					: _("invalid id");
				break;
			case EIDRM:
				errmsg = iskey
					? _("already removed key")
					: _("already removed id");
				break;
			default:
				if (iskey)
				        err(EXIT_FAILURE, _("key failed"));
                                err(EXIT_FAILURE, _("id failed"));
			}
			warnx("%s (%s)", errmsg, optarg);
			continue;
		}
	}

	/* print usage if we still have some arguments left over */
	if (optind != argc) {
		warnx(_("unknown argument: %s"), argv[optind]);
		usage(stderr);
	}

	if (error == 0 ? EXIT_SUCCESS : EXIT_FAILURE);
}
