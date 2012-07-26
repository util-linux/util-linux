/*
 * Copyright (C) 2011 Karel Zak <kzak@redhat.com>
 * Originally from Ted's losetup.c
 *
 * losetup.c - setup and control loop devices
 */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <inttypes.h>
#include <getopt.h>

#include "c.h"
#include "nls.h"
#include "strutils.h"
#include "loopdev.h"
#include "xgetpass.h"
#include "closestream.h"
#include "optutils.h"

enum {
	A_CREATE = 1,		/* setup a new device */
	A_DELETE,		/* delete given device(s) */
	A_DELETE_ALL,		/* delete all devices */
	A_SHOW,			/* list devices */
	A_SHOW_ONE,		/* print info about one device */
	A_FIND_FREE,		/* find first unused */
	A_SET_CAPACITY,		/* set device capacity */
};

static int verbose;


static int printf_loopdev(struct loopdev_cxt *lc)
{
	uint64_t x;
	dev_t dev = 0;
	ino_t ino = 0;
	char *fname = NULL;
	uint32_t type;

	fname = loopcxt_get_backing_file(lc);
	if (!fname)
		return -EINVAL;

	if (loopcxt_get_backing_devno(lc, &dev) == 0)
		loopcxt_get_backing_inode(lc, &ino);

	if (!dev && !ino) {
		/*
		 * Probably non-root user (no permissions to
		 * call LOOP_GET_STATUS ioctls).
		 */
		printf("%s: []: (%s)",
			loopcxt_get_device(lc), fname);

		if (loopcxt_get_offset(lc, &x) == 0 && x)
				printf(_(", offset %ju"), x);

		if (loopcxt_get_sizelimit(lc, &x) == 0 && x)
				printf(_(", sizelimit %ju"), x);
		printf("\n");
		return 0;
	}

	printf("%s: [%04d]:%" PRIu64 " (%s)",
		loopcxt_get_device(lc), (int) dev, ino, fname);

	if (loopcxt_get_offset(lc, &x) == 0 && x)
			printf(_(", offset %ju"), x);

	if (loopcxt_get_sizelimit(lc, &x) == 0 && x)
			printf(_(", sizelimit %ju"), x);

	if (loopcxt_get_encrypt_type(lc, &type) == 0) {
		const char *e = loopcxt_get_crypt_name(lc);

		if ((!e || !*e) && type == 1)
			e = "XOR";
		if (e && *e)
			printf(_(", encryption %s (type %u)"), e, type);
	}
	printf("\n");
	return 0;
}

static int show_all_loops(struct loopdev_cxt *lc, const char *file,
			  uint64_t offset, int flags)
{
	struct stat sbuf, *st = &sbuf;

	if (loopcxt_init_iterator(lc, LOOPITER_FL_USED))
		return -1;

	if (!file || stat(file, st))
		st = NULL;

	while (loopcxt_next(lc) == 0) {

		if (file && !loopcxt_is_used(lc, st, file, offset, flags))
			continue;

		printf_loopdev(lc);
	}
	loopcxt_deinit_iterator(lc);
	return 0;
}

static int set_capacity(struct loopdev_cxt *lc)
{
	int fd = loopcxt_get_fd(lc);

	if (fd < 0)
		warn(_("cannot open %s"), loopcxt_get_device(lc));
	else if (ioctl(fd, LOOP_SET_CAPACITY) != 0)
		warnx(_("%s: set capacity failed"), loopcxt_get_device(lc));
	else
		return 0;

	return -1;
}

static int delete_loop(struct loopdev_cxt *lc)
{
	if (loopcxt_delete_device(lc))
		warn(_("%s: detach failed"), loopcxt_get_device(lc));
	else
		return 0;

	return -1;
}

static int delete_all_loops(struct loopdev_cxt *lc)
{
	int res = 0;

	if (loopcxt_init_iterator(lc, LOOPITER_FL_USED))
		return -1;

	while (loopcxt_next(lc) == 0)
		res += delete_loop(lc);

	loopcxt_deinit_iterator(lc);
	return res;
}

