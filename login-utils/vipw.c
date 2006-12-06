/*
 * Copyright (c) 1987 Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Updated Thu Oct 12 09:56:55 1995 by faith@cs.unc.edu with security
 * patches from Zefram <A.Main@dcs.warwick.ac.uk>
 *
 */

#ifndef lint
char copyright[] =
"@(#) Copyright (c) 1987 Regents of the University of California.\n\
 All rights reserved.\n";
#endif /* not lint */

#ifndef lint
/*static char sccsid[] = "from: @(#)vipw.c	5.16 (Berkeley) 3/3/91";*/
static char rcsid[] = "$Id: vipw.c,v 1.4 1995/10/12 14:46:36 faith Exp $";
#endif /* not lint */

#include <sys/types.h>
#include <sys/stat.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <sys/file.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <paths.h>
#include <unistd.h>

#include "pathnames.h"


char *progname = "vipw";
void pw_error __P((char *, int, int));


copyfile(from, to)
	register int from, to;
{
	register int nr, nw, off;
	char buf[8*1024];
	
	while ((nr = read(from, buf, sizeof(buf))) > 0)
		for (off = 0; off < nr; nr -= nw, off += nw)
			if ((nw = write(to, buf + off, nr)) < 0)
				pw_error(_PATH_PTMP, 1, 1);
	if (nr < 0)
		pw_error(_PATH_PASSWD, 1, 1);
}


void
pw_init()
{
	struct rlimit rlim;

	/* Unlimited resource limits. */
	rlim.rlim_cur = rlim.rlim_max = RLIM_INFINITY;
	(void)setrlimit(RLIMIT_CPU, &rlim);
	(void)setrlimit(RLIMIT_FSIZE, &rlim);
	(void)setrlimit(RLIMIT_STACK, &rlim);
	(void)setrlimit(RLIMIT_DATA, &rlim);
	(void)setrlimit(RLIMIT_RSS, &rlim);

	/* Don't drop core (not really necessary, but GP's). */
	rlim.rlim_cur = rlim.rlim_max = 0;
	(void)setrlimit(RLIMIT_CORE, &rlim);

	/* Turn off signals. */
	(void)signal(SIGALRM, SIG_IGN);
	(void)signal(SIGHUP, SIG_IGN);
	(void)signal(SIGINT, SIG_IGN);
	(void)signal(SIGPIPE, SIG_IGN);
	(void)signal(SIGQUIT, SIG_IGN);
	(void)signal(SIGTERM, SIG_IGN);
	(void)signal(SIGTSTP, SIG_IGN);
	(void)signal(SIGTTOU, SIG_IGN);

	/* Create with exact permissions. */
	(void)umask(0);
}

int
pw_lock()
{
	int lockfd, fd, ret;
	char *p;

	/* 
	 * If the password file doesn't exist, the system is hosed.
	 * Might as well try to build one.  Set the close-on-exec bit so
	 * that users can't get at the encrypted passwords while editing.
	 * Open should allow flock'ing the file; see 4.4BSD.	XXX
	 */
	lockfd = open(_PATH_PASSWD, O_RDONLY, 0);
	if (lockfd < 0) {
		(void)fprintf(stderr, "%s: %s: %s\n",
		    progname, _PATH_PASSWD, strerror(errno));
		exit(1);
	}
#if 0 /* flock()ing is superfluous here, with the ptmp/ptmptmp system. */
	if (flock(lockfd, LOCK_EX|LOCK_NB)) {
		(void)fprintf(stderr,
		    "%s: the password file is busy.\n", progname);
		exit(1);
	}
#endif

	if ((fd = open(_PATH_PTMPTMP, O_WRONLY|O_CREAT, 0644)) == -1) {
		(void)fprintf(stderr,
		    "%s: %s: %s\n", progname, _PATH_PTMPTMP, strerror(errno));
		exit(1);
	}
	ret = link(_PATH_PTMPTMP, _PATH_PTMP);
	(void)unlink(_PATH_PTMPTMP);
	if (ret == -1) {
	    if (errno == EEXIST)
		(void)fprintf(stderr, 
		    "%s: the password file is busy\n", progname);
	    else
		(void)fprintf(stderr, "%s: can't link %s: %s\n", progname,
		    _PATH_PTMP, strerror(errno));
	    exit(1);
	}
	copyfile(lockfd, fd);
	(void)close(lockfd);
	(void)close(fd);
	return(1);
}

void
pw_unlock()
{
	unlink(_PATH_PASSWD ".OLD");
	link(_PATH_PASSWD, _PATH_PASSWD ".OLD" );
	if (rename(_PATH_PTMP, _PATH_PASSWD) == -1) {
	    (void)fprintf(stderr, 
	    "%s: can't unlock %s: %s (your changes are still in %s)\n", 
	    progname, _PATH_PASSWD, strerror(errno), _PATH_PTMP);
	    exit(1);
	}
	(void)unlink(_PATH_PTMP);
}


void
pw_edit(notsetuid)
	int notsetuid;
{
	int pstat;
	pid_t pid;
	char *p, *editor;

	if (!(editor = getenv("EDITOR")))
		editor = _PATH_VI;
	if ((p = strrchr(strtok(editor," \t"), '/')) != NULL)
		++p;
	else 
		p = editor;

	if (!(pid = vfork())) {
		if (notsetuid) {
			(void)setgid(getgid());
			(void)setuid(getuid());
		}
		execlp(editor, p, _PATH_PTMP, NULL);
		_exit(1);
	}
	for (;;) {
	    pid = waitpid(pid, &pstat, WUNTRACED);
	    if (WIFSTOPPED(pstat)) {
		/* the editor suspended, so suspend us as well */
		kill(getpid(), SIGSTOP);
		kill(pid, SIGCONT);
	    } else {
		break;
	    }
	}
	if (pid == -1 || !WIFEXITED(pstat) || WEXITSTATUS(pstat) != 0)
		pw_error(editor, 1, 1);
}

void
pw_error(name, err, eval)
	char *name;
	int err, eval;
{
	int sverrno;

	if (err) {
		sverrno = errno;
		(void)fprintf(stderr, "%s: ", progname);
		if (name)
			(void)fprintf(stderr, "%s: ", name);
		(void)fprintf(stderr, "%s\n", strerror(sverrno));
	}
	(void)fprintf(stderr,
	    "%s: %s unchanged\n", progname, _PATH_PASSWD);
	(void)unlink(_PATH_PTMP);
	exit(eval);
}

main()
{
	register int pfd, tfd;
	struct stat begin, end;

	pw_init();
	pw_lock();

	if (stat(_PATH_PTMP, &begin))
		pw_error(_PATH_PTMP, 1, 1);
	pw_edit(0);
	if (stat(_PATH_PTMP, &end))
		pw_error(_PATH_PTMP, 1, 1);
	if (begin.st_mtime == end.st_mtime) {
		(void)fprintf(stderr, "vipw: no changes made\n");
		pw_error((char *)NULL, 0, 0);
	}
	pw_unlock();
	exit(0);
}
