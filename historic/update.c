/*
 * update.c -- periodically sync the filesystems to disk
 */

#include <sys/types.h>
#include <unistd.h>
#include <limits.h>
#include <signal.h>

void alarm_handler(int sig)
{
}

int main(int argc, char *argv[])
{
	int i;
	int interval;
	struct sigaction sa;
	sigset_t empty_set;
	sigset_t alarm_set;

	interval = (argc > 1) ? atoi(argv[1]) : 30;
	if (fork() > 0)
		exit(0);
	chdir("/");
	for (i = 0; i < OPEN_MAX; i++)
		close(i);
	setsid();
	sa.sa_handler = SIG_IGN;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);
	sa.sa_handler = alarm_handler;
	sigaction(SIGALRM, &sa, NULL);
	sigemptyset(&empty_set);
	sigemptyset(&alarm_set);
	sigaddset(&alarm_set, SIGALRM);
	sigprocmask(SIG_BLOCK, &alarm_set, NULL);
	for (;;) {
		alarm(interval);
		sigsuspend(&empty_set);
		sync();
	}
}
