#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "c.h"
#include "nls.h"
#include "partx.h"
#include "sysfs.h"
#include "strutils.h"
#include "closestream.h"

static void __attribute__ ((__noreturn__)) usage(FILE * out)
{
	fputs(USAGE_HEADER, out);
	fprintf(out, _(" %s <disk device> <partition number> <length>\n"),
		program_invocation_short_name);

	fputs(USAGE_SEPARATOR, out);
	fputs(_("Tell the kernel about the new size of a partition.\n"), out);

	fputs(USAGE_OPTIONS, out);
	fputs(USAGE_HELP, out);
	fputs(USAGE_VERSION, out);
	fprintf(out, USAGE_MAN_TAIL("resizepart(8)"));
	exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}

static int get_partition_start(int fd, int partno, uint64_t *start)
{
	struct stat st;
	struct sysfs_cxt disk = UL_SYSFSCXT_EMPTY,
			 part = UL_SYSFSCXT_EMPTY;
	dev_t devno = 0;
	int rc = -1;

	/*
	 * wholedisk
	 */
	if (fstat(fd, &st) || !S_ISBLK(st.st_mode))
		goto done;
	devno = st.st_rdev;
	if (sysfs_init(&disk, devno, NULL))
		goto done;
	/*
	 * partition
	 */
	devno = sysfs_partno_to_devno(&disk, partno);
	if (!devno)
		goto done;
	if (sysfs_init(&part, devno, &disk))
		goto done;
	if (sysfs_read_u64(&part, "start", start))
		goto done;

	rc = 0;
done:
	sysfs_deinit(&part);
	sysfs_deinit(&disk);
	return rc;
}

int main(int argc, char **argv)
{
	int c, fd, partno;
	const char *wholedisk;
	uint64_t start;

	static const struct option longopts[] = {
		{"help", no_argument, 0, 'h'},
		{"version", no_argument, 0, 'V'},
		{NULL, no_argument, 0, '0'},
	};

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	while ((c = getopt_long(argc, argv, "Vh", longopts, NULL)) != -1)
		switch (c) {
		case 'V':
			printf(UTIL_LINUX_VERSION);
			return EXIT_SUCCESS;
		case 'h':
			usage(stdout);
		default:
			usage(stderr);
		}

	if (argc != 4)
		usage(stderr);

	wholedisk = argv[1];
	partno = strtou32_or_err(argv[2], _("invalid partition number argument"));

	if ((fd = open(wholedisk, O_RDONLY)) < 0)
		err(EXIT_FAILURE, _("cannot open %s"), wholedisk);

	if (get_partition_start(fd, partno, &start))
		err(EXIT_FAILURE, _("%s: failed to get start of the partition number %s"),
				wholedisk, argv[2]);

	if (partx_resize_partition(fd, partno, start,
			strtou64_or_err(argv[3], _("invalid length argument"))))
		err(EXIT_FAILURE, _("failed to resize partition"));

	if (close_fd(fd) != 0)
		err(EXIT_FAILURE, _("write failed"));

	return 0;
}
