/*
 * rename.c - aeb 2000-01-01
 *
--------------------------------------------------------------
#!/bin/sh
if [ $# -le 2 ]; then echo call: rename from to files; exit; fi
FROM="$1"
TO="$2"
shift
shift
for i in $@; do N=`echo "$i" | sed "s/$FROM/$TO/g"`; mv "$i" "$N"; done
--------------------------------------------------------------
 * This shell script will do renames of files, but may fail
 * in cases involving special characters. Here a C version.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <getopt.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "nls.h"
#include "xalloc.h"
#include "c.h"
#include "closestream.h"

static int do_rename(char *from, char *to, char *s, int verbose, int symtarget)
{
	char *newname, *where, *p, *q, *target = NULL;
	int flen, tlen, slen;
	struct stat sb;

	if (symtarget) {
		if (lstat(s, &sb) == -1)
			err(EXIT_FAILURE, _("%s: lstat failed"), s);

		if (!S_ISLNK(sb.st_mode))
			errx(EXIT_FAILURE, _("%s: not a symbolic link"), s);

		target = xmalloc(sb.st_size + 1);
		if (readlink(s, target, sb.st_size + 1) < 0)
			err(EXIT_FAILURE, _("%s: readlink failed"), s);

		target[sb.st_size] = '\0';
		where = strstr(target, from);
	} else
		where = strstr(s, from);

	if (where == NULL) {
		free(target);
		return 0;
	}

	flen = strlen(from);
	tlen = strlen(to);
	if (symtarget) {
		slen = strlen(target);
		p = target;
	} else {
		slen = strlen(s);
		p = s;
	}
	newname = xmalloc(tlen + slen + 1);

	q = newname;
	while (p < where)
		*q++ = *p++;
	p = to;
	while (*p)
		*q++ = *p++;
	p = where + flen;
	while (*p)
		*q++ = *p++;
	*q = 0;

	if (symtarget) {
		if (0 > unlink(s))
			err(EXIT_FAILURE, _("%s: unlink failed"), s);
		if (symlink(newname, s) != 0)
			err(EXIT_FAILURE, _("%s: symlinking to %s failed"), s, newname);
		if (verbose)
			printf("%s: `%s' -> `%s'\n", s, target, newname);
	} else {
		if (rename(s, newname) != 0)
			err(EXIT_FAILURE, _("%s: rename to %s failed"), s, newname);
		if (verbose)
			printf("`%s' -> `%s'\n", s, newname);
	}

	free(newname);
	free(target);
	return 1;
}

static void __attribute__ ((__noreturn__)) usage(FILE * out)
{
	fputs(USAGE_HEADER, out);
	fprintf(out,
	      _(" %s [options] <expression> <replacement> <file>...\n"),
		program_invocation_short_name);
	fputs(USAGE_OPTIONS, out);
	fputs(_(" -v, --verbose    explain what is being done\n"), out);
	fputs(_(" -s, --symlink    act on the target of symlinks\n"), out);
	fputs(USAGE_SEPARATOR, out);
	fputs(USAGE_HELP, out);
	fputs(USAGE_VERSION, out);
	fprintf(out, USAGE_MAN_TAIL("rename(1)"));
	exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}

int main(int argc, char **argv)
{
	char *from, *to;
	int i, c, symtarget=0, verbose = 0;

	static const struct option longopts[] = {
		{"verbose", no_argument, NULL, 'v'},
		{"version", no_argument, NULL, 'V'},
		{"help", no_argument, NULL, 'h'},
		{"symlink", no_argument, NULL, 's'},
		{NULL, 0, NULL, 0}
	};

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	while ((c = getopt_long(argc, argv, "vsVh", longopts, NULL)) != -1)
		switch (c) {
		case 'v':
			verbose = 1;
			break;
		case 's':
			symtarget = 1;
			break;
		case 'V':
			printf(UTIL_LINUX_VERSION);
			return EXIT_SUCCESS;
		case 'h':
			usage(stdout);
		default:
			usage(stderr);
		}

	argc -= optind;
	argv += optind;

	if (argc < 3) {
		warnx(_("not enough arguments"));
		usage(stderr);
	}

	from = argv[0];
	to = argv[1];

	for (i = 2; i < argc; i++)
		do_rename(from, to, argv[i], verbose, symtarget);

	return EXIT_SUCCESS;
}
