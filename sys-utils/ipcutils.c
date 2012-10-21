
#include "c.h"
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
