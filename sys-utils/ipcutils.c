
#include <inttypes.h>

#include "c.h"
#include "nls.h"
#include "xalloc.h"
#include "path.h"
#include "pathnames.h"
#include "ipcutils.h"
#include "strutils.h"

#ifndef SEMVMX
# define SEMVMX  32767	/* <= 32767 semaphore maximum value */
#endif
#ifndef SHMMIN
# define SHMMIN 1	/* min shared segment size in bytes */
#endif


int ipc_msg_get_limits(struct ipc_limits *lim)
{
	if (path_exist(_PATH_PROC_IPC_MSGMNI) &&
	    path_exist(_PATH_PROC_IPC_MSGMNB) &&
	    path_exist(_PATH_PROC_IPC_MSGMAX)) {

		lim->msgmni = path_read_s32(_PATH_PROC_IPC_MSGMNI);
		lim->msgmnb = path_read_s32(_PATH_PROC_IPC_MSGMNB);
		lim->msgmax = path_read_s32(_PATH_PROC_IPC_MSGMAX);
	} else {
		struct msginfo msginfo;

		if (msgctl(0, IPC_INFO, (struct msqid_ds *) &msginfo) < 0)
			return 1;
		lim->msgmni = msginfo.msgmni;
		lim->msgmnb = msginfo.msgmnb;
		lim->msgmax = msginfo.msgmax;
	}

	return 0;
}

int ipc_sem_get_limits(struct ipc_limits *lim)
{
	FILE *f;
	int rc = 0;

	lim->semvmx = SEMVMX;

	f = path_fopen("r", 0, _PATH_PROC_IPC_SEM);
	if (f) {
		rc = fscanf(f, "%d\t%d\t%d\t%d",
		       &lim->semmsl, &lim->semmns, &lim->semopm, &lim->semmni);
		fclose(f);

	}

	if (rc == 4) {
		struct seminfo seminfo;
		union semun arg = { .array = (ushort *) &seminfo };

		if (semctl(0, 0, IPC_INFO, arg) < 0)
			return 1;
		lim->semmni = seminfo.semmni;
		lim->semmsl = seminfo.semmsl;
		lim->semmns = seminfo.semmns;
		lim->semopm = seminfo.semopm;
	}

	return 0;
}

int ipc_shm_get_limits(struct ipc_limits *lim)
{
	lim->shmmin = SHMMIN;

	if (path_exist(_PATH_PROC_IPC_SHMALL) &&
	    path_exist(_PATH_PROC_IPC_SHMMAX) &&
	    path_exist(_PATH_PROC_IPC_SHMMNI)) {

		lim->shmall = path_read_u64(_PATH_PROC_IPC_SHMALL);
		lim->shmmax = path_read_u64(_PATH_PROC_IPC_SHMMAX);
		lim->shmmni = path_read_u64(_PATH_PROC_IPC_SHMMNI);

	} else {
		struct shminfo shminfo;

		if (shmctl(0, IPC_INFO, (struct shmid_ds *) &shminfo) < 0)
			return 1;
		lim->shmmni = shminfo.shmmni;
		lim->shmall = shminfo.shmall;
	}

	return 0;
}

