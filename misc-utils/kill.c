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
 *  1999-11-13 aeb Accept signal numbers 128+s.
 *
 * Copyright (C) 2014 Sami Kerola <kerolasa@iki.fi>
 * Copyright (C) 2014 Karel Zak <kzak@redhat.com>
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
#include "signames.h"
#include "strutils.h"
#include "ttyutils.h"
#include "xalloc.h"

/* partial success, otherwise we return regular EXIT_{SUCCESS,FAILURE} */
#define KILL_EXIT_SOMEOK	64

enum {
	KILL_FIELD_WIDTH = 11,
	KILL_OUTPUT_WIDTH = 72
};

struct kill_control {
	char *arg;
	pid_t pid;
	int numsig;
#ifdef HAVE_SIGQUEUE
	union sigval sigdata;
#endif
	unsigned int
		check_all:1,
		do_kill:1,
		do_pid:1,
		use_sigval:1,
		verbose:1;
};

static void print_signal_name(int signum)
{
	size_t n;

	for (n = 0; n < ARRAY_SIZE(sys_signame); n++) {
		if (sys_signame[n].val == signum) {
			printf("%s\n", sys_signame[n].name);
			return;
		}
	}
#ifdef SIGRTMIN
	if (SIGRTMIN <= signum && signum <= SIGRTMAX) {
		printf("RT%d\n", signum - SIGRTMIN);
		return;
	}
#endif
	printf("%d\n", signum);
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

static void print_all_signals(FILE *fp, int pretty)
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
	width = get_terminal_width(KILL_OUTPUT_WIDTH + 1) - 1;
	for (n = 0; n < ARRAY_SIZE(sys_signame); n++)
		pretty_print_signal(fp, width, &lpos,
				    sys_signame[n].val, sys_signame[n].name);
#ifdef SIGRTMIN
	pretty_print_signal(fp, width, &lpos, SIGRTMIN, "RTMIN");
	pretty_print_signal(fp, width, &lpos, SIGRTMAX, "RTMAX");
#endif
	fputc('\n', fp);
}

static void err_nosig(char *name)
{
	warnx(_("unknown signal %s; valid signals:"), name);
	print_all_signals(stderr, 1);
	exit(EXIT_FAILURE);
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
			return -1;
		return numsig;
	}
	return signame_to_signum(arg);
}

static void __attribute__((__noreturn__)) usage(void)
{
	FILE *out = stdout;
	fputs(USAGE_HEADER, out);
	fprintf(out, _(" %s [options] <pid>|<name>...\n"), program_invocation_short_name);

	fputs(USAGE_SEPARATOR, out);
	fputs(_("Forcibly terminate a process.\n"), out);

	fputs(USAGE_OPTIONS, out);
	fputs(_(" -a, --all              do not restrict the name-to-pid conversion to processes\n"
		"                          with the same uid as the present process\n"), out);
	fputs(_(" -s, --signal <signal>  send this <signal> instead of SIGTERM\n"), out);
#ifdef HAVE_SIGQUEUE
	fputs(_(" -q, --queue <value>    use sigqueue(2), not kill(2), and pass <value> as data\n"), out);
#endif
	fputs(_(" -p, --pid              print pids without signaling them\n"), out);
	fputs(_(" -l, --list[=<signal>]  list signal names, or convert a signal number to a name\n"), out);
	fputs(_(" -L, --table            list signal names and numbers\n"), out);
	fputs(_("     --verbose          print pids that will be signaled\n"), out);

	fputs(USAGE_SEPARATOR, out);
	printf(USAGE_HELP_OPTIONS(24));
	printf(USAGE_MAN_TAIL("kill(1)"));

	exit(EXIT_SUCCESS);
}

