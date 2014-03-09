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
 *
 *  1999-02-22 Arkadiusz Miśkiewicz <misiek@pld.ORG.PL>
 *  - added Native Language Support
 *
 *  1999-11-13 aeb Accept signal numers 128+s.
 *
 */

#include <ctype.h>		/* for isdigit() */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "c.h"
#include "closestream.h"
#include "nls.h"
#include "procutils.h"
#include "strutils.h"
#include "ttyutils.h"
#include "xalloc.h"

enum {
	KILL_FIELD_WIDTH = 11,
	KILL_OUTPUT_WIDTH = 72
};

struct kill_control {
	char *arg;
	pid_t pid;
	int numsig;
	unsigned int
		check_all:1,
		do_kill:1,
		do_pid:1;
};

struct signv {
	const char *name;
	int val;
} sys_signame[] = {
	/* POSIX signals */
	{ "HUP",	SIGHUP },	/* 1 */
	{ "INT",	SIGINT },	/* 2 */
	{ "QUIT",	SIGQUIT },	/* 3 */
	{ "ILL",	SIGILL },	/* 4 */
#ifdef SIGTRAP
	{ "TRAP",	SIGTRAP },	/* 5 */
#endif
	{ "ABRT",	SIGABRT },	/* 6 */
#ifdef SIGIOT
	{ "IOT",	SIGIOT },	/* 6, same as SIGABRT */
#endif
#ifdef SIGEMT
	{ "EMT",	SIGEMT },	/* 7 (mips,alpha,sparc*) */
#endif
#ifdef SIGBUS
	{ "BUS",	SIGBUS },	/* 7 (arm,i386,m68k,ppc), 10 (mips,alpha,sparc*) */
#endif
	{ "FPE",	SIGFPE },	/* 8 */
	{ "KILL",	SIGKILL },	/* 9 */
	{ "USR1",	SIGUSR1 },	/* 10 (arm,i386,m68k,ppc), 30 (alpha,sparc*), 16 (mips) */
	{ "SEGV",	SIGSEGV },	/* 11 */
	{ "USR2",	SIGUSR2 },	/* 12 (arm,i386,m68k,ppc), 31 (alpha,sparc*), 17 (mips) */
	{ "PIPE",	SIGPIPE },	/* 13 */
	{ "ALRM",	SIGALRM },	/* 14 */
	{ "TERM",	SIGTERM },	/* 15 */
#ifdef SIGSTKFLT
	{ "STKFLT",	SIGSTKFLT },	/* 16 (arm,i386,m68k,ppc) */
#endif
	{ "CHLD",	SIGCHLD },	/* 17 (arm,i386,m68k,ppc), 20 (alpha,sparc*), 18 (mips) */
#ifdef SIGCLD
	{ "CLD",	SIGCLD },	/* same as SIGCHLD (mips) */
#endif
	{ "CONT",	SIGCONT },	/* 18 (arm,i386,m68k,ppc), 19 (alpha,sparc*), 25 (mips) */
	{ "STOP",	SIGSTOP },	/* 19 (arm,i386,m68k,ppc), 17 (alpha,sparc*), 23 (mips) */
	{ "TSTP",	SIGTSTP },	/* 20 (arm,i386,m68k,ppc), 18 (alpha,sparc*), 24 (mips) */
	{ "TTIN",	SIGTTIN },	/* 21 (arm,i386,m68k,ppc,alpha,sparc*), 26 (mips) */
	{ "TTOU",	SIGTTOU },	/* 22 (arm,i386,m68k,ppc,alpha,sparc*), 27 (mips) */
#ifdef SIGURG
	{ "URG",	SIGURG },	/* 23 (arm,i386,m68k,ppc), 16 (alpha,sparc*), 21 (mips) */
#endif
#ifdef SIGXCPU
	{ "XCPU",	SIGXCPU },	/* 24 (arm,i386,m68k,ppc,alpha,sparc*), 30 (mips) */
#endif
#ifdef SIGXFSZ
	{ "XFSZ",	SIGXFSZ },	/* 25 (arm,i386,m68k,ppc,alpha,sparc*), 31 (mips) */
#endif
#ifdef SIGVTALRM
	{ "VTALRM",	SIGVTALRM },	/* 26 (arm,i386,m68k,ppc,alpha,sparc*), 28 (mips) */
#endif
#ifdef SIGPROF
	{ "PROF",	SIGPROF },	/* 27 (arm,i386,m68k,ppc,alpha,sparc*), 29 (mips) */
#endif
#ifdef SIGWINCH
	{ "WINCH",	SIGWINCH },	/* 28 (arm,i386,m68k,ppc,alpha,sparc*), 20 (mips) */
#endif
#ifdef SIGIO
	{ "IO",		SIGIO },	/* 29 (arm,i386,m68k,ppc), 23 (alpha,sparc*), 22 (mips) */
#endif
#ifdef SIGPOLL
	{ "POLL",	SIGPOLL },	/* same as SIGIO */
#endif
#ifdef SIGINFO
	{ "INFO",	SIGINFO },	/* 29 (alpha) */
#endif
#ifdef SIGLOST
	{ "LOST",	SIGLOST },	/* 29 (arm,i386,m68k,ppc,sparc*) */
#endif
#ifdef SIGPWR
	{ "PWR",	SIGPWR },	/* 30 (arm,i386,m68k,ppc), 29 (alpha,sparc*), 19 (mips) */
#endif
#ifdef SIGUNUSED
	{ "UNUSED",	SIGUNUSED },	/* 31 (arm,i386,m68k,ppc) */
#endif
#ifdef SIGSYS
	{ "SYS",	SIGSYS },	/* 31 (mips,alpha,sparc*) */
#endif
};