int ipc_shm_get_info(int id, struct shm_data **shmds)
{
	FILE *f;
	int i = 0, maxid;
	struct shm_data *p;
	struct shm_info dummy;

	p = *shmds = xcalloc(1, sizeof(struct shm_data));
	p->next = NULL;

	f = path_fopen("r", 0, _PATH_PROC_SYSV_SHM);
	if (!f)
		goto shm_fallback;

	while (fgetc(f) != '\n');		/* skip header */

	while (feof(f) == 0) {
		if (fscanf(f,
			  "%d %d  %o %"SCNu64 " %u %u  "
			  "%"SCNu64 " %u %u %u %u %"SCNi64 " %"SCNi64 " %"SCNi64
			  " %"SCNu64 " %"SCNu64 "\n",
			   &p->shm_perm.key,
			   &p->shm_perm.id,
			   &p->shm_perm.mode,
			   &p->shm_segsz,
			   &p->shm_cprid,
			   &p->shm_lprid,
			   &p->shm_nattch,
			   &p->shm_perm.uid,
			   &p->shm_perm.gid,
			   &p->shm_perm.cuid,
			   &p->shm_perm.cgid,
			   &p->shm_atim,
			   &p->shm_dtim,
			   &p->shm_ctim,
			   &p->shm_rss,
			   &p->shm_swp) != 16)
			continue;

		if (id > -1) {
			/* ID specified */
			if (id == p->shm_perm.id) {
				i = 1;
				break;
			} else
				continue;
		}

		p->next = xcalloc(1, sizeof(struct shm_data));
		p = p->next;
		p->next = NULL;
		i++;
	}

	if (i == 0)
		free(*shmds);
	fclose(f);
	return i;

	/* Fallback; /proc or /sys file(s) missing. */
shm_fallback:
	maxid = shmctl(0, SHM_INFO, (struct shmid_ds *) &dummy);

	for (int j = 0; j <= maxid; j++) {
		int shmid;
		struct shmid_ds shmseg;
		struct ipc_perm *ipcp = &shmseg.shm_perm;

		shmid = shmctl(j, SHM_STAT, &shmseg);
		if (shmid < 0 || (id > -1 && shmid != id)) {
			continue;
		}

		i++;
		p->shm_perm.key = ipcp->KEY;
		p->shm_perm.id = shmid;
		p->shm_perm.mode = ipcp->mode;
		p->shm_segsz = shmseg.shm_segsz;
		p->shm_cprid = shmseg.shm_cpid;
		p->shm_lprid = shmseg.shm_lpid;
		p->shm_nattch = shmseg.shm_nattch;
		p->shm_perm.uid = ipcp->uid;
		p->shm_perm.gid = ipcp->gid;
		p->shm_perm.cuid = ipcp->cuid;
		p->shm_perm.cgid = ipcp->cuid;
		p->shm_atim = shmseg.shm_atime;
		p->shm_dtim = shmseg.shm_dtime;
		p->shm_ctim = shmseg.shm_ctime;
		p->shm_rss = 0xdead;
		p->shm_swp = 0xdead;

		if (id < 0) {
			p->next = xcalloc(1, sizeof(struct shm_data));
			p = p->next;
			p->next = NULL;
		} else
			break;
	}

	if (i == 0)
		free(*shmds);
	return i;
}

void ipc_shm_free_info(struct shm_data *shmds)
{
	while (shmds) {
		struct shm_data *next = shmds->next;
		free(shmds);
		shmds = next;
	}
}

static void get_sem_elements(struct sem_data *p)
{
	size_t i;

	if (!p || !p->sem_nsems || p->sem_perm.id < 0)
		return;

	p->elements = xcalloc(p->sem_nsems, sizeof(struct sem_elem));

	for (i = 0; i < p->sem_nsems; i++) {
		struct sem_elem *e = &p->elements[i];
		union semun arg = { .val = 0 };

		e->semval = semctl(p->sem_perm.id, i, GETVAL, arg);
		if (e->semval < 0)
			err(EXIT_FAILURE, _("%s failed"), "semctl(GETVAL)");

		e->ncount = semctl(p->sem_perm.id, i, GETNCNT, arg);
		if (e->ncount < 0)
			err(EXIT_FAILURE, _("%s failed"), "semctl(GETNCNT)");

		e->zcount = semctl(p->sem_perm.id, i, GETZCNT, arg);
		if (e->zcount < 0)
			err(EXIT_FAILURE, _("%s failed"), "semctl(GETZCNT)");

		e->pid = semctl(p->sem_perm.id, i, GETPID, arg);
		if (e->pid < 0)
			err(EXIT_FAILURE, _("%s failed"), "semctl(GETPID)");
	}
}

