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
 *
 * Fri, 8 Mar 1996 18:01:39, Swen Thuemmler <swen@uni-paderborn.de>:
 * Omit the call to connect() for Linux version 1.3.11 or later.
 */

/*
 * nfsmount.c,v 1.1.1.1 1993/11/18 08:40:51 jrs Exp
 */

#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <netdb.h>
#include <rpc/rpc.h>
#include <rpc/pmap_prot.h>
#include <rpc/pmap_clnt.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/utsname.h>
#include <arpa/inet.h>

#include "sundries.h"
#include "nfsmount.h"

#if defined(__GLIBC__)
#define _LINUX_SOCKET_H
#endif /* __GLIBC__ */
#define _I386_BITOPS_H
#include <linux/fs.h>
#include <linux/nfs.h>
#include "nfs_mount3.h"

static char *nfs_strerror(int stat);

#define MAKE_VERSION(p,q,r)	(65536*(p) + 256*(q) + (r))

static int
linux_version_code(void) {
	struct utsname my_utsname;
	int p, q, r;

	if (uname(&my_utsname) == 0) {
		p = atoi(strtok(my_utsname.release, "."));
		q = atoi(strtok(NULL, "."));
		r = atoi(strtok(NULL, "."));
		return MAKE_VERSION(p,q,r);
	}
	return 0;
}

/*
 * nfs_mount_version according to the kernel sources seen at compile time.
 */
static int nfs_mount_version = KERNEL_NFS_MOUNT_VERSION;

/*
 * Unfortunately, the kernel prints annoying console messages
 * in case of an unexpected nfs mount version (instead of
 * just returning some error).  Therefore we'll have to try
 * and figure out what version the kernel expects.
 *
 * Variables:
 *	KERNEL_NFS_MOUNT_VERSION: kernel sources at compile time
 *	NFS_MOUNT_VERSION: these nfsmount sources at compile time
 *	nfs_mount_version: version this source and running kernel can handle
 */
static void
find_kernel_nfs_mount_version(void) {
	int kernel_version = linux_version_code();

	if (kernel_version) {
	     if (kernel_version < MAKE_VERSION(2,1,32))
		  nfs_mount_version = 1;
	     else
		  nfs_mount_version = 3;
	}
	if (nfs_mount_version > NFS_MOUNT_VERSION)
	     nfs_mount_version = NFS_MOUNT_VERSION;
}

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
	int nolock;
	int retry;
	int tcp;
	int mountprog;
	int mountvers;
	int nfsprog;
	int nfsvers;

	find_kernel_nfs_mount_version();

	msock = fsock = -1;
	mclient = NULL;
	if (strlen(spec) >= sizeof(hostdir)) {
		fprintf(stderr, "mount: "
			"excessively long host:dir argument\n");
		goto fail;
	}
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

	server_addr.sin_family = AF_INET;
	if (!inet_aton(hostname, &server_addr.sin_addr)) {
		if ((hp = gethostbyname(hostname)) == NULL) {
			fprintf(stderr, "mount: can't get address for %s\n",
				hostname);
			goto fail;
		} else
			memcpy(&server_addr.sin_addr, hp->h_addr, hp->h_length);
	}

	memcpy (&mount_server_addr, &server_addr, sizeof (mount_server_addr));

	/* add IP address to mtab options for use when unmounting */

	s = inet_ntoa(server_addr.sin_addr);
	old_opts = *extra_opts;
	if (!old_opts)
		old_opts = "";
	if (strlen(old_opts) + strlen(s) + 10 >= sizeof(new_opts)) {
		fprintf(stderr, "mount: "
			"excessively long option argument\n");
		goto fail;
	}
	sprintf(new_opts, "%s%saddr=%s",
		old_opts, *old_opts ? "," : "", s);
	*extra_opts = xstrdup(new_opts);

	/* Set default options.
	 * rsize/wsize (and bsize, for ver >= 3) are left 0 in order to
	 * let the kernel decide.
	 * timeo is filled in after we know whether it'll be TCP or UDP. */
	memset(&data, 0, sizeof(data));
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
	nolock = 0;
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
			        mounthost=xstrndup(opteq+1,
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
				if (nfs_mount_version >= 2)
					data.namlen = val;
				else
#endif
				printf("Warning: Option namlen is not supported.\n");
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
			else if (!strcmp(opt, "lock")) {
				if (nfs_mount_version >= 3)
					nolock = !val;
				else
					printf("Warning: option nolock is not supported.\n");
			} else {
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
	if (nfs_mount_version >= 2)
		data.flags |= (tcp ? NFS_MOUNT_TCP : 0);
#endif
#if NFS_MOUNT_VERSION >= 3
	if (nfs_mount_version >= 3)
		data.flags |= (nolock ? NFS_MOUNT_NONLM : 0);
#endif

	/* Adjust options if none specified */
	if (!data.timeo)
		data.timeo = tcp ? 70 : 7;

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

	data.version = nfs_mount_version;
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
		(xdrproc_t) xdr_dirpath, &dirname,
		(xdrproc_t) xdr_fhstatus, &status,
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
		if (nfs_mount_version < 3) {
	     		printf("NFS over TCP is not supported.\n");
			goto fail;
		}
		fsock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	} else
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
	 /*
	  * connect() the socket for kernels 1.3.10 and below only,
	  * to avoid problems with multihomed hosts.
	  * --Swen
	  */
	if (linux_version_code() <= 66314
	    && connect(fsock, (struct sockaddr *) &server_addr,
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
	return 1;
}
	

/*
 * We need to translate between nfs status return values and
 * the local errno values which may not be the same.
 *
 * Andreas Schwab <schwab@LS5.informatik.uni-dortmund.de>: change errno:
 * "after #include <errno.h> the symbol errno is reserved for any use,
 *  it cannot even be used as a struct tag or field name".
 */

#ifndef EDQUOT
#define EDQUOT	ENOSPC
#endif

static struct {
	enum nfs_stat stat;
	int errnum;
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
	/* Throw in some NFSv3 values for even more fun (HP returns these) */
	{ 71,			EREMOTE		},

	{ -1,			EIO		}
};

static char *nfs_strerror(int stat)
{
	int i;
	static char buf[256];

	for (i = 0; nfs_errtbl[i].stat != -1; i++) {
		if (nfs_errtbl[i].stat == stat)
			return strerror(nfs_errtbl[i].errnum);
	}
	sprintf(buf, "unknown nfs status return value: %d", stat);
	return buf;
}

