/* Original author unknown, but may be "krishna balasub@cis.ohio-state.edu"
   Modified Sat Oct  9 10:55:28 1993 for 0.99.13 */

/* 

  Patches from Mike Jagdis (jaggy@purplet.demon.co.uk) applied Wed Feb
  8 12:12:21 1995 by faith@cs.unc.edu to print numeric uids if no
  passwd file entry.

  Patch from arnolds@ifns.de (Heinz-Ado Arnolds) applied Mon Jul 1
  19:30:41 1996 by janl@math.uio.no to add code missing in case PID:
  clauses.

  Patched to display the key field -- hy@picksys.com 12/18/96

*/

#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <errno.h>
#include <time.h>
#include <pwd.h>
#include <grp.h>
#if 0
#define __KERNEL__		/* yuk */
#include <linux/linkage.h>
#endif
/* X/OPEN tells us to use <sys/{types,ipc,sem}.h> for semctl() */
/* X/OPEN tells us to use <sys/{types,ipc,msg}.h> for msgctl() */
/* X/OPEN tells us to use <sys/{types,ipc,shm}.h> for shmctl() */
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/msg.h>
#include <sys/shm.h>
/* The last arg of semctl is a union semun, but where is it defined?
   X/OPEN tells us to define it ourselves, but until recently
   Linux include files would also define it. */
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

/* X/OPEN (Jan 1987) does not define fields key, seq in struct ipc_perm;
   libc 4/5 does not mention struct ipc_term at all, but includes
   <linux/ipc.h>, which defines a struct ipc_perm with such fields.
   glibc-1.09 has no support for sysv ipc.
   glibc 2 uses __key, __seq */
#if defined (__GNU_LIBRARY__) && __GNU_LIBRARY__ > 1
#define KEY __key
#else
#define KEY key
#endif

#define LIMITS 1
#define STATUS 2
#define CREATOR 3
#define TIME 4
#define PID 5

void do_shm (char format);
void do_sem (char format);
void do_msg (char format);
void print_shm (int id);
void print_msg (int id);
void print_sem (int id);

static char *progname;

void usage(void)
{
	printf ("usage : %s -asmq -tclup \n", progname);
	printf ("\t%s [-s -m -q] -i id\n", progname);
	printf ("\t%s -h for help.\n", progname); 
	return;
}

void help (void)
{
	printf ("%s provides information on ipc facilities for", progname);
        printf (" which you have read access.\n"); 
	printf ("Resource Specification:\n\t-m : shared_mem\n\t-q : messages\n");
	printf ("\t-s : semaphores\n\t-a : all (default)\n");
	printf ("Output Format:\n\t-t : time\n\t-p : pid\n\t-c : creator\n");
	printf ("\t-l : limits\n\t-u : summary\n");
	printf ("-i id [-s -q -m] : details on resource identified by id\n");
	usage();
	return;
}

int main (int argc, char **argv)
{
	int opt, msg = 0, sem = 0, shm = 0, id=0, print=0; 
	char format = 0;
	char options[] = "atcluphsmqi:";

	progname = argv[0];
	while ((opt = getopt (argc, argv, options)) != EOF) {
		switch (opt) {
		case 'i':
			id = atoi (optarg);
			print = 1;
			break;
		case 'a':
			msg = shm = sem = 1;
			break;
		case 'q':
			msg = 1;
			break;
		case 's':
			sem = 1;
			break;
		case 'm':
			shm = 1;
			break;
		case 't':
			format = TIME;
			break;
		case 'c':
			format = CREATOR;
			break;
		case 'p':
			format = PID;
			break;
		case 'l':
			format = LIMITS;
			break;
		case 'u':
			format = STATUS;
			break;
		case 'h': 
			help();
			exit (0);
		case '?':
			usage();
			exit (0);
		}
	}

	if  (print) {
		if (shm) { 
			print_shm (id);
			exit (0);
		}
		if (sem) { 
			print_sem (id);
			exit (0);
		}
		if (msg) {
			print_msg (id);
			exit (0);
		}
		usage();
	}

	if ( !shm && !msg && !sem)
		msg = sem = shm = 1;
	printf ("\n");

	if (shm) {	
		do_shm (format);
		printf ("\n");
	}
	if (sem) {	
		do_sem (format);
		printf ("\n");
	}
	if (msg) {	
		do_msg (format);
		printf ("\n");
	}
	return 0;
}