static int arg_to_signum(char *arg, int mask);
static void nosig(char *name);
static void printsig(const struct kill_control *ctl);
static void printsignals(FILE *fp, int pretty);
static void __attribute__((__noreturn__)) usage(FILE *out);
static int kill_verbose(const struct kill_control *ctl);

#ifdef HAVE_SIGQUEUE
static int use_sigval;
static union sigval sigdata;
#endif

int main(int argc, char **argv)
{
	struct kill_control ctl;
	char *arg;
	int errors = EXIT_SUCCESS;

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);
	memset(&ctl, 0, sizeof(ctl));

	ctl.numsig = SIGTERM;
	ctl.do_pid = (!strcmp(program_invocation_short_name, "pid"));	/* Yecch */
	if (ctl.do_pid)	/* FIXME: remove in March 2016.  */
		warnx(_("use of 'kill --pid' option as command name is deprecated"));
	/* Loop through the arguments.  Actually, -a is the only option
	 * can be used with other options.  The 'kill' is basically a
	 * one-option-at-most program. */
	for (argc--, argv++; 0 < argc; argc--, argv++) {
		arg = *argv;
		if (*arg != '-')
			break;
		if (!strcmp(arg, "--")) {
			argc--, argv++;
			break;
		}
		if (!strcmp(arg, "-v") || !strcmp(arg, "-V") ||
		    !strcmp(arg, "--version")) {
			printf(UTIL_LINUX_VERSION);
			return EXIT_SUCCESS;
		}
		if (!strcmp(arg, "-h") || !strcmp(arg, "--help"))
			usage(stdout);

		if (!strcmp(arg, "-a") || !strcmp(arg, "--all")) {
			ctl.check_all = 1;
			continue;
		}
		if (!strcmp(arg, "-l") || !strcmp(arg, "--list")) {
			if (argc < 2) {
				printsignals(stdout, 0);
				return EXIT_SUCCESS;
			}
			if (2 < argc)
				errx(EXIT_FAILURE, _("too many arguments"));
			/* argc == 2, accept "kill -l $?" */
			arg = argv[1];
			if ((ctl.numsig = arg_to_signum(arg, 1)) < 0)
				errx(EXIT_FAILURE, _("unknown signal: %s"),
				     arg);
			printsig(&ctl);
			return EXIT_SUCCESS;
		}
		/* for compatibility with procps kill(1) */
		if (!strncmp(arg, "--list=", 7) || !strncmp(arg, "-l=", 3)) {
			char *p = strchr(arg, '=') + 1;
			if ((ctl.numsig = arg_to_signum(p, 1)) < 0)
				errx(EXIT_FAILURE, _("unknown signal: %s"), p);
			printsig(&ctl);
			return EXIT_SUCCESS;
		}
		if (!strcmp(arg, "-L") || !strcmp(arg, "--table")) {
			printsignals(stdout, 1);
			return EXIT_SUCCESS;
		}
		if (!strcmp(arg, "-p") || !strcmp(arg, "--pid")) {
			ctl.do_pid = 1;
			if (ctl.do_kill)
				errx(EXIT_FAILURE, _("%s and %s are mutually exclusive"), "--pid", "--signal");
			continue;
		}
		if (!strcmp(arg, "-s") || !strcmp(arg, "--signal")) {
			if (argc < 2)
				errx(EXIT_FAILURE, _("not enough arguments"));
			ctl.do_kill = 1;
			if (ctl.do_pid)
				errx(EXIT_FAILURE, _("%s and %s are mutually exclusive"), "--pid", "--signal");
			argc--, argv++;
			arg = *argv;
			if ((ctl.numsig = arg_to_signum(arg, 0)) < 0) {
				nosig(arg);
				return EXIT_FAILURE;
			}
			continue;
		}
		if (!strcmp(arg, "-q") || !strcmp(arg, "--queue")) {
			if (argc < 2)
				errx(EXIT_FAILURE, _("option '%s' requires an argument"), arg);
			argc--, argv++;
			arg = *argv;
#ifdef HAVE_SIGQUEUE
			sigdata.sival_int =
			    strtos32_or_err(arg, _("invalid sigval argument"));
			use_sigval = 1;
#endif
			continue;
		}
		/* 'arg' begins with a dash but is not a known option.
		 * So it's probably something like -HUP, or -1/-n try to
		 * deal with it.
		 *
		 * -n could be signal n, or pid -n (i.e., process group
		 * number).  In case of doubt POSIX tells us to assume a
		 * signal.  If a signal has been parsed, assume it is a
		 * pid, break.  */
		if (ctl.do_kill)
			break;
		arg++;
		if ((ctl.numsig = arg_to_signum(arg, 0)) < 0)
			errx(EXIT_FAILURE, _("invalid sigval argument"));
		ctl.do_kill = 1;
		if (ctl.do_pid)
			errx(EXIT_FAILURE, _("%s and %s are mutually exclusive"), "--pid", "--signal");
		continue;
	}
	if (!*argv)
		errx(EXIT_FAILURE, _("not enough arguments"));
	if (ctl.do_pid)
		ctl.numsig = -1;

	/* We are done with the options.  The rest of the arguments
	 * should be process ids and names, kill them.  */
	for (/* nothing */; (ctl.arg = *argv) != NULL; argv++) {
		char *ep;

		ctl.pid = strtol(ctl.arg, &ep, 10);
		if (!*ep)
			errors |= kill_verbose(&ctl);
		else {
			struct proc_processes *ps = proc_open_processes();
			int ct = 0;

			if (!ps)
				continue;
			if (ctl.check_all)
				proc_processes_filter_by_uid(ps, getuid());
			proc_processes_filter_by_name(ps, ctl.arg);
			while (proc_next_pid(ps, &(ctl.pid)) == 0) {
				errors |= kill_verbose(&ctl);
				ct++;
			}
			if (!ct) {
				errors = EXIT_FAILURE;
				warnx(_("cannot find process \"%s\""), ctl.arg);
			}
			proc_close_processes(ps);
		}
	}
	return errors;
}

