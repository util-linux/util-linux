/*
 * ctrlaltdel.c - Set the function of the Ctrl-Alt-Del combination
 * Created 4-Jul-92 by Peter Orbaek <poe@daimi.aau.dk>
 * 1999-02-22 Arkadiusz Miśkiewicz <misiek@pld.ORG.PL>
 * - added Native Language Support
 */

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/reboot.h>
#include "nls.h"
#include "c.h"
#include "closestream.h"
#include "pathnames.h"
#include "path.h"

#define LINUX_REBOOT_CMD_CAD_ON 0x89ABCDEF
#define LINUX_REBOOT_CMD_CAD_OFF 0x00000000

static void __attribute__((__noreturn__)) usage(void)
{
	FILE *out = stdout;
	fputs(USAGE_HEADER, out);
	fprintf(out, _(" %s hard|soft\n"), program_invocation_short_name);

	fputs(USAGE_SEPARATOR, out);
	fprintf(out, _("Set the function of the Ctrl-Alt-Del combination.\n"));

	fputs(USAGE_OPTIONS, out);
	printf(USAGE_HELP_OPTIONS(16));
	printf(USAGE_MAN_TAIL("ctrlaltdel(8)"));
	exit(EXIT_SUCCESS);
}

static int get_cad(void)
{
	uint64_t val;

	if (ul_path_read_u64(NULL, &val, _PATH_PROC_CTRL_ALT_DEL) != 0)
		err(EXIT_FAILURE, _("cannot read %s"), _PATH_PROC_CTRL_ALT_DEL);

	switch (val) {
	case 0:
		fputs("soft\n", stdout);
		break;
	case 1:
		fputs("hard\n", stdout);
		break;
	default:
		printf("%s hard\n", _("implicit"));
		warnx(_("unexpected value in %s: %ju"), _PATH_PROC_CTRL_ALT_DEL, val);
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}

static int set_cad(const char *arg)
{
	unsigned int cmd;

	if (geteuid()) {
		warnx(_("You must be root to set the Ctrl-Alt-Del behavior"));
		return EXIT_FAILURE;
	}
	if (!strcmp("hard", arg))
		cmd = LINUX_REBOOT_CMD_CAD_ON;
	else if (!strcmp("soft", arg))
		cmd = LINUX_REBOOT_CMD_CAD_OFF;
	else {
		warnx(_("unknown argument: %s"), arg);
		return EXIT_FAILURE;
	}
	if (reboot(cmd) < 0) {
		warnx("reboot");
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}

int main(int argc, char **argv)
{
	int ch, ret;
	static const struct option longopts[] = {
		{"version", no_argument, NULL, 'V'},
		{"help", no_argument, NULL, 'h'},
		{NULL, 0, NULL, 0}
	};

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	while ((ch = getopt_long(argc, argv, "Vh", longopts, NULL)) != -1)
		switch (ch) {
		case 'V':
			printf(UTIL_LINUX_VERSION);
			return EXIT_SUCCESS;
		case 'h':
			usage();
		default:
			errtryhelp(EXIT_FAILURE);
		}

	if (argc < 2)
		ret = get_cad();
	else
		ret = set_cad(argv[1]);
	return ret;
}