void print_perms (int id, struct ipc_perm *ipcp)
{
	struct passwd *pw;
	struct group *gr;

	printf ("%-10d%-10o", id, ipcp->mode & 0777);

	if ((pw = getpwuid(ipcp->cuid)))
		printf("%-10s", pw->pw_name);
	else
		printf("%-10d", ipcp->cuid);
	if ((gr = getgrgid(ipcp->cgid)))
		printf("%-10s", gr->gr_name);
	else
		printf("%-10d", ipcp->cgid);

	if ((pw = getpwuid(ipcp->uid)))
		printf("%-10s", pw->pw_name);
	else
		printf("%-10d", ipcp->uid);
	if ((gr = getgrgid(ipcp->gid)))
		printf("%-10s\n", gr->gr_name);
	else
		printf("%-10d\n", ipcp->gid);
}


void do_shm (char format)
{
	int maxid, shmid, id;
	struct shmid_ds shmseg;
	struct shm_info shm_info;
	struct shminfo shminfo;
	struct ipc_perm *ipcp = &shmseg.shm_perm;
	struct passwd *pw;

	maxid = shmctl (0, SHM_INFO, (struct shmid_ds *) &shm_info);
	if (maxid < 0) {
		printf ("kernel not configured for shared memory\n");
		return;
	}
	
	switch (format) {
	case LIMITS:
		printf ("------ Shared Memory Limits --------\n");
		if ((shmctl (0, IPC_INFO, (struct shmid_ds *) &shminfo)) < 0 )
			return;
		printf ("max number of segments = %d\n", shminfo.shmmni);
		printf ("max seg size (kbytes) = %d\n", shminfo.shmmax >> 10);
		printf ("max total shared memory (kbytes) = %d\n", shminfo.shmall << 2);
		printf ("min seg size (bytes) = %d\n", shminfo.shmmin);
		return;

	case STATUS:
		printf ("------ Shared Memory Status --------\n");
		printf ("segments allocated %d\n", shm_info.used_ids);
		printf ("pages allocated %ld\n", shm_info.shm_tot);
		printf ("pages resident  %ld\n", shm_info.shm_rss);
		printf ("pages swapped   %ld\n", shm_info.shm_swp);
		printf ("Swap performance: %ld attempts\t %ld successes\n", 
			shm_info.swap_attempts, shm_info.swap_successes);
		return;

	case CREATOR:
		printf ("------ Shared Memory Segment Creators/Owners --------\n");
		printf ("%-10s%-10s%-10s%-10s%-10s%-10s\n",
		 "shmid","perms","cuid","cgid","uid","gid");
		break;

	case TIME:
		printf ("------ Shared Memory Attach/Detach/Change Times --------\n");
		printf ("%-10s%-10s  %-20s%-20s%-20s\n",
			"shmid","owner","attached","detached","changed");
		break;

	case PID:
		printf ("------ Shared Memory Creator/Last-op --------\n");
		printf ("%-10s%-10s%-10s%-10s\n","shmid","owner","cpid","lpid");
		break;

	default:
		printf ("------ Shared Memory Segments --------\n");
		printf ("%-10s%-10s%-10s%-10s%-10s%-10s%-12s\n", "key","shmid",
			"owner","perms","bytes","nattch","status");
		break;
	}

	for (id = 0; id <= maxid; id++) {
		shmid = shmctl (id, SHM_STAT, &shmseg);
		if (shmid  < 0) 
			continue;
		if (format == CREATOR)  {
			print_perms (shmid, ipcp);
			continue;
		}
		pw = getpwuid(ipcp->uid);
		switch (format) {
		case TIME: 
			if (pw)
				printf ("%-10d%-10.10s", shmid, pw->pw_name);
			else
				printf ("%-10d%-10d", shmid, ipcp->uid);
			printf("  %-20.16s%-20.16s%-20.16s\n",
			shmseg.shm_atime ? ctime(&shmseg.shm_atime) + 4 : "Not set",
		 	shmseg.shm_dtime ? ctime(&shmseg.shm_dtime) + 4 : "Not set",
			shmseg.shm_ctime ? ctime(&shmseg.shm_ctime) + 4 : "Not set");
			break;
		case PID:
			if (pw)
				printf ("%-10d%-10.10s", shmid, pw->pw_name);
			else
				printf ("%-10d%-10d", shmid, ipcp->uid);
			printf ("%-10d%-10d\n",
				shmseg.shm_cpid, shmseg.shm_lpid);
			break;
			
		default:
		        printf( "0x%08x ",ipcp->KEY );
			if (pw)
				printf ("%-10d%-10.10s", shmid, pw->pw_name);
			else
				printf ("%-10d%-10d", shmid, ipcp->uid);
			printf ("%-10o%-10d%-10d%-6s%-6s\n", 
				ipcp->mode & 0777, 
				shmseg.shm_segsz, shmseg.shm_nattch,
				ipcp->mode & SHM_DEST ? "dest" : " ",
				ipcp->mode & SHM_LOCKED ? "locked" : " ");
			break;
		}
	}
	return;
}


