/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Copyright (c) 2025 Christian Goeschel Ndjomouo <cgoesc2@wgu.edu>
 * Written by Christian Goeschel Ndjomouo <cgoesc2@wgu.edu>
 *
 * getpidino - retrieve the pidfd inode number for a given PID
 */
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "c.h"
#include "closestream.h"
#include "nls.h"
#include "pidfd-utils.h"
#include "strutils.h"

static bool print_pid = false;

static void print_pidfd_inode(char *pidstr)
{
  int pfd, rc = 0;
  pid_t pid;
  struct stat f;

  pid = strtopid_or_err(pidstr, _("invalid PID argument"));
  /* FIXME: replace this with ul_get_valid_pidfd() */
  pfd = pidfd_open(pid, 0);
  if (pfd < 0)
    err(EXIT_FAILURE, _("pidfd_open() failed %d"), pid);

  rc = fstat(pfd, &f);
  if (rc) {
		close(pfd);
    err(EXIT_FAILURE, _("fstat() failed: %d"), pfd);
  }

  if (print_pid) {
    printf("%d:%"PRIu64"\n", pid, f.st_ino);
  } else {
    printf("%"PRIu64"\n", f.st_ino);
  }
  close(pfd);
}

static void __attribute__((__noreturn__)) usage(void)
{
	fputs(USAGE_HEADER, stdout);
	fprintf(stdout, _(" %s [options] PID...\n"), program_invocation_short_name);

	fputs(USAGE_SEPARATOR, stdout);
	fputsln(_("Returns the pidfd inode number for a given PID."), stdout);

	fputs(USAGE_OPTIONS, stdout);
	fputsln(_(" -p, --print-pid         print the pid and pidfd inode (colon-separated)"), stdout);
	fputs(USAGE_SEPARATOR, stdout);
	fprintf(stdout, USAGE_HELP_OPTIONS(25)); /* char offset to align option descriptions */
	fprintf(stdout, USAGE_MAN_TAIL("getpidino(1)"));
	exit(EXIT_SUCCESS);
}

int main(int argc, char **argv)
{
	int c;

	static const struct option longopts[] = {
		{ "print-pid",                  no_argument,       NULL, 'p'          },
		{ "version",                    no_argument,       NULL, 'V'          },
		{ "help",                       no_argument,       NULL, 'h'          },
		{ NULL, 0, NULL, 0 }
	};

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	close_stdout_atexit();

	while ((c = getopt_long(argc, argv, "pVh", longopts, NULL)) != -1)
		switch (c) {
		case 'p':
      print_pid = true;
      break;
		case 'V':
			print_version(EXIT_SUCCESS);
		case 'h':
			usage();
		default:
			errtryhelp(EXIT_FAILURE);
		}
  
  if (argc - optind < 1) {
    warnx(_("too few arguments"));
		errtryhelp(EXIT_FAILURE);
  }

  argv += optind - 1;
  while (*++argv) {
    print_pidfd_inode(*argv);
  }

	return EXIT_SUCCESS;
}