static void usage(FILE *out)
{
	fputs(USAGE_HEADER, out);

	fprintf(out,
	      _(" %1$s [options] [<loopdev>]\n"
		" %1$s [options] -f | <loopdev> <file>\n"),
		program_invocation_short_name);

	fputs(USAGE_OPTIONS, out);
	fputs(_(" -a, --all                     list all used devices\n"
		" -d, --detach <loopdev> [...]  detach one or more devices\n"
		" -D, --detach-all              detach all used devices\n"
		" -f, --find                    find first unused device\n"
		" -c, --set-capacity <loopdev>  resize device\n"
		" -j, --associated <file>       list all devices associated with <file>\n"), out);
	fputs(USAGE_SEPARATOR, out);

	fputs(_(" -e, --encryption <type>       enable encryption with specified <name/num>\n"
		" -o, --offset <num>            start at offset <num> into file\n"
		"     --sizelimit <num>         device limited to <num> bytes of the file\n"
		" -p, --pass-fd <num>           read passphrase from file descriptor <num>\n"
		" -P, --partscan                create partitioned loop device\n"
		" -r, --read-only               setup read-only loop device\n"
		"     --show                    print device name after setup (with -f)\n"
		" -v, --verbose                 verbose mode\n"), out);

	fputs(USAGE_SEPARATOR, out);
	fputs(USAGE_HELP, out);
	fputs(USAGE_VERSION, out);

	fprintf(out, USAGE_MAN_TAIL("losetup(8)"));

	exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}

static void warn_size(const char *filename, uint64_t size)
{
	struct stat st;

	if (!size) {
		if (stat(filename, &st))
			return;
		size = st.st_size;
	}

	if (size < 512)
		warnx(_("%s: warning: file smaller than 512 bytes, the loop device "
			"maybe be useless or invisible for system tools."),
			filename);
	else if (size % 512)
		warnx(_("%s: warning: file does not fit into a 512-byte sector "
		        "the end of the file will be ignored."),
			filename);
}