void do_sem (char format)
{
	int maxid, semid, id;
	struct semid_ds semary;
	struct seminfo seminfo;
	struct ipc_perm *ipcp = &semary.sem_perm;
	struct passwd *pw;
	union semun arg;

	arg.array = (ushort *)  &seminfo;
	maxid = semctl (0, 0, SEM_INFO, arg);
	if (maxid < 0) {
		printf ("kernel not configured for semaphores\n");
		return;
	}
	
	switch (format) {
	case LIMITS:
		printf ("------ Semaphore Limits --------\n");
		arg.array = (ushort *) &seminfo; /* damn union */
		if ((semctl (0, 0, IPC_INFO, arg)) < 0 )
			return;
		printf ("max number of arrays = %d\n", seminfo.semmni);
		printf ("max semaphores per array = %d\n", seminfo.semmsl);
		printf ("max semaphores system wide = %d\n", seminfo.semmns);
		printf ("max ops per semop call = %d\n", seminfo.semopm);
		printf ("semaphore max value = %d\n", seminfo.semvmx);
		return;

	case STATUS:
		printf ("------ Semaphore Status --------\n");
		printf ("used arrays = %d\n", seminfo.semusz);
		printf ("allocated semaphores = %d\n", seminfo.semaem);
		return;

	case CREATOR:
		printf ("------ Semaphore Arrays Creators/Owners --------\n");
		printf ("%-10s%-10s%-10s%-10s%-10s%-10s\n",
		 "semid","perms","cuid","cgid","uid","gid");
		break;

	case TIME:
		printf ("------ Shared Memory Operation/Change Times --------\n");
		printf ("%-8s%-10s  %-26.24s %-26.24s\n",
			"shmid","owner","last-op","last-changed");
		break;

	case PID:
		break;

	default:
		printf ("------ Semaphore Arrays --------\n");
		printf ("%-10s%-10s%-10s%-10s%-10s%-12s\n", 
			"key","semid","owner","perms","nsems","status");
		break;
	}

	for (id = 0; id <= maxid; id++) {
		arg.buf = (struct semid_ds *) &semary;
		semid = semctl (id, 0, SEM_STAT, arg);
		if (semid < 0) 
			continue;
		if (format == CREATOR)  {
			print_perms (semid, ipcp);
			continue;
		}
		pw = getpwuid(ipcp->uid);
		switch (format) {
		case TIME: 
			if (pw)
				printf ("%-8d%-10.10s", semid, pw->pw_name);
			else
				printf ("%-8d%-10d", semid, ipcp->uid);
			printf ("  %-26.24s %-26.24s\n", 
				semary.sem_otime ? ctime(&semary.sem_otime) : "Not set",
				semary.sem_ctime ? ctime(&semary.sem_ctime) : "Not set");
			break;
		case PID:
			break;
			
		default:
		        printf( "0x%08x ",ipcp->KEY );
			if (pw)
				printf ("%-10d%-10.9s", semid, pw->pw_name);
			else
				printf ("%-10d%-9d", semid, ipcp->uid);
			printf ("%-10o%-10d\n", 
				ipcp->mode & 0777,
				semary.sem_nsems);
			break;
		}
	}
	return;
}


