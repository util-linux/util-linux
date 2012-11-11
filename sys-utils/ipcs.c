/* Original author unknown, may be "krishna balasub@cis.ohio-state.edu" */
/*
 * Modified Sat Oct  9 10:55:28 1993 for 0.99.13
 *
 * Patches from Mike Jagdis (jaggy@purplet.demon.co.uk) applied Wed Feb 8
 * 12:12:21 1995 by faith@cs.unc.edu to print numeric uids if no passwd file
 * entry.
 *
 * Patch from arnolds@ifns.de (Heinz-Ado Arnolds) applied Mon Jul 1 19:30:41
 * 1996 by janl@math.uio.no to add code missing in case PID: clauses.
 *
 * Patched to display the key field -- hy@picksys.com 12/18/96
 *
 * 1999-02-22 Arkadiusz Mi¶kiewicz <misiek@pld.ORG.PL>
 * - added Native Language Support
 */

#include <errno.h>
#include <features.h>
#include <getopt.h>

#include "c.h"
#include "nls.h"
#include "closestream.h"

#include "ipcutils.h"

#define LIMITS 1
#define STATUS 2
#define CREATOR 3
#define TIME 4
#define PID 5

static void do_shm (char format);
static void print_shm (int id);
static void do_sem (char format);
static void print_sem (int id);
static void do_msg (char format);

void print_msg (int id);

static void __attribute__ ((__noreturn__)) usage(FILE * out)
{
	fprintf(out, USAGE_HEADER);
	fprintf(out, " %s [resource ...] [output-format]\n", program_invocation_short_name);
	fprintf(out, " %s [resource] -i <id>\n", program_invocation_short_name);
	fprintf(out, USAGE_OPTIONS);
	fputs(_(" -i, --id <id>  print details on resource identified by id\n"), out);
	fprintf(out, USAGE_HELP);
	fprintf(out, USAGE_VERSION);
	fputs(_("\n"), out);
	fputs(_("Resource options:\n"), out);
	fputs(_(" -m, --shmems      shared memory segments\n"), out);
	fputs(_(" -q, --queues      message queues\n"), out);
	fputs(_(" -s, --semaphores  semaphores\n"), out);
	fputs(_(" -a, --all         all (default)\n"), out);
	fputs(_("\n"), out);
	fputs(_("Output format:\n"), out);
	fputs(_(" -t, --time        show attach, detach and change times\n"), out);
	fputs(_(" -p, --pid         show creator and last operations PIDs\n"), out);
	fputs(_(" -c, --creator     show creator and owner\n"), out);
	fputs(_(" -l, --limits      show resource limits\n"), out);
	fputs(_(" -u, --summary     show status summary\n"), out);
	fprintf(out, USAGE_MAN_TAIL("ipcs(1)"));
	exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}

int main (int argc, char **argv)
{
	int opt, msg = 0, sem = 0, shm = 0, id=0, print=0;
	char format = 0;
	static const struct option longopts[] = {
		{"id", required_argument, NULL, 'i'},
		{"shmems", no_argument, NULL, 'm'},
		{"queues", no_argument, NULL, 'q'},
		{"semaphores", no_argument, NULL, 's'},
		{"all", no_argument, NULL, 'a'},
		{"time", no_argument, NULL, 't'},
		{"pid", no_argument, NULL, 'p'},
		{"creator", no_argument, NULL, 'c'},
		{"limits", no_argument, NULL, 'l'},
		{"summary", no_argument, NULL, 'u'},
		{"version", no_argument, NULL, 'V'},
		{"help", no_argument, NULL, 'h'},
		{NULL, 0, NULL, 0}
	};
	char options[] = "i:mqsatpcluVh";

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	while ((opt = getopt_long(argc, argv, options, longopts, NULL)) != -1) {
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
			usage(stdout);
		case 'V':
			printf(UTIL_LINUX_VERSION);
			return EXIT_SUCCESS;
		default:
			usage(stderr);
		}
	}

	if  (print) {
		if (shm)
			print_shm (id);
		if (sem)
			print_sem (id);
		if (msg)
			print_msg (id);
		if (!shm && !sem && !msg )
			usage (stderr);
	} else {
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
	}
	return EXIT_SUCCESS;
}

