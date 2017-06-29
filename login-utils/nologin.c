/*
 * Copyright (C) 2013 Karel Zak <kzak@redhat.com>
 */

#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <getopt.h>

#include "c.h"
#include "nls.h"
#include "pathnames.h"

/*
 * Always return EXIT_FAILURE (1), don't try to be smart!
 */

static void __attribute__((__noreturn__)) usage(void)
{
	FILE *out = stdout;
	fputs(USAGE_HEADER, out);
	fprintf(out,
		_(" %s [options]\n"), program_invocation_short_name);

	fputs(USAGE_SEPARATOR, out);
	fputs(_("Politely refuse a login.\n"), out);

	fputs(USAGE_OPTIONS, out);
	printf(USAGE_HELP_OPTIONS(16));

	printf(USAGE_MAN_TAIL("nologin(8)"));
	exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
	int c, fd = -1;
	struct stat st;
	static const struct option longopts[] = {
		{ "help",    0, NULL, 'h' },
		{ "version", 0, NULL, 'V' },
		{ NULL, 0, NULL, 0 }
	};

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

	while ((c = getopt_long(argc, argv, "hV", longopts, NULL)) != -1) {
		switch (c) {
		case 'h':
			usage();
			break;
		case 'V':
			printf(UTIL_LINUX_VERSION);
			return EXIT_FAILURE;
		default:
			errtryhelp(EXIT_FAILURE);
		}
	}

	fd = open(_PATH_NOLOGIN_TXT, O_RDONLY);
	if (fd < 0)
		goto dflt;

	c = fstat(fd, &st);
	if (c < 0 || !S_ISREG(st.st_mode))
		goto dflt;
	else {
		char buf[BUFSIZ];
		ssize_t rd;

		while ((rd = read(fd, buf, sizeof(buf))) > 0)
			ignore_result( write(STDOUT_FILENO, buf, rd) );

		close(fd);
		return EXIT_FAILURE;
	}

dflt:
	if (fd >= 0)
		close(fd);
	fprintf(stdout, _("This account is currently not available.\n"));
	return EXIT_FAILURE;
}
