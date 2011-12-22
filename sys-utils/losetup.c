/*
 * Copyright (C) 2011 Karel Zak <kzak@redhat.com>
 * Originally from Ted's losetup.c
 *
 * losetup.c - setup and control loop devices
 */
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/sysmacros.h>
#include <inttypes.h>
#include <dirent.h>
#include <getopt.h>
#include <stdarg.h>

#include "strutils.h"
#include "nls.h"
#include "pathnames.h"
#include "loopdev.h"
#include "xalloc.h"
#include "canonicalize.h"

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

/*
 * A function to read the passphrase either from the terminal or from
 * an open file descriptor.
 */
static char *xgetpass(int pfd, const char *prompt)
{
	char *pass;
	int buflen, i;

        if (pfd < 0) /* terminal */
		return getpass(prompt);

	pass = NULL;
	buflen = 0;
	for (i=0; ; i++) {
		if (i >= buflen-1) {
				/* we're running out of space in the buffer.
				 * Make it bigger: */
			char *tmppass = pass;
			buflen += 128;
			pass = realloc(tmppass, buflen);
			if (pass == NULL) {
				/* realloc failed. Stop reading. */
				warn(_("Out of memory while reading passphrase"));
				pass = tmppass; /* the old buffer hasn't changed */
				break;
			}
		}
		if (read(pfd, pass+i, 1) != 1 ||
		    pass[i] == '\n' || pass[i] == 0)
			break;
	}

	if (pass == NULL)
		return "";

	pass[i] = 0;
	return pass;
}

static int printf_loopdev(struct loopdev_cxt *lc)
{
	uint64_t x;
	dev_t dev = 0;
	ino_t ino = 0;
	char *fname = NULL;
	int type;

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
		loopcxt_get_device(lc), dev, ino, fname);

	if (loopcxt_get_offset(lc, &x) == 0 && x)
			printf(_(", offset %ju"), x);

	if (loopcxt_get_sizelimit(lc, &x) == 0 && x)
			printf(_(", sizelimit %ju"), x);

	if (loopcxt_get_encrypt_type(lc, &type) == 0) {
		const char *e = loopcxt_get_crypt_name(lc);

		if ((!e || !*e) && type == 1)
			e = "XOR";
		if (e && *e)
			printf(_(", encryption %s (type %ju)"), e, type);
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
		warn(_("%s: open failed"), loopcxt_get_device(lc));
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

static void
usage(FILE *out) {

  fputs(_("\nUsage:\n"), out);
  fprintf(out,
	_(" %1$s loop_device                             give info\n"
	  " %1$s -a | --all                              list all used\n"
	  " %1$s -d | --detach <loopdev> [<loopdev> ...] delete\n"
	  " %1$s -D | --detach-all                       delete all used\n"
	  " %1$s -f | --find                             find unused\n"
	  " %1$s -c | --set-capacity <loopdev>           resize\n"
	  " %1$s -j | --associated <file> [-o <num>]     list all associated with <file>\n"
	  " %1$s [options] {-f|--find|loopdev} <file>    setup\n"),
	program_invocation_short_name);

  fputs(_("\nOptions:\n"), out);
  fputs(_(" -e, --encryption <type> enable data encryption with specified <name/num>\n"
	  " -h, --help              this help\n"
	  " -o, --offset <num>      start at offset <num> into file\n"
	  "     --sizelimit <num>   loop limited to only <num> bytes of the file\n"
	  " -p, --pass-fd <num>     read passphrase from file descriptor <num>\n"
	  " -r, --read-only         setup read-only loop device\n"
	  "     --show              print device name (with -f <file>)\n"
	  " -v, --verbose           verbose mode\n\n"), out);

	exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
 }

int main(int argc, char **argv)
{
	struct loopdev_cxt lc;
	int act = 0, flags = 0, passfd = -1, c;
	char *file = NULL, *encryption = NULL;
	uint64_t offset = 0, sizelimit = 0;
	int res = 0, showdev = 0, lo_flags = 0;

	loopcxt_init(&lc, 0);
	/*loopcxt_enable_debug(&lc, TRUE);*/

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
		{ "sizelimit", 1, 0, 128 },
		{ "pass-fd", 1, 0, 'p' },
		{ "read-only", 0, 0, 'r' },
	        { "show", 0, 0, 's' },
		{ "verbose", 0, 0, 'v' },
		{ NULL, 0, 0, 0 }
	};

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

	while ((c = getopt_long(argc, argv, "ac:d:De:E:fhj:o:p:rsv",
				longopts, NULL)) != -1) {

		if (act && strchr("acdDfj", c))
			errx(EXIT_FAILURE,
				_("the options %s are mutually exclusive"),
				"--{all,associated,set-capacity,detach,detach-all,find}");

		switch (c) {
		case 'a':
			act = A_SHOW;
			break;
		case 'c':
			act = A_SET_CAPACITY;
			loopcxt_set_device(&lc, optarg);
			break;
		case 'r':
			lo_flags |= LO_FLAGS_READ_ONLY;
			break;
		case 'd':
			act = A_DELETE;
			loopcxt_set_device(&lc, optarg);
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
			if (strtosize(optarg, &offset))
				errx(EXIT_FAILURE,
				     _("invalid offset '%s' specified"), optarg);
			flags |= LOOPDEV_FL_OFFSET;
			break;
		case 'p':
			passfd = strtol_or_err(optarg,
					_("invalid passphrase file descriptor"));
			break;
		case 's':
			showdev = 1;
			break;
		case 'v':
			verbose = 1;
			break;
	        case 128:			/* --sizelimit */
			if (strtosize(optarg, &sizelimit))
				errx(EXIT_FAILURE,
				     _("invalid size '%s' specified"), optarg);
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
		loopcxt_set_device(&lc, argv[optind++]);
	}
	if (!act) {
		/*
		 * losetup <loopdev> <backing_file>
		 */
		act = A_CREATE;

		if (optind >= argc)
			errx(EXIT_FAILURE, _("no loop device specified"));
		loopcxt_set_device(&lc, argv[optind++]);

		if (optind >= argc)
			errx(EXIT_FAILURE, _("no file specified"));
		file = argv[optind++];
	}

	if (act != A_CREATE &&
	    (encryption || sizelimit || passfd != -1 || lo_flags || showdev))
		errx(EXIT_FAILURE,
			_("the options %s are allowed to loop device setup only"),
			"--{encryption,sizelimit,pass-fd,read-only,show}");

	if (act != A_CREATE && act != A_SHOW && (flags & LOOPDEV_FL_OFFSET))
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
				loopcxt_set_offset(&lc, sizelimit);
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
				warn(_("failed to setup loop device"));
				break;
			}
		} while (hasdev == 0);

		free(pass);

		if (showdev && res == 0)
			printf("%s\n", loopcxt_get_device(&lc));
		break;
	}
	case A_DELETE:
		res = delete_loop(&lc);
		while (optind < argc) {
			loopcxt_set_device(&lc, argv[optind++]);
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

