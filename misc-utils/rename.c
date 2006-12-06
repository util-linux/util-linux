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
#include "nls.h"

static char *progname;

static int
do_rename(char *from, char *to, char *s) {
	char *newname, *where, *p, *q;
	int flen, tlen, slen;

	where = strstr(s, from);
	if (where == NULL)
		return 0;

	flen = strlen(from);
	tlen = strlen(to);
	slen = strlen(s);
	newname = malloc(tlen+slen+1);
	if (newname == NULL) {
		fprintf(stderr, _("%s: out of memory\n"), progname);
		exit(1);
	}

	p = s;
	q = newname;
	while (p < where)
		*q++ = *p++;
	p = to;
	while (*p)
		*q++ = *p++;
	p = where+flen;
	while (*p)
		*q++ = *p++;
	*q = 0;

	if (rename(s, newname) != 0) {
		int errsv = errno;
		fprintf(stderr, _("%s: renaming %s to %s failed: %s\n"),
				  progname, s, newname, strerror(errsv));
		exit(1);
	}

	return 1;
}

int
main(int argc, char **argv) {
	char *from, *to, *p;
	int i, ct;

	progname = argv[0];
	if ((p = strrchr(progname, '/')) != NULL)
		progname = p+1;

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

	if (argc == 2) {
		if (!strcmp(argv[1], "-V") || !strcmp(argv[1], "--version")) {
			printf(_("%s from %s\n"),
			       progname, util_linux_version);
			return 0;
		}
	}

	if (argc < 3) {
		fprintf(stderr, _("call: %s from to files...\n"), progname);
		exit(1);
	}

	from = argv[1];
	to = argv[2];

	ct = 0;
	for (i=3; i<argc; i++)
		ct += do_rename(from, to, argv[i]);
	return 0;
}
