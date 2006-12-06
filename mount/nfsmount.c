/*
 * nfsmount.c -- Linux NFS mount
 * Copyright (C) 1993 Rick Sladkey <jrs@world.std.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Wed Feb  8 12:51:48 1995, biro@yggdrasil.com (Ross Biro): allow all port
 * numbers to be specified on the command line.
 */

/*
 * nfsmount.c,v 1.1.1.1 1993/11/18 08:40:51 jrs Exp
 */

#include <stdio.h>
#include <rpc/rpc.h>
#include <rpc/pmap_prot.h>
#include <rpc/pmap_clnt.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <string.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <errno.h>

#include "sundries.h"

#include "mount.h"

#include <linux/fs.h>
#include <linux/nfs.h>
#include <linux/nfs_mount.h>

static char *strndup (char *str, int n) {
  char *ret;
  ret = malloc (n+1);
  if (ret == NULL) {
    perror ("malloc");
    return (NULL);
  }
  strncpy (ret, str, n);
  return (ret);
}

static char *nfs_strerror(int stat);

int nfsmount(const char *spec, const char *node, int *flags,
	     char **extra_opts, char **mount_opts)
{
	char hostdir[1024];
	CLIENT *mclient;
	char *hostname;
	char *dirname;
	char *old_opts;
	char *mounthost=NULL;
	char new_opts[1024];
	fhandle root_fhandle;
	struct timeval total_timeout;
	enum clnt_stat clnt_stat;
	static struct nfs_mount_data data;
	char *opt, *opteq;
	int val;
	struct hostent *hp;
	struct sockaddr_in server_addr;
	struct sockaddr_in mount_server_addr;
	int msock, fsock;
	struct timeval pertry_timeout;
	struct fhstatus status;
	char *s;
	int port;
	int mountport;
	int bg;
	int soft;
	int intr;
	int posix;
	int nocto;
	int noac;
	int retry;
	int tcp;
	int mountprog;
	int mountvers;
	int nfsprog;
	int nfsvers;

	msock = fsock = -1;
	mclient = NULL;
	strcpy(hostdir, spec);
	if ((s = (strchr(hostdir, ':')))) {
		hostname = hostdir;
		dirname = s + 1;
		*s = '\0';
	}
	else {
		fprintf(stderr, "mount: "
			"directory to mount not in host:dir format\n");
		goto fail;
	}

	if (hostname[0] >= '0' && hostname[0] <= '9') {
		server_addr.sin_family = AF_INET;
		server_addr.sin_addr.s_addr = inet_addr(hostname);
	}
	else if ((hp = gethostbyname(hostname)) == NULL) {
		fprintf(stderr, "mount: can't get address for %s\n", hostname);
		goto fail;
	}
	else {
		server_addr.sin_family = AF_INET;
		memcpy(&server_addr.sin_addr, hp->h_addr, hp->h_length);
	}

	memcpy (&mount_server_addr, &server_addr, sizeof (mount_server_addr));

	/* add IP address to mtab options for use when unmounting */

	old_opts = *extra_opts;
	if (!old_opts)
		old_opts = "";
	sprintf(new_opts, "%s%saddr=%s",
		old_opts, *old_opts ? "," : "",
		inet_ntoa(server_addr.sin_addr));
	*extra_opts = strdup(new_opts);

	/* set default options */

	data.rsize	= 0; /* let kernel decide */
	data.wsize	= 0; /* let kernel decide */
	data.timeo	= 7;
	data.retrans	= 3;
	data.acregmin	= 3;
	data.acregmax	= 60;
	data.acdirmin	= 30;
	data.acdirmax	= 60;
#if NFS_MOUNT_VERSION >= 2
	data.namlen	= NAME_MAX;
#endif

	bg = 0;
	soft = 0;
	intr = 0;
	posix = 0;
	nocto = 0;
	noac = 0;
	retry = 10000;
	tcp = 0;

	mountprog = MOUNTPROG;
	mountvers = MOUNTVERS;
	port = 0;
	mountport = 0;
	nfsprog = NFS_PROGRAM;
	nfsvers = NFS_VERSION;

	/* parse options */

	for (opt = strtok(old_opts, ","); opt; opt = strtok(NULL, ",")) {
		if ((opteq = strchr(opt, '='))) {
			val = atoi(opteq + 1);	
			*opteq = '\0';
			if (!strcmp(opt, "rsize"))
				data.rsize = val;
			else if (!strcmp(opt, "wsize"))
				data.wsize = val;
			else if (!strcmp(opt, "timeo"))
				data.timeo = val;
			else if (!strcmp(opt, "retrans"))
				data.retrans = val;
			else if (!strcmp(opt, "acregmin"))
				data.acregmin = val;
			else if (!strcmp(opt, "acregmax"))
				data.acregmax = val;
			else if (!strcmp(opt, "acdirmin"))
				data.acdirmin = val;
			else if (!strcmp(opt, "acdirmax"))
				data.acdirmax = val;
			else if (!strcmp(opt, "actimeo")) {
				data.acregmin = val;
				data.acregmax = val;
				data.acdirmin = val;
				data.acdirmax = val;
			}
			else if (!strcmp(opt, "retry"))
				retry = val;
			else if (!strcmp(opt, "port"))
				port = val;
			else if (!strcmp(opt, "mountport"))
			        mountport = val;
			else if (!strcmp(opt, "mounthost"))
			        mounthost=strndup(opteq+1,
						  strcspn(opteq+1," \t\n\r,"));
			else if (!strcmp(opt, "mountprog"))
				mountprog = val;
			else if (!strcmp(opt, "mountvers"))
				mountvers = val;
			else if (!strcmp(opt, "nfsprog"))
				nfsprog = val;
			else if (!strcmp(opt, "nfsvers"))
				nfsvers = val;
			else if (!strcmp(opt, "namlen")) {
#if NFS_MOUNT_VERSION >= 2
				data.namlen = val;
#else
				printf("Warning: Option namlen is not supported.\n");
#endif
			}
			else if (!strcmp(opt, "addr"))
				/* ignore */;
			else {
				printf("unknown nfs mount parameter: "
				       "%s=%d\n", opt, val);
				goto fail;
			}
		}
		else {
			val = 1;
			if (!strncmp(opt, "no", 2)) {
				val = 0;
				opt += 2;
			}
			if (!strcmp(opt, "bg")) 
				bg = val;
			else if (!strcmp(opt, "fg")) 
				bg = !val;
			else if (!strcmp(opt, "soft"))
				soft = val;
			else if (!strcmp(opt, "hard"))
				soft = !val;
			else if (!strcmp(opt, "intr"))
				intr = val;
			else if (!strcmp(opt, "posix"))
				posix = val;
			else if (!strcmp(opt, "cto"))
				nocto = !val;
			else if (!strcmp(opt, "ac"))
				noac = !val;
			else if (!strcmp(opt, "tcp"))
				tcp = val;
			else if (!strcmp(opt, "udp"))
				tcp = !val;
			else {
				printf("unknown nfs mount option: "
				       "%s%s\n", val ? "" : "no", opt);
				goto fail;
			}
		}
	}
	data.flags = (soft ? NFS_MOUNT_SOFT : 0)
		| (intr ? NFS_MOUNT_INTR : 0)
		| (posix ? NFS_MOUNT_POSIX : 0)
		| (nocto ? NFS_MOUNT_NOCTO : 0)
		| (noac ? NFS_MOUNT_NOAC : 0);
#if NFS_MOUNT_VERSION >= 2
	data.flags |= (tcp ? NFS_MOUNT_TCP : 0);
#endif

#ifdef NFS_MOUNT_DEBUG
	printf("rsize = %d, wsize = %d, timeo = %d, retrans = %d\n",
		data.rsize, data.wsize, data.timeo, data.retrans);
	printf("acreg (min, max) = (%d, %d), acdir (min, max) = (%d, %d)\n",
		data.acregmin, data.acregmax, data.acdirmin, data.acdirmax);
	printf("port = %d, bg = %d, retry = %d, flags = %.8x\n",
		port, bg, retry, data.flags);
	printf("mountprog = %d, mountvers = %d, nfsprog = %d, nfsvers = %d\n",
		mountprog, mountvers, nfsprog, nfsvers);
	printf("soft = %d, intr = %d, posix = %d, nocto = %d, noac = %d\n",
		(data.flags & NFS_MOUNT_SOFT) != 0,
		(data.flags & NFS_MOUNT_INTR) != 0,
		(data.flags & NFS_MOUNT_POSIX) != 0,
		(data.flags & NFS_MOUNT_NOCTO) != 0,
		(data.flags & NFS_MOUNT_NOAC) != 0);
#if NFS_MOUNT_VERSION >= 2
	printf("tcp = %d\n",
		(data.flags & NFS_MOUNT_TCP) != 0);
#endif
#if 0
	goto fail;
#endif
#endif

	data.version = NFS_MOUNT_VERSION;
	*mount_opts = (char *) &data;

	if (*flags & MS_REMOUNT)
		return 0;

	/* create mount deamon client */
	/* See if the nfs host = mount host. */
	if (mounthost) {
	  if (mounthost[0] >= '0' && mounthost[0] <= '9') {
	    mount_server_addr.sin_family = AF_INET;
	    mount_server_addr.sin_addr.s_addr = inet_addr(hostname);
	  }
	  else if ((hp = gethostbyname(mounthost)) == NULL) {
		fprintf(stderr, "mount: can't get address for %s\n", hostname);
		goto fail;
	  }
	  else {
	    mount_server_addr.sin_family = AF_INET;
	    memcpy(&mount_server_addr.sin_addr, hp->h_addr, hp->h_length);
	  }
	}

	mount_server_addr.sin_port = htons(mountport);
	msock = RPC_ANYSOCK;
	if ((mclient = clnttcp_create(&mount_server_addr,
	    mountprog, mountvers, &msock, 0, 0)) == NULL) {
		mount_server_addr.sin_port = htons(mountport);
		msock = RPC_ANYSOCK;
		pertry_timeout.tv_sec = 3;
		pertry_timeout.tv_usec = 0;
		if ((mclient = clntudp_create(&mount_server_addr,
		    mountprog, mountvers, pertry_timeout, &msock)) == NULL) {
			clnt_pcreateerror("mount clntudp_create");
			goto fail;
		}
#ifdef NFS_MOUNT_DEBUG
		printf("using UDP for mount deamon\n");
#endif
	}
#ifdef NFS_MOUNT_DEBUG
	else
		printf("using TCP for mount deamon\n");
#endif
	mclient->cl_auth = authunix_create_default();
	total_timeout.tv_sec = 20;
	total_timeout.tv_usec = 0;

	/* try to mount hostname:dirname */

	clnt_stat = clnt_call(mclient, MOUNTPROC_MNT,
		xdr_dirpath, &dirname,
		xdr_fhstatus, &status,
		total_timeout);
	if (clnt_stat != RPC_SUCCESS) {
		clnt_perror(mclient, "rpc mount");
		goto fail;
	}
	if (status.fhs_status != 0) {
		fprintf(stderr,
			"mount: %s:%s failed, reason given by server: %s\n",
			hostname, dirname, nfs_strerror(status.fhs_status));
		goto fail;
	}
	memcpy((char *) &root_fhandle, (char *) status.fhstatus_u.fhs_fhandle,
		sizeof (root_fhandle));

	/* create nfs socket for kernel */

	if (tcp) {
#if NFS_MOUNT_VERSION >= 2
		fsock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#else
		printf("NFS over TCP is not supported.\n");
		goto fail;
#endif
	}
	else
		fsock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (fsock < 0) {
		perror("nfs socket");
		goto fail;
	}
	if (bindresvport(fsock, 0) < 0) {
		perror("nfs bindresvport");
		goto fail;
	}
	if (port == 0) {
		server_addr.sin_port = PMAPPORT;
		port = pmap_getport(&server_addr, nfsprog, nfsvers,
			tcp ? IPPROTO_TCP : IPPROTO_UDP);
		if (port == 0)
			port = NFS_PORT;
#ifdef NFS_MOUNT_DEBUG
		else
			printf("used portmapper to find NFS port\n");
#endif
	}
#ifdef NFS_MOUNT_DEBUG
	printf("using port %d for nfs deamon\n", port);
#endif
	server_addr.sin_port = htons(port);
	if (connect(fsock, (struct sockaddr *) &server_addr,
	    sizeof (server_addr)) < 0) {
		perror("nfs connect");
		goto fail;
	}

	/* prepare data structure for kernel */

	data.fd = fsock;
	memcpy((char *) &data.root, (char *) &root_fhandle,
		sizeof (root_fhandle));
	memcpy((char *) &data.addr, (char *) &server_addr, sizeof(data.addr));
	strncpy(data.hostname, hostname, sizeof(data.hostname));

	/* clean up */

	auth_destroy(mclient->cl_auth);
	clnt_destroy(mclient);
	close(msock);
	return 0;

	/* abort */

fail:
	if (msock != -1) {
		auth_destroy(mclient->cl_auth);
		clnt_destroy(mclient);
		close(msock);
	}
	if (fsock != -1)
		close(fsock);
	return 1;}
	

