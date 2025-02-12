/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Copyright (C) 2012 Sami Kerola <kerolasa@iki.fi>
 * Copyright (C) 2012-2023 Karel Zak <kzak@redhat.com>
 */
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
	memset(lim, 0, sizeof(*lim));

	if (access(_PATH_PROC_IPC_MSGMNI, F_OK) == 0 &&
	    access(_PATH_PROC_IPC_MSGMNB, F_OK) == 0 &&
	    access(_PATH_PROC_IPC_MSGMAX, F_OK) == 0) {

		if (ul_path_read_s32(NULL, &lim->msgmni, _PATH_PROC_IPC_MSGMNI) != 0)
			return 1;
		if (ul_path_read_s32(NULL, &lim->msgmnb, _PATH_PROC_IPC_MSGMNB) != 0)
			return 1;
		if (ul_path_read_u64(NULL, &lim->msgmax, _PATH_PROC_IPC_MSGMAX) != 0)
			return 1;
	} else {
		struct msginfo msginfo;

		if (msgctl(0, IPC_INFO, (struct msqid_ds *) &msginfo) < 0)
			return 1;
		lim->msgmni = msginfo.msgmni;
		lim->msgmnb = msginfo.msgmnb;
		lim->msgmax = msginfo.msgmax;
	}

	/* POSIX IPC */
#ifdef HAVE_MQUEUE_H
	FILE *f;

	f = fopen(_PATH_PROC_POSIX_IPC_MSGMNI, "r");
	if (f) {
		if(fscanf(f, "%"SCNu64, &lim->msgmni_posix) != 1)
			lim->msgmni_posix = 0;
		fclose(f);
	}

	f = fopen(_PATH_PROC_POSIX_IPC_MSGMNB, "r");
	if (f) {
		if(fscanf(f, "%"SCNu64, &lim->msgmnb_posix) != 1)
			lim->msgmnb_posix = 0;
		fclose(f);
	}

	f = fopen(_PATH_PROC_POSIX_IPC_MSGMAX, "r");
	if (f) {
		if(fscanf(f, "%"SCNu64, &lim->msgmax_posix) != 1)
			lim->msgmax_posix = 0;
		fclose(f);
	}
#endif
	return 0;
}

