/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * test_sigstate - ...
 *
 * Written by Masatake YAMATO <yamato@redhat.com>
 */

#include "c.h"

#include <signal.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <err.h>

#define _U_ __attribute__((__unused__))

static enum {
	HANDLER_NONE,
	HANDLER_WRITE,
	HANDLER_READ
} handler_state = HANDLER_NONE;

static int ERRNO;

static void handler(int signum _U_)
{
	char c;

	if (write(1, "USR1\n", 5) == -1) {
		handler_state = HANDLER_WRITE;
		goto out;
	}

	if (read(0, &c, 1) != -1)
		_exit(0);
	handler_state = HANDLER_READ;

 out:
	ERRNO = errno;
}

int main(int argc _U_, char **argv _U_)
{
	sigset_t block_set;

	sigemptyset(&block_set);
	sigaddset(&block_set, SIGINT);
	sigaddset(&block_set, SIGILL);
	sigaddset(&block_set, SIGABRT);
	sigaddset(&block_set, SIGFPE);
	sigaddset(&block_set, SIGSEGV);
	sigaddset(&block_set, SIGTERM);

	if (sigprocmask(SIG_SETMASK, &block_set, NULL) == -1)
		err(EXIT_FAILURE, "failed to mask signals");

	raise(SIGINT);
	raise(SIGILL);

#define sigignore(S) if (signal(S, SIG_IGN) == SIG_ERR) \
		err(EXIT_FAILURE, "failed to make " #S "ignored")

	sigignore(SIGHUP);
	sigignore(SIGQUIT);
	sigignore(SIGTRAP);
	sigignore(SIGPIPE);
	sigignore(SIGALRM);

	signal(SIGBUS, SIG_DFL);
	signal(SIGFPE, SIG_DFL);
	signal(SIGSEGV, SIG_DFL);
	signal(32, SIG_DFL);
	signal(33, SIG_DFL);

	if (signal(SIGUSR1, handler) == SIG_ERR)
		err(EXIT_FAILURE, "failed to set a signal handler for SIGUSR1");
	if (signal(SIGILL, handler) == SIG_ERR)
		err(EXIT_FAILURE, "failed to set a signal handler for SIGILL");

	printf("%d\n", getpid());
	if (fflush(stdout) == EOF)
		err(EXIT_FAILURE, "failed to flush stdout");

	pause();
	if (ERRNO == 0)
		errx(EXIT_FAILURE, "caught an unexpected signal");
	errno = ERRNO;
	errx(EXIT_FAILURE, "failed in %s an ack from the command invoker",
	     handler_state == HANDLER_WRITE? "writing": "reading");

	return 0;		/* UNREACHABLE */
}
