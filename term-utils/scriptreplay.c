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


#include "c.h"
#include "debug.h"
#include "xalloc.h"
#include "closestream.h"
#include "nls.h"
#include "strutils.h"

static UL_DEBUG_DEFINE_MASK(scriptreplay);
UL_DEBUG_DEFINE_MASKNAMES(scriptreplay) = UL_DEBUG_EMPTY_MASKNAMES;

#define SCRIPTREPLAY_DEBUG_INIT	(1 << 1)
#define SCRIPTREPLAY_DEBUG_TIMING (1 << 2)
#define SCRIPTREPLAY_DEBUG_LOG	(1 << 3)
#define SCRIPTREPLAY_DEBUG_MISC	(1 << 4)
#define SCRIPTREPLAY_DEBUG_ALL	0xFFFF

#define DBG(m, x)       __UL_DBG(scriptreplay, SCRIPTREPLAY_DEBUG_, m, x)
#define ON_DBG(m, x)    __UL_DBG_CALL(scriptreplay, SCRIPTREPLAY_DEBUG_, m, x)

#define SCRIPT_MIN_DELAY 0.0001		/* from original sripreplay.pl */

/*
 * The script replay is driven by timing file where each entry describes one
 * step in the replay. The timing step may refer input or output (or
 * signal, extra informations, etc.)
 *
 * The step data are stored in log files, the right log file for the step is
 * selected from replay_setup.
 *
 * TODO: move struct replay_{log,step,setup} to script-playutils.c to make it
 * usable for scriptlive(1) code.
 */

enum {
	REPLAY_TIMING_SIMPLE,		/* timing info in classic "<delta> <offset>" format */
	REPLAY_TIMING_MULTI		/* multiple streams in format "<type> <delta> <offset|etc> */
};

struct replay_log {
	const char	*streams;		/* 'I'nput, 'O'utput or both */
	const char	*filename;
	FILE		*fp;
};

struct replay_step {
	char	type;		/* 'I'nput, 'O'utput, ... */
	double	delay;
	size_t	size;

	struct replay_log *data;
};

struct replay_setup {
	struct replay_log	*logs;
	size_t			nlogs;

	struct replay_step	step;	/* current step */

	FILE			*timing_fp;
	const char		*timing_filename;
	int			timing_format;
	int			timing_line;
};

static void scriptreplay_init_debug(void)
{
	__UL_INIT_DEBUG_FROM_ENV(scriptreplay, SCRIPTREPLAY_DEBUG_, 0, SCRIPTREPLAY_DEBUG);
}

static int ignore_line(FILE *f)
{
	int c;

	while((c = fgetc(f)) != EOF && c != '\n');
	if (ferror(f))
		return -errno;

	DBG(LOG, ul_debug("  ignore line"));
	return 0;
}

static int replay_set_timing_file(struct replay_setup *stp, const char *filename)
{
	int c, rc = 0;

	assert(stp);
	assert(filename);

	stp->timing_filename = filename;
	stp->timing_line = 0;

	stp->timing_fp = fopen(filename, "r");
	if (!stp->timing_fp)
		rc = -errno;
	else {
		/* detect timing file format */
		c = fgetc(stp->timing_fp);
		if (c != EOF) {
			if (isdigit((unsigned int) c))
				stp->timing_format = REPLAY_TIMING_SIMPLE;
			else
				stp->timing_format = REPLAY_TIMING_MULTI;
			ungetc(c, stp->timing_fp);
		} else if (ferror(stp->timing_fp))
			rc = -errno;
	}

	if (rc) {
		fclose(stp->timing_fp);
		stp->timing_fp = NULL;
	}

	DBG(TIMING, ul_debug("timing file set to %s [rc=%d]", filename, rc));
	return rc;
}

static int replay_associate_log(struct replay_setup *stp,
				const char *streams, const char *filename)
{
	struct replay_log *log;
	int rc;

	assert(stp);
	assert(streams);
	assert(filename);

	stp->logs = xrealloc(stp->logs, (stp->nlogs + 1) *  sizeof(*log));
	log = &stp->logs[stp->nlogs];
	stp->nlogs++;

	log->filename = filename;
	log->streams = streams;

	/* open the file and skip the first line */
	log->fp = fopen(filename, "r");
	rc = log->fp == NULL ? -errno : ignore_line(log->fp);

	DBG(LOG, ul_debug("accociate log file %s with '%s' [rc=%d]", filename, streams, rc));
	return rc;
}

static int is_wanted_stream(char type, const char *streams)
{
	if (streams == NULL)
		return 1;
	if (strchr(streams, type))
		return 1;
	return 0;
}

static int read_multistream_step(struct replay_step *step, FILE *f, char type)
{
	int rc = 0;
	char nl;

	switch (type) {
	case 'O': /* output */
	case 'I': /* input */
		rc = fscanf(f, "%lf %zu%c\n", &step->delay, &step->size, &nl);
		if (rc != 3 || nl != '\n')
			rc = -EINVAL;
		else
			rc = 0;
		break;

	case 'S': /* signal */
		rc = ignore_line(f);	/* not implemnted yet */
		break;

	case 'H': /* header */
		rc = ignore_line(f);	/* not implemnted yet */
		break;
	default:
		break;
	}

	DBG(TIMING, ul_debug(" read step delay & size [rc=%d]", rc));
	return rc;
}

static struct replay_log *replay_get_stream_log(struct replay_setup *stp, char stream)
{
	size_t i;

	for (i = 0; i < stp->nlogs; i++) {
		struct replay_log *log = &stp->logs[i];

		if (is_wanted_stream(stream, log->streams))
			return log;
	}
	return NULL;
}