int ipc_sem_get_limits(struct ipc_limits *lim)
{
	FILE *f;
	int rc = 0;

	lim->semvmx = SEMVMX;

	f = fopen(_PATH_PROC_IPC_SEM, "r");
	if (f) {
		rc = fscanf(f, "%d\t%d\t%d\t%d",
		       &lim->semmsl, &lim->semmns, &lim->semopm, &lim->semmni);
		fclose(f);
	}

	if (rc != 4) {
		struct seminfo seminfo = { .semmni = 0 };
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

	if (access(_PATH_PROC_IPC_SHMALL, F_OK) == 0 &&
	    access(_PATH_PROC_IPC_SHMMAX, F_OK) == 0 &&
	    access(_PATH_PROC_IPC_SHMMNI, F_OK) == 0) {

		ul_path_read_u64(NULL, &lim->shmall, _PATH_PROC_IPC_SHMALL);
		ul_path_read_u64(NULL, &lim->shmmax, _PATH_PROC_IPC_SHMMAX);
		ul_path_read_u64(NULL, &lim->shmmni, _PATH_PROC_IPC_SHMMNI);

	} else {
		struct shminfo *shminfo;
		struct shmid_ds shmbuf;

		if (shmctl(0, IPC_INFO, &shmbuf) < 0)
			return 1;
		shminfo = (struct shminfo *) &shmbuf;
		lim->shmmni = shminfo->shmmni;
		lim->shmall = shminfo->shmall;
		lim->shmmax = shminfo->shmmax;
	}

	return 0;
}

int ipc_shm_get_info(int id, struct shm_data **shmds)
{
	FILE *f;
	int i = 0, maxid, j;
	char buf[BUFSIZ];
	struct shm_data *p;
	struct shmid_ds dummy;

	p = *shmds = xcalloc(1, sizeof(struct shm_data));
	p->next = NULL;

	f = fopen(_PATH_PROC_SYSV_SHM, "r");
	if (!f)
		goto shm_fallback;

	while (fgetc(f) != '\n');		/* skip header */

	while (fgets(buf, sizeof(buf), f) != NULL) {
		/* scan for the first 14-16 columns (e.g. Linux 2.6.32 has 14) */
		p->shm_rss = 0xdead;
		p->shm_swp = 0xdead;
		if (sscanf(buf,
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
			   &p->shm_swp) < 14)
			continue; /* invalid line, skipped */

		if (id > -1) {
			/* ID specified */
			if (id == p->shm_perm.id) {
				i = 1;
				break;
			}
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
	maxid = shmctl(0, SHM_INFO, &dummy);

	for (j = 0; j <= maxid; j++) {
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

#ifndef HAVE_SYS_MMAN_H
int posix_ipc_shm_get_info(const char *name __attribute__((unused)),
			   struct posix_shm_data **shmds __attribute__((unused)))
{
	warnx(_("POSIX shared memory is not supported"));
	return -1;
}
#else
int posix_ipc_shm_get_info(const char *name, struct posix_shm_data **shmds)
{
	DIR *d;
	int i = 0;
	struct posix_shm_data *p;
	struct dirent *de;

	d = opendir(_PATH_DEV_SHM);
	if (!d) {
		warnx(_("cannot open %s"), _PATH_DEV_SHM);
		return -1;
	}

	p = *shmds = xcalloc(1, sizeof(struct posix_shm_data));
	p->next = NULL;

	while ((de = readdir(d)) != NULL) {
		if ((de->d_name[0] == '.' && de->d_name[1] == '\0') ||
		    (de->d_name[0] == '.' && de->d_name[1] == '.' &&
		     de->d_name[2] == '\0'))
			continue;

		if (strncmp(de->d_name, "sem.", 4) == 0)
			continue;

		struct stat st;
		if (fstatat(dirfd(d), de->d_name, &st, 0) < 0)
			continue;

		p->name = xstrdup(de->d_name);
		p->cuid = st.st_uid;
		p->cgid = st.st_gid;
		p->size = st.st_size;
		p->mode = st.st_mode;
		p->mtime = st.st_mtime;

		if (name != NULL) {
			if (strcmp(name, p->name) == 0) {
				i = 1;
				break;
			}
			continue;
		}

		p->next = xcalloc(1, sizeof(struct posix_shm_data));
		p = p->next;
		p->next = NULL;
		i++;
	}

	if (i == 0)
		free(*shmds);
	closedir(d);
	return i;
}
#endif /* HAVE_SYS_MMAN_H */

void posix_ipc_shm_free_info(struct posix_shm_data *shmds)
{
	while (shmds) {
		struct posix_shm_data *next = shmds->next;
		free(shmds);
		shmds = next;
	}
}

static void get_sem_elements(struct sem_data *p)
{
	size_t i;

	if (!p || !p->sem_nsems || p->sem_nsems > SIZE_MAX || p->sem_perm.id < 0)
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
	int i = 0, maxid, j;
	struct sem_data *p;
	struct seminfo dummy;
	union semun arg;

	p = *semds = xcalloc(1, sizeof(struct sem_data));
	p->next = NULL;

	f = fopen(_PATH_PROC_SYSV_SEM, "r");
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
			}
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

	for (j = 0; j <= maxid; j++) {
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

#ifndef HAVE_SEMAPHORE_H
int posix_ipc_sem_get_info(const char *name __attribute__((__unused__)),
			   struct posix_sem_data **semds __attribute__((__unused__)))
{
	warnx(_("POSIX semaphore is not supported"));
	return -1;
}
#else
int posix_ipc_sem_get_info(const char *name, struct posix_sem_data **semds)
{
	DIR *d;
	int i = 0;
	struct posix_sem_data *p;
	struct dirent *de;

	d = opendir(_PATH_DEV_SHM);
	if (!d) {
		warnx(_("cannot open %s"), _PATH_DEV_SHM);
		return -1;
	}

	p = *semds = xcalloc(1, sizeof(struct posix_sem_data));
	p->next = NULL;

	while ((de = readdir(d)) != NULL) {
		if ((de->d_name[0] == '.' && de->d_name[1] == '\0') ||
		    (de->d_name[0] == '.' && de->d_name[1] == '.' &&
		     de->d_name[2] == '\0'))
			continue;

		if (strncmp(de->d_name, "sem.", 4) != 0)
			continue;

		struct stat st;
		if (fstatat(dirfd(d), de->d_name, &st, 0) < 0)
			continue;

		sem_t *sem = sem_open(de->d_name + 4, 0);
		if (sem == SEM_FAILED)
			continue;
		sem_getvalue(sem, &p->sval);
		sem_close(sem);

		p->sname = xstrdup(de->d_name + 4);
		p->cuid = st.st_uid;
		p->cgid = st.st_gid;
		p->mode = st.st_mode;
		p->mtime = st.st_mtime;

		if (name != NULL) {
			if (strcmp(name, p->sname) == 0) {
				i = 1;
				break;
			}
			continue;
		}

		p->next = xcalloc(1, sizeof(struct posix_sem_data));
		p = p->next;
		p->next = NULL;
		i++;
	}

	if (i == 0)
		free(*semds);
	closedir(d);
	return i;
}
#endif /* HAVE_SEMAPHORE_H */

void posix_ipc_sem_free_info(struct posix_sem_data *semds)
{
	while (semds) {
		struct posix_sem_data *next = semds->next;
		free(semds);
		semds = next;
	}
}

int ipc_msg_get_info(int id, struct msg_data **msgds)
{
	FILE *f;
	int i = 0, maxid, j;
	struct msg_data *p;
	struct msqid_ds dummy;
	struct msqid_ds msgseg;

	p = *msgds = xcalloc(1, sizeof(struct msg_data));
	p->next = NULL;

	f = fopen(_PATH_PROC_SYSV_MSG, "r");
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
				if (msgctl(id, IPC_STAT, &msgseg) != -1)
					p->q_qbytes = msgseg.msg_qbytes;
				i = 1;
				break;
			}
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

	for (j = 0; j <= maxid; j++) {
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

#ifndef HAVE_MQUEUE_H
int posix_ipc_msg_get_info(const char *name __attribute__((unused)),
			   struct posix_msg_data **msgds __attribute__((unused)))
{
	warnx(_("POSIX message queues are not supported"));
	return -1;
}
#else
int posix_ipc_msg_get_info(const char *name, struct posix_msg_data **msgds)
{
	FILE *f;
	DIR *d;
	int i = 0;
	struct posix_msg_data *p;
	struct dirent *de;

	if (access(_PATH_DEV_MQUEUE, F_OK) != 0) {
		warnx(_("%s does not seem to be mounted. Use 'mount -t mqueue none %s' to mount it."), _PATH_DEV_MQUEUE, _PATH_DEV_MQUEUE);
		return -1;
	}

	if (name && name[0] != '/') {
		warnx(_("mqueue name must start with '/': %s"), name);
		return -1;
	}

	d = opendir(_PATH_DEV_MQUEUE);
	if (!d) {
		warnx(_("cannot open %s"), _PATH_DEV_MQUEUE);
		return -1;
	}

	p = *msgds = xcalloc(1, sizeof(struct msg_data));
	p->next = NULL;

	while ((de = readdir(d)) != NULL) {
		if ((de->d_name[0] == '.' && de->d_name[1] == '\0') ||
		    (de->d_name[0] == '.' && de->d_name[1] == '.' &&
		     de->d_name[2] == '\0'))
			continue;

		struct stat st;
		if (fstatat(dirfd(d), de->d_name, &st, 0) < 0)
			continue;

		int fd = openat(dirfd(d), de->d_name, O_RDONLY);
		if (fd < 0)
			continue;

		f = fdopen(fd, "r");
		if (!f) {
			close(fd);
			continue;
		}
		if (fscanf(f, "QSIZE:%"SCNu64, &p->q_cbytes) != 1) {
			fclose(f);
			continue;
		}
		fclose(f);

		p->mname = strconcat("/", de->d_name);

		mqd_t mq = mq_open(p->mname, O_RDONLY);
		struct mq_attr attr;
		mq_getattr(mq, &attr);
		p->q_qnum = attr.mq_curmsgs;
		mq_close(mq);

		p->cuid = st.st_uid;
		p->cgid = st.st_gid;
		p->mode = st.st_mode;
		p->mtime = st.st_mtime;

		if (name != NULL) {
			if (strcmp(name, p->mname) == 0) {
				i = 1;
				break;
			}
			continue;
		}

		p->next = xcalloc(1, sizeof(struct posix_msg_data));
		p = p->next;
		p->next = NULL;
		i++;
	}

	if (i == 0)
		free(*msgds);
	closedir(d);

	return i;
}
#endif /* HAVE_MQUEUE_H */

void posix_ipc_msg_free_info(struct posix_msg_data *msgds)
{
	while (msgds) {
		struct posix_msg_data *next = msgds->next;
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
		snprintf(format, sizeof(format), "%%%dju", width);
		printf(format, size);
		break;
	case IPC_UNIT_KB:
		snprintf(format, sizeof(format), "%%%dju", width);
		printf(format, size / 1024);
		break;
	case IPC_UNIT_HUMAN:
	{
		char *tmp;
		snprintf(format, sizeof(format), "%%%ds", width);
		printf(format, (tmp = size_to_human_string(SIZE_SUFFIX_1LETTER, size)));
		free(tmp);
		break;
	}
	default:
		/* impossible occurred */
		abort();
	}

	if (end)
		printf("%s", end);
}
