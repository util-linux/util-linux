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

#define RENAME_EXIT_SOMEOK	2
#define RENAME_EXIT_NOTHING	4
#define RENAME_EXIT_UNEXPLAINED	64

static int do_rename(char *from, char *to, char *s, int verbose, int symtarget)
{
	char *newname = NULL, *where, *p, *q, *target = NULL;
	int flen, tlen, slen, ret = 1;
	struct stat sb;

	if (symtarget) {
		if (lstat(s, &sb) == -1) {
			warn(_("%s: lstat failed"), s);
			return 2;
		}
		if (!S_ISLNK(sb.st_mode)) {
			warnx(_("%s: not a symbolic link"), s);
			return 2;
		}

		target = xmalloc(sb.st_size + 1);
		if (readlink(s, target, sb.st_size + 1) < 0) {
			warn(_("%s: readlink failed"), s);
			ret = 2;
			goto out;
		}

		target[sb.st_size] = '\0';
		where = strstr(target, from);
	} else {
		char *file;

		file = rindex(s, '/');
		if (file == NULL)
			file = s;
		where = strstr(file, from);
	}
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
		if (0 > unlink(s)) {
			warn(_("%s: unlink failed"), s);
			ret = 2;
			goto out;
		}
		if (symlink(newname, s) != 0) {
			warn(_("%s: symlinking to %s failed"), s, newname);
			ret = 2;
			goto out;
		}
		if (verbose)
			printf("%s: `%s' -> `%s'\n", s, target, newname);
	} else {
		if (rename(s, newname) != 0) {
			warn(_("%s: rename to %s failed"), s, newname);
			ret = 2;
			goto out;
		}
		if (verbose)
			printf("`%s' -> `%s'\n", s, newname);
	}
 out:
	free(newname);
	free(target);
	return ret;
}

static void __attribute__ ((__noreturn__)) usage(FILE * out)
{
	fputs(USAGE_HEADER, out);
	fprintf(out,
	      _(" %s [options] expression replacement file...\n"),
		program_invocation_short_name);
	fputs(USAGE_OPTIONS, out);
	fputs(_(" -v, --verbose    explain what is being done\n"), out);
	fputs(_(" -s, --symlink    act on symlink target\n"), out);
	fputs(USAGE_SEPARATOR, out);
	fputs(USAGE_HELP, out);
	fputs(USAGE_VERSION, out);
	fprintf(out, USAGE_MAN_TAIL("rename(1)"));
	exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}

int main(int argc, char **argv)
{
	char *from, *to;
	int i, c, ret = 0, symtarget = 0, verbose = 0;

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
		ret |= do_rename(from, to, argv[i], verbose, symtarget);

	switch (ret) {
	case 0:
		return RENAME_EXIT_NOTHING;
	case 1:
		return EXIT_SUCCESS;
	case 2:
		return EXIT_FAILURE;
	case 3:
		return RENAME_EXIT_SOMEOK;
	default:
		return RENAME_EXIT_UNEXPLAINED;
	}
}