int ipc_sem_get_info(int id, struct sem_data **semds)
{
	FILE *f;
	int i = 0, maxid;
	struct sem_data *p;
	struct seminfo dummy;
	union semun arg;

	p = *semds = xcalloc(1, sizeof(struct sem_data));
	p->next = NULL;

	f = path_fopen("r", 0, _PATH_PROC_SYSV_SEM);
	if (!f)
		goto sem_fallback;

	while (fgetc(f) != '\n') ;	/* skip header */

	while (feof(f) == 0) {
		if (fscanf(f,
			   "%d %d  %o %" SCNu64 " %u %u %u %u %"
			    SCNi64 " %" SCNi64 "\n",
			   &p->sem_perm.key,
			   &p->sem_perm.id,
			   &p->sem_perm.mode,
			   &p->sem_nsems,
			   &p->sem_perm.uid,
			   &p->sem_perm.gid,
			   &p->sem_perm.cuid,
			   &p->sem_perm.cgid,
			   &p->sem_otime,
			   &p->sem_ctime) != 10)
			continue;

		if (id > -1) {
			/* ID specified */
			if (id == p->sem_perm.id) {
				get_sem_elements(p);
				i = 1;
				break;
			} else
				continue;
		}

		p->next = xcalloc(1, sizeof(struct sem_data));
		p = p->next;
		p->next = NULL;
		i++;
	}

	if (i == 0)
		free(*semds);
	fclose(f);
	return i;

	/* Fallback; /proc or /sys file(s) missing. */
sem_fallback:
	arg.array = (ushort *) (void *)&dummy;
	maxid = semctl(0, 0, SEM_INFO, arg);

	for (int j = 0; j <= maxid; j++) {
		int semid;
		struct semid_ds semseg;
		struct ipc_perm *ipcp = &semseg.sem_perm;
		arg.buf = (struct semid_ds *)&semseg;

		semid = semctl(j, 0, SEM_STAT, arg);
		if (semid < 0 || (id > -1 && semid != id)) {
			continue;
		}

		i++;
		p->sem_perm.key = ipcp->KEY;
		p->sem_perm.id = semid;
		p->sem_perm.mode = ipcp->mode;
		p->sem_nsems = semseg.sem_nsems;
		p->sem_perm.uid = ipcp->uid;
		p->sem_perm.gid = ipcp->gid;
		p->sem_perm.cuid = ipcp->cuid;
		p->sem_perm.cgid = ipcp->cuid;
		p->sem_otime = semseg.sem_otime;
		p->sem_ctime = semseg.sem_ctime;

		if (id < 0) {
			p->next = xcalloc(1, sizeof(struct sem_data));
			p = p->next;
			p->next = NULL;
			i++;
		} else {
			get_sem_elements(p);
			break;
		}
	}

	if (i == 0)
		free(*semds);
	return i;
}

void ipc_sem_free_info(struct sem_data *semds)
{
	while (semds) {
		struct sem_data *next = semds->next;
		free(semds->elements);
		free(semds);
		semds = next;
	}
}

