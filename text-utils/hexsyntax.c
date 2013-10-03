/*-
 * Copyright (c) 1990 The Regents of the University of California.
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
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <err.h>
#include <limits.h>
#include <getopt.h>
#include "hexdump.h"
#include "nls.h"
#include "strutils.h"
#include "c.h"

off_t skip;				/* bytes to skip */


void
newsyntax(int argc, char ***argvp)
{
	int ch;
	char **argv;

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

	argv = *argvp;
	while ((ch = getopt_long(argc, argv, "bcCde:f:n:os:vxhV", longopts, NULL)) != -1) {
		switch (ch) {
		case 'b':
			add("\"%07.7_Ax\n\"");
			add("\"%07.7_ax \" 16/1 \"%03o \" \"\\n\"");
			break;
		case 'c':
			add("\"%07.7_Ax\n\"");
			add("\"%07.7_ax \" 16/1 \"%3_c \" \"\\n\"");
			break;
		case 'C':
			add("\"%08.8_Ax\n\"");
			add("\"%08.8_ax  \" 8/1 \"%02x \" \"  \" 8/1 \"%02x \" ");
			add("\"  |\" 16/1 \"%_p\" \"|\\n\"");
			break;
		case 'd':
			add("\"%07.7_Ax\n\"");
			add("\"%07.7_ax \" 8/2 \"  %05u \" \"\\n\"");
			break;
		case 'e':
			add(optarg);
			break;
		case 'f':
			addfile(optarg);
			break;
		case 'n':
			length = strtosize_or_err(optarg, _("failed to parse length"));
			break;
		case 'o':
			add("\"%07.7_Ax\n\"");
			add("\"%07.7_ax \" 8/2 \" %06o \" \"\\n\"");
			break;
		case 's':
			skip = strtosize_or_err(optarg, _("failed to parse offset"));
			break;
		case 'v':
			vflag = ALL;
			break;
		case 'x':
			add("\"%07.7_Ax\n\"");
			add("\"%07.7_ax \" 8/2 \"   %04x \" \"\\n\"");
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

	if (!fshead) {
		add("\"%07.7_Ax\n\"");
		add("\"%07.7_ax \" 8/2 \"%04x \" \"\\n\"");
	}

	*argvp += optind;
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