void do_msg (char format)
{
	int maxid, msqid, id;
	struct msqid_ds msgque;
	struct msginfo msginfo;
	struct ipc_perm *ipcp = &msgque.msg_perm;
	struct passwd *pw;

	maxid = msgctl (0, MSG_INFO, (struct msqid_ds *) &msginfo);
	if (maxid < 0) {
		printf ("kernel not configured for shared memory\n");
		return;
	}
	
	switch (format) {
	case LIMITS:
		if ((msgctl (0, IPC_INFO, (struct msqid_ds *) &msginfo)) < 0 )
			return;
		printf ("------ Messages: Limits --------\n");
		printf ("max queues system wide = %d\n", msginfo.msgmni);
		printf ("max size of message (bytes) = %d\n", msginfo.msgmax);
		printf ("default max size of queue (bytes) = %d\n", msginfo.msgmnb);
		return;

	case STATUS:
		printf ("------ Messages: Status --------\n");
		printf ("allocated queues = %d\n", msginfo.msgpool);
		printf ("used headers = %d\n", msginfo.msgmap);
		printf ("used space = %d bytes\n", msginfo.msgtql);
		return;

	case CREATOR:
		printf ("------ Message Queues: Creators/Owners --------\n");
		printf ("%-10s%-10s%-10s%-10s%-10s%-10s\n",
		 "msqid","perms","cuid","cgid","uid","gid");
		break;

	case TIME:
		printf ("------ Message Queues Send/Recv/Change Times --------\n");
		printf ("%-8s%-10s  %-20s%-20s%-20s\n",
			"msqid","owner","send","recv","change");
		break;

	case PID:
 		printf ("------ Message Queues PIDs --------\n");
 		printf ("%-10s%-10s%-10s%-10s\n","msqid","owner","lspid","lrpid");
		break;

	default:
		printf ("------ Message Queues --------\n");
		printf ("%-10s%-10s%-10s%-10s%-12s%-12s\n", "key","msqid",
			"owner", "perms", "used-bytes", "messages");
		break;
	}

	for (id = 0; id <= maxid; id++) {
		msqid = msgctl (id, MSG_STAT, &msgque);
		if (msqid  < 0) 
			continue;
		if (format == CREATOR)  {
			print_perms (msqid, ipcp);
			continue;
		}
		pw = getpwuid(ipcp->uid);
		switch (format) {
		case TIME: 
			if (pw)
				printf ("%-8d%-10.10s", msqid, pw->pw_name);
			else
				printf ("%-8d%-10d", msqid, ipcp->uid);
			printf ("  %-20.16s%-20.16s%-20.16s\n", 
			msgque.msg_stime ? ctime(&msgque.msg_stime) + 4 : "Not set",
		 	msgque.msg_rtime ? ctime(&msgque.msg_rtime) + 4 : "Not set",
			msgque.msg_ctime ? ctime(&msgque.msg_ctime) + 4 : "Not set");
			break;
		case PID:
 			if (pw)
 				printf ("%-8d%-10.10s", msqid, pw->pw_name);
 			else
 				printf ("%-8d%-10d", msqid, ipcp->uid);
 			printf ("  %5d     %5d\n",
 			msgque.msg_lspid, msgque.msg_lrpid);
  			break;

		default:
		        printf( "0x%08x ",ipcp->KEY );
			if (pw)
				printf ("%-10d%-10.10s", msqid, pw->pw_name);
			else
				printf ("%-10d%-10d", msqid, ipcp->uid);
			printf ("%-10o%-12d%-12d\n", 
			ipcp->mode & 0777, msgque.msg_cbytes,
				msgque.msg_qnum);
			break;
		}
	}
	return;
}


