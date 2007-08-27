/*
 * Copyright (C) 2003-2007 Red Hat, Inc.
 *
 * This file is part of util-linux-ng.
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

#ifndef _GNU_SOURCE
#  define _GNU_SOURCE
#endif

#include <syscall.h>
#include <linux/personality.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <error.h>
#include <sys/utsname.h>
#include "nls.h"

#define set_pers(pers) ((long)syscall(SYS_personality, pers))

struct {
    char c;
    const char *name;
    unsigned int option;
} flags[] = {
    {'R', "ADDR_NO_RANDOMIZE",  0x0040000},
    {'F', "FDPIC_FUNCPTRS",     0x0080000},
    {'Z', "MMAP_PAGE_ZERO",     0x0100000},
    {'L', "ADDR_COMPAT_LAYOUT", 0x0200000},
    {'X', "READ_IMPLIES_EXEC",  0x0400000},
    {'B', "ADDR_LIMIT_32BIT",   0x0800000},
    {'I', "SHORT_INODE",        0x1000000},
    {'S', "WHOLE_SECONDS",      0x2000000},
    {'T', "STICKY_TIMEOUTS",    0x4000000},
    {'3', "ADDR_LIMIT_3GB",     0x8000000}
};

static void __attribute__((__noreturn__))
show_help(void)
{
  int f;
  const char *p = program_invocation_short_name;

  if (!*p)
    p = "setarch";

  printf(_("Usage: %s%s [options] [program [program arguments]]\n\nOptions:\n"),
         p, !strcmp(p, "setarch") ? " <arch>" : "");

  for (f = 0; f < sizeof(flags) / sizeof(flags[0]); f++)
    printf(_("\t-%c\tEnable %s\n"), flags[f].c, flags[f].name);

  printf(_("\nFor more information see setarch(8).\n"));
  exit(EXIT_SUCCESS);
}

static void __attribute__((__noreturn__))
show_usage(const char *s)
{
  const char *p = program_invocation_short_name;

  if (!*p)
    p = "setarch";

  fprintf(stderr, _("%s: %s\nTry `%s --help' for more information.\n"), p, s, p);
  exit(EXIT_FAILURE);
}

int set_arch(const char *pers, unsigned long options)
{
  struct utsname un;
  int i;
  unsigned long pers_value, res;

  struct {
    int perval;
    const char *target_arch, *result_arch;
  } transitions[] = {
    {PER_LINUX32, "linux32", NULL},
    {PER_LINUX, "linux64", NULL},
#if defined(__powerpc__) || defined(__powerpc64__)
    {PER_LINUX32, "ppc32", "ppc"},
    {PER_LINUX32, "ppc", "ppc"},
    {PER_LINUX, "ppc64", "ppc64"},
    {PER_LINUX, "ppc64pseries", "ppc64"},
    {PER_LINUX, "ppc64iseries", "ppc64"},
#endif
#if defined(__x86_64__) || defined(__i386__) || defined(__ia64__)
    {PER_LINUX32, "i386", "i386"},
    {PER_LINUX32, "i486", "i386"},
    {PER_LINUX32, "i586", "i386"},
    {PER_LINUX32, "i686", "i386"},
    {PER_LINUX32, "athlon", "i386"},
#endif
#if defined(__x86_64__) || defined(__i386__)
    {PER_LINUX, "x86_64", "x86_64"},
#endif
#if defined(__ia64__) || defined(__i386__)
    {PER_LINUX, "ia64", "ia64"},
#endif
#if defined(__hppa__)
    {PER_LINUX32, "parisc32", "parisc"},
    {PER_LINUX32, "parisc", "parisc"},
    {PER_LINUX, "parisc64", "parisc64"},
#endif
#if defined(__s390x__) || defined(__s390__)
    {PER_LINUX32, "s390", "s390"},
    {PER_LINUX, "s390x", "s390x"},
#endif
#if defined(__sparc64__) || defined(__sparc__)
    {PER_LINUX32, "sparc", "sparc"},
    {PER_LINUX32, "sparc32bash", "sparc"},
    {PER_LINUX32, "sparc32", "sparc"},
    {PER_LINUX, "sparc64", "sparc64"},
#endif
#if defined(__mips64__) || defined(__mips__)
    {PER_LINUX32, "mips32", "mips"},
    {PER_LINUX32, "mips", "mips"},
    {PER_LINUX, "mips64", "mips64"},
#endif
#if defined(__alpha__)
    {PER_LINUX, "alpha", "alpha"},
#endif
    {-1, NULL, NULL}
  };

  for(i = 0; transitions[i].perval >= 0; i++)
      if(!strcmp(pers, transitions[i].target_arch))
	break;

  if(transitions[i].perval < 0)
    error(EXIT_FAILURE, 0, _("%s: Unrecognized architecture"), pers);

  pers_value = transitions[i].perval | options;
  res = set_pers(pers_value);
  if(res == -EINVAL)
    return 1;

  uname(&un);
  if(transitions[i].result_arch &&
	strcmp(un.machine, transitions[i].result_arch))
  {
    if(strcmp(transitions[i].result_arch, "i386")
       || (strcmp(un.machine, "i486")
	   && strcmp(un.machine, "i586")
	   && strcmp(un.machine, "i686")
	   && strcmp(un.machine, "athlon")))
      error(EXIT_FAILURE, 0, _("%s: Unrecognized architecture"), pers);
  }

  return 0;
}

int main(int argc, char *argv[])
{
  const char *p;
  unsigned long options = 0;
  int verbose = 0;

  setlocale(LC_ALL, "");
  bindtextdomain(PACKAGE, LOCALEDIR);
  textdomain(PACKAGE);

  if (argc < 1)
    show_usage(_("Not enough arguments"));

  p = program_invocation_short_name;
  if (!strcmp(p, "setarch")) {
    argv++;
    argc--;
    if (argc < 1)
      show_usage(_("Not enough arguments"));
    p = argv[0];
    if (!strcmp(p, "-h") || !strcmp(p, "--help"))
      show_help();
  }
  #if defined(__sparc64__) || defined(__sparc__)
   if (!strcmp(p, "sparc32bash")) {
       if (set_arch(p, NULL))
         error(EXIT_FAILURE, errno, "Failed to set personality to %s", p);
       execl("/bin/bash", NULL);
       error(EXIT_FAILURE, errno, "/bin/bash");
   }
  #endif
  for (argv++, argc--; argc && argv[0][0] == '-'; argv++, argc--) {
    int n, unknown = 1;
    const char *arg = argv[0];

    if (!strcmp(arg, "--help"))
      show_help();

    /* compatibitity with an old Debian setarch implementation
     * TODO: add long options for all flags
     */
    if (!strcmp(arg, "--3gb"))
      arg="-3";
    else if (!strcmp(arg, "--4gb"))
      continue;				/* just ignore this one */

    for (n = 1; arg[n]; n++) {
      int f;

      if (arg[n] == 'v') {
	verbose = 1;
	continue;
      }

      if (arg[n] == 'h')
	show_help();

      for (f = 0; f < sizeof(flags) / sizeof(flags[0]); f++) {
	if (arg[n] == flags[f].c) {
	  if (verbose)
	    fprintf(stderr, _("Switching on %s.\n"), flags[f].name);
	  options |= flags[f].option;
	  unknown = 0;
	  break;
	}
      }
      if (unknown)
	error(0, 0, _("Unknown option `%c' ignored"), arg[n]);
    }
  }

  if (set_arch(p, options))
    error(EXIT_FAILURE, errno, _("Failed to set personality to %s"), p);

  if (!argc) {
    execl("/bin/sh", "-sh", NULL);
    error(EXIT_FAILURE, errno, "/bin/sh");
  }

  execvp(argv[0], argv);
  error(EXIT_FAILURE, errno, "%s", argv[0]);
  return EXIT_FAILURE;
}
