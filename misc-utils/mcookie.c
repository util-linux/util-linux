/* mcookie.c -- Generates random numbers for xauth
 * Created: Fri Feb  3 10:42:48 1995 by faith@cs.unc.edu
 * Revised: Fri Mar 19 07:48:01 1999 by faith@acm.org
 * Public Domain 1995, 1999 Rickard E. Faith (faith@acm.org)
 * This program comes with ABSOLUTELY NO WARRANTY.
 * 
 * This program gathers some random bits of data and used the MD5
 * message-digest algorithm to generate a 128-bit hexadecimal number for
 * use with xauth(1).
 *
 * NOTE: Unless /dev/random is available, this program does not actually
 * gather 128 bits of random information, so the magic cookie generated
 * will be considerably easier to guess than one might expect.
 *
 * 1999-02-22 Arkadiusz Miśkiewicz <misiek@pld.ORG.PL>
 * - added Native Language Support
 * 1999-03-21 aeb: Added some fragments of code from Colin Plumb.
 *
 */

#include "c.h"
#include "md5.h"
#include "nls.h"
#include "closestream.h"
#include "randutils.h"
#include "xalloc.h"

#include <fcntl.h>
#include <getopt.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>

enum {
	BUFFERSIZE = 4096,
	RAND_BYTES = 128
};

/* The basic function to hash a file */
static size_t hash_file(struct MD5Context *ctx, int fd)
{
	size_t count = 0;
	ssize_t r;
	unsigned char buf[BUFFERSIZE];

	while ((r = read(fd, buf, sizeof(buf))) > 0) {
		MD5Update(ctx, buf, r);
		count += r;
	}
	/* Separate files with a null byte */
	buf[0] = '\0';
	MD5Update(ctx, buf, 1);
	return count;
}

static void __attribute__ ((__noreturn__)) usage(FILE * out)
{
	fputs(_("\nUsage:\n"), out);
	fprintf(out,
	      _(" %s [options]\n"), program_invocation_short_name);

	fputs(_("\nOptions:\n"), out);
	fputs(_(" -f, --file <file> use file as a cookie seed\n"
		" -v, --verbose     explain what is being done\n"
		" -V, --version     output version information and exit\n"
		" -h, --help        display this help and exit\n\n"), out);

	exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}

static void randomness_from_files(char **files, int nfiles,
				  struct MD5Context *ctx, int verbose)
{
	int fd, i;
	size_t count = 0;

	for (i = 0; i < nfiles; i++) {
		if (files[i][0] == '-' && !files[i][1])
			fd = STDIN_FILENO;
		else
			fd = open(files[i], O_RDONLY);

		if (fd < 0) {
			warn(_("cannot open %s"), files[i]);
		} else {
			count = hash_file(ctx, fd);
			if (verbose)
				fprintf(stderr,
					P_("Got %zu byte from %s\n",
					   "Got %zu bytes from %s\n", count),
					count, files[i]);

			if (fd != STDIN_FILENO)
				if (close(fd))
					err(EXIT_FAILURE,
					    _("closing %s failed"), files[i]);
		}
	}
}

int main(int argc, char **argv)
{
	size_t i;
	struct MD5Context ctx;
	unsigned char digest[MD5LENGTH];
	unsigned char buf[RAND_BYTES];
	char **files = NULL;
	int nfiles;
	int c;
	int verbose = 0;

	static const struct option longopts[] = {
		{"file", required_argument, NULL, 'f'},
		{"verbose", no_argument, NULL, 'v'},
		{"version", no_argument, NULL, 'V'},
		{"help", no_argument, NULL, 'h'},
		{NULL, 0, NULL, 0}
	};

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	MD5Init(&ctx);

	if (2 < argc) {
		files = xmalloc(sizeof(char *) * argc);
		nfiles = 0;
	}

	while ((c =
		getopt_long(argc, argv, "f:vVh", longopts, NULL)) != -1)
		switch (c) {
		case 'v':
			verbose = 1;
			break;
		case 'f':
			files[nfiles] = optarg;
			nfiles++;
			break;
		case 'V':
			printf(UTIL_LINUX_VERSION);
			return EXIT_SUCCESS;
		case 'h':
			usage(stdout);
		default:
			usage(stderr);
		}

	randomness_from_files(files, nfiles, &ctx, verbose);
	free(files);

	random_get_bytes(&buf, RAND_BYTES);
	MD5Update(&ctx, buf, RAND_BYTES);
	if (verbose)
		fprintf(stderr,
			_("Got %d bytes from %s\n"), RAND_BYTES, random_tell_source());

	MD5Final(digest, &ctx);
	for (i = 0; i < MD5LENGTH; i++)
		printf("%02x", digest[i]);
	putchar('\n');

	return EXIT_SUCCESS;
}