static void do_shm (char format)
{
	struct passwd *pw;
	struct shm_data *shmds, *shmdsp;

	switch (format) {
	case LIMITS:
	{
		struct ipc_limits lim;

		printf (_("------ Shared Memory Limits --------\n"));
		if (ipc_shm_get_limits(&lim))
			return;
		printf (_("max number of segments = %ju\n"), lim.shmmni);
		printf (_("max seg size (kbytes) = %ju\n"), lim.shmmax / 1024);
		printf (_("max total shared memory (kbytes) = %ju\n"),
					(lim.shmall / 1024) * getpagesize());
		printf (_("min seg size (bytes) = %ju\n"), lim.shmmin);
		return;
	}
	case STATUS:
	{
		int maxid;
		struct shm_info shm_info;

		maxid = shmctl (0, SHM_INFO, (struct shmid_ds *) &shm_info);
		if (maxid < 0) {
			printf (_("kernel not configured for shared memory\n"));
			return;
		}

		printf (_("------ Shared Memory Status --------\n"));
		/*
		 * TRANSLATORS: This output format is maintained for backward
		 * compatibility as ipcs is used in scripts. For consistency
		 * with the rest, the translated form can follow this model:
		 *
		 * "segments allocated = %d\n"
		 * "pages allocated = %ld\n"
		 * "pages resident = %ld\n"
		 * "pages swapped = %ld\n"
		 * "swap performance = %ld attempts, %ld successes\n"
		 */
		printf (_("segments allocated %d\n"
			  "pages allocated %ld\n"
			  "pages resident  %ld\n"
			  "pages swapped   %ld\n"
			  "Swap performance: %ld attempts\t %ld successes\n"),
			shm_info.used_ids,
			shm_info.shm_tot,
			shm_info.shm_rss,
			shm_info.shm_swp,
			shm_info.swap_attempts, shm_info.swap_successes);
		return;
	}

	/*
	 * Headers only
	 */
	case CREATOR:
		printf (_("------ Shared Memory Segment Creators/Owners --------\n"));
		printf ("%-10s %-10s %-10s %-10s %-10s %-10s\n",
			_("shmid"),_("perms"),_("cuid"),_("cgid"),_("uid"),_("gid"));
		break;

	case TIME:
		printf (_("------ Shared Memory Attach/Detach/Change Times --------\n"));
		printf ("%-10s %-10s %-20s %-20s %-20s\n",
			_("shmid"),_("owner"),_("attached"),_("detached"),
			_("changed"));
		break;

	case PID:
		printf (_("------ Shared Memory Creator/Last-op PIDs --------\n"));
		printf ("%-10s %-10s %-10s %-10s\n",
			_("shmid"),_("owner"),_("cpid"),_("lpid"));
		break;

	default:
		printf (_("------ Shared Memory Segments --------\n"));
		printf ("%-10s %-10s %-10s %-10s %-10s %-10s %-12s\n",
			_("key"),_("shmid"),_("owner"),_("perms"),_("bytes"),
			_("nattch"),_("status"));
		break;
	}

	/*
	 * Print data
	 */
	if (ipc_shm_get_info(-1, &shmds) < 1)
		return;
	shmdsp = shmds;

	for (shmdsp = shmds; shmdsp->next != NULL; shmdsp = shmdsp->next) {
		if (format == CREATOR)  {
			ipc_print_perms(stdout, &shmdsp->shm_perm);
			continue;
		}
		pw = getpwuid(shmdsp->shm_perm.uid);
		switch (format) {
		case TIME:
			if (pw)
				printf ("%-10d %-10.10s", shmdsp->shm_perm.id, pw->pw_name);
			else
				printf ("%-10d %-10u", shmdsp->shm_perm.id, shmdsp->shm_perm.uid);
			/* ctime uses static buffer: use separate calls */
			printf(" %-20.16s", shmdsp->shm_atim
			       ? ctime(&shmdsp->shm_atim) + 4 : _("Not set"));
			printf(" %-20.16s", shmdsp->shm_dtim
			       ? ctime(&shmdsp->shm_dtim) + 4 : _("Not set"));
			printf(" %-20.16s\n", shmdsp->shm_ctim
			       ? ctime(&shmdsp->shm_ctim) + 4 : _("Not set"));
			break;
		case PID:
			if (pw)
				printf ("%-10d %-10.10s", shmdsp->shm_perm.id, pw->pw_name);
			else
				printf ("%-10d %-10u", shmdsp->shm_perm.id, shmdsp->shm_perm.uid);
			printf (" %-10u %-10u\n",
				shmdsp->shm_cprid, shmdsp->shm_lprid);
			break;

		default:
			printf("0x%08x ", shmdsp->shm_perm.key);
			if (pw)
				printf ("%-10d %-10.10s", shmdsp->shm_perm.id, pw->pw_name);
			else
				printf ("%-10d %-10u", shmdsp->shm_perm.id, shmdsp->shm_perm.uid);
			printf (" %-10o %-10lu %-10ld %-6s %-6s\n",
				shmdsp->shm_perm.mode & 0777,
				shmdsp->shm_segsz,
				shmdsp->shm_nattch,
				shmdsp->shm_perm.mode & SHM_DEST ? _("dest") : " ",
				shmdsp->shm_perm.mode & SHM_LOCKED ? _("locked") : " ");
			break;
		}
	}

	ipc_shm_free_info(shmds);
	return;
}

