/*
 * Copyright (c) 1989 The Regents of the University of California.
 * All rights reserved.
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

 /* 1999-02-22 Arkadiusz Miśkiewicz <misiek@pld.ORG.PL>
  * - added Native Language Support
  */

#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <err.h>
#include <limits.h>
#include <getopt.h>
#include "hexdump.h"

#include "list.h"
#include "nls.h"
#include "c.h"
#include "strutils.h"
#include "closestream.h"

struct list_head fshead;				/* head of format strings */
ssize_t blocksize;			/* data block size */
int exitval;				/* final exit value */
ssize_t length = -1;			/* max bytes to read */
void hex_free(void);

off_t skip;				/* bytes to skip */

int
parse_args(int argc, char **argv)
{
	int ch;
	char *hex_offt = "\"%07.7_Ax\n\"";

	static const struct option longopts[] = {
		{"one-byte-octal", no_argument, NULL, 'b'},
		{"one-byte-char", required_argument, NULL, 'c'},
		{"canonical", required_argument, NULL, 'C'},
		{"two-bytes-decimal", no_argument, NULL, 'd'},
		{"two-bytes-octal", required_argument, NULL, 'o'},
		{"two-bytes-hex", no_argument, NULL, 'x'},
		{"format", required_argument, NULL, 'e'},
		{"format-file", required_argument, NULL, 'f'},
		{"length", required_argument, NULL, 'n'},
		{"skip", required_argument, NULL, 's'},
		{"no-squeezing", no_argument, NULL, 'v'},
		{"help", no_argument, NULL, 'h'},
		{"version", no_argument, NULL, 'V'},
		{NULL, no_argument, NULL, 0}
	};

	while ((ch = getopt_long(argc, argv, "bcCde:f:L::n:os:vxhV", longopts, NULL)) != -1) {
		switch (ch) {
		case 'b':
			add_fmt(hex_offt);
			add_fmt("\"%07.7_ax \" 16/1 \"%03o \" \"\\n\"");
			break;
		case 'c':
			add_fmt(hex_offt);
			add_fmt("\"%07.7_ax \" 16/1 \"%3_c \" \"\\n\"");
			break;
		case 'C':
			add_fmt("\"%08.8_Ax\n\"");
			add_fmt("\"%08.8_ax  \" 8/1 \"%02x \" \"  \" 8/1 \"%02x \" ");
			add_fmt("\"  |\" 16/1 \"%_p\" \"|\\n\"");
			break;
		case 'd':
			add_fmt(hex_offt);
			add_fmt("\"%07.7_ax \" 8/2 \"  %05u \" \"\\n\"");
			break;
		case 'e':
			add_fmt(optarg);
			break;
		case 'f':
			addfile(optarg);
			break;
		case 'n':
			length = strtosize_or_err(optarg, _("failed to parse length"));
			break;
		case 'o':
			add_fmt(hex_offt);
			add_fmt("\"%07.7_ax \" 8/2 \" %06o \" \"\\n\"");
			break;
		case 's':
			skip = strtosize_or_err(optarg, _("failed to parse offset"));
			break;
		case 'v':
			vflag = ALL;
			break;
		case 'x':
			add_fmt(hex_offt);
			add_fmt("\"%07.7_ax \" 8/2 \"   %04x \" \"\\n\"");
			break;
		case 'h':
			usage(stdout);
		case 'V':
			printf(UTIL_LINUX_VERSION);
			exit(EXIT_SUCCESS);
			break;
		default:
			usage(stderr);
		}
	}

	if (list_empty(&fshead)) {
		add_fmt(hex_offt);
		add_fmt("\"%07.7_ax \" 8/2 \"%04x \" \"\\n\"");
	}
	return optind;
}

void __attribute__((__noreturn__)) usage(FILE *out)
{
	fputs(USAGE_HEADER, out);
	fprintf(out, _(" %s [options] <file>...\n"), program_invocation_short_name);
	fputs(USAGE_OPTIONS, out);
	fputs(_(" -b, --one-byte-octal      one-byte octal display\n"), out);
	fputs(_(" -c, --one-byte-char       one-byte character display\n"), out);
	fputs(_(" -C, --canonical           canonical hex+ASCII display\n"), out);
	fputs(_(" -d, --two-bytes-decimal   two-byte decimal display\n"), out);
	fputs(_(" -o, --two-bytes-octal     two-byte octal display\n"), out);
	fputs(_(" -x, --two-bytes-hex       two-byte hexadecimal display\n"), out);
	fputs(_(" -e, --format <format>     format string to be used for displaying data\n"), out);
	fputs(_(" -f, --format-file <file>  file that contains format strings\n"), out);
	fputs(_(" -n, --length <length>     interpret only length bytes of input\n"), out);
	fputs(_(" -s, --skip <offset>       skip offset bytes from the beginning\n"), out);
	fputs(_(" -v, --no-squeezing        output identical lines\n"), out);
	fputs(USAGE_SEPARATOR, out);
	fputs(USAGE_HELP, out);
	fputs(USAGE_VERSION, out);
	fprintf(out, USAGE_MAN_TAIL("hexdump(1)"));

	exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}

int main(int argc, char **argv)
{
	struct list_head *p;
	FS *tfs;
	char *c;
	INIT_LIST_HEAD(&fshead);

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	if (!(c = strrchr(argv[0], 'o')) || strcmp(c, "od")) {
		argv += parse_args(argc, argv);
	} else
		errx(EXIT_FAILURE, _("calling hexdump as od has been deprecated "
				     "in favour to GNU coreutils od."));

	/* figure out the data block size */
	blocksize = 0;
	list_for_each(p, &fshead) {
		tfs = list_entry(p, FS, fslist);
		if ((tfs->bcnt = block_size(tfs)) > blocksize)
			blocksize = tfs->bcnt;
	}

	/* rewrite the rules, do syntax checking */
	list_for_each(p, &fshead)
		rewrite(list_entry(p, FS, fslist));

	next(argv);
	display();
	hex_free();
	return exitval;
}

void hex_free(void)
{
	struct list_head *p, *pn, *q, *qn, *r, *rn;
	FS *fs;
	FU *fu;
	PR *pr;
	list_for_each_safe(p, pn, &fshead) {
		fs = list_entry(p, FS, fslist);
		list_for_each_safe(q, qn, &fs->fulist) {
			fu = list_entry(q, FU, fulist);
			list_for_each_safe(r, rn, &fu->prlist) {
				pr = list_entry(r, PR, prlist);
				free(pr->fmt);
				free(pr);
			}
			free(fu->fmt);
			free(fu);
		}
		free(fs);
	}
}
