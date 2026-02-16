/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file may be redistributed under the terms of the
 * GNU General Public License.
 *
 * Based on linux-perf/git scm
 *
 * Some modifications and simplifications for util-linux
 * by Davidlohr Bueso <dave@xxxxxxx> - March 2012.
 */

#include <unistd.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <err.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <signal.h>

#include "c.h"
#include "xalloc.h"
#include "nls.h"
#include "ttyutils.h"
#include "pager.h"

static const char *pager_argv[] = { "sh", "-c", NULL, NULL };

struct child_process {
	const char **argv;
	pid_t pid;
	int in;

	int org_err;
	int org_out;
	struct sigaction orig_sigchld;
	struct sigaction orig_sigint;
	struct sigaction orig_sighup;
	struct sigaction orig_sigterm;
	struct sigaction orig_sigquit;
	struct sigaction orig_sigpipe;
};
static struct child_process pager_process;

static volatile sig_atomic_t pager_caught_signal;

static inline void close_pair(int fd[2])
{
	close(fd[0]);
	close(fd[1]);
}

static void pager_preexec(void)
{
	/*
	 * Work around bug in "less" by not starting it until we
	 * have real input
	 */
	fd_set in, ex;

	FD_ZERO(&in);
	FD_SET(STDIN_FILENO, &in);
	ex = in;

	select(STDIN_FILENO + 1, &in, NULL, &ex, NULL);

	if (setenv("LESS", "FRSX", 0) != 0)
		warn(_("failed to set the %s environment variable"), "LESS");
}

static int start_command(struct child_process *cmd)
{
	int fdin[2];

	if (pipe(fdin) < 0)
		return -1;
	cmd->in = fdin[1];

	fflush(NULL);
	cmd->pid = fork();
	if (!cmd->pid) {
		dup2(fdin[0], STDIN_FILENO);
		close_pair(fdin);

		pager_preexec();
		execvp(cmd->argv[0], (char *const*) cmd->argv);
		errexec(cmd->argv[0]);
	}

	if (cmd->pid < 0) {
		close_pair(fdin);
		return -1;
	}

	close(fdin[0]);
	return 0;
}

static void wait_for_pager(void)
{
	pid_t waiting;

	if (!pager_process.pid)
		return;

	do {
		waiting = waitpid(pager_process.pid, NULL, 0);
		if (waiting == -1 && errno != EINTR)
			ul_sig_err(EXIT_FAILURE, "waitpid failed");
	} while (waiting == -1);
}

static void catch_signal(int signo)
{
	pager_caught_signal = signo;
}

static void wait_for_pager_signal(int signo __attribute__ ((__unused__)))
{
	UL_PROTECT_ERRNO;

	/* signal EOF to pager */
	close(STDOUT_FILENO);
	close(STDERR_FILENO);

	wait_for_pager();
	_exit(EXIT_FAILURE);
}

static int has_command(const char *cmd)
{
	const char *path;
	char *b, *c, *p, *s;
	int rc = 0;

	if (!cmd)
		goto done;

	c = xstrdup(cmd);
	if (!c)
		goto done;
	b = strtok(c, " ");	/* cmd may contain options */
	if (!b)
		goto cleanup;

	if (strchr(b, '/')) {
		rc = access(b, X_OK) == 0;
		goto cleanup;
	}

	path = getenv("PATH");
	if (!path)
		goto cleanup;
	p = xstrdup(path);
	if (!p)
		goto cleanup;

	for (s = strtok(p, ":"); s; s = strtok(NULL, ":")) {
		int fd = open(s, O_RDONLY|O_CLOEXEC);
		if (fd < 0)
			continue;
		rc = faccessat(fd, b, X_OK, 0) == 0;
		close(fd);
		if (rc)
			break;
	}
	free(p);
cleanup:
	free(c);
done:
	/*fprintf(stderr, "has PAGER '%s': rc=%d\n", cmd, rc);*/
	return rc;
}