static int replay_seek_log(struct replay_log *log, size_t move)
{
	DBG(LOG, ul_debug(" %s: seek ++ %zu", log->filename, move));
	return fseek(log->fp, move, SEEK_CUR) == (off_t) -1 ? -errno : 0;
}

/* returns next step with pointer to the right log file for specified streams (e.g.
 * "IOS" for in/out/signals) or all streams if stream is NULL.
 *
 * returns: 0 = success, <0 = error, 1 = done (EOF)
 */
static int replay_get_next_step(struct replay_setup *stp, char *streams, struct replay_step **xstep)
{
	struct replay_step *step;
	int rc;
	double ignored_delay = 0;

	assert(stp);
	assert(stp->timing_fp);
	assert(xstep && *xstep);

	/* old format supports only 'O'utput */
	if (stp->timing_format == REPLAY_TIMING_SIMPLE &&
	    !is_wanted_stream('O', streams))
		return 1;


	step = &stp->step;
	*xstep = NULL;

	do {
		struct replay_log *log = NULL;

		rc = 1;	/* done */
		if (feof(stp->timing_fp))
			break;

		DBG(TIMING, ul_debug("reading next step"));

		memset(step, 0, sizeof(*step));
		stp->timing_line++;

		switch (stp->timing_format) {
		case REPLAY_TIMING_SIMPLE:
			/* old format supports only output entries and format is the same
			 * as new format, but without <type> prefix */
			rc = read_multistream_step(step, stp->timing_fp, 'O');
			if (rc == 0)
				step->type = 'O';	/* 'O'utput */
			break;
		case REPLAY_TIMING_MULTI:
			rc = fscanf(stp->timing_fp, "%c ", &step->type);
			if (rc != 1)
				rc = -EINVAL;
			else
				rc = read_multistream_step(step,
						stp->timing_fp,
						step->type);
			break;
		}

		if (rc)
			break;;		/* error */

		DBG(TIMING, ul_debug(" step entry is '%c'", step->type));

		log = replay_get_stream_log(stp, step->type);
		if (log) {
			if (is_wanted_stream(step->type, streams)) {
				step->data = log;
				*xstep = step;
				DBG(LOG, ul_debug(" use %s as data source", log->filename));
				goto done;
			}
			/* The step entry is unwanted, but we keep the right
			 * position in the log file although the data are ignored.
			 */
			replay_seek_log(log, step->size);
		} else
			DBG(TIMING, ul_debug(" not found log for '%c' stream", step->type));

		DBG(TIMING, ul_debug(" ignore step '%c' [delay=%f]",
					step->type, step->delay));
		ignored_delay += step->delay;
	} while (rc == 0);

done:
	if (ignored_delay)
		step->delay += ignored_delay;

	DBG(TIMING, ul_debug("reading next step done [rc=%d delay=%f (ignored=%f) size=%zu]",
				rc, step->delay, ignored_delay, step->size));
	return rc;
}

/* return: 0 = success, <0 = error, 1 = done (EOF) */
static int replay_emit_step_data(struct replay_step *step, int fd)
{
	size_t ct;
	int rc = 0;
	char buf[BUFSIZ];

	assert(step);
	assert(step->size);
	assert(step->data);
	assert(step->data->fp);

	for (ct = step->size; ct > 0; ) {
		size_t len, cc;

		cc = ct > sizeof(buf) ? sizeof(buf): ct;
		len = fread(buf, 1, cc, step->data->fp);

		if (!len) {
			DBG(LOG, ul_debug("log data emit: failed to read log %m"));
			break;
		}

		ct -= len;
		cc = write(fd, buf, len);
		if (cc != len) {
			rc = -errno;
			DBG(LOG, ul_debug("log data emit: failed write data %m"));
			break;
		}
	}

	if (ct && ferror(step->data->fp))
		rc = -errno;
	if (ct && feof(step->data->fp))
		rc = 1;

	DBG(LOG, ul_debug("log data emited [rc=%d size=%zu]", rc, step->size));
	return rc;
}

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
	fputs(_(" -t, --timing <file>     script timing log file\n"), out);
	fputs(_(" -s, --typescript <file> script data log file\n"), out);
	fputs(_(" -d, --divisor <num>     speed up or slow down execution with time divisor\n"), out);
	fputs(_(" -m, --maxdelay <num>    wait at most this many seconds between updates\n"), out);
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

	DBG(TIMING, ul_debug("going to sleep for %fs", delay));

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

int
main(int argc, char *argv[])
{
	struct replay_setup setup = { .nlogs = 0 };
	struct replay_step *step;
	int rc;

	const char *sname = NULL, *tname = NULL;
	double divi = 1, maxdelay = 0;
	int diviopt = FALSE, maxdelayopt = FALSE, idx;
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
	close_stdout_atexit();

	scriptreplay_init_debug();

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
			print_version(EXIT_SUCCESS);
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

	if (replay_set_timing_file(&setup, tname) != 0)
		err(EXIT_FAILURE, _("cannot open %s"), tname);

	if (replay_associate_log(&setup, "O", sname) != 0)
		err(EXIT_FAILURE, _("cannot open %s"), sname);

	do {
		rc = replay_get_next_step(&setup, "O", &step);
		if (rc)
			break;

		step->delay /= divi;
		if (maxdelayopt && step->delay > maxdelay)
			step->delay = maxdelay;
		if (step->delay > SCRIPT_MIN_DELAY)
			delay_for(step->delay);

		rc = replay_emit_step_data(step, STDOUT_FILENO);
	} while (rc == 0);

	if (step && rc < 0)
		err(EXIT_FAILURE, _("%s: log file error"), step->data->filename);
	else if (rc < 0)
		err(EXIT_FAILURE, _("%s: line %d: timing file error"),
				setup.timing_filename,
				setup.timing_line);
	printf("\n");
	exit(EXIT_SUCCESS);
}
