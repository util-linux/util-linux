/*
 * Copyright (C) 1995  Andries E. Brouwer (aeb@cwi.nl)
 * Copyright (C) 2014 Karel Zak <kzak@redhat.com>
 *
 * This program is free software. You can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation: either Version 1
 * or (at your option) any later version.
 *
 * A.V. Le Blanc (LeBlanc@mcc.ac.uk) wrote Linux fdisk 1992-1994,
 * patched by various people (faith@cs.unc.edu, martin@cs.unc.edu,
 * leisner@sdsp.mc.xerox.com, esr@snark.thyrsus.com, aeb@cwi.nl)
 * 1993-1995, with version numbers (as far as I have seen) 0.93 - 2.0e.
 * This program had (head,sector,cylinder) as basic unit, and was
 * (therefore) broken in several ways for the use on larger disks -
 * for example, my last patch (from 2.0d to 2.0e) was required
 * to allow a partition to cross cylinder 8064, and to write an
 * extended partition past the 4GB mark.
 *
 * Karel Zak wrote new sfdisk based on libfdisk from util-linux
 * in 2014.
 */

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <sys/stat.h>

#include "c.h"
#include "xalloc.h"
#include "nls.h"
#include "debug.h"
#include "strutils.h"
#include "closestream.h"

#include "libfdisk.h"

/*
 * sfdisk debug stuff (see fdisk.h and include/debug.h)
 */
UL_DEBUG_DEFINE_MASK(sfdisk);
UL_DEBUG_DEFINE_MASKANEMS(sfdisk) = UL_DEBUG_EMPTY_MASKNAMES;

#define SFDISKPROG_DEBUG_INIT	(1 << 1)
#define SFDISKPROG_DEBUG_PARSE	(1 << 2)
#define SFDISKPROG_DEBUG_MISC	(1 << 3)
#define SFDISKPROG_DEBUG_ALL	0xFFFF

#define DBG(m, x)       __UL_DBG(sfdisk, SFDISKPROG_DEBUG_, m, x)
#define ON_DBG(m, x)    __UL_DBG_CALL(sfdisk, SFDISKPROG_DEBUG_, m, x)


static void sfdiskprog_init_debug(void)
{
	__UL_INIT_DEBUG(sfdisk, SFDISKPROG_DEBUG_, 0, SFDISK_DEBUG);
}

static void __attribute__ ((__noreturn__)) usage(FILE *out)
{
	fputs(USAGE_HEADER, out);

	fprintf(out,
	      _(" %1$s [options] <disk>\n"), program_invocation_short_name);

	fputs(USAGE_OPTIONS, out);

	fputs(USAGE_SEPARATOR, out);
	fputs(USAGE_HELP, out);
	fputs(USAGE_VERSION, out);

	fprintf(out, USAGE_MAN_TAIL("sfdisk(8)"));
	exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}

int main(int argc, char *argv[])
{
	struct fdisk_context *cxt;	/* libfdisk stuff */
	const char *diskpath;
	int rc, c;

	static const struct option longopts[] = {
		{ "help",    no_argument,       NULL, 'h' },
		{ "version", no_argument,       NULL, 'v' },
		{ NULL, 0, 0, 0 },
	};

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	while((c = getopt_long(argc, argv, "hv", longopts, NULL)) != -1) {
		switch(c) {
		case 'h':
			usage(stdout);
			break;
		case 'v':
			printf(_("%s from %s\n"), program_invocation_short_name,
						  PACKAGE_STRING);
			return EXIT_SUCCESS;
		}
	}

	fdisk_init_debug(0);
	sfdiskprog_init_debug();

	if (argc - optind != 1)
		usage(stderr);

	diskpath = argv[optind];

	cxt = fdisk_new_context();
	if (!cxt)
		err(EXIT_FAILURE, _("failed to allocate libfdisk context"));

	rc = fdisk_assign_device(cxt, diskpath, 0);
	if (rc != 0)
		err(EXIT_FAILURE, _("cannot open %s"), diskpath);

	rc = fdisk_deassign_device(cxt, 0);
	fdisk_unref_context(cxt);

	DBG(MISC, ul_debug("bye! [rc=%d]", rc));
	return rc == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