int main(int argc, char **argv)
{
	struct loopdev_cxt lc;
	int act = 0, flags = 0, passfd = -1, c;
	char *file = NULL, *encryption = NULL;
	uint64_t offset = 0, sizelimit = 0;
	int res = 0, showdev = 0, lo_flags = 0;

	enum {
		OPT_SIZELIMIT = CHAR_MAX + 1,
		OPT_SHOW
	};
	static const struct option longopts[] = {
		{ "all", 0, 0, 'a' },
		{ "set-capacity", 1, 0, 'c' },
		{ "detach", 1, 0, 'd' },
		{ "detach-all", 0, 0, 'D' },
		{ "encryption", 1, 0, 'e' },
		{ "find", 0, 0, 'f' },
		{ "help", 0, 0, 'h' },
		{ "associated", 1, 0, 'j' },
		{ "offset", 1, 0, 'o' },
		{ "sizelimit", 1, 0, OPT_SIZELIMIT },
		{ "pass-fd", 1, 0, 'p' },
		{ "partscan", 0, 0, 'P' },
		{ "read-only", 0, 0, 'r' },
	        { "show", 0, 0, OPT_SHOW },
		{ "verbose", 0, 0, 'v' },
		{ "version", 0, 0, 'V' },
		{ NULL, 0, 0, 0 }
	};

	static const ul_excl_t excl[] = {	/* rows and cols in ASCII order */
		{ 'D','a','c','d','f','j' },
		{ 0 }
	};
	int excl_st[ARRAY_SIZE(excl)] = UL_EXCL_STATUS_INIT;

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	if (loopcxt_init(&lc, 0))
		err(EXIT_FAILURE, _("failed to initialize loopcxt"));

	loopcxt_enable_debug(&lc, getenv("LOOPDEV_DEBUG") ? TRUE : FALSE);

	while ((c = getopt_long(argc, argv, "ac:d:De:E:fhj:o:p:PrvV",
				longopts, NULL)) != -1) {

		err_exclusive_options(c, longopts, excl, excl_st);

		switch (c) {
		case 'a':
			act = A_SHOW;
			break;
		case 'c':
			act = A_SET_CAPACITY;
			if (loopcxt_set_device(&lc, optarg))
				err(EXIT_FAILURE, _("%s: failed to use device"),
						optarg);
			break;
		case 'r':
			lo_flags |= LO_FLAGS_READ_ONLY;
			break;
		case 'd':
			act = A_DELETE;
			if (loopcxt_set_device(&lc, optarg))
				err(EXIT_FAILURE, _("%s: failed to use device"),
						optarg);
			break;
		case 'D':
			act = A_DELETE_ALL;
			break;
		case 'E':
		case 'e':
			encryption = optarg;
			break;
		case 'f':
			act = A_FIND_FREE;
			break;
		case 'h':
			usage(stdout);
			break;
		case 'j':
			act = A_SHOW;
			file = optarg;
			break;
		case 'o':
			offset = strtosize_or_err(optarg, _("failed to parse offset"));
			flags |= LOOPDEV_FL_OFFSET;
			break;
		case 'p':
			passfd = strtou32_or_err(optarg,
					_("invalid passphrase file descriptor"));
			break;
		case 'P':
			lo_flags |= LO_FLAGS_PARTSCAN;
			break;
		case OPT_SHOW:
			showdev = 1;
			break;
		case 'v':
			verbose = 1;
			break;
		case 'V':
			printf(UTIL_LINUX_VERSION);
			return EXIT_SUCCESS;
		case OPT_SIZELIMIT:			/* --sizelimit */
			sizelimit = strtosize_or_err(optarg, _("failed to parse size"));
			flags |= LOOPDEV_FL_SIZELIMIT;
                        break;
		default:
			usage(stderr);
		}
	}

	if (argc == 1)
		usage(stderr);

	if (act == A_FIND_FREE && optind < argc) {
		/*
		 * losetup -f <backing_file>
		 */
		act = A_CREATE;
		file = argv[optind++];
	}
	if (!act && optind + 1 == argc) {
		/*
		 * losetup <device>
		 */
		act = A_SHOW_ONE;
		if (loopcxt_set_device(&lc, argv[optind]))
			err(EXIT_FAILURE, _("%s: failed to use device"),
					argv[optind]);
		optind++;
	}
	if (!act) {
		/*
		 * losetup <loopdev> <backing_file>
		 */
		act = A_CREATE;

		if (optind >= argc)
			errx(EXIT_FAILURE, _("no loop device specified"));
		if (loopcxt_set_device(&lc, argv[optind]))
			err(EXIT_FAILURE, _("%s failed to use device"),
					argv[optind]);
		optind++;

		if (optind >= argc)
			errx(EXIT_FAILURE, _("no file specified"));
		file = argv[optind++];
	}

	if (act != A_CREATE &&
	    (encryption || sizelimit || passfd != -1 || lo_flags || showdev))
		errx(EXIT_FAILURE,
			_("the options %s are allowed to loop device setup only"),
			"--{encryption,sizelimit,pass-fd,read-only,show}");

	if ((flags & LOOPDEV_FL_OFFSET) &&
	    act != A_CREATE && (act != A_SHOW || !file))
		errx(EXIT_FAILURE, _("the option --offset is not allowed in this context."));

	switch (act) {
	case A_CREATE:
	{
		char *pass = NULL;
		int hasdev = loopcxt_has_device(&lc);

		if (encryption) {
#ifdef MCL_FUTURE
			if(mlockall(MCL_CURRENT | MCL_FUTURE))
				err(EXIT_FAILURE, _("couldn't lock into memory"));
#endif
			pass = xgetpass(passfd, _("Password: "));
		}
		do {
			/* Note that loopcxt_{find_unused,set_device}() resets
			 * loopcxt struct.
			 */
			if (!hasdev && (res = loopcxt_find_unused(&lc))) {
				warnx(_("not found unused device"));
				break;
			}
			if (encryption && pass)
				loopcxt_set_encryption(&lc, encryption, pass);
			if (flags & LOOPDEV_FL_OFFSET)
				loopcxt_set_offset(&lc, offset);
			if (flags & LOOPDEV_FL_SIZELIMIT)
				loopcxt_set_sizelimit(&lc, sizelimit);
			if (lo_flags)
				loopcxt_set_flags(&lc, lo_flags);
			if ((res = loopcxt_set_backing_file(&lc, file))) {
				warn(_("%s: failed to use backing file"), file);
				break;
			}
			errno = 0;
			res = loopcxt_setup_device(&lc);
			if (res == 0)
				break;			/* success */
			if (errno != EBUSY) {
				warn(_("%s: failed to setup loop device"),
					hasdev && loopcxt_get_fd(&lc) < 0 ?
					    loopcxt_get_device(&lc) : file);
				break;
			}
		} while (hasdev == 0);

		free(pass);

		if (res == 0) {
			if (showdev)
				printf("%s\n", loopcxt_get_device(&lc));
			warn_size(file, sizelimit);
		}
		break;
	}
	case A_DELETE:
		res = delete_loop(&lc);
		while (optind < argc) {
			if (loopcxt_set_device(&lc, argv[optind]))
				warn(_("%s: failed to use device"),
						argv[optind]);
			optind++;
			res += delete_loop(&lc);
		}
		break;
	case A_DELETE_ALL:
		res = delete_all_loops(&lc);
		break;
	case A_FIND_FREE:
		if (loopcxt_find_unused(&lc))
			warn(_("find unused loop device failed"));
		else
			printf("%s\n", loopcxt_get_device(&lc));
		break;
	case A_SHOW:
		res = show_all_loops(&lc, file, offset, flags);
		break;
	case A_SHOW_ONE:
		res = printf_loopdev(&lc);
		if (res)
			warn(_("%s"), loopcxt_get_device(&lc));
		break;
	case A_SET_CAPACITY:
		res = set_capacity(&lc);
		break;
	default:
		usage(stderr);
		break;
	}

	loopcxt_deinit(&lc);
	return res ? EXIT_FAILURE : EXIT_SUCCESS;
}

