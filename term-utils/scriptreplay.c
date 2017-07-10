/*
 * Copyright (C) 2008, Karel Zak <kzak@redhat.com>
 * Copyright (C) 2008, James Youngman <jay@gnu.org>
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
 * Based on scriptreplay.pl by Joey Hess <joey@kitenet.net>
 */

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <limits.h>
#include <math.h>
#include <sys/select.h>
#include <unistd.h>
#include <getopt.h>

#include "closestream.h"
#include "nls.h"
#include "strutils.h"
#include "c.h"

#define SCRIPT_MIN_DELAY 0.0001		/* from original sripreplay.pl */

static void __attribute__((__noreturn__))
usage(void)
{
	FILE *out = stdout;
	fputs(USAGE_HEADER, out);
	fprintf(out,
	      _(" %s [-t] timingfile [typescript] [divisor]\n"),
	      program_invocation_short_name);

	fputs(USAGE_SEPARATOR, out);
	fputs(_("Play back terminal typescripts, using timing information.\n"), out);

	fputs(USAGE_OPTIONS, out);
	fputs(_(" -t, --timing <file>     script timing output file\n"
		" -s, --typescript <file> script terminal session output file\n"
		" -d, --divisor <num>     speed up or slow down execution with time divisor\n"
		" -m, --maxdelay <num>    wait at most this many seconds between updates\n"
		), out);
	printf(USAGE_HELP_OPTIONS(25));

	printf(USAGE_MAN_TAIL("scriptreplay(1)"));
	exit(EXIT_SUCCESS);
}

static double
getnum(const char *s)
{
	const double d = strtod_or_err(s, _("failed to parse number"));

	if (isnan(d)) {
		errno = EINVAL;
		err(EXIT_FAILURE, "%s: %s", _("failed to parse number"), s);
	}
	return d;
}

static void
delay_for(double delay)
{
#ifdef HAVE_NANOSLEEP
	struct timespec ts, remainder;
	ts.tv_sec = (time_t) delay;
	ts.tv_nsec = (delay - ts.tv_sec) * 1.0e9;

	while (-1 == nanosleep(&ts, &remainder)) {
		if (EINTR == errno)
			ts = remainder;
		else
			break;
	}
#else
	struct timeval tv;
	tv.tv_sec = (long) delay;
	tv.tv_usec = (delay - tv.tv_sec) * 1.0e6;
	select(0, NULL, NULL, NULL, &tv);
#endif
}

static void
emit(FILE *fd, const char *filename, size_t ct)
{
	char buf[BUFSIZ];

	while(ct) {
		size_t len, cc;

		cc = ct > sizeof(buf) ? sizeof(buf) : ct;
		len = fread(buf, 1, cc, fd);

		if (!len)
		       break;

		ct -= len;
		cc = write(STDOUT_FILENO, buf, len);
		if (cc != len)
			err(EXIT_FAILURE, _("write to stdout failed"));
	}

	if (!ct)
		return;
	if (feof(fd))
		errx(EXIT_FAILURE, _("unexpected end of file on %s"), filename);

	err(EXIT_FAILURE, _("failed to read typescript file %s"), filename);
}


int
main(int argc, char *argv[])
{
	FILE *tfile, *sfile;
	const char *sname = NULL, *tname = NULL;
	double divi = 1, maxdelay = 0;
	int c, diviopt = FALSE, maxdelayopt = FALSE, idx;
	unsigned long line;
	int ch;

	static const struct option longopts[] = {
		{ "timing",	required_argument,	0, 't' },
		{ "typescript",	required_argument,	0, 's' },
		{ "divisor",	required_argument,	0, 'd' },
		{ "maxdelay",	required_argument,	0, 'm' },
		{ "version",	no_argument,		0, 'V' },
		{ "help",	no_argument,		0, 'h' },
		{ NULL,		0, 0, 0 }
	};

	/* Because we use space as a separator, we can't afford to use any
	 * locale which tolerates a space in a number.  In any case, script.c
	 * sets the LC_NUMERIC locale to C, anyway.
	 */
	setlocale(LC_ALL, "");
	setlocale(LC_NUMERIC, "C");

	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	while ((ch = getopt_long(argc, argv, "t:s:d:m:Vh", longopts, NULL)) != -1)
		switch(ch) {
		case 't':
			tname = optarg;
			break;
		case 's':
			sname = optarg;
			break;
		case 'd':
			diviopt = TRUE;
			divi = getnum(optarg);
			break;
		case 'm':
			maxdelayopt = TRUE;
			maxdelay = getnum(optarg);
			break;
		case 'V':
			printf(UTIL_LINUX_VERSION);
			exit(EXIT_SUCCESS);
		case 'h':
			usage();
		default:
			errtryhelp(EXIT_FAILURE);
			}
	argc -= optind;
	argv += optind;
	idx = 0;

	if ((argc < 1 && !tname) || argc > 3) {
		warnx(_("wrong number of arguments"));
		errtryhelp(EXIT_FAILURE);
	}
	if (!tname)
		tname = argv[idx++];
	if (!sname)
		sname = idx < argc ? argv[idx++] : "typescript";
	if (!diviopt)
		divi = idx < argc ? getnum(argv[idx]) : 1;
	if (maxdelay < 0)
		maxdelay = 0;
	tfile = fopen(tname, "r");
	if (!tfile)
		err(EXIT_FAILURE, _("cannot open %s"), tname);
	sfile = fopen(sname, "r");
	if (!sfile)
		err(EXIT_FAILURE, _("cannot open %s"), sname);

	/* ignore the first typescript line */
	while((c = fgetc(sfile)) != EOF && c != '\n');

	for(line = 1; ; line++) {
		double delay;
		size_t blk;
		char nl;
		if (fscanf(tfile, "%lf %zu%c\n", &delay, &blk, &nl) != 3 ||
				                                 nl != '\n') {
			if (feof(tfile))
				break;
			if (ferror(tfile))
				err(EXIT_FAILURE,
					_("failed to read timing file %s"), tname);
			errx(EXIT_FAILURE,
				_("timing file %s: line %lu: unexpected format"),
				tname, line);
		}
		delay /= divi;

		if (maxdelayopt && delay > maxdelay)
			delay = maxdelay;

		if (delay > SCRIPT_MIN_DELAY)
			delay_for(delay);

		emit(sfile, sname, blk);
	}

	fclose(sfile);
	fclose(tfile);
	printf("\n");
	exit(EXIT_SUCCESS);
}