#ifdef SIGRTMIN
static int rtsig_to_signum(char *sig)
{
	int num, maxi = 0;
	char *ep = NULL;

	if (strncasecmp(sig, "min+", 4) == 0)
		sig += 4;
	else if (strncasecmp(sig, "max-", 4) == 0) {
		sig += 4;
		maxi = 1;
	}
	if (!isdigit(*sig))
		return -1;
	errno = 0;
	num = strtol(sig, &ep, 10);
	if (!ep || sig == ep || errno || num < 0)
		return -1;
	num = maxi ? SIGRTMAX - num : SIGRTMIN + num;
	if (num < SIGRTMIN || SIGRTMAX < num)
		return -1;
	return num;
}
#endif

static int signame_to_signum(char *sig)
{
	size_t n;

	if (!strncasecmp(sig, "sig", 3))
		sig += 3;
#ifdef SIGRTMIN
	/* RT signals */
	if (!strncasecmp(sig, "rt", 2))
		return rtsig_to_signum(sig + 2);
#endif
	/* Normal sugnals */
	for (n = 0; n < ARRAY_SIZE(sys_signame); n++) {
		if (!strcasecmp(sys_signame[n].name, sig))
			return sys_signame[n].val;
	}
	return (-1);
}

static int arg_to_signum(char *arg, int maskbit)
{
	int numsig;
	char *ep;

	if (isdigit(*arg)) {
		numsig = strtol(arg, &ep, 10);
		if (NSIG <= numsig && maskbit && (numsig & 128) != 0)
			numsig -= 128;
		if (*ep != 0 || numsig < 0 || NSIG <= numsig)
			return (-1);
		return (numsig);
	}
	return signame_to_signum(arg);
}