/*
 * We need to translate between nfs status return values and
 * the local errno values which may not be the same.
 */

#ifndef EDQUOT
#define EDQUOT	ENOSPC
#endif

static struct {
	enum nfs_stat stat;
	int errno;
} nfs_errtbl[] = {
	{ NFS_OK,		0		},
	{ NFSERR_PERM,		EPERM		},
	{ NFSERR_NOENT,		ENOENT		},
	{ NFSERR_IO,		EIO		},
	{ NFSERR_NXIO,		ENXIO		},
	{ NFSERR_ACCES,		EACCES		},
	{ NFSERR_EXIST,		EEXIST		},
	{ NFSERR_NODEV,		ENODEV		},
	{ NFSERR_NOTDIR,	ENOTDIR		},
	{ NFSERR_ISDIR,		EISDIR		},
#ifdef NFSERR_INVAL
	{ NFSERR_INVAL,		EINVAL		},	/* that Sun forgot */
#endif
	{ NFSERR_FBIG,		EFBIG		},
	{ NFSERR_NOSPC,		ENOSPC		},
	{ NFSERR_ROFS,		EROFS		},
	{ NFSERR_NAMETOOLONG,	ENAMETOOLONG	},
	{ NFSERR_NOTEMPTY,	ENOTEMPTY	},
	{ NFSERR_DQUOT,		EDQUOT		},
	{ NFSERR_STALE,		ESTALE		},
#ifdef EWFLUSH
	{ NFSERR_WFLUSH,	EWFLUSH		},
#endif
	{ -1,			EIO		}
};

static char *nfs_strerror(int stat)
{
	int i;
	static char buf[256];

	for (i = 0; nfs_errtbl[i].stat != -1; i++) {
		if (nfs_errtbl[i].stat == stat)
			return strerror(nfs_errtbl[i].errno);
	}
	sprintf(buf, "unknown nfs status return value: %d", stat);
	return buf;
}