int ipc_msg_get_info(int id, struct msg_data **msgds)
{
	FILE *f;
	int i = 0, maxid;
	struct msg_data *p;
	struct msqid_ds dummy;
	struct msqid_ds msgseg;

	p = *msgds = xcalloc(1, sizeof(struct msg_data));
	p->next = NULL;

	f = path_fopen("r", 0, _PATH_PROC_SYSV_MSG);
	if (!f)
		goto msg_fallback;

	while (fgetc(f) != '\n') ;	/* skip header */

	while (feof(f) == 0) {
		if (fscanf(f,
			   "%d %d  %o  %" SCNu64 " %" SCNu64
			   " %u %u %u %u %u %u %" SCNi64 " %" SCNi64 " %" SCNi64 "\n",
			   &p->msg_perm.key,
			   &p->msg_perm.id,
			   &p->msg_perm.mode,
			   &p->q_cbytes,
			   &p->q_qnum,
			   &p->q_lspid,
			   &p->q_lrpid,
			   &p->msg_perm.uid,
			   &p->msg_perm.gid,
			   &p->msg_perm.cuid,
			   &p->msg_perm.cgid,
			   &p->q_stime,
			   &p->q_rtime,
			   &p->q_ctime) != 14)
			continue;

		if (id > -1) {
			/* ID specified */
			if (id == p->msg_perm.id) {
				/*
				 * FIXME: q_qbytes are not in /proc
				 *
				 */
				if (msgctl(id, IPC_STAT, &msgseg) != -1)
					p->q_qbytes = msgseg.msg_qbytes;
				i = 1;
				break;
			} else
				continue;
		}

		p->next = xcalloc(1, sizeof(struct msg_data));
		p = p->next;
		p->next = NULL;
		i++;
	}

	if (i == 0)
		free(*msgds);
	fclose(f);
	return i;

	/* Fallback; /proc or /sys file(s) missing. */
msg_fallback:
	maxid = msgctl(0, MSG_INFO, &dummy);

	for (int j = 0; j <= maxid; j++) {
		int msgid;
		struct ipc_perm *ipcp = &msgseg.msg_perm;

		msgid = msgctl(j, MSG_STAT, &msgseg);
		if (msgid < 0 || (id > -1 && msgid != id)) {
			continue;
		}

		i++;
		p->msg_perm.key = ipcp->KEY;
		p->msg_perm.id = msgid;
		p->msg_perm.mode = ipcp->mode;
		p->q_cbytes = msgseg.msg_cbytes;
		p->q_qnum = msgseg.msg_qnum;
		p->q_lspid = msgseg.msg_lspid;
		p->q_lrpid = msgseg.msg_lrpid;
		p->msg_perm.uid = ipcp->uid;
		p->msg_perm.gid = ipcp->gid;
		p->msg_perm.cuid = ipcp->cuid;
		p->msg_perm.cgid = ipcp->cgid;
		p->q_stime = msgseg.msg_stime;
		p->q_rtime = msgseg.msg_rtime;
		p->q_ctime = msgseg.msg_ctime;
		p->q_qbytes = msgseg.msg_qbytes;

		if (id < 0) {
			p->next = xcalloc(1, sizeof(struct msg_data));
			p = p->next;
			p->next = NULL;
		} else
			break;
	}

	if (i == 0)
		free(*msgds);
	return i;
}

void ipc_msg_free_info(struct msg_data *msgds)
{
	while (msgds) {
		struct msg_data *next = msgds->next;
		free(msgds);
		msgds = next;
	}
}

void ipc_print_perms(FILE *f, struct ipc_stat *is)
{
	struct passwd *pw;
	struct group *gr;

	fprintf(f, "%-10d %-10o", is->id, is->mode & 0777);

	if ((pw = getpwuid(is->cuid)))
		fprintf(f, " %-10s", pw->pw_name);
	else
		fprintf(f, " %-10u", is->cuid);

	if ((gr = getgrgid(is->cgid)))
		fprintf(f, " %-10s", gr->gr_name);
	else
		fprintf(f, " %-10u", is->cgid);

	if ((pw = getpwuid(is->uid)))
		fprintf(f, " %-10s", pw->pw_name);
	else
		fprintf(f, " %-10u", is->uid);

	if ((gr = getgrgid(is->gid)))
		fprintf(f, " %-10s\n", gr->gr_name);
	else
		fprintf(f, " %-10u\n", is->gid);
}

void ipc_print_size(int unit, char *msg, uint64_t size, const char *end,
		    int width)
{
	char format[32];

	if (!msg)
		/* NULL */ ;
	else if (msg[strlen(msg) - 1] == '=')
		printf("%s", msg);
	else if (unit == IPC_UNIT_BYTES)
		printf(_("%s (bytes) = "), msg);
	else if (unit == IPC_UNIT_KB)
		printf(_("%s (kbytes) = "), msg);
	else
		printf("%s = ", msg);

	switch (unit) {
	case IPC_UNIT_DEFAULT:
	case IPC_UNIT_BYTES:
		sprintf(format, "%%%dju", width);
		printf(format, size);
		break;
	case IPC_UNIT_KB:
		sprintf(format, "%%%dju", width);
		printf(format, size / 1024);
		break;
	case IPC_UNIT_HUMAN:
		sprintf(format, "%%%ds", width);
		printf(format, size_to_human_string(SIZE_SUFFIX_1LETTER, size));
		break;
	default:
		/* impossible occurred */
		abort();
	}

	if (end)
		printf("%s", end);
}