static void do_sem (char format)
{
	struct passwd *pw;
	struct sem_data *semds, *semdsp;

	switch (format) {
	case LIMITS:
	{
		struct ipc_limits lim;

		printf (_("------ Semaphore Limits --------\n"));
		if (ipc_sem_get_limits(&lim))
			return;
		printf (_("max number of arrays = %d\n"), lim.semmni);
		printf (_("max semaphores per array = %d\n"), lim.semmsl);
		printf (_("max semaphores system wide = %d\n"), lim.semmns);
		printf (_("max ops per semop call = %d\n"), lim.semopm);
		printf (_("semaphore max value = %d\n"), lim.semvmx);
		return;
	}
	case STATUS:
	{
		struct seminfo seminfo;
		union semun arg;
		arg.array = (ushort *)  (void *) &seminfo;
		if (semctl (0, 0, SEM_INFO, arg) < 0) {
			printf (_("kernel not configured for semaphores\n"));
			return;
		}
		printf (_("------ Semaphore Status --------\n"));
		printf (_("used arrays = %d\n"), seminfo.semusz);
		printf (_("allocated semaphores = %d\n"), seminfo.semaem);
		return;
	}

	case CREATOR:
		printf (_("------ Semaphore Arrays Creators/Owners --------\n"));
		printf ("%-10s %-10s %-10s %-10s %-10s %-10s\n",
			_("semid"),_("perms"),_("cuid"),_("cgid"),_("uid"),_("gid"));
		break;

	case TIME:
		printf (_("------ Semaphore Operation/Change Times --------\n"));
		printf ("%-8s %-10s %-26.24s %-26.24s\n",
			_("semid"),_("owner"),_("last-op"),_("last-changed"));
		break;

	case PID:
		break;

	default:
		printf (_("------ Semaphore Arrays --------\n"));
		printf ("%-10s %-10s %-10s %-10s %-10s\n",
			_("key"),_("semid"),_("owner"),_("perms"),_("nsems"));
		break;
	}

	/*
	 * Print data
	 */
	if (ipc_sem_get_info(-1, &semds) < 1)
		return;
	semdsp = semds;

	for (semdsp = semds; semdsp->next != NULL; semdsp = semdsp->next) {
		if (format == CREATOR)  {
			ipc_print_perms(stdout, &semdsp->sem_perm);
			continue;
		}
		pw = getpwuid(semdsp->sem_perm.uid);
		switch (format) {
		case TIME:
			if (pw)
				printf ("%-8d %-10.10s", semdsp->sem_perm.id, pw->pw_name);
			else
				printf ("%-8d %-10u", semdsp->sem_perm.id, semdsp->sem_perm.uid);
			printf ("  %-26.24s", semdsp->sem_otime
				? ctime(&semdsp->sem_otime) : _("Not set"));
			printf (" %-26.24s\n", semdsp->sem_ctime
				? ctime(&semdsp->sem_ctime) : _("Not set"));
			break;
		case PID:
			break;

		default:
			printf("0x%08x ", semdsp->sem_perm.key);
			if (pw)
				printf ("%-10d %-10.10s", semdsp->sem_perm.id, pw->pw_name);
			else
				printf ("%-10d %-10u", semdsp->sem_perm.id, semdsp->sem_perm.uid);
			printf (" %-10o %-10ld\n",
				semdsp->sem_perm.mode & 0777,
				semdsp->sem_nsems);
			break;
		}
	}

	ipc_sem_free_info(semds);
	return;
}

