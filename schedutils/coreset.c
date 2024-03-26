/*
 * coreset.c - set or retrieve a task's core scheduling cookie
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Copyright (C) 2024 Phil Auld <pauld@redhat.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <errno.h>
#include <sched.h>
#include <stddef.h>
#include <sys/prctl.h>

#include "c.h"
#include "closestream.h"
#include "optutils.h"
#include "strutils.h"

// These are in prctl.h on systems new enough. My dev system isn't
// should check for this in .configure step and not build
// this?

#ifndef PR_SCHED_CORE
/* Request the scheduler to share a core */
# define PR_SCHED_CORE                   62
#  define PR_SCHED_CORE_GET              0
#  define PR_SCHED_CORE_CREATE           1 /* create unique core_sched cookie */
#  define PR_SCHED_CORE_SHARE_TO         2 /* push core_sched cookie to pid */
#  define PR_SCHED_CORE_SHARE_FROM       3 /* pull core_sched cookie to pid */
#  define PR_SCHED_CORE_MAX              4
#endif

/* some prctl.h have the above but not these */
#ifndef PR_SCHED_CORE_SCOPE_THREAD
# define PR_SCHED_CORE_SCOPE_THREAD             0  /* PIDTYPE_PID  */
# define PR_SCHED_CORE_SCOPE_THREAD_GROUP       1  /* PIDTYPE_TGID */
# define PR_SCHED_CORE_SCOPE_PROCESS_GROUP      2  /* PIDTYPE_PGID */
#endif

/* basic operation to perform */
enum cmd_type {
	CORE_SHOW,    /* this just does PR_SCHED_CORE_GET */
	CORE_CREATE,  /* PR_SCHED_CORE_CREATE */
	CORE_PUSH,    /* PR_SCHED_CORE_SHARE_TO */
	CORE_COPY,    /* PR_SCHED_CORE_SHARE_FROM */
	CORE_COPYPUSH, /* SHARE_FROM and then SHARE_TO */
};

struct coreset {
	pid_t		pid;		/* task PID (or tid) */
	unsigned long   cookie;         /* storage for current cookie */
	int             cmd;            /* what to do: one of cmd_type */
	pid_t           dest;           /* only valid for COPYPUSH */
	int             scope;          /* one of PR_SCHED_CORE_SCOPE_THREAD(0), PR_SCHED_CORE_SCOPE_THREAD_GROUP(1)
                                         * or PR_SCHED_CORE_SCOPE_PROCESS_GROUP  (2)
                                         */
};

static void __attribute__((__noreturn__)) usage(void)
{
	FILE *out = stdout;
	fprintf(out,
		_("Usage: %s [options] [-p pid] [cmd [args...]]\n\n"),
		program_invocation_short_name);

	fputs(USAGE_SEPARATOR, out);
	fputs(_("Show or change the core scheduling cookie for a process or thread.\n"), out);
	fputs(USAGE_SEPARATOR, out);

	fprintf(out, _(
		"Options:\n"
		" -c, --copy              copy the cookie from give pid to this cmd (or dest pid)\n"
		" -n, --new               create new cookie on pid or cmd\n"
		" -t, --to                copy current task's cookie to existing pid or cmd\n"
		" Absence of one of the mutually exclusive above options just reports current cookie on given pid (or cmd)\n"
		" -p, --pid               operate on existing given pid/tid\n"
		" -d, --dest              Use with -c, copy cookie from pid to dest_pid\n"
		" -s, --scope             0, 1 or 2: apply change to task (0), thread group (1) or process group (2) of given pid/tid\n"
                " Default scope is 0. Scope is ignored in some cases where it does not have an effect\n"
		));
	fprintf(out, USAGE_HELP_OPTIONS(25));

	fputs(USAGE_SEPARATOR, out);
	fprintf(out, _(
		"The default behavior is to show existing cookie (which is of limited value):\n"
		"    %1$s sshd -b 1024\n"
		"    %1$s -p 700\n"
		"Create a new cookie for existing task:\n"
		"    %1$s -n -p 700\n"
		"or task and all its sibling threads:\n"
                "    %1$s -s 1 -n -p 700\n"
		"Copy cookie from existing task to new task:\n"
		"    %1$s -c -p 700  sshd -b 1024\n"
		"Copy cookie from existing task to a different existing task:\n"
		"    %1$s -c -p 700 -d 12345\n"
		"Clear cookie for all processes for given task (assuming current shell has no cookie):\n"
		"    %1$s -s 2 -t -p 700\n"
		"Note: pid can also be a tid as retrieved with the gettid(2) syscall.\n"),
		program_invocation_short_name);

	fputs(USAGE_SEPARATOR, out);
	fprintf(out, _("Core scheduling is available in kernels starting with v5.14.\n"));

	fprintf(out, USAGE_MAN_TAIL("coreset(1)"));
	exit(EXIT_SUCCESS);
}

