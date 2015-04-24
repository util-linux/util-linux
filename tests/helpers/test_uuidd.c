/*
 * Copyright (C) 2006 Hewlett-Packard Development Company, L.P.
 * Huschaam Hussain <Huschaam.Hussain@hp.com>
 *	TSG Solution Alliances Engineering
 *	SAP Technology Group
 *
 * Copyright (C) 2015 Karel Zak <kzak@redhat.com>
 */
#include <error.h>
#include <libgen.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <uuid/uuid.h>

#include "c.h"
#include "xalloc.h"
#include "strutils.h"

#define LOG(level,args) if (loglev >= level) { fprintf args; }

size_t nprocesses = 4;
size_t nthreads = 4;
size_t nobjects = 4096;
size_t loglev = 1;

struct processentry {
	pid_t		pid;
	int		status;
};
typedef struct processentry process_t;

struct threadentry {
	process_t	*proc;
	pthread_t	tid;		/* pthread_self() / phtread_create() */
	pthread_attr_t	thread_attr;
	size_t		index;		/* index in object[] */
	int		retval;		/* pthread exit() */
};
typedef struct threadentry thread_t;


struct objectentry {
	uuid_t		uuid;
	thread_t	*thread;
	size_t		id;
};
typedef struct objectentry object_t;

static int shmem_id;
static object_t *object;


static void __attribute__((__noreturn__)) usage(FILE *out)
{
	fprintf(out, "\n %s [options]\n", program_invocation_short_name);

	fprintf(out, "  -p <num>     number of of nprocesses (default:%zu)\n", nprocesses);
	fprintf(out, "  -t <num>     number of nthreads (default:%zu)\n", nthreads);
	fprintf(out, "  -o <num>     number of nobjects (default:%zu)\n", nobjects);
	fprintf(out, "  -l <level>   log level (default:%zu)\n", loglev);
	fprintf(out, "  -h           display help\n");

	exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}

static void allocate_segment(int *id, void **address, size_t number, size_t size)
{
	*id = shmget(IPC_PRIVATE, number * size, IPC_CREAT | 0600);
	if (*id == -1)
		err(EXIT_FAILURE, "shmget failed");

	*address = shmat(*id, NULL, 0);
	if (*address == (void *)-1)
		err(EXIT_FAILURE, "shmat failed");

	LOG(2, (stderr,
	     "allocate shared memory segment [id=%d,address=0x%p]\n",
	     *id, *address));
}

static void remove_segment(int id, void *address)
{
	if (shmdt(address) == -1)
		err(EXIT_FAILURE, "shmdt failed");
	if (shmctl(id, IPC_RMID, NULL) == -1)
		err(EXIT_FAILURE, "shmctl failed");
	LOG(2,
	    (stderr,
	     "remove shared memory segment [id=%d,address=0x%p]\n",
	     id, address));
}

static void object_uuid_create(object_t * object)
{
	uuid_generate_time(object->uuid);
}

static void object_uuid_to_string(object_t * object, char **string_uuid)
{
	uuid_unparse(object->uuid, *string_uuid);
}

static int object_uuid_compare(const void *object1, const void *object2)
{
	uuid_t *uuid1 = &((object_t *) object1)->uuid,
	       *uuid2 = &((object_t *) object2)->uuid;

	return uuid_compare(*uuid1, *uuid2);
}

static void *create_uuids(thread_t *th)
{
	size_t i;

	for (i = th->index; i < th->index + nobjects; i++) {
		object_uuid_create(&object[i]);
		object[i].thread = th;
		object[i].id = i - th->index;
	}
	return 0;
}

static void *thread_body(void *arg)
{
	thread_t *th = (thread_t *) arg;

	return create_uuids(th);
}