static void do_msg (char format)
{
	struct passwd *pw;
	struct msg_data *msgds, *msgdsp;

	switch (format) {
	case LIMITS:
	{
		struct ipc_limits lim;

		if (ipc_msg_get_limits(&lim))
			return;
		printf (_("------ Messages Limits --------\n"));
		printf (_("max queues system wide = %d\n"), lim.msgmni);
		printf (_("max size of message (bytes) = %zu\n"), lim.msgmax);
		printf (_("default max size of queue (bytes) = %d\n"), lim.msgmnb);
		return;
	}
	case STATUS:
	{
		struct msginfo msginfo;
		if (msgctl (0, MSG_INFO, (struct msqid_ds *) (void *) &msginfo) < 0) {
			printf (_("kernel not configured for message queues\n"));
			return;
		}
		printf (_("------ Messages Status --------\n"));
		printf (_("allocated queues = %d\n"), msginfo.msgpool);
		printf (_("used headers = %d\n"), msginfo.msgmap);
		printf (_("used space = %d bytes\n"), msginfo.msgtql);
		return;
	}
	case CREATOR:
		printf (_("------ Message Queues Creators/Owners --------\n"));
		printf ("%-10s %-10s %-10s %-10s %-10s %-10s\n",
			_("msqid"),_("perms"),_("cuid"),_("cgid"),_("uid"),_("gid"));
		break;

	case TIME:
		printf (_("------ Message Queues Send/Recv/Change Times --------\n"));
		printf ("%-8s %-10s %-20s %-20s %-20s\n",
			_("msqid"),_("owner"),_("send"),_("recv"),_("change"));
		break;

	case PID:
		printf (_("------ Message Queues PIDs --------\n"));
		printf ("%-10s %-10s %-10s %-10s\n",
			_("msqid"),_("owner"),_("lspid"),_("lrpid"));
		break;

	default:
		printf (_("------ Message Queues --------\n"));
		printf ("%-10s %-10s %-10s %-10s %-12s %-12s\n",
			_("key"), _("msqid"), _("owner"), _("perms"),
			_("used-bytes"), _("messages"));
		break;
	}

	/*
	 * Print data
	 */
	if (ipc_msg_get_info(-1, &msgds) < 1)
		return;
	msgdsp = msgds;

	for (msgdsp = msgds; msgdsp->next != NULL; msgdsp = msgdsp->next) {
		if (format == CREATOR) {
			ipc_print_perms(stdout, &msgdsp->msg_perm);
			continue;
		}
		pw = getpwuid(msgdsp->msg_perm.uid);
		switch (format) {
		case TIME:
			if (pw)
				printf ("%-8d %-10.10s", msgdsp->msg_perm.id, pw->pw_name);
			else
				printf ("%-8d %-10u", msgdsp->msg_perm.id, msgdsp->msg_perm.uid);
			printf (" %-20.16s", msgdsp->q_stime
				? ctime(&msgdsp->q_stime) + 4 : _("Not set"));
			printf (" %-20.16s", msgdsp->q_rtime
				? ctime(&msgdsp->q_rtime) + 4 : _("Not set"));
			printf (" %-20.16s\n", msgdsp->q_ctime
				? ctime(&msgdsp->q_ctime) + 4 : _("Not set"));
			break;
		case PID:
			if (pw)
				printf ("%-8d %-10.10s", msgdsp->msg_perm.id, pw->pw_name);
			else
				printf ("%-8d %-10u", msgdsp->msg_perm.id, msgdsp->msg_perm.uid);
			printf ("  %5d     %5d\n",
				msgdsp->q_lspid, msgdsp->q_lrpid);
			break;

		default:
			printf( "0x%08x ",msgdsp->msg_perm.key );
			if (pw)
				printf ("%-10d %-10.10s", msgdsp->msg_perm.id, pw->pw_name);
			else
				printf ("%-10d %-10u", msgdsp->msg_perm.id, msgdsp->msg_perm.uid);
			printf (" %-10o %-12ld %-12ld\n",
				msgdsp->msg_perm.mode & 0777,
				msgdsp->q_cbytes,
				msgdsp->q_qnum);
			break;
		}
	}

	ipc_msg_free_info(msgds);
	return;
}