void print_shm (int shmid)
{
	struct shmid_ds shmds;
	struct ipc_perm *ipcp = &shmds.shm_perm;

	if (shmctl (shmid, IPC_STAT, &shmds) == -1) {
		perror ("shmctl ");
		return;
	}

	printf ("\nShared memory Segment shmid=%d\n", shmid);
	printf ("uid=%d\tgid=%d\tcuid=%d\tcgid=%d\n",
		ipcp->uid, ipcp->gid, ipcp->cuid, ipcp->cgid);
	printf ("mode=%#o\taccess_perms=%#o\n", ipcp->mode, ipcp->mode & 0777);
	printf ("bytes=%d\tlpid=%d\tcpid=%d\tnattch=%d\n", 
		shmds.shm_segsz, shmds.shm_lpid, shmds.shm_cpid, 
		shmds.shm_nattch);
	printf ("att_time=%s", shmds.shm_atime ? ctime (&shmds.shm_atime) : 
		"Not set\n");
	printf ("det_time=%s", shmds.shm_dtime ? ctime (&shmds.shm_dtime) : 
		"Not set\n");
	printf ("change_time=%s", ctime (&shmds.shm_ctime));
	printf ("\n");
	return;
}

 

void print_msg (int msqid)
{
	struct msqid_ds buf;
	struct ipc_perm *ipcp = &buf.msg_perm;

	if (msgctl (msqid, IPC_STAT, &buf) == -1) {
		perror ("msgctl ");
		return;
	}
	printf ("\nMessage Queue msqid=%d\n", msqid);
	printf ("uid=%d\tgid=%d\tcuid=%d\tcgid=%d\tmode=%#o\n",
		ipcp->uid, ipcp->gid, ipcp->cuid, ipcp->cgid, ipcp->mode);
	printf ("cbytes=%d\tqbytes=%d\tqnum=%d\tlspid=%d\tlrpid=%d\n",
		buf.msg_cbytes, buf.msg_qbytes, buf.msg_qnum, buf.msg_lspid, 
		buf.msg_lrpid);
	printf ("send_time=%srcv_time=%schange_time=%s", 
		buf.msg_rtime? ctime (&buf.msg_rtime) : "Not Set\n",
		buf.msg_stime? ctime (&buf.msg_stime) : "Not Set\n",
		buf.msg_ctime? ctime (&buf.msg_ctime) : "Not Set\n");
	printf ("\n");
	return;
}

void print_sem (int semid)
{
	struct semid_ds semds;
	struct ipc_perm *ipcp = &semds.sem_perm;
	union semun arg;
	int i;

	arg.buf = &semds;
	if (semctl (semid, 0, IPC_STAT, arg) < 0) {
		perror ("semctl ");
		return;
	}
	printf ("\nSemaphore Array semid=%d\n", semid);
	printf ("uid=%d\t gid=%d\t cuid=%d\t cgid=%d\n",
		ipcp->uid, ipcp->gid, ipcp->cuid, ipcp->cgid);
	printf ("mode=%#o, access_perms=%#o\n", ipcp->mode, ipcp->mode & 0777);
	printf ("nsems = %d\n", semds.sem_nsems);
	printf ("otime = %s", semds.sem_otime ? ctime (&semds.sem_otime) : 
		"Not set\n");
	printf ("ctime = %s", ctime (&semds.sem_ctime));	

	printf ("%-10s%-10s%-10s%-10s%-10s\n", "semnum","value","ncount",
		"zcount","pid");
	arg.val = 0;
	for (i=0; i< semds.sem_nsems; i++) {
		int val, ncnt, zcnt, pid;
		val = semctl (semid, i, GETVAL, arg);
		ncnt = semctl (semid, i, GETNCNT, arg);
		zcnt = semctl (semid, i, GETZCNT, arg);
		pid = semctl (semid, i, GETPID, arg);
		if (val < 0 || ncnt < 0 || zcnt < 0 || pid < 0) {
			perror ("semctl ");
			exit (1);
		}
		printf ("%-10d%-10d%-10d%-10d%-10d\n", i, val, ncnt, zcnt, pid);
	}
	printf ("\n");
	return;
}