static void __setup_pager(void)
{
	const char *pager = getenv("PAGER");
	struct sigaction sa;

	if (!isatty(STDOUT_FILENO))
		return;

	if (!pager)
		pager = "less";
	else if (!*pager || !strcmp(pager, "cat"))
		return;

	if (!has_command(pager))
		return;

	/* spawn the pager */
	pager_argv[2] = pager;
	pager_process.argv = pager_argv;
	pager_process.in = -1;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = SIG_DFL;

	/* this makes sure that waitpid works as expected */
	sigaction(SIGCHLD, &sa, &pager_process.orig_sigchld);

	if (start_command(&pager_process))
		return;

	/* original process continues, but writes to the pipe */
	dup2(pager_process.in, STDOUT_FILENO);
	setvbuf(stdout, NULL, _IOLBF, 0);
	if (isatty(STDERR_FILENO)) {
		dup2(pager_process.in, STDERR_FILENO);
		setvbuf(stderr, NULL, _IOLBF, 0);
	}
	close(pager_process.in);

	sa.sa_handler = wait_for_pager_signal;

	/* this makes sure that the parent terminates after the pager */
	sigaction(SIGINT,  &sa, &pager_process.orig_sigint);
	sigaction(SIGHUP,  &sa, &pager_process.orig_sighup);
	sigaction(SIGTERM, &sa, &pager_process.orig_sigterm);
	sigaction(SIGQUIT, &sa, &pager_process.orig_sigquit);
	sigaction(SIGPIPE, &sa, &pager_process.orig_sigpipe);
}

/* Setup pager and redirect output to the $PAGER. The pager is closed at exit.
 */
void pager_redirect(void)
{
	if (pager_process.pid)
		return;		/* already running */

	pager_open();

	atexit(pager_close);
}

/* Setup pager and redirect output, the pager may be closed by pager_close().
 */
void pager_open(void)
{
	if (pager_process.pid)
		return;		/* already running */

	pager_process.org_out = dup(STDOUT_FILENO);
	pager_process.org_err = dup(STDERR_FILENO);

	__setup_pager();

	if (!pager_process.pid) {
		close(pager_process.org_out);
		close(pager_process.org_err);
		memset(&pager_process, 0, sizeof(pager_process));
	}
}

/* Close pager and restore original std{out,err}.
 */
void pager_close(void)
{
	struct sigaction sa;

	if (pager_process.pid == 0)
		return;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = catch_signal;

	/* set flag instead of calling wait_for_pager again */
	sigaction(SIGINT,  &sa, NULL);
	sigaction(SIGHUP,  &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGQUIT, &sa, NULL);
	sigaction(SIGPIPE, &sa, NULL);

	fflush(NULL);

	/* signal EOF to pager */
	close(STDOUT_FILENO);
	close(STDERR_FILENO);

	/* restore original output */
	clearerr(stdout);
	dup2(pager_process.org_out, STDOUT_FILENO);
	clearerr(stderr);
	dup2(pager_process.org_err, STDERR_FILENO);

	close(pager_process.org_out);
	close(pager_process.org_err);

	wait_for_pager();

	/* restore original signal settings */
	sigaction(SIGCHLD, &pager_process.orig_sigchld, NULL);
	sigaction(SIGINT,  &pager_process.orig_sigint, NULL);
	sigaction(SIGHUP,  &pager_process.orig_sighup, NULL);
	sigaction(SIGTERM, &pager_process.orig_sigterm, NULL);
	sigaction(SIGQUIT, &pager_process.orig_sigquit, NULL);
	sigaction(SIGPIPE, &pager_process.orig_sigpipe, NULL);

	if (pager_caught_signal)
		exit(EXIT_FAILURE);

	memset(&pager_process, 0, sizeof(pager_process));
	pager_caught_signal = 0;
}

#ifdef TEST_PROGRAM_PAGER

#define MAX 255

int main(int argc __attribute__ ((__unused__)),
	 char *argv[] __attribute__ ((__unused__)))
{
	int i;

	pager_redirect();
	for (i = 0; i < MAX; i++)
		printf("%d\n", i);
	return EXIT_SUCCESS;
}
#endif /* TEST_PROGRAM_PAGER */