static void print_shm(int shmid)
{
	struct shm_data *shmdata;

	if (ipc_shm_get_info(shmid, &shmdata) < 1) {
		warnx(_("id %d not found"), shmid);
		return;
	}

	printf(_("\nShared memory Segment shmid=%d\n"), shmid);
	printf(_("uid=%u\tgid=%u\tcuid=%u\tcgid=%u\n"),
	       shmdata->shm_perm.uid, shmdata->shm_perm.uid,
	       shmdata->shm_perm.cuid, shmdata->shm_perm.cgid);
	printf(_("mode=%#o\taccess_perms=%#o\n"), shmdata->shm_perm.mode,
	       shmdata->shm_perm.mode & 0777);
	printf(_("bytes=%ju\tlpid=%u\tcpid=%u\tnattch=%jd\n"),
	       shmdata->shm_segsz, shmdata->shm_lprid, shmdata->shm_cprid,
	       shmdata->shm_nattch);
	printf(_("att_time=%-26.24s\n"),
	       shmdata->shm_atim ? ctime(&(shmdata->shm_atim)) : _("Not set"));
	printf(_("det_time=%-26.24s\n"),
	       shmdata->shm_dtim ? ctime(&shmdata->shm_dtim) : _("Not set"));
	printf(_("change_time=%-26.24s\n"), ctime(&shmdata->shm_ctim));
	printf("\n");

	ipc_shm_free_info(shmdata);
}


void print_msg(int msgid)
{
	struct msg_data *msgdata;

	if (ipc_msg_get_info(msgid, &msgdata) < 1) {
		warnx(_("id %d not found"), msgid);
		return;
	}

	printf(_("\nMessage Queue msqid=%d\n"), msgid);
	printf(_("uid=%u\tgid=%u\tcuid=%u\tcgid=%u\tmode=%#o\n"),
	       msgdata->msg_perm.uid, msgdata->msg_perm.uid,
	       msgdata->msg_perm.cuid, msgdata->msg_perm.cgid,
	       msgdata->msg_perm.mode);
	printf(_("cbytes=%jd\tqbytes=%jd\tqnum=%jd\tlspid=%d\tlrpid=%d\n"),
	       msgdata->q_cbytes, msgdata->q_qbytes, msgdata->q_qnum,
	       msgdata->q_lspid, msgdata->q_lrpid);
	printf(_("send_time=%-26.24s\n"),
	       msgdata->q_stime ? ctime(&msgdata->q_stime) : _("Not set"));
	printf(_("rcv_time=%-26.24s\n"),
	       msgdata->q_rtime ? ctime(&msgdata->q_rtime) : _("Not set"));
	printf(_("change_time=%-26.24s\n"),
	       msgdata->q_ctime ? ctime(&msgdata->q_ctime) : _("Not set"));
	printf("\n");

	ipc_msg_free_info(msgdata);
}

static void print_sem(int semid)
{
	struct sem_data *semdata;
	size_t i;

	if (ipc_sem_get_info(semid, &semdata) < 1) {
		warnx(_("id %d not found"), semid);
		return;
	}

	printf(_("\nSemaphore Array semid=%d\n"), semid);
	printf(_("uid=%u\t gid=%u\t cuid=%u\t cgid=%u\n"),
	       semdata->sem_perm.uid, semdata->sem_perm.uid,
	       semdata->sem_perm.cuid, semdata->sem_perm.cgid);
	printf(_("mode=%#o, access_perms=%#o\n"),
	       semdata->sem_perm.mode, semdata->sem_perm.mode & 0777);
	printf(_("nsems = %ld\n"), semdata->sem_nsems);
	printf(_("otime = %-26.24s\n"),
	       semdata->sem_otime ? ctime(&semdata->sem_otime) : _("Not set"));
	printf(_("ctime = %-26.24s\n"), ctime(&semdata->sem_ctime));

	printf("%-10s %-10s %-10s %-10s %-10s\n",
	       _("semnum"), _("value"), _("ncount"), _("zcount"), _("pid"));

	for (i = 0; i < semdata->sem_nsems; i++) {
		struct sem_elem *e = &semdata->elements[i];
		printf("%-10zd %-10d %-10d %-10d %-10d\n",
		       i, e->semval, e->ncount, e->zcount, e->pid);
	}
	printf("\n");
	ipc_sem_free_info(semdata);
}
