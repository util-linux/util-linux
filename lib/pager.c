/*
 * Based on linux-perf/git scm
 *
 * Some modifications and simplifications for util-linux
 * by Davidlohr Bueso <dave@xxxxxxx> - March 2012.
 */

#include <unistd.h>
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

#define NULL_DEVICE	"/dev/null"

void setup_pager(void);

static const char *pager_argv[] = { "sh", "-c", NULL, NULL };

struct child_process {
	const char **argv;
	pid_t pid;
	int in;
	int out;
	int err;
	unsigned no_stdin:1;
	void (*preexec_cb)(void);
};
static struct child_process pager_process;

static inline void close_pair(int fd[2])
{
	close(fd[0]);
	close(fd[1]);
}

static int start_command(struct child_process *cmd)
{
	int need_in;
	int fdin[2];

	/*
	 * In case of errors we must keep the promise to close FDs
	 * that have been passed in via ->in and ->out.
	 */
	need_in = !cmd->no_stdin && cmd->in < 0;
	if (need_in) {
		if (pipe(fdin) < 0) {
			if (cmd->out > 0)
				close(cmd->out);
			return -1;
		}
		cmd->in = fdin[1];
	}

	fflush(NULL);
	cmd->pid = fork();
	if (!cmd->pid) {
		if (need_in) {
			dup2(fdin[0], STDIN_FILENO);
			close_pair(fdin);
		} else if (cmd->in > 0) {
			dup2(cmd->in, STDIN_FILENO);
			close(cmd->in);
		}

		cmd->preexec_cb();
		execvp(cmd->argv[0], (char *const*) cmd->argv);
		exit(127); /* cmd not found */
	}

	if (cmd->pid < 0) {
		if (need_in)
			close_pair(fdin);
		else if (cmd->in)
			close(cmd->in);
		return -1;
	}

	if (need_in)
		close(fdin[0]);
	else if (cmd->in)
		close(cmd->in);
	return 0;
}

static int wait_or_whine(pid_t pid)
{
	for (;;) {
		int status, code;
		pid_t waiting = waitpid(pid, &status, 0);

		if (waiting < 0) {
			if (errno == EINTR)
				continue;
			err(EXIT_FAILURE, _("waitpid failed (%s)"), strerror(errno));
		}
		if (waiting != pid)
			return -1;
		if (WIFSIGNALED(status))
			return -1;

		if (!WIFEXITED(status))
			return -1;
		code = WEXITSTATUS(status);
		switch (code) {
		case 127:
			return -1;
		case 0:
			return 0;
		default:
			return -1;
		}
	}
}

static int finish_command(struct child_process *cmd)
{
	return wait_or_whine(cmd->pid);
}

static void pager_preexec(void)
{
	/*
	 * Work around bug in "less" by not starting it until we
	 * have real input
	 */
	fd_set in;

	FD_ZERO(&in);
	FD_SET(STDIN_FILENO, &in);
	select(1, &in, NULL, &in, NULL);

	setenv("LESS", "FRSX", 0);
}

static void wait_for_pager(void)
{
	fflush(stdout);
	fflush(stderr);
	/* signal EOF to pager */
	close(STDOUT_FILENO);
	close(STDERR_FILENO);
	finish_command(&pager_process);
}

static void wait_for_pager_signal(int signo)
{
	wait_for_pager();
	raise(signo);
}

void setup_pager(void)
{
	const char *pager = getenv("PAGER");

	if (!isatty(STDOUT_FILENO))
		return;

	if (!pager)
		pager = "less";
	else if (!*pager || !strcmp(pager, "cat"))
		return;

	/* spawn the pager */
	pager_argv[2] = pager;
	pager_process.argv = pager_argv;
	pager_process.in = -1;
	pager_process.preexec_cb = pager_preexec;

	if (start_command(&pager_process))
		return;

	/* original process continues, but writes to the pipe */
	dup2(pager_process.in, STDOUT_FILENO);
	if (isatty(STDERR_FILENO))
		dup2(pager_process.in, STDERR_FILENO);
	close(pager_process.in);

	/* this makes sure that the parent terminates after the pager */
	signal(SIGINT, wait_for_pager_signal);
	signal(SIGHUP, wait_for_pager_signal);
	signal(SIGTERM, wait_for_pager_signal);
	signal(SIGQUIT, wait_for_pager_signal);
	signal(SIGPIPE, wait_for_pager_signal);

	atexit(wait_for_pager);
}

#ifdef TEST_PROGRAM

#define MAX 255

int main(int argc __attribute__ ((__unused__)),
	 char *argv[] __attribute__ ((__unused__)))
{
	int i;

	setup_pager();
	for (i = 0; i < MAX; i++)
		printf("%d\n", i);
	return EXIT_SUCCESS;
}
#endif /* TEST_PROGRAM */