static void print_cookie(struct coreset *cs, int isnew)
{
	char *msg;
	pid_t pid;
	if (cs->cmd == CORE_COPY || !cs->pid)
                pid = getpid(); /* with copy we want to report current's cookie */
        else if (cs->cmd == CORE_COPYPUSH)
                pid = cs->dest; /* with copypush we are changing dest's cookie */
        else
                pid = cs->pid;

	msg = isnew ? _("pid %d's new cookie: 0x%0x\n") :
		_("pid %d's current cookie: 0x%0x\n");

	printf(msg, pid, cs->cookie);
}

static void __attribute__((__noreturn__)) err_cookie(pid_t pid, int set)
{
	char *msg;

	if (set == CORE_COPY)
		msg = _("failed to copy pid %d's core scheduling cookie");
	else
		msg = set ? _("failed to set pid %d's core scheduling cookie") :
			    _("failed to get pid %d's core scheduling cookie");

	err(EXIT_FAILURE, msg, pid ? pid : getpid());
}

static unsigned long get_cookie(struct coreset *cs)
{
	unsigned long cookie;
	pid_t pid;
	if (cs->cmd == CORE_COPY)
		pid = 0; /* with copy we want to report current's cookie */
	else if (cs->cmd == CORE_COPYPUSH)
		pid = cs->dest; /* with copypush we are changing dest's cookie */
	else 
		pid = cs->pid; 

	if (prctl(PR_SCHED_CORE, PR_SCHED_CORE_GET, pid, PR_SCHED_CORE_SCOPE_THREAD, &cookie) < 0 )
		err_cookie(pid ? pid : getpid(), FALSE);

	return cookie;
}

static void do_coreset(struct coreset *cs)
{
	/* read the current cookie */
	cs->cookie = get_cookie(cs);
	print_cookie(cs, FALSE);

	switch (cs->cmd) {
	case CORE_SHOW:
		return;
	case CORE_CREATE:
                /* create a new cookie for given task (may be 0). Scope only applies with existing pid */
		if (prctl(PR_SCHED_CORE, PR_SCHED_CORE_CREATE, cs->pid, cs->scope, NULL) < 0)
			err_cookie(cs->pid, cs->cmd);
	        break;
	case CORE_COPY:
		/* copy cookie, which could be none, from source pid to current task.  Scope must be 0 so we force it*/
		if (prctl(PR_SCHED_CORE, PR_SCHED_CORE_SHARE_FROM, cs->pid, PR_SCHED_CORE_SCOPE_THREAD, NULL) < 0)
			err_cookie(cs->pid, cs->cmd);
		break;
	case CORE_PUSH:
                /* push current task's cookie, which could be none, to given pid. Scope is meaningful */
		if (prctl(PR_SCHED_CORE, PR_SCHED_CORE_SHARE_TO, cs->pid, cs->scope, NULL) < 0)
			err_cookie(cs->pid, cs->cmd);
		break;
	case CORE_COPYPUSH:
		/* copy pid's cookie here then push it to dest */
		if (prctl(PR_SCHED_CORE, PR_SCHED_CORE_SHARE_FROM, cs->pid, PR_SCHED_CORE_SCOPE_THREAD, NULL) < 0)
                        err_cookie(cs->pid, cs->cmd);
		if (prctl(PR_SCHED_CORE, PR_SCHED_CORE_SHARE_TO, cs->dest, cs->scope, NULL) < 0)
                        err_cookie(cs->pid, cs->cmd);

	}

	/* re-read the cookie */
	cs->cookie = get_cookie(cs);
	print_cookie(cs, TRUE);
}

