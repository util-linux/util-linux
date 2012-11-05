
#include <inttypes.h>

#include "c.h"
#include "xalloc.h"
#include "path.h"
#include "pathnames.h"
#include "ipcutils.h"

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
	int i, maxid;
	struct shm_data *p;
	struct shm_info dummy;

	p = *shmds = xmalloc(sizeof(struct shm_data));
	p->next = NULL;

	f = path_fopen("r", 0, _PATH_PROC_SYSV_SHM);
	if (!f)
		goto fallback;

	while (fgetc(f) != '\n');		/* skip header */

	for (i = 0; !feof(f); i++) {
		if (fscanf(f,
			  "%d %d  %o %"SCNu64 " %u %u  "
			  "%"SCNu64 " %u %u %u %u %"SCNu64 " %"SCNu64 " %"SCNu64
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

		if (id > -1 && id != p->shm_perm.id) {
			i--;
			continue;	/* id specified, but does not match */
		}
		if (id < 0) {
			p->next = xmalloc(sizeof(struct shm_data));
			p = p->next;
			p->next = NULL;
		}
	}

	if (i == 0)
		free(*shmds);
	fclose(f);
	return i;

	/* Fallback; /proc or /sys file(s) missing. */
fallback:
	i = id < 0 ? 0 : id;

	maxid = shmctl(0, SHM_INFO, (struct shmid_ds *) &dummy);
	if (maxid < 0)
		return 0;

	while (i <= maxid) {
		int shmid;
		struct shmid_ds shmseg;
		struct ipc_perm *ipcp = &shmseg.shm_perm;

		shmid = shmctl(i, SHM_STAT, &shmseg);
		if (shmid < 0) {
			if (-1 < id) {
				free(*shmds);
				return 0;
			}
			i++;
			continue;
		}

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
			p->next = xmalloc(sizeof(struct shm_data));
			p = p->next;
			p->next = NULL;
			i++;
		} else
			return 1;
	}

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