static void nosig(char *name)
{
	warnx(_("unknown signal %s; valid signals:"), name);
	printsignals(stderr, 1);
}

static void printsig(const struct kill_control *ctl)
{
	size_t n;

	for (n = 0; n < ARRAY_SIZE(sys_signame); n++) {
		if (sys_signame[n].val == ctl->numsig) {
			printf("%s\n", sys_signame[n].name);
			return;
		}
	}
#ifdef SIGRTMIN
	if (SIGRTMIN <= ctl->numsig && ctl->numsig <= SIGRTMAX) {
		printf("RT%d\n", ctl->numsig - SIGRTMIN);
		return;
	}
#endif
	printf("%d\n", ctl->numsig);
}

static void pretty_print_signal(FILE *fp, size_t term_width, size_t *lpos,
				int signum, const char *name)
{
	if (term_width < (*lpos + KILL_FIELD_WIDTH)) {
		fputc('\n', fp);
		*lpos = 0;
	}
	*lpos += KILL_FIELD_WIDTH;
	fprintf(fp, "%2d %-8s", signum, name);
}

static void printsignals(FILE *fp, int pretty)
{
	size_t n, lth, lpos = 0, width;

	if (!pretty) {
		for (n = 0; n < ARRAY_SIZE(sys_signame); n++) {
			lth = 1 + strlen(sys_signame[n].name);
			if (KILL_OUTPUT_WIDTH < lpos + lth) {
				fputc('\n', fp);
				lpos = 0;
			} else if (lpos)
				fputc(' ', fp);
			lpos += lth;
			fputs(sys_signame[n].name, fp);
		}
#ifdef SIGRTMIN
		fputs(" RT<N> RTMIN+<N> RTMAX-<N>", fp);
#endif
		fputc('\n', fp);
		return;
	}
	/* pretty print */
	width = get_terminal_width();
	if (width == 0)
		width = KILL_OUTPUT_WIDTH;
	else
		width -= 1;
	for (n = 0; n < ARRAY_SIZE(sys_signame); n++)
		pretty_print_signal(fp, width, &lpos,
				    sys_signame[n].val, sys_signame[n].name);
#ifdef SIGRTMIN
	pretty_print_signal(fp, width, &lpos, SIGRTMIN, "RTMIN");
	pretty_print_signal(fp, width, &lpos, SIGRTMAX, "RTMAX");
#endif
	fputc('\n', fp);
}

static void __attribute__((__noreturn__)) usage(FILE *out)
{
	fputs(USAGE_HEADER, out);
	fprintf(out, _(" %s [options] <pid|name> [...]\n"), program_invocation_short_name);
	fputs(USAGE_OPTIONS, out);
	fputs(_(" -a, --all              do not restrict the name-to-pid conversion to processes\n"
		"                        with the same uid as the present process\n"), out);
	fputs(_(" -s, --signal <sig>     send specified signal\n"), out);
	fputs(_(" -q, --queue <sig>      use sigqueue(2) rather than kill(2)\n"), out);
	fputs(_(" -p, --pid              print pids without signaling them\n"), out);
	fputs(_(" -l, --list [=<signal>] list signal names, or convert one to a name\n"), out);
	fputs(_(" -L, --table            list signal names and numbers\n"), out);
	fputs(USAGE_SEPARATOR, out);
	fputs(USAGE_HELP, out);
	fputs(USAGE_VERSION, out);
	fprintf(out, USAGE_MAN_TAIL("kill(1)"));
	exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}

static int kill_verbose(const struct kill_control *ctl)
{
	int rc = 0;

	if (ctl->numsig < 0) {
		printf("%ld\n", (long) ctl->pid);
		return 0;
	}
#ifdef HAVE_SIGQUEUE
	if (use_sigval)
		rc = sigqueue(ctl->pid, ctl->numsig, sigdata);
	else
#endif
		rc = kill(ctl->pid, ctl->numsig);
	if (rc < 0) {
		warn(_("sending signal to %s failed"), ctl->arg);
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}