int main(int argc, char **argv)
{
	pid_t pid = 0, dest_pid = 0;
	int c, copy = 0, create = 0, push = 0;
	int do_exec = 0;
	struct coreset cs;

	static const struct option longopts[] = {
		{ "copy",	no_argument, NULL, 'c' },
		{ "dest",	required_argument, NULL, 'd' },
		{ "new",	no_argument, NULL, 'n' },
		{ "pid",	required_argument, NULL, 'p' },
		{ "scope",	required_argument, NULL, 's' },
		{ "to",	        no_argument, NULL, 't' },
		{ "help",	no_argument, NULL, 'h' },
		{ "version",	no_argument, NULL, 'V' },
		{ NULL,		0, NULL,  0  }
	};
	static const ul_excl_t excl[] = {       /* rows and cols in ASCII order */
                { 'c','n','t'},
                { 0 }
        };
        int excl_st[ARRAY_SIZE(excl)] = UL_EXCL_STATUS_INIT;

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	close_stdout_atexit();

	memset(&cs, 0, sizeof(cs));

	while ((c = getopt_long(argc, argv, "cd:np:s:thV", longopts, NULL)) != -1) {
                err_exclusive_options(c, longopts, excl, excl_st);
		switch (c) {
		case 'c':
			copy = 1;
			break;
		case 'd':
			dest_pid = strtopid_or_err(optarg, _("invalid copy dest PID argument"));
			break;
		case 'n':
		        create = 1;
			break;
		case 'p':
			pid = strtopid_or_err(optarg, _("invalid PID argument"));
			break;
		case 's':
			cs.scope = strtos32_or_err(optarg, _("invalid scope argument"));
			break;
		case 't':
		        push = 1;
			break;
		case 'V':
			print_version(EXIT_SUCCESS);
		case 'h':
			usage();
		default:
			errtryhelp(EXIT_FAILURE);
		}
	}

	// pid and no command is okay. No pid and no command is not.  Copy and no command is okay if dest_pid.
	// push and no command is okay. copy and push require pid
	if (((!pid || (copy && !dest_pid)) && argc - optind < 1) || ((copy || push) && !pid)) {
		warnx(_("bad usage"));
		errtryhelp(EXIT_FAILURE);
	}

	/* ensure we have dest pid only with copy operation */
	if (dest_pid && !copy) {
		warnx(_("Dest pid can only be used with copy"));
		errtryhelp(EXIT_FAILURE);
	}
        
	/* scope must be one of PR_SCHED_CORE_SCOPE_*  */
	if (cs.scope < PR_SCHED_CORE_SCOPE_THREAD || cs.scope > PR_SCHED_CORE_SCOPE_PROCESS_GROUP) {
		warnx(_("invalid scope"));
		errtryhelp(EXIT_FAILURE);
	}

	if (argc - optind > 0)
		do_exec = 1;

	if (create)
		cs.cmd = CORE_CREATE;
	else if (dest_pid) {
		cs.cmd = CORE_COPYPUSH;
		cs.dest = dest_pid;
	} else if (copy)
		cs.cmd = CORE_COPY;
	else if (push)
		cs.cmd = CORE_PUSH;
	else
		cs.cmd = CORE_SHOW;

	/* create and show with a pid don't use the command */
	if ((pid  || dest_pid) && do_exec && (cs.cmd == CORE_SHOW || cs.cmd == CORE_CREATE)) {
		warnx(_("Ignoring extraneous input"));
		do_exec = 0;
	}

	if (pid)
		cs.pid = pid;

	do_coreset(&cs);

	if (do_exec) {
		argv += optind;
		execvp(argv[0], argv);
		errexec(argv[0]);
	}

	return EXIT_SUCCESS;
}
