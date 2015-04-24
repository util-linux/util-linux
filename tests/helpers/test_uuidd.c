/* (C) 2006 Hewlett-Packard Development Company, L.P.                         */
/* Huschaam Hussain, TSG Solution Alliances Engineering, SAP Technology Group */
/* eMail: Huschaam.Hussain@hp.com                                             */

#include <errno.h>
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

#define LOG(level,args) if (logging >= level) { fprintf args; }

char *program;
pid_t pid;

int processes = 4;
int threads = 4;
int objects = 4096;
int logging = 0;
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

void
error(int line, char *text)
{
	LOG(0,
	    (stderr, "%s %d: line %d: %s: errno %d: %s\n", program, pid, line,
	     text, errno, strerror(errno)));
	exit(errno);
}				/* of error */

void
uuid_error(int line, char *text, int status)
{
	LOG(0,
	    (stderr, "%s %d: line %d: %s: status %d\n", program, pid, line,
	     text, status));
	exit(1);
}				/* of uuid error */

void
pthread_error(int line, char *text, int value)
{
	LOG(0,
	    (stderr, "%s %d: line %d: %s: errno %d: %s\n", program, pid, line,
	     text, value, strerror(value)));
	exit(value);
}				/* of pthread error */

void
read_arguments(int argc, char *argv[])
{
	int status, i;
	unsigned long parameter;

	status = 0;
	while (!status && ((i = getopt(argc, argv, "p:t:o:l:h")) != -1)) {
		switch (i) {
		case 'p':
			processes = strtol(optarg, (char **)NULL, 10);
			status = errno != 0 || processes < 1
			    || processes > 1024;
			if (status)
				LOG(0,
				    (stderr,
				     "error: bad value for <number of processes>\n"));
			break;
		case 't':
			threads = strtol(optarg, (char **)NULL, 10);
			status = errno != 0 || threads < 1 || threads > 8192;
			if (status)
				LOG(0,
				    (stderr,
				     "error: bad value for <number of threads>\n"));
			break;
		case 'o':
			objects = strtol(optarg, (char **)NULL, 10);
			status = errno != 0 || objects < 1 || objects > 1048576;
			if (status)
				LOG(0,
				    (stderr,
				     "error: bad value for <number of objects>\n"));
			break;
		case 'l':
			logging = strtol(optarg, (char **)NULL, 10);
			if (errno != 0 || logging < 0 || logging > 3) {
				status = 1;
				fprintf(stderr,
					"error: bad value for <logging level>\n");
			}	/* of if */
			break;
		case 'h':
			help = 1;
			break;
		default:
			status = 1;
		}		/* of switch */
	}			/* of while */
	if (optind != argc)
		status = 1;
	if (status != 0 || help != 0) {
		LOG(0, (stderr, "usage: %s [options]\n", program));
		LOG(0,
		    (stderr,
		     "       -p <number of processes> [1-1024] default:%d\n",
		     processes));
		LOG(0,
		    (stderr,
		     "       -t <number of threads> [1-8192] default:%d\n",
		     threads));
		LOG(0,
		    (stderr,
		     "       -o <number of objects> [1-1048576] default:%d\n",
		     objects));
		LOG(0,
		    (stderr, "       -l <logging level> [0|1|2|3] default:%d\n",
		     logging));
		LOG(0, (stderr, "       -h display help\n"));
		exit(status);
	}			/* of if */
	LOG(1, (stderr, "%s %d: processes=%d\n", program, pid, processes));
	LOG(1, (stderr, "%s %d: threads=%d\n", program, pid, threads));
	LOG(1, (stderr, "%s %d: objects=%d\n", program, pid, objects));
	LOG(1, (stderr, "%s %d: logging=%d\n", program, pid, logging));
	LOG(1, (stderr, "%s %d: help=%d\n", program, pid, help));
}				/* of read arguments */

void
allocate_segment(int *id, void **address, size_t number, size_t size)
{
	*id = shmget(IPC_PRIVATE, number * size, IPC_CREAT | 0600);
	if (*id == -1)
		error(__LINE__ - 2, "shmget()");
	*address = shmat(*id, NULL, 0);
	if (*address == (void *)-1)
		error(__LINE__ - 2, "shmat()");
	LOG(2,
	    (stderr,
	     "%s %d: allocate shared memory segment [id=%d,address=0x%p]\n",
	     program, pid, *id, *address));
}				/* of allocate segment */

void
remove_segment(int id, void *address)
{
	if (shmdt(address) == -1)
		error(__LINE__ - 1, "shmdt()");
	if (shmctl(id, IPC_RMID, NULL) == -1)
		error(__LINE__ - 1, "shmctl()");
	LOG(2,
	    (stderr,
	     "%s %d: remove shared memory segment [id=%d,address=0x%p]\n",
	     program, pid, id, address));
}				/* of remove segment */

void
object_uuid_create(object_t * object)
{
	uuid_generate_time(object->uuid);
}				/* of object uuid create */