static char **parse_arguments(int argc, char **argv, struct kill_control *ctl)
{
	char *arg;

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
			exit(EXIT_SUCCESS);
		}
		if (!strcmp(arg, "-h") || !strcmp(arg, "--help"))
			usage();
		if (!strcmp(arg, "--verbose")) {
			ctl->verbose = 1;
			continue;
		}
		if (!strcmp(arg, "-a") || !strcmp(arg, "--all")) {
			ctl->check_all = 1;
			continue;
		}
		if (!strcmp(arg, "-l") || !strcmp(arg, "--list")) {
			if (argc < 2) {
				print_all_signals(stdout, 0);
				exit(EXIT_SUCCESS);
			}
			if (2 < argc)
				errx(EXIT_FAILURE, _("too many arguments"));
			/* argc == 2, accept "kill -l $?" */
			arg = argv[1];
			if ((ctl->numsig = arg_to_signum(arg, 1)) < 0)
				errx(EXIT_FAILURE, _("unknown signal: %s"),
				     arg);
			print_signal_name(ctl->numsig);
			exit(EXIT_SUCCESS);
		}
		/* for compatibility with procps kill(1) */
		if (!strncmp(arg, "--list=", 7) || !strncmp(arg, "-l=", 3)) {
			char *p = strchr(arg, '=') + 1;
			if ((ctl->numsig = arg_to_signum(p, 1)) < 0)
				errx(EXIT_FAILURE, _("unknown signal: %s"), p);
			print_signal_name(ctl->numsig);
			exit(EXIT_SUCCESS);
		}
		if (!strcmp(arg, "-L") || !strcmp(arg, "--table")) {
			print_all_signals(stdout, 1);
			exit(EXIT_SUCCESS);
		}
		if (!strcmp(arg, "-p") || !strcmp(arg, "--pid")) {
			ctl->do_pid = 1;
			if (ctl->do_kill)
				errx(EXIT_FAILURE, _("%s and %s are mutually exclusive"), "--pid", "--signal");
#ifdef HAVE_SIGQUEUE
			if (ctl->use_sigval)
				errx(EXIT_FAILURE, _("%s and %s are mutually exclusive"), "--pid", "--queue");
#endif
			continue;
		}
		if (!strcmp(arg, "-s") || !strcmp(arg, "--signal")) {
			if (argc < 2)
				errx(EXIT_FAILURE, _("not enough arguments"));
			ctl->do_kill = 1;
			if (ctl->do_pid)
				errx(EXIT_FAILURE, _("%s and %s are mutually exclusive"), "--pid", "--signal");
			argc--, argv++;
			arg = *argv;
			if ((ctl->numsig = arg_to_signum(arg, 0)) < 0)
				err_nosig(arg);
			continue;
		}
#ifdef HAVE_SIGQUEUE
		if (!strcmp(arg, "-q") || !strcmp(arg, "--queue")) {
			if (argc < 2)
				errx(EXIT_FAILURE, _("option '%s' requires an argument"), arg);
			if (ctl->do_pid)
				errx(EXIT_FAILURE, _("%s and %s are mutually exclusive"), "--pid", "--queue");
			argc--, argv++;
			arg = *argv;
			ctl->sigdata.sival_int = strtos32_or_err(arg, _("argument error"));
			ctl->use_sigval = 1;
			continue;
		}
#endif
		/* 'arg' begins with a dash but is not a known option.
		 * So it's probably something like -HUP, or -1/-n try to
		 * deal with it.
		 *
		 * -n could be either signal n or pid -n (a process group
		 * number).  In case of doubt, POSIX tells us to assume a
		 * signal.  But if a signal has already been parsed, then
		 * assume it is a process group, so stop parsing options. */
		if (ctl->do_kill)
			break;
		arg++;
		if ((ctl->numsig = arg_to_signum(arg, 0)) < 0)
			errx(EXIT_FAILURE, _("invalid signal name or number: %s"), arg);
		ctl->do_kill = 1;
		if (ctl->do_pid)
			errx(EXIT_FAILURE, _("%s and %s are mutually exclusive"), "--pid", "--signal");
		continue;
	}
	if (!*argv)
		errx(EXIT_FAILURE, _("not enough arguments"));
	return argv;
}


static int kill_verbose(const struct kill_control *ctl)
{
	int rc = 0;

	if (ctl->verbose)
		printf(_("sending signal %d to pid %d\n"), ctl->numsig, ctl->pid);
	if (ctl->do_pid) {
		printf("%ld\n", (long) ctl->pid);
		return 0;
	}
#ifdef HAVE_SIGQUEUE
	if (ctl->use_sigval)
		rc = sigqueue(ctl->pid, ctl->numsig, ctl->sigdata);
	else
#endif
		rc = kill(ctl->pid, ctl->numsig);

	if (rc < 0)
		warn(_("sending signal to %s failed"), ctl->arg);
	return rc;
}

int main(int argc, char **argv)
{
	struct kill_control ctl = { .numsig = SIGTERM };
	int nerrs = 0, ct = 0;

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	argv = parse_arguments(argc, argv, &ctl);

	/* The rest of the arguments should be process ids and names. */
	for ( ; (ctl.arg = *argv) != NULL; argv++) {
		char *ep = NULL;

		errno = 0;
		ctl.pid = strtol(ctl.arg, &ep, 10);
		if (errno == 0 && ep && *ep == '\0' && ctl.arg < ep) {
			if (kill_verbose(&ctl) != 0)
				nerrs++;
			ct++;
		} else {
			struct proc_processes *ps = proc_open_processes();
			int found = 0;

			if (!ps)
				continue;
			if (!ctl.check_all)
				proc_processes_filter_by_uid(ps, getuid());

			proc_processes_filter_by_name(ps, ctl.arg);
			while (proc_next_pid(ps, &ctl.pid) == 0) {
				if (kill_verbose(&ctl) != 0)
					nerrs++;
				ct++;
				found = 1;
			}
			proc_close_processes(ps);

			if (!found) {
				nerrs++, ct++;
				warnx(_("cannot find process \"%s\""), ctl.arg);
			}
		}
	}

	if (ct && nerrs == 0)
		return EXIT_SUCCESS;	/* full success */
	else if (ct == nerrs)
		return EXIT_FAILURE;	/* all failed */

	return KILL_EXIT_SOMEOK;	/* partial success */
}

