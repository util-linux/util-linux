/*
 * Copyright (c) 1988, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
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
 */
/*
 *  oct 5 1994 -- almost entirely re-written to allow for process names.
 *  modifications (c) salvatore valente <svalente@mit.edu>
 *  may be used / modified / distributed under the same terms as the original.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <signal.h>

#ifdef __linux__
/*
 *  sys_signame -- an ordered list of signals.
 *  lifted from /usr/include/linux/signal.h
 *  this particular order is only correct for linux.
 *  this is _not_ portable.
 */
char *sys_signame[NSIG] = {
    "zero",  "HUP",  "INT",   "QUIT", "ILL",   "TRAP", "IOT",  "UNUSED",
    "FPE",   "KILL", "USR1",  "SEGV", "USR2",  "PIPE", "ALRM", "TERM",
    "STKFLT","CHLD", "CONT",  "STOP", "TSTP",  "TTIN", "TTOU", "IO",
    "XCPU",  "XFSZ", "VTALRM","PROF", "WINCH", NULL
};
#endif

int main (int argc, char *argv[]);
extern char *mybasename(char *);
int signame_to_signum (char *sig);
int arg_to_signum (char *arg);
void nosig (char *name);
void printsig (int sig);
void printsignals (FILE *fp);
int usage (int status);
int kill_verbose (char *procname, int pid, int sig);

extern int *get_pids (char *, int);

char version_string[] = "kill v2.0\n";
char *whoami;

int main (int argc, char *argv[])
{
    int errors, numsig, pid;
    char *ep, *arg;
    int do_pid, do_kill, check_all;
    int *pids, *ip;

    whoami = mybasename (*argv);
    numsig = SIGTERM;
    do_pid = (! strcmp (whoami, "pid"));
    do_kill = 0;
    check_all = 0;

    /*  loop through the arguments.
	actually, -a is the only option can be used with other options.
	`kill' is basically a one-option-at-most program.  */
    for (argc--, argv++; argc > 0; argc--, argv++) {
	arg = *argv;
	if (*arg != '-') {
	    break;
	}
	if (! strcmp (arg, "-u")) {
	    return usage (0);
	}
	if (! strcmp (arg, "-v")) {
	    fputs (version_string, stdout);
	    return 0;
	}
	if (! strcmp (arg, "-a")) {
	    check_all++;
	    continue;
	}
	if (! strcmp (arg, "-l")) {
	    if (argc < 2) {
		printsignals (stdout);
		return 0;
	    }
	    if (argc > 2) {
		return usage (1);
	    }
	    /* argc == 2 */
	    arg = argv[1];
	    if ((numsig = arg_to_signum (arg)) < 0) {
		fprintf (stderr, "%s: unknown signal %s\n", whoami, arg);
		return 1;
	    }
	    printsig (numsig);
	    return 0;
	}
	if (! strcmp (arg, "-p")) {
	    do_pid++;
	    if (do_kill)
		return usage (1);
	    continue;
	}
	if (! strcmp (arg, "-s")) {
	    if (argc < 2) {
		return usage (1);
	    }
	    do_kill++;
	    if (do_pid)
		return usage (1);
	    argc--, argv++;
	    arg = *argv;
	    if ((numsig = arg_to_signum (arg)) < 0) {
		nosig (arg);
		return 1;
	    }
	    continue;
	}
	/*  `arg' begins with a dash but is not a known option.
	    so it's probably something like -HUP, or -1/-n
	    try to deal with it.
	    -n could be signal n, or pid -n (i.e. process group n).
	    If a signal has been parsed, assume it's a pid, break */
	if (do_kill)
	  break;
	arg++;
	if ((numsig = arg_to_signum (arg)) < 0) {
	    return usage (1);
	}
	do_kill++;
	if (do_pid)
	    return usage (1);
	continue;
    }

    if (! *argv) {
	return usage (1);
    }
    if (do_pid) {
	numsig = -1;
    }

    /*  we're done with the options.
	the rest of the arguments should be process ids and names.
	kill them.  */
    for (errors = 0; (arg = *argv) != NULL; argv++) {
	pid = strtol (arg, &ep, 10);
	if (! *ep)
	    errors += kill_verbose (arg, pid, numsig);
	else {
	    pids = get_pids (arg, check_all);
	    if (! pids) {
		errors++;
		fprintf (stderr, "%s: can't find process \"%s\"\n",
			 whoami, arg);
		continue;
	    }
	    for (ip = pids; *ip >= 0; ip++)
		errors += kill_verbose (arg, *ip, numsig);
	    free (pids);
	}
    }
    return (errors);
}


int signame_to_signum (char *sig)
{
    int n;

    if (! strncasecmp (sig, "sig", 3))
	sig += 3;
    for (n = 1; (n < NSIG) && (sys_signame[n] != NULL); n++) {
	if (! strcasecmp (sys_signame[n], sig))
	    return n;
    }
    return (-1);
}

int arg_to_signum (char *arg)
{
    int numsig;
    char *ep;

    if (isdigit (*arg)) {
	numsig = strtol (arg, &ep, 10);
	if (*ep != 0 || numsig < 0 || numsig >= NSIG)
	    return (-1);
	return (numsig);
    }
    return (signame_to_signum (arg));
}

void nosig (char *name)
{
    fprintf (stderr, "%s: unknown signal %s; valid signals:\n", whoami, name);
    printsignals (stderr);
}

void printsig (int sig)
{
    printf ("%s\n", sys_signame[sig]);
}

void printsignals (FILE *fp)
{
    int n;

    for (n = 1; (n < NSIG) && (sys_signame[n] != NULL); n++) {
	fputs (sys_signame[n], fp);
	if (n == (NSIG / 2) || n == (NSIG - 1))
	    fputc ('\n', fp);
	else
	    fputc (' ', fp);
    }
    if (n < (NSIG - 1))
	fputc ('\n', fp);
}

int usage (int status)
{
    FILE *fp;

    fp = (status == 0 ? stdout : stderr);
    fprintf (fp, "usage: %s [ -s signal | -p ] [ -a ] pid ...\n", whoami);
    fprintf (fp, "       %s -l [ signal ]\n", whoami);
    return status;
}

int kill_verbose (char *procname, int pid, int sig)
{
    if (sig < 0) {
	printf ("%d\n", pid);
	return 0;
    }
    if (kill (pid, sig) < 0) {
	fprintf (stderr, "%s ", whoami);
	perror (procname);
	return 1;
    }
    return 0;
}
