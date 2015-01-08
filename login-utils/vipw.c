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
 * Updated Thu Nov  9 21:58:53 1995 by Martin Schulze
 * <joey@finlandia.infodrom.north.de>.  Support for vigr.
 *
 * Martin Schulze's patches adapted to Util-Linux by Nicolai Langfeldt.
 *
 * 1999-02-22 Arkadiusz Miśkiewicz <misiek@pld.ORG.PL>
 * - added Native Language Support
 * Sun Mar 21 1999 - Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 * - fixed strerr(errno) in gettext calls
 */

#include <errno.h>
#include <fcntl.h>
#include <paths.h>
#include <pwd.h>
#include <shadow.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/param.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "c.h"
#include "fileutils.h"
#include "closestream.h"
#include "nls.h"
#include "setpwnam.h"
#include "strutils.h"
#include "xalloc.h"
#include "rpmatch.h"

#ifdef HAVE_LIBSELINUX
# include <selinux/selinux.h>
#endif

#define FILENAMELEN 67

enum {
	VIPW,
	VIGR
};
int program;
char orig_file[FILENAMELEN];	/* original file /etc/passwd or /etc/group */
char *tmp_file;			/* tmp file */

void pw_error __P((char *, int, int));

static void copyfile(int from, int to)
{
	int nr, nw, off;
	char buf[8 * 1024];

	while ((nr = read(from, buf, sizeof(buf))) > 0)
		for (off = 0; off < nr; nr -= nw, off += nw)
			if ((nw = write(to, buf + off, nr)) < 0)
				pw_error(tmp_file, 1, 1);

	if (nr < 0)
		pw_error(orig_file, 1, 1);
}

static void pw_init(void)
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

static FILE * pw_tmpfile(int lockfd)
{
	FILE *fd;
	char *tmpname = NULL;
	char *dir = "/etc";

	if ((fd = xfmkstemp(&tmpname, dir)) == NULL) {
		ulckpwdf();
		err(EXIT_FAILURE, _("can't open temporary file"));
	}

	copyfile(lockfd, fileno(fd));
	tmp_file = tmpname;
	return fd;
}

static void pw_write(void)
{
	char tmp[FILENAMELEN + 4];

	sprintf(tmp, "%s%s", orig_file, ".OLD");
	unlink(tmp);

	if (link(orig_file, tmp))
		warn(_("%s: create a link to %s failed"), orig_file, tmp);

#ifdef HAVE_LIBSELINUX
	if (is_selinux_enabled() > 0) {
		security_context_t passwd_context = NULL;
		int ret = 0;
		if (getfilecon(orig_file, &passwd_context) < 0) {
			warnx(_("Can't get context for %s"), orig_file);
			pw_error(orig_file, 1, 1);
		}
		ret = setfilecon(tmp_file, passwd_context);
		freecon(passwd_context);
		if (ret != 0) {
			warnx(_("Can't set context for %s"), tmp_file);
			pw_error(tmp_file, 1, 1);
		}
	}
#endif

	if (rename(tmp_file, orig_file) == -1) {
		int errsv = errno;
		errx(EXIT_FAILURE,
		     ("cannot write %s: %s (your changes are still in %s)"),
		     orig_file, strerror(errsv), tmp_file);
	}
	unlink(tmp_file);
	free(tmp_file);
}