static void create_nthreads(process_t *proc, size_t index)
{
	thread_t *thread;
	size_t i, result;
	pid_t pid = getpid();

	thread = (thread_t *) xcalloc(nthreads, sizeof(thread_t));
	for (i = 0; i < nthreads; i++) {
		result = pthread_attr_init(&thread[i].thread_attr);
		if (result)
			error(EXIT_FAILURE, result, "pthread_attr_init failed");

		thread[i].index = index;
		thread[i].proc = proc;
		result = pthread_create(&thread[i].tid,
					&thread[i].thread_attr,
					&thread_body,
					&thread[i]);
		if (result)
			error(EXIT_FAILURE, result, "pthread_create failed");

		LOG(2,
		    (stderr, "%d: started thread [tid=%d,index=%zu]\n",
		     pid, (int) thread[i].tid, thread[i].index));
		index += nobjects;
	}

	for (i = 0; i < nthreads; i++) {
		result = pthread_join(thread[i].tid, (void *)&thread[i].retval);
		if (result)
			error(EXIT_FAILURE, result, "pthread_join failed");
		LOG(2,
		    (stderr, "%d: thread exited [tid=%d,return=%d]\n",
		     pid, (int) thread[i].tid, thread[i].retval));
	}
	free(thread);
}

static void create_nprocesses(void)
{
	process_t *process;
	size_t i;

	process = (process_t *) xcalloc(nprocesses, sizeof(process_t));
	for (i = 0; i < nprocesses; i++) {
		process[i].pid = fork();
		switch (process[i].pid) {
		case -1: /* error */
			err(EXIT_FAILURE, "fork failed");
			break;
		case 0: /* child */
			process[i].pid = getpid();
			create_nthreads(&process[i], i * nthreads * nobjects);
			exit(EXIT_SUCCESS);
			break;
		default: /* parent */
			LOG(2, (stderr, "started process [pid=%d]\n",
						process[i].pid));
			break;
		}
	}

	for (i = 0; i < nprocesses; i++) {
		if (waitpid(process[i].pid, &process[i].status, 0) ==
		    (pid_t) - 1)
			err(EXIT_FAILURE, "waitpid failed");

		LOG(2,
		    (stderr, "process exited [pid=%d,status=%d]\n",
		     process[i].pid, process[i].status));
	}
	free(process);
}

static void object_dump(size_t i)
{
	char uuid_string[37], *p;

	p = uuid_string;
	object_uuid_to_string(&object[i], &p);
	fprintf(stderr, "object[%zu]: {uuid=<%s>,pid=%d,tid=%d,id=%zu}\n",
	     i, p, object[i].thread->proc->pid,
	     (int) object[i].thread->tid, object[i].id);
}

int main(int argc, char *argv[])
{
	size_t i, count;
	int c;

	while (((c = getopt(argc, argv, "p:t:o:l:h")) != -1)) {
		switch (c) {
		case 'p':
			nprocesses = strtou32_or_err(optarg, "invalid nprocesses number argument");
			break;
		case 't':
			nthreads = strtou32_or_err(optarg, "invalid nthreads number argument");
			break;
		case 'o':
			nobjects = strtou32_or_err(optarg, "invalid nobjects number argument");
			break;
		case 'l':
			loglev = strtou32_or_err(optarg, "invalid log level argument");
			break;
		case 'h':
			usage(stdout);
			break;
		default:
			usage(stderr);
			break;
		}
	}

	if (optind != argc)
		usage(stderr);

	if (loglev == 1)
		fprintf(stderr, "requested: %zu nprocesses, %zu nthreads, %zu nobjects\n",
				nprocesses, nthreads, nobjects);


	allocate_segment(&shmem_id, (void **)&object,
			 nprocesses * nthreads * nobjects, sizeof(object_t));
	create_nprocesses();
	if (loglev >= 3) {
		for (i = 0; i < nprocesses * nthreads * nobjects; i++)
			object_dump(i);
	}

	qsort(object, nprocesses * nthreads * nobjects, sizeof(object_t),
	      object_uuid_compare);
	LOG(2, (stdout, "qsort() done\n"));
	count = 0;
	for (i = 0; i < nprocesses * nthreads * nobjects - 1; i++) {
		if (object_uuid_compare(&object[i], &object[i + 1]) == 0) {
			if (loglev >= 1)
				fprintf(stderr, "nobjects #%zu and #%zu have duplicate UUIDs\n",
					i, i + 1);
					object_dump(i);
					object_dump(i + 1);
			count = count + 1;
		}
	}
	remove_segment(shmem_id, object);
	if (count == 0)
		printf("test successful (no duplicate UUIDs found)\n");
	else
		printf("test failed (found %zu duplicate UUIDs)\n", count);
}
