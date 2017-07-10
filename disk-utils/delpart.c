#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>

#include "c.h"
#include "nls.h"
#include "partx.h"
#include "strutils.h"

static void __attribute__((__noreturn__)) usage(void)
{
	FILE *out = stdout;
	fputs(USAGE_HEADER, out);
	fprintf(out, _(" %s <disk device> <partition number>\n"),
		program_invocation_short_name);

	fputs(USAGE_SEPARATOR, out);
	fputs(_("Tell the kernel to forget about a specified partition.\n"), out);

	fputs(USAGE_OPTIONS, out);
	printf(USAGE_HELP_OPTIONS(16));
	printf(USAGE_MAN_TAIL("delpart(8)"));
	exit(EXIT_SUCCESS);
}

int main(int argc, char **argv)
{
	int c, fd;

	static const struct option longopts[] = {
		{"help",    no_argument, NULL, 'h'},
		{"version", no_argument, NULL, 'V'},
		{NULL, 0, NULL, 0},
	};

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

	while ((c = getopt_long(argc, argv, "Vh", longopts, NULL)) != -1)
		switch (c) {
		case 'V':
			printf(UTIL_LINUX_VERSION);
			return EXIT_SUCCESS;
		case 'h':
			usage();
		default:
			errtryhelp(EXIT_FAILURE);
		}

	if (argc != 3) {
		warnx(_("not enough arguments"));
		errtryhelp(EXIT_FAILURE);
	}


	if ((fd = open(argv[1], O_RDONLY)) < 0)
		err(EXIT_FAILURE, _("cannot open %s"), argv[1]);

	if (partx_del_partition(fd,
			strtou32_or_err(argv[2], _("invalid partition number argument"))))
		err(EXIT_FAILURE, _("failed to remove partition"));

	return EXIT_SUCCESS;
}
