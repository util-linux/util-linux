/*
 * Copyright (C) 2003-2007 Red Hat, Inc.
 *
 * This file is part of util-linux.
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This file is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 *
 * Written by Elliot Lee <sopwith@redhat.com>
 * New personality options & code added by Jindrich Novy <jnovy@redhat.com>
 * ADD_NO_RANDOMIZE flag added by Arjan van de Ven <arjanv@redhat.com>
 * Help and MIPS support from Mike Frysinger (vapier@gentoo.org)
 * Better error handling from Dmitry V. Levin (ldv@altlinux.org)
 *
 * based on ideas from the ppc32 util by Guy Streeter (2002-01), based on the
 * sparc32 util by Jakub Jelinek (1998, 1999)
 */

#include <sys/personality.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <getopt.h>
#include <limits.h>
#include <sys/utsname.h>
#include "nls.h"
#include "c.h"
#include "closestream.h"

#ifndef HAVE_PERSONALITY
# include <syscall.h>
# define personality(pers) ((long)syscall(SYS_personality, pers))
#endif

#define turn_on(_flag, _opts) \
	do { \
		(_opts) |= _flag; \
		if (verbose) \
			printf(_("Switching on %s.\n"), #_flag); \
	} while(0)

#ifndef UNAME26
# define UNAME26                 0x0020000
#endif
#ifndef ADDR_NO_RANDOMIZE
# define ADDR_NO_RANDOMIZE       0x0040000
#endif
#ifndef FDPIC_FUNCPTRS
# define FDPIC_FUNCPTRS          0x0080000
#endif
#ifndef MMAP_PAGE_ZERO
# define MMAP_PAGE_ZERO          0x0100000
#endif
#ifndef ADDR_COMPAT_LAYOUT
# define ADDR_COMPAT_LAYOUT      0x0200000
#endif
#ifndef READ_IMPLIES_EXEC
# define READ_IMPLIES_EXEC       0x0400000
#endif
#ifndef ADDR_LIMIT_32BIT
# define ADDR_LIMIT_32BIT        0x0800000
#endif
#ifndef SHORT_INODE
# define SHORT_INODE             0x1000000
#endif
#ifndef WHOLE_SECONDS
# define WHOLE_SECONDS           0x2000000
#endif
#ifndef STICKY_TIMEOUTS
# define STICKY_TIMEOUTS         0x4000000
#endif
#ifndef ADDR_LIMIT_3GB
# define ADDR_LIMIT_3GB          0x8000000
#endif


struct arch_domain {
	int		perval;		/* PER_* */
	const char	*target_arch;
	const char	*result_arch;
};


static void __attribute__((__noreturn__)) usage(int archwrapper)
{
	fputs(USAGE_HEADER, stdout);
	if (!archwrapper)
		printf(_(" %s [<arch>] [options] [<program> [<argument>...]]\n"), program_invocation_short_name);
	else
		printf(_(" %s [options] [<program> [<argument>...]]\n"), program_invocation_short_name);

	fputs(USAGE_SEPARATOR, stdout);
	fputs(_("Change the reported architecture and set personality flags.\n"), stdout);

	fputs(USAGE_OPTIONS, stdout);
	fputs(_(" -B, --32bit              turns on ADDR_LIMIT_32BIT\n"), stdout);
	fputs(_(" -F, --fdpic-funcptrs     makes function pointers point to descriptors\n"), stdout);
	fputs(_(" -I, --short-inode        turns on SHORT_INODE\n"), stdout);
	fputs(_(" -L, --addr-compat-layout changes the way virtual memory is allocated\n"), stdout);
	fputs(_(" -R, --addr-no-randomize  disables randomization of the virtual address space\n"), stdout);
	fputs(_(" -S, --whole-seconds      turns on WHOLE_SECONDS\n"), stdout);
	fputs(_(" -T, --sticky-timeouts    turns on STICKY_TIMEOUTS\n"), stdout);
	fputs(_(" -X, --read-implies-exec  turns on READ_IMPLIES_EXEC\n"), stdout);
	fputs(_(" -Z, --mmap-page-zero     turns on MMAP_PAGE_ZERO\n"), stdout);
	fputs(_(" -3, --3gb                limits the used address space to a maximum of 3 GB\n"), stdout);
	fputs(_("     --4gb                ignored (for backward compatibility only)\n"), stdout);
	fputs(_("     --uname-2.6          turns on UNAME26\n"), stdout);
	fputs(_(" -v, --verbose            say what options are being switched on\n"), stdout);

	if (!archwrapper)
		fputs(_("     --list               list settable architectures, and exit\n"), stdout);

	fputs(USAGE_SEPARATOR, stdout);
	printf(USAGE_HELP_OPTIONS(26));
	printf(USAGE_MAN_TAIL("setarch(8)"));

	exit(EXIT_SUCCESS);
}

/*
 * Returns inilialized list of all available execution domains.
 */
static struct arch_domain *init_arch_domains(void)
{
	struct utsname un;
	size_t i;

	static struct arch_domain transitions[] =
	{
		{UNAME26,	"uname26",	NULL},
		{PER_LINUX32,	"linux32",	NULL},
		{PER_LINUX,	"linux64",	NULL},
#if defined(__powerpc__) || defined(__powerpc64__)
# ifdef __BIG_ENDIAN__
		{PER_LINUX32,	"ppc32",	"ppc"},
		{PER_LINUX32,	"ppc",		"ppc"},
		{PER_LINUX,	"ppc64",	"ppc64"},
		{PER_LINUX,	"ppc64pseries",	"ppc64"},
		{PER_LINUX,	"ppc64iseries",	"ppc64"},
# else
		{PER_LINUX32,	"ppc32",	"ppcle"},
		{PER_LINUX32,	"ppc",		"ppcle"},
		{PER_LINUX32,	"ppc32le",	"ppcle"},
		{PER_LINUX32,	"ppcle",	"ppcle"},
		{PER_LINUX,	"ppc64le",	"ppc64le"},
# endif
#endif
#if defined(__x86_64__) || defined(__i386__) || defined(__ia64__)
		{PER_LINUX32,	"i386",		"i386"},
		{PER_LINUX32,	"i486",		"i386"},
		{PER_LINUX32,	"i586",		"i386"},
		{PER_LINUX32,	"i686",		"i386"},
		{PER_LINUX32,	"athlon",	"i386"},
#endif
#if defined(__x86_64__) || defined(__i386__)
		{PER_LINUX,	"x86_64",	"x86_64"},
#endif
#if defined(__ia64__) || defined(__i386__)
		{PER_LINUX,	"ia64",		"ia64"},
#endif
#if defined(__hppa__)
		{PER_LINUX32,	"parisc32",	"parisc"},
		{PER_LINUX32,	"parisc",	"parisc"},
		{PER_LINUX,	"parisc64",	"parisc64"},
#endif
#if defined(__s390x__) || defined(__s390__)
		{PER_LINUX32,	"s390",		"s390"},
		{PER_LINUX,	"s390x",	"s390x"},
#endif
#if defined(__sparc64__) || defined(__sparc__)
		{PER_LINUX32,	"sparc",	"sparc"},
		{PER_LINUX32,	"sparc32bash",	"sparc"},
		{PER_LINUX32,	"sparc32",	"sparc"},
		{PER_LINUX,	"sparc64",	"sparc64"},
#endif
#if defined(__mips64__) || defined(__mips__)
		{PER_LINUX32,	"mips32",	"mips"},
		{PER_LINUX32,	"mips",		"mips"},
		{PER_LINUX,	"mips64",	"mips64"},
#endif
#if defined(__alpha__)
		{PER_LINUX,	"alpha",	"alpha"},
		{PER_LINUX,	"alphaev5",	"alpha"},
		{PER_LINUX,	"alphaev56",	"alpha"},
		{PER_LINUX,	"alphaev6",	"alpha"},
		{PER_LINUX,	"alphaev67",	"alpha"},
#endif
		/* place holder, will be filled up at runtime */
		{-1,		NULL,		NULL},
		{-1,		NULL,		NULL}
	};

	/* Add the trivial transition {PER_LINUX, machine, machine} if no
	 * such target_arch is hardcoded yet.  */
	uname(&un);
	for (i = 0; transitions[i].perval >= 0; i++)
		if (!strcmp(un.machine, transitions[i].target_arch))
			break;
	if (transitions[i].perval < 0) {
		unsigned long wrdsz = CHAR_BIT * sizeof(void *);
		if (wrdsz == 32 || wrdsz == 64) {
			/* fill up the place holder */
			transitions[i].perval = wrdsz == 32 ? PER_LINUX32 : PER_LINUX;
			transitions[i].target_arch = un.machine;
			transitions[i].result_arch = un.machine;
		}
	}

	return transitions;
}

/*
 * List all execution domains from transitions
 */
static void list_arch_domains(struct arch_domain *doms)
{
	struct arch_domain *d;

	for (d = doms; d->target_arch != NULL; d++)
		printf("%s\n", d->target_arch);
}

static struct arch_domain *get_arch_domain(struct arch_domain *doms, const char *pers)
{
	struct arch_domain *d;

	for (d = doms; d->perval >= 0; d++) {
		if (!strcmp(pers, d->target_arch))
			break;
	}

	return !d || d->perval < 0 ? NULL : d;
}

static void verify_arch_domain(struct arch_domain *dom, const char *wanted)
{
	struct utsname un;

	if (!dom || !dom->result_arch)
		return;

	uname(&un);
	if (strcmp(un.machine, dom->result_arch)) {
		if (strcmp(dom->result_arch, "i386")
		    || (strcmp(un.machine, "i486")
			&& strcmp(un.machine, "i586")
			&& strcmp(un.machine, "i686")
			&& strcmp(un.machine, "athlon")))
			errx(EXIT_FAILURE, _("Kernel cannot set architecture to %s"), wanted);
	}
}

int main(int argc, char *argv[])
{
	const char *arch = NULL;
	unsigned long options = 0;
	int verbose = 0;
	int archwrapper;
	int c;
	struct arch_domain *doms, *target;
	unsigned long pers_value = 0;
	char *shell = NULL, *shell_arg = NULL;

	/* Options without equivalent short options */
	enum {
		OPT_4GB = CHAR_MAX + 1,
		OPT_UNAME26,
		OPT_LIST
	};

	/* Options --3gb and --4gb are for compatibility with an old
	 * Debian setarch implementation.  */
	static const struct option longopts[] = {
		{"help",		no_argument,	NULL,	'h'},
		{"version",		no_argument,	NULL,	'V'},
		{"verbose",		no_argument,	NULL,	'v'},
		{"addr-no-randomize",	no_argument,	NULL,	'R'},
		{"fdpic-funcptrs",	no_argument,	NULL,	'F'},
		{"mmap-page-zero",	no_argument,	NULL,	'Z'},
		{"addr-compat-layout",	no_argument,	NULL,	'L'},
		{"read-implies-exec",	no_argument,	NULL,	'X'},
		{"32bit",		no_argument,	NULL,	'B'},
		{"short-inode",		no_argument,	NULL,	'I'},
		{"whole-seconds",	no_argument,	NULL,	'S'},
		{"sticky-timeouts",	no_argument,	NULL,	'T'},
		{"3gb",			no_argument,	NULL,	'3'},
		{"4gb",			no_argument,	NULL,	OPT_4GB},
		{"uname-2.6",		no_argument,	NULL,	OPT_UNAME26},
		{"list",		no_argument,	NULL,	OPT_LIST},
		{NULL,			0,		NULL,	0}
	};

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	if (argc < 1) {
		warnx(_("Not enough arguments"));
		errtryhelp(EXIT_FAILURE);
	}
	archwrapper = strcmp(program_invocation_short_name, "setarch") != 0;
	if (archwrapper) {
		arch = program_invocation_short_name;	/* symlinks to setarch */

		/* Don't use ifdef sparc here, we get "Unrecognized architecture"
		 * error message later if necessary */
		if (strcmp(arch, "sparc32bash") == 0) {
			shell = "/bin/bash";
			shell_arg = "";
			goto set_arch;
		}
	} else {
		if (1 < argc && *argv[1] != '-') {
			arch = argv[1];
			argv[1] = argv[0];	/* for getopt_long() to get the program name */
			argv++;
			argc--;
		}
	}

	while ((c = getopt_long(argc, argv, "+hVv3BFILRSTXZ", longopts, NULL)) != -1) {
		switch (c) {
		case 'h':
			usage(archwrapper);
			break;
		case 'V':
			printf(UTIL_LINUX_VERSION);
			return EXIT_SUCCESS;
		case 'v':
			verbose = 1;
			break;
		case 'R':
			turn_on(ADDR_NO_RANDOMIZE, options);
			break;
		case 'F':
			turn_on(FDPIC_FUNCPTRS, options);
			break;
		case 'Z':
			turn_on(MMAP_PAGE_ZERO, options);
			break;
		case 'L':
			turn_on(ADDR_COMPAT_LAYOUT, options);
			break;
		case 'X':
			turn_on(READ_IMPLIES_EXEC, options);
			break;
		case 'B':
			turn_on(ADDR_LIMIT_32BIT, options);
			break;
		case 'I':
			turn_on(SHORT_INODE, options);
			break;
		case 'S':
			turn_on(WHOLE_SECONDS, options);
			break;
		case 'T':
			turn_on(STICKY_TIMEOUTS, options);
			break;
		case '3':
			turn_on(ADDR_LIMIT_3GB, options);
			break;
		case OPT_4GB:	/* just ignore this one */
			break;
		case OPT_UNAME26:
			turn_on(UNAME26, options);
			break;
		case OPT_LIST:
			if (!archwrapper) {
				list_arch_domains(init_arch_domains());
				return EXIT_SUCCESS;
			} else
				warnx(_("unrecognized option '--list'"));
			/* fallthrough */
		default:
			errtryhelp(EXIT_FAILURE);
		}
	}

	if (!arch && !options)
		errx(EXIT_FAILURE, _("no architecture argument or personality flags specified"));

	argc -= optind;
	argv += optind;

set_arch:
	/* get execution domain (architecture) */
	if (arch) {
		doms = init_arch_domains();
		target = get_arch_domain(doms, arch);

		if (!target)
			errx(EXIT_FAILURE, _("%s: Unrecognized architecture"), arch);
		pers_value = target->perval;
	}

	/* add personality flags */
	pers_value |= options;

	/* call kernel */
	if (personality(pers_value) < 0) {
		/*
		 * Depending on architecture and kernel version, personality
		 * syscall is either capable or incapable of returning an error.
		 * If the return value is not an error, then it's the previous
		 * personality value, which can be an arbitrary value
		 * undistinguishable from an error value.
		 * To make things clear, a second call is needed.
		 */
		if (personality(pers_value) < 0)
			err(EXIT_FAILURE, _("failed to set personality to %s"), arch);
	}

	/* make sure architecture is set as expected */
	if (arch)
		verify_arch_domain(target, arch);

	if (!argc) {
		shell = "/bin/sh";
		shell_arg = "-sh";
	}
	if (verbose) {
		printf(_("Execute command `%s'.\n"), shell ? shell : argv[0]);
		/* flush all output streams before exec */
		fflush(NULL);
	}

	/* Execute shell */
	if (shell) {
		execl(shell, shell_arg, NULL);
		errexec(shell);
	}

	/* Execute on command line specified command */
	execvp(argv[0], argv);
	errexec(argv[0]);
}
