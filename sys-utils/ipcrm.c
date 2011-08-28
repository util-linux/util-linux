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
#include "strutils.h"

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

int remove_id(int type, int iskey, int id)
{
	char *errmsg;
	/* needed to delete semaphores */
	union semun arg;
	arg.val = 0;

	/* do the removal */
	switch (type) {
	case SHM:
		shmctl(id, IPC_RMID, NULL);
		break;
	case MSG:
		msgctl(id, IPC_RMID, NULL);
		break;
	case SEM:
		semctl(id, 0, IPC_RMID, arg);
		break;
	default:
		errx(EXIT_FAILURE, "impossible occurred");
	}

	/* how did the removal go? */
	switch (errno) {
	case 0:
		return 0;
	case EACCES:
	case EPERM:
		errmsg = iskey ? _("permission denied for key") : _("permission denied for id");
		break;
	case EINVAL:
		errmsg = iskey ? _("invalid key") : _("invalid id");
		break;
	case EIDRM:
		errmsg = iskey ? _("already removed key") : _("already removed id");
		break;
	default:
		if (iskey)
			err(EXIT_FAILURE, _("key failed"));
		err(EXIT_FAILURE, _("id failed"));
	}
	warnx("%s (%d)", errmsg, id);
	return 1;
}

static int remove_arg_list(type_id type, int argc, char **argv)
{
	int id;
	char *end;
	int nb_errors = 0;

	do {
		id = strtoul(argv[0], &end, 10);
		if (*end != 0) {
			warnx(_("invalid id: %s"), argv[0]);
			nb_errors++;
		} else {
			if (remove_id(type, 0, id))
				nb_errors++;
		}
		argc--;
		argv++;
	} while (argc);
	return (nb_errors);
}

static int deprecated_main(int argc, char **argv)
{
	type_id type;

	if (!strcmp(argv[1], "shm"))
		type = SHM;
	else if (!strcmp(argv[1], "msg"))
		type = MSG;
	else if (!strcmp(argv[1], "sem"))
		type = SEM;
	else
		return 0;

	if (argc < 3) {
		warnx(_("not enough arguments"));
		usage(stderr);
	}

	if (remove_arg_list(type, argc - 2, &argv[2]))
		exit(EXIT_FAILURE);

	printf(_("resource(s) deleted\n"));
	return 1;
}

unsigned long strtokey(const char *str, const char *errmesg)
{
	unsigned long num;
	char *end = NULL;

	if (str == NULL || *str == '\0')
		goto err;
	errno = 0;
	/* keys are in hex or decimal */
	num = strtoul(str, &end, 0);

	if (errno || str == end || (end && *end))
		goto err;

	return num;
 err:
	if (errno)
		err(EXIT_FAILURE, "%s: '%s'", errmesg, str);
	else
		errx(EXIT_FAILURE, "%s: '%s'", errmesg, str);
	return 0;
}

static int key_to_id(type_id type, char *optarg)
{
	int id;
	/* keys are in hex or decimal */
	key_t key = strtokey(optarg, "failed to parse argument");
	if (key == IPC_PRIVATE) {
		warnx(_("illegal key (%s)"), optarg);
		return -1;
	}
	switch (type) {
	case SHM:
		id = shmget(key, 0, 0);
		break;
	case MSG:
		id = msgget(key, 0);
		break;
	case SEM:
		id = semget(key, 0, 0);
		break;
	default:
		errx(EXIT_FAILURE, "impossible occurred");
	}
	if (id < 0) {
		char *errmsg;
		switch (errno) {
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
	}
	return id;
}

int main(int argc, char **argv)
{
	int c;
	int ret = 0;
	int id = -1;
	int iskey;

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
	 * then remove argument list */
	if (deprecated_main(argc, argv))
		return EXIT_SUCCESS;

	/* process new syntax to conform with SYSV ipcrm */
	for (id = -1;
	    (c = getopt_long(argc, argv, "q:m:s:Q:M:S:hV", longopts, NULL)) != -1;
	    id = -1) {
		switch (c) {
		case 'M':
			iskey = 0;
			id = key_to_id(SHM, optarg);
			if (id < 0) {
				ret++;
				break;
			}
		case 'm':
			if (id < 0) {
				iskey = 1;
				id = strtoll_or_err(optarg, _("failed to parse argument"));
			}
			if (remove_id(SHM, iskey, id))
				ret++;
			break;
		case 'Q':
			iskey = 0;
			id = key_to_id(MSG, optarg);
			if (id < 0) {
				ret++;
				break;
			}
		case 'q':
			if (id < 0) {
				iskey = 1;
				id = strtoll_or_err(optarg, _("failed to parse argument"));
			}
			if (remove_id(MSG, iskey, id))
				ret++;
			break;
		case 'S':
			iskey = 0;
			id = key_to_id(SEM, optarg);
			if (id < 0) {
				ret++;
				break;
			}
		case 's':
			if (id < 0) {
				iskey = 1;
				id = strtoll_or_err(optarg, _("failed to parse argument"));
			}
			if (remove_id(SEM, iskey, id))
				ret++;
			break;
		case 'h':
			usage(stdout);
		case 'V':
			printf(UTIL_LINUX_VERSION);
			return EXIT_SUCCESS;
		default:
			usage(stderr);
		}
	}

	/* print usage if we still have some arguments left over */
	if (optind != argc) {
		warnx(_("unknown argument: %s"), argv[optind]);
		usage(stderr);
	}

	return ret == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
