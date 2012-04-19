/*-*- Mode: C; c-basic-offset: 8; indent-tabs-mode: t -*-*/

/*
 * wdctl(8) - show hardware watchdog status
 *
 * Copyright (C) 2012 Lennart Poettering
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <linux/watchdog.h>
#include <sys/ioctl.h>
#include <getopt.h>
#include <stdio.h>
#include <unistd.h>

#include "nls.h"
#include "c.h"
#include "closestream.h"
#include "pathnames.h"

static const struct {
	uint32_t flag;
	const char *name;
} flag_names[] = {
	{ WDIOF_OVERHEAT,      N_("OVERHEAT: Reset due to CPU overheat") },
	{ WDIOF_FANFAULT,      N_("FANFAULT: Fan failed") },
	{ WDIOF_EXTERN1,       N_("EXTERN1: External relay 1") },
	{ WDIOF_EXTERN2,       N_("EXTERN2: External relay 2") },
	{ WDIOF_POWERUNDER,    N_("POWERUNDER: Power bad/power fault") },
	{ WDIOF_CARDRESET,     N_("CARDRESET: Card previously reset the CPU") },
	{ WDIOF_POWEROVER,     N_("POWEROVER: Power over voltage") },
	{ WDIOF_SETTIMEOUT,    N_("SETTIMEOUT: Set timeout (in seconds)") },
	{ WDIOF_MAGICCLOSE,    N_("MAGICCLOSE: Supports magic close char") },
	{ WDIOF_PRETIMEOUT,    N_("PRETIMEOUT: Pretimeout (in seconds)") },
	{ WDIOF_KEEPALIVEPING, N_("KEEPALIVEPING: Keep alive ping reply") }
};

static void usage(int status)
{
	FILE *out = status == EXIT_SUCCESS ? stdout : stderr;

	fputs(USAGE_HEADER, out);
	fprintf(out,
	      _(" %s [options]\n"), program_invocation_short_name);

	fputs(USAGE_OPTIONS, out);
	fprintf(out,
	      _(" -d, --device <path>   device to use (default is %s)\n"), _PATH_WATCHDOG_DEV);

	fputs(USAGE_SEPARATOR, out);
	fputs(USAGE_HELP, out);
	fputs(USAGE_VERSION, out);
	fprintf(out, USAGE_MAN_TAIL("wdctl(1)"));

	exit(status);
}

static void print_options(uint32_t options)
{
	unsigned i;

	if (options == 0) {
		puts(_("\tNo flags set."));
		return;
	}

	for (i = 0; i < ARRAY_SIZE(flag_names); i++) {
		if (options & flag_names[i].flag)
			printf("\t%s\n", _(flag_names[i].name));
		options &= ~flag_names[i].flag;
	}

	if (options)
		printf(_("\tUnknown flags 0x%x\n"), options);
}

int main(int argc, char *argv[])
{
	static const struct option longopts[] = {
		{ "help",    no_argument,       0, 'h' },
		{ "version", no_argument,       0, 'V'},
		{ "device",  required_argument, 0, 'd' },
		{ NULL, 0, 0, 0 }
	};

	int c, status, sec, fd;
	const char *device = _PATH_WATCHDOG_DEV;
	struct watchdog_info ident;

	setlocale(LC_MESSAGES, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	while((c = getopt_long(argc, argv, "hVd:", longopts, NULL)) != -1) {

		switch(c) {
		case 'h':
			usage(EXIT_SUCCESS);
		case 'V':
			printf(UTIL_LINUX_VERSION);
			return EXIT_SUCCESS;
		case 'd':
			device = optarg;
			break;
		default:
			usage(EXIT_FAILURE);
		}
	}

	if (optind < argc)
		usage(EXIT_FAILURE);

	fd = open(device, O_WRONLY|O_CLOEXEC);
	if (fd < 0) {
		if (errno == EBUSY)
			errx(EXIT_FAILURE, _("Watchdog already in use, terminating."));

		err(EXIT_FAILURE, _("Failed to open watchdog device"));
	}

        if (ioctl(fd, WDIOC_GETSUPPORT, &ident) >= 0) {
                printf(_("Identity:\n\t%s\n"
			 "Firmware version:\n\t%x\n"
			 "Supported options:\n"),
			 ident.identity,
			 ident.firmware_version);
		print_options(ident.options);
	}

	if (ioctl(fd, WDIOC_GETSTATUS, &status) >= 0) {
		puts(_("Status:"));
		print_options((uint32_t) status);
	}

	if (ioctl(fd, WDIOC_GETBOOTSTATUS, &status) >= 0) {
		puts(_("Boot status:"));
		print_options((uint32_t) status);
	}

	if (ioctl(fd, WDIOC_GETTIMEOUT, &sec) >= 0)
		printf(_("Timeout:\n\t%is\n"), sec);

	if (ioctl(fd, WDIOC_GETPRETIMEOUT, &sec) >= 0)
		printf(_("Pre-Timeout:\n\t%is\n"), sec);

	if (ioctl(fd, WDIOC_GETTIMELEFT, &sec) >= 0)
		printf(_("Time Left:\n\t%is\n"), sec);

	for (;;) {
		/* We just opened this to query the state, not to arm
		 * it hence use the magic close character */

		static const char v = 'V';

		if (write(fd, &v, 1) >= 0)
			break;

		if (errno != EINTR) {
			warn(_("Failed to disarm watchdog"));
			break;
		}

		/* Let's try hard, since if we don't get this right
		 * the machine might end up rebooting. */
	}

	close(fd);

	return EXIT_SUCCESS;
}