static void pw_edit(void)
{
	int pstat;
	pid_t pid;
	char *p, *editor, *tk;

	editor = getenv("EDITOR");
	editor = xstrdup(editor ? editor : _PATH_VI);

	tk = strtok(editor, " \t");
	if (tk && (p = strrchr(tk, '/')) != NULL)
		++p;
	else
		p = editor;

	pid = fork();
	if (pid < 0)
		err(EXIT_FAILURE, _("fork failed"));

	if (!pid) {
		execlp(editor, p, tmp_file, NULL);
		/* Shouldn't get here */
		_exit(EXIT_FAILURE);
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

	free(editor);
}

void __attribute__((__noreturn__))
pw_error(char *name, int err, int eval)
{
	if (err) {
		if (name)
			warn("%s: ", name);
		else
			warn(NULL);
	}
	warnx(_("%s unchanged"), orig_file);
	unlink(tmp_file);
	ulckpwdf();
	exit(eval);
}

static void edit_file(int is_shadow)
{
	struct stat begin, end;
	int passwd_file, ch_ret;
	FILE *tmp_fd;

	pw_init();

	/* acquire exclusive lock */
	if (lckpwdf() < 0)
		err(EXIT_FAILURE, _("cannot get lock"));

	passwd_file = open(orig_file, O_RDONLY, 0);
	if (passwd_file < 0)
		err(EXIT_FAILURE, _("cannot open %s"), orig_file);
	tmp_fd = pw_tmpfile(passwd_file);

	if (fstat(fileno(tmp_fd), &begin))
		pw_error(tmp_file, 1, 1);

	pw_edit();

	if (fstat(fileno(tmp_fd), &end))
		pw_error(tmp_file, 1, 1);
	/* Some editors, such as Vim with 'writebackup' mode enabled,
	 * use "atomic save" in which the old file is deleted and a new
	 * one with the same name created in its place.  */
	if (end.st_nlink == 0) {
		if (close_stream(tmp_fd) != 0)
			err(EXIT_FAILURE, _("write error"));
		tmp_fd = fopen(tmp_file, "r");
		if (!tmp_file)
			err(EXIT_FAILURE, _("cannot open %s"), tmp_file);
		if (fstat(fileno(tmp_fd), &end))
			pw_error(tmp_file, 1, 1);
	}
	if (begin.st_mtime == end.st_mtime) {
		warnx(_("no changes made"));
		pw_error((char *)NULL, 0, 0);
	}
	/* pw_tmpfile() will create the file with mode 600 */
	if (!is_shadow)
		ch_ret = fchmod(fileno(tmp_fd), 0644);
	else
		ch_ret = fchmod(fileno(tmp_fd), 0400);
	if (ch_ret < 0)
		err(EXIT_FAILURE, "%s: %s", _("cannot chmod file"), orig_file);
	if (close_stream(tmp_fd) != 0)
		err(EXIT_FAILURE, _("write error"));
	pw_write();
	close(passwd_file);
	ulckpwdf();
}

static void __attribute__((__noreturn__)) usage(FILE *out)
{
	fputs(USAGE_HEADER, out);
	fprintf(out, " %s\n", program_invocation_short_name);

	fputs(USAGE_SEPARATOR, out);
	fputs(_("Edit the password or group file.\n"), out);

	fputs(USAGE_OPTIONS, out);
	fputs(USAGE_HELP, out);
	fputs(USAGE_VERSION, out);
	fprintf(out, USAGE_MAN_TAIL("vipw(8)"));
	exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}

int main(int argc, char *argv[])
{
	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	if (!strcmp(program_invocation_short_name, "vigr")) {
		program = VIGR;
		xstrncpy(orig_file, GROUP_FILE, sizeof(orig_file));
	} else {
		program = VIPW;
		xstrncpy(orig_file, PASSWD_FILE, sizeof(orig_file));
	}

	if (1 < argc) {
		if (!strcmp(argv[1], "-V") || !strcmp(argv[1], "--version")) {
			printf(UTIL_LINUX_VERSION);
			exit(EXIT_SUCCESS);
		}
		if (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help"))
			usage(stdout);
		usage(stderr);
	}

	edit_file(0);

	if (program == VIGR) {
		strncpy(orig_file, SGROUP_FILE, FILENAMELEN - 1);
	} else {
		strncpy(orig_file, SHADOW_FILE, FILENAMELEN - 1);
	}

	if (access(orig_file, F_OK) == 0) {
		char response[80];

		printf((program == VIGR)
		       ? _("You are using shadow groups on this system.\n")
		       : _("You are using shadow passwords on this system.\n"));
		/* TRANSLATORS: this program uses for y and n rpmatch(3),
		 * which means they can be translated. */
		printf(_("Would you like to edit %s now [y/n]? "), orig_file);

		if (fgets(response, sizeof(response), stdin)) {
			if (rpmatch(response) == 1)
				edit_file(1);
		}
	}
	exit(EXIT_SUCCESS);
}