void
object_uuid_to_string(object_t * object, unsigned char **string_uuid)
{
	uuid_unparse(object->uuid, *string_uuid);
}				/* of object uuid to string */

int
object_uuid_compare(const void *object1, const void *object2)
{
	uuid_t *uuid1, *uuid2;
	int result;

	uuid1 = &((object_t *) object1)->uuid;
	uuid2 = &((object_t *) object2)->uuid;
	result = uuid_compare(*uuid1, *uuid2);
	return (result);
}				/* of object uuid compare */

void *
create_uuids(void *p)
{
	long index, i;

	index = *((long *)p);
	for (i = index; i < index + objects; i++) {
		object_uuid_create(&object[i]);
		object[i].pid = pid;
		object[i].tid = pthread_self();
		object[i].pos = i - index;
	}			/* of for */
	return (0);
}				/* of create uuids */

void
create_threads(int index)
{
	thread_t *thread;
	int i, result;

	pid = getpid();
	thread = (thread_t *) calloc(threads, sizeof(thread_t));
	if (thread == NULL)
		error(__LINE__ - 2, "calloc");
	for (i = 0; i < threads; i++) {
		result = pthread_attr_init(&thread[i].thread_attr);
		if (result != 0)
			pthread_error(__LINE__, "pthread_attr_init", result);
		thread[i].arg = index;
		result =
		    pthread_create(&thread[i].thread, &thread[i].thread_attr,
				   &create_uuids, &thread[i].arg);
		if (result != 0)
			pthread_error(__LINE__, "pthread_create", result);
		LOG(2,
		    (stderr, "%s %d: started thread [tid=%d,arg=%d]\n", program,
		     pid, thread[i].thread, thread[i].arg));
		index = index + objects;
	}			/* of for */
	for (i = 0; i < threads; i++) {
		result =
		    pthread_join(thread[i].thread, (void *)&thread[i].value);
		if (result != 0)
			pthread_error(__LINE__, "pthread_join", result);
		LOG(2,
		    (stderr, "%s %d: thread exited [tid=%d,value=%d]\n",
		     program, pid, thread[i].thread, thread[i].value));
	}			/* of for */
	free(thread);
}				/* of create threads */

void
create_processes()
{
	process_t *process;
	int i;

	process = (process_t *) calloc(processes, sizeof(process_t));
	if (process == NULL)
		error(__LINE__ - 2, "calloc");
	for (i = 0; i < processes; i++) {
		process[i].pid = fork();
		switch (process[i].pid) {
		case -1:
			error(__LINE__ - 3, "fork()");
			break;
		case 0:
			create_threads(i * threads * objects);
			exit(0);
			break;
		default:
			LOG(2,
			    (stderr, "%s %d: started process [pid=%d]\n",
			     program, pid, process[i].pid));
			break;
		}		/* of switch */
	}			/* of for */
	for (i = 0; i < processes; i++) {
		if (waitpid(process[i].pid, &process[i].status, 0) ==
		    (pid_t) - 1)
			error(__LINE__ - 1, "waitpid()");
		LOG(2,
		    (stderr, "%s %d: process exited [pid=%d,status=%d]\n",
		     program, pid, process[i].pid, process[i].status));
	}			/* of for */
	free(process);
}				/* of create processes */

void
object_dump(int i)
{
	unsigned char uuid_string[37], *p;

	p = uuid_string;
	object_uuid_to_string(&object[i], &p);
	LOG(0,
	    (stderr, "%s %d: object[%d]=[uuid=<%s>,pid=%d,tid=%d,pos=%d]\n",
	     program, pid, i, p, object[i].pid, object[i].tid, object[i].pos));
}				/* of object dump */

int
main(int argc, char *argv[])
{
	int i, count;

	errno = 0;
	program = strdup(basename(argv[0]));
	pid = getpid();
	read_arguments(argc, argv);
	allocate_segment(&objectid, (void **)&object,
			 processes * threads * objects, sizeof(object_t));
	create_processes();
	if (logging >= 3) {
		for (i = 0; i < processes * threads * objects; i++) {
			object_dump(i);
		}		/* of for */
	}			/* of if */
	qsort(object, processes * threads * objects, sizeof(object_t),
	      object_uuid_compare);
	LOG(2, (stdout, "%s %d: qsort() done\n", program, pid));
	count = 0;
	for (i = 0; i < processes * threads * objects - 1; i++) {
		if (object_uuid_compare(&object[i], &object[i + 1]) == 0) {
			if (logging >= 1) {
				LOG(0,
				    (stdout,
				     "%s %d: objects #%d and #%d have duplicate UUIDs\n",
				     program, pid, i, i + 1));
				object_dump(i);
				object_dump(i + 1);
			}	/* of if */
			count = count + 1;
		}		/* of if */
	}			/* of for */
	remove_segment(objectid, object);
	if (count == 0) {
		LOG(0,
		    (stdout, "test successful (no duplicate UUIDs found)\n"));
	} /* of if */
	else {
		LOG(0,
		    (stdout, "test failed (found %d duplicate UUIDs)\n",
		     count));
	}			/* of else */
}				/* of main */
