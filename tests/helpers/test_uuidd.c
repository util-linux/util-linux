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

#define LOG(level,args) if (logging >= level) { fprintf args; }

pid_t pid;

int processes = 4;
int threads = 4;
int objects = 4096;
int logging = 1;
int help = 0;

struct threadentry {
	pthread_t thread;
	pthread_attr_t thread_attr;
	long arg;		/* index in object[] */
	long value;		/* thread retval */
};
typedef struct threadentry thread_t;

struct processentry {
	pid_t pid;
	int status;
};
typedef struct processentry process_t;

struct objectentry {
	uuid_t uuid;
	pid_t pid;
	pthread_t tid;
	int pos;
};
typedef struct objectentry object_t;

int objectid;
object_t *object;


static void __attribute__((__noreturn__)) usage(FILE *out)
{
	fprintf(out, "\n %s [options]\n", program_invocation_short_name);

	fprintf(out, "  -p <num>     number of of processes (default:%d)\n", processes);
	fprintf(out, "  -t <num>     number of threads (default:%d)\n", threads);
	fprintf(out, "  -o <num>     number of objects (default:%d)\n", objects);
	fprintf(out, "  -l <level>   log level (default:%d)\n", logging);
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
	     "%d: allocate shared memory segment [id=%d,address=0x%p]\n",
	     pid, *id, *address));
}

static void remove_segment(int id, void *address)
{
	if (shmdt(address) == -1)
		err(EXIT_FAILURE, "shmdt failed");
	if (shmctl(id, IPC_RMID, NULL) == -1)
		err(EXIT_FAILURE, "shmctl failed");
	LOG(2,
	    (stderr,
	     "%d: remove shared memory segment [id=%d,address=0x%p]\n",
	     pid, id, address));
}

static void object_uuid_create(object_t * object)
{
	uuid_generate_time(object->uuid);
}

static void object_uuid_to_string(object_t * object, unsigned char **string_uuid)
{
	uuid_unparse(object->uuid, *string_uuid);
}

static int object_uuid_compare(const void *object1, const void *object2)
{
	uuid_t *uuid1, *uuid2;
	int result;

	uuid1 = &((object_t *) object1)->uuid;
	uuid2 = &((object_t *) object2)->uuid;
	result = uuid_compare(*uuid1, *uuid2);
	return (result);
}

static void *create_uuids(void *p)
{
	long index, i;

	index = *((long *)p);
	for (i = index; i < index + objects; i++) {
		object_uuid_create(&object[i]);
		object[i].pid = pid;
		object[i].tid = pthread_self();
		object[i].pos = i - index;
	}
	return (0);
}

static void create_threads(int index)
{
	thread_t *thread;
	int i, result;
	pid_t pid = getpid();

	thread = (thread_t *) xcalloc(threads, sizeof(thread_t));
	for (i = 0; i < threads; i++) {
		result = pthread_attr_init(&thread[i].thread_attr);
		if (result)
			error(EXIT_FAILURE, result, "pthread_attr_init failed");

		thread[i].arg = index;
		result =  pthread_create(&thread[i].thread, &thread[i].thread_attr,
				   &create_uuids, &thread[i].arg);
		if (result)
			error(EXIT_FAILURE, result, "pthread_create failed");

		LOG(2,
		    (stderr, "%d: started thread [tid=%d,arg=%d]\n",
		     pid, thread[i].thread, thread[i].arg));
		index += objects;
	}

	for (i = 0; i < threads; i++) {
		result = pthread_join(thread[i].thread, (void *)&thread[i].value);
		if (result)
			error(EXIT_FAILURE, result, "pthread_join failed");
		LOG(2,
		    (stderr, "%d: thread exited [tid=%d,value=%d]\n",
		     pid, thread[i].thread, thread[i].value));
	}
	free(thread);
}

static void create_processes()
{
	process_t *process;
	int i;

	process = (process_t *) xcalloc(processes, sizeof(process_t));
	for (i = 0; i < processes; i++) {
		process[i].pid = fork();
		switch (process[i].pid) {
		case -1:
			err(EXIT_FAILURE, "fork failed");
			break;
		case 0:
			create_threads(i * threads * objects);
			exit(EXIT_SUCCESS);
			break;
		default:
			LOG(2,
			    (stderr, "%d: started process [pid=%d]\n",
			     pid, process[i].pid));
			break;
		}
	}

	for (i = 0; i < processes; i++) {
		if (waitpid(process[i].pid, &process[i].status, 0) ==
		    (pid_t) - 1)
			err(EXIT_FAILURE, "waitpid failed");

		LOG(2,
		    (stderr, "%d: process exited [pid=%d,status=%d]\n",
		     pid, process[i].pid, process[i].status));
	}
	free(process);
}

static void object_dump(int i)
{
	unsigned char uuid_string[37], *p;

	p = uuid_string;
	object_uuid_to_string(&object[i], &p);
	LOG(0,
	    (stderr, "%d: object[%d]=[uuid=<%s>,pid=%d,tid=%d,pos=%d]\n",
	     pid, i, p, object[i].pid, object[i].tid, object[i].pos));
}

int main(int argc, char *argv[])
{
	int i, count;
	int c;

	while (((c = getopt(argc, argv, "p:t:o:l:h")) != -1)) {
		switch (c) {
		case 'p':
			processes = strtou32_or_err(optarg, "invalid processes number argument");
			break;
		case 't':
			threads = strtou32_or_err(optarg, "invalid threads number argument");
			break;
		case 'o':
			objects = strtou32_or_err(optarg, "invalid objects number argument");
			break;
		case 'l':
			logging = strtou32_or_err(optarg, "invalid log level argument");
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

	if (logging == 1)
		fprintf(stderr, "requested: %d processes, %d threads, %d objects\n",
				processes, threads, objects);


	allocate_segment(&objectid, (void **)&object,
			 processes * threads * objects, sizeof(object_t));
	create_processes();
	if (logging >= 3) {
		for (i = 0; i < processes * threads * objects; i++) {
			object_dump(i);
		}
	}
	qsort(object, processes * threads * objects, sizeof(object_t),
	      object_uuid_compare);
	LOG(2, (stdout, "%d: qsort() done\n", pid));
	count = 0;
	for (i = 0; i < processes * threads * objects - 1; i++) {
		if (object_uuid_compare(&object[i], &object[i + 1]) == 0) {
			if (logging >= 1) {
				LOG(0,
				    (stdout,
				     "%d: objects #%d and #%d have duplicate UUIDs\n",
				     pid, i, i + 1));
				object_dump(i);
				object_dump(i + 1);
			}
			count = count + 1;
		}
	}
	remove_segment(objectid, object);
	if (count == 0) {
		LOG(0,
		    (stdout, "test successful (no duplicate UUIDs found)\n"));
	}
	else {
		LOG(0,
		    (stdout, "test failed (found %d duplicate UUIDs)\n",
		     count));
	}
}
