/*
 * mkswap.c - set up a linux swap device
 *
 * (C) 1991 Linus Torvalds. This file may be redistributed as per
 * the Linux copyright.
 */

/*
 * 20.12.91  -	time began. Got VM working yesterday by doing this by hand.
 *
 * Usage: mkswap [-c] [-vN] [-f] device [size-in-blocks]
 *
 *	-c   for readability checking. (Use it unless you are SURE!)
 *	-vN  for swap areas version N. (Only N=0,1 known today.)
 *      -f   for forcing swap creation even if it would smash partition table.
 *
 * The device may be a block device or an image of one, but this isn't
 * enforced (but it's not much fun on a character device :-).
 *
 * Patches from jaggy@purplet.demon.co.uk (Mike Jagdis) to make the
 * size-in-blocks parameter optional added Wed Feb  8 10:33:43 1995.
 *
 * Version 1 swap area code (for kernel 2.1.117), aeb, 981010.
 *
 * Sparc fixes, jj@ultra.linux.cz (Jakub Jelinek), 981201 - mangled by aeb.
 * V1_MAX_PAGES fixes, jj, 990325.
 * sparc64 fixes, jj, 000219.
 *
 * 1999-02-22 Arkadiusz Miśkiewicz <misiek@pld.ORG.PL>
 * - added Native Language Support
 *
 */

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <limits.h>
#include <mntent.h>
#include <sys/utsname.h>
#include <sys/stat.h>
#include <errno.h>
#include <getopt.h>
#ifdef HAVE_LIBSELINUX
#include <selinux/selinux.h>
#include <selinux/context.h>
#endif

#include "linux_version.h"
#include "swapheader.h"
#include "strutils.h"
#include "nls.h"
#include "blkdev.h"
#include "pathnames.h"
#include "all-io.h"
#include "xalloc.h"
#include "c.h"
#include "closestream.h"
#include "ismounted.h"

#ifdef HAVE_LIBUUID
# include <uuid.h>
#endif

#ifdef HAVE_LIBBLKID
# include <blkid.h>
#endif

static char *device_name = NULL;
static int DEV = -1;
static unsigned long long PAGES = 0;
static unsigned long badpages = 0;
static int check = 0;

#define SELINUX_SWAPFILE_TYPE	"swapfile_t"

static unsigned int user_pagesize;
static unsigned int pagesize;
static unsigned long *signature_page = NULL;

static void
init_signature_page(void)
{

	unsigned int kernel_pagesize = pagesize = getpagesize();

	if (user_pagesize) {
		if (!is_power_of_2(user_pagesize) ||
		    user_pagesize < sizeof(struct swap_header_v1_2) + 10)
			errx(EXIT_FAILURE,
				_("Bad user-specified page size %u"),
				user_pagesize);
		pagesize = user_pagesize;
	}

	if (user_pagesize && user_pagesize != kernel_pagesize)
		warnx(_("Using user-specified page size %d, "
				"instead of the system value %d"),
				pagesize, kernel_pagesize);

	signature_page = (unsigned long *) xcalloc(1, pagesize);
}

static void
write_signature(char *sig)
{
	char *sp = (char *) signature_page;

	strncpy(sp + pagesize - SWAP_SIGNATURE_SZ, sig, SWAP_SIGNATURE_SZ);
}

static void
write_uuid_and_label(unsigned char *uuid, char *volume_name)
{
	struct swap_header_v1_2 *h;

	/* Sanity check */
	if (sizeof(struct swap_header_v1_2) != SWAP_HEADER_SIZE) {
		warnx(_("Bad swap header size, no label written."));
		return;
	}

	h = (struct swap_header_v1_2 *) signature_page;
	if (uuid)
		memcpy(h->uuid, uuid, sizeof(h->uuid));
	if (volume_name) {
		xstrncpy(h->volume_name, volume_name, sizeof(h->volume_name));
		if (strlen(volume_name) > strlen(h->volume_name))
			warnx(_("Label was truncated."));
	}
	if (uuid || volume_name) {
		if (volume_name)
			printf("LABEL=%s, ", h->volume_name);
		else
			printf(_("no label, "));
#ifdef HAVE_LIBUUID
		if (uuid) {
			char uuid_string[37];
			uuid_unparse(uuid, uuid_string);
			printf("UUID=%s\n", uuid_string);
		} else
#endif
			printf(_("no uuid\n"));
	}
}

#define MAX_BADPAGES	((pagesize-1024-128*sizeof(int)-10)/sizeof(int))
#define MIN_GOODPAGES	10

static void __attribute__ ((__noreturn__)) usage(FILE *out)
{
	fprintf(out,
		_("\nUsage:\n"
		  " %s [options] device [size]\n"),
		program_invocation_short_name);

	fprintf(out, _(
		"\nOptions:\n"
		" -c, --check               check bad blocks before creating the swap area\n"
		" -f, --force               allow swap size area be larger than device\n"
		" -p, --pagesize SIZE       specify page size in bytes\n"
		" -L, --label LABEL         specify label\n"
		" -v, --swapversion NUM     specify swap-space version number\n"
		" -U, --uuid UUID           specify the uuid to use\n"
		" -V, --version             output version information and exit\n"
		" -h, --help                display this help and exit\n\n"));

	exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}

static void
page_bad(int page)
{
	struct swap_header_v1_2 *p = (struct swap_header_v1_2 *) signature_page;

	if (badpages == MAX_BADPAGES)
		errx(EXIT_FAILURE, _("too many bad pages"));
	p->badpages[badpages] = page;
	badpages++;
}

static void
check_blocks(void)
{
	unsigned int current_page;
	int do_seek = 1;
	char *buffer;

	buffer = xmalloc(pagesize);
	current_page = 0;
	while (current_page < PAGES) {

		ssize_t rc;

		if (do_seek && lseek(DEV,current_page*pagesize,SEEK_SET) !=
		    current_page*pagesize)
			errx(EXIT_FAILURE, _("seek failed in check_blocks"));

		rc = read(DEV, buffer, pagesize);
		do_seek = (rc < 0 || (size_t) rc != pagesize);
		if (do_seek)
			page_bad(current_page);
		current_page++;
	}
	printf(P_("%lu bad page\n", "%lu bad pages\n", badpages), badpages);
	free(buffer);
}

/* return size in pages */
static unsigned long long
get_size(const char *file)
{
	int fd;
	unsigned long long size;

	fd = open(file, O_RDONLY);
	if (fd < 0) {
		perror(file);
		exit(EXIT_FAILURE);
	}
	if (blkdev_get_size(fd, &size) == 0)
		size /= pagesize;

	close(fd);
	return size;
}

#ifdef HAVE_LIBBLKID
static blkid_probe
new_prober(int fd)
{
	blkid_probe pr = blkid_new_probe();
	if (!pr)
		errx(EXIT_FAILURE, _("unable to alloc new libblkid probe"));
	if (blkid_probe_set_device(pr, fd, 0, 0))
		errx(EXIT_FAILURE, _("unable to assign device to libblkid probe"));
	return pr;
}
#endif

static void
wipe_device(int fd, const char *devname, int force)
{
	char *type = NULL;
	int zap = 1;
#ifdef HAVE_LIBBLKID
	blkid_probe pr = NULL;
#endif
	if (!force) {
		if (lseek(fd, 0, SEEK_SET) != 0)
			errx(EXIT_FAILURE, _("unable to rewind swap-device"));

#ifdef HAVE_LIBBLKID
		pr = new_prober(fd);
		blkid_probe_enable_partitions(pr, 1);
		blkid_probe_enable_superblocks(pr, 0);

		if (blkid_do_fullprobe(pr) == 0 &&
		    blkid_probe_lookup_value(pr, "PTTYPE",
				(const char **) &type, NULL) == 0 && type) {
			type = xstrdup(type);
			zap = 0;
		}
#else
		/* don't zap if compiled without libblkid */
		zap = 0;
#endif
	}

	if (zap) {
		/*
		 * Wipe boodbits
		 */
		char buf[1024];

		if (lseek(fd, 0, SEEK_SET) != 0)
			errx(EXIT_FAILURE, _("unable to rewind swap-device"));

		memset(buf, 0, sizeof(buf));
		if (write_all(fd, buf, sizeof(buf)))
			errx(EXIT_FAILURE, _("unable to erase bootbits sectors"));
#ifdef HAVE_LIBBLKID
		/*
		 * Wipe rest of the device
		 */
		if (!pr)
			pr = new_prober(fd);

		blkid_probe_enable_superblocks(pr, 1);
		blkid_probe_enable_partitions(pr, 0);
		blkid_probe_set_superblocks_flags(pr, BLKID_SUBLKS_MAGIC|BLKID_SUBLKS_TYPE);

		while (blkid_do_probe(pr) == 0) {
			const char *data = NULL;

			if (blkid_probe_lookup_value(pr, "TYPE", &data, NULL) == 0 && data)
				warnx(_("%s: warning: wiping old %s signature."), devname, data);
			blkid_do_wipe(pr, 0);
		}
#endif
	} else {
		warnx(_("%s: warning: don't erase bootbits sectors"),
			devname);
		if (type)
			fprintf(stderr, _("        (%s partition table detected). "), type);
		else
			fprintf(stderr, _("        (compiled without libblkid). "));
		fprintf(stderr, _("Use -f to force.\n"));
	}
#ifdef HAVE_LIBBLKID
	blkid_free_probe(pr);
#endif
}

int
main(int argc, char **argv) {
	struct stat statbuf;
	struct swap_header_v1_2 *hdr;
	int c;
	unsigned long long goodpages;
	unsigned long long sz;
	off_t offset;
	int force = 0;
	int version = SWAP_VERSION;
	char *block_count = 0;
	char *opt_label = NULL;
	unsigned char *uuid = NULL;
#ifdef HAVE_LIBUUID
	const char *opt_uuid = NULL;
	uuid_t uuid_dat;
#endif
	static const struct option longopts[] = {
		{ "check",       no_argument,       0, 'c' },
		{ "force",       no_argument,       0, 'f' },
		{ "pagesize",    required_argument, 0, 'p' },
		{ "label",       required_argument, 0, 'L' },
		{ "swapversion", required_argument, 0, 'v' },
		{ "uuid",        required_argument, 0, 'U' },
		{ "version",     no_argument,       0, 'V' },
		{ "help",        no_argument,       0, 'h' },
		{ NULL,          0, 0, 0 }
	};

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	while((c = getopt_long(argc, argv, "cfp:L:v:U:Vh", longopts, NULL)) != -1) {
		switch (c) {
		case 'c':
			check=1;
			break;
		case 'f':
			force=1;
			break;
		case 'p':
			user_pagesize = strtou32_or_err(optarg, _("parsing page size failed"));
			break;
		case 'L':
			opt_label = optarg;
			break;
		case 'v':
			version = strtos32_or_err(optarg, _("parsing version number failed"));
			break;
		case 'U':
#ifdef HAVE_LIBUUID
			opt_uuid = optarg;
#else
			warnx(_("warning: ignoring -U (UUIDs are unsupported by %s)"),
				program_invocation_short_name);
#endif
			break;
		case 'V':
			printf(UTIL_LINUX_VERSION);
			exit(EXIT_SUCCESS);
		case 'h':
			usage(stdout);
		default:
			usage(stderr);
		}
	}
	if (optind < argc)
		device_name = argv[optind++];
	if (optind < argc)
		block_count = argv[optind++];
	if (optind != argc) {
		warnx(_("only one device argument is currently supported"));
		usage(stderr);
	}

	if (version != SWAP_VERSION)
		errx(EXIT_FAILURE,
			_("swapspace version %d is not supported"), version);

#ifdef HAVE_LIBUUID
	if(opt_uuid) {
		if (uuid_parse(opt_uuid, uuid_dat) != 0)
			errx(EXIT_FAILURE, _("error: parsing UUID failed"));
	} else
		uuid_generate(uuid_dat);
	uuid = uuid_dat;
#endif

	init_signature_page();	/* get pagesize */

	if (!device_name) {
		warnx(_("error: Nowhere to set up swap on?"));
		usage(stderr);
	}
	if (block_count) {
		/* this silly user specified the number of blocks explicitly */
		uint64_t blks = strtou64_or_err(block_count,
					_("invalid block count argument"));
		PAGES = blks / (pagesize / 1024);
	}
	sz = get_size(device_name);
	if (!PAGES)
		PAGES = sz;
	else if (PAGES > sz && !force)
		errx(EXIT_FAILURE,
			_("error: "
			  "size %llu KiB is larger than device size %llu KiB"),
			PAGES*(pagesize/1024), sz*(pagesize/1024));

	if (PAGES < MIN_GOODPAGES)
		errx(EXIT_FAILURE,
		     _("error: swap area needs to be at least %ld KiB"),
		     (long)(MIN_GOODPAGES * pagesize / 1024));
	if (PAGES > UINT32_MAX) {
		/* true when swap is bigger than 17.59 terabytes */
		PAGES = UINT32_MAX;
		warnx(_("warning: truncating swap area to %llu KiB"),
			PAGES * pagesize / 1024);
	}

	if (is_mounted(device_name))
		errx(EXIT_FAILURE, _("error: "
			"%s is mounted; will not make swapspace"),
			device_name);

	if (stat(device_name, &statbuf) < 0) {
		perror(device_name);
		exit(EXIT_FAILURE);
	}
	if (S_ISBLK(statbuf.st_mode))
		DEV = open(device_name, O_RDWR | O_EXCL);
	else
		DEV = open(device_name, O_RDWR);

	if (DEV < 0) {
		perror(device_name);
		exit(EXIT_FAILURE);
	}

	if (!S_ISBLK(statbuf.st_mode))
		check=0;
	else if (blkdev_is_misaligned(DEV))
		warnx(_("warning: %s is misaligned"), device_name);

	if (check)
		check_blocks();

	wipe_device(DEV, device_name, force);

	hdr = (struct swap_header_v1_2 *) signature_page;
	hdr->version = version;
	hdr->last_page = PAGES - 1;
	hdr->nr_badpages = badpages;

	if (badpages > PAGES - MIN_GOODPAGES)
		errx(EXIT_FAILURE, _("Unable to set up swap-space: unreadable"));

	goodpages = PAGES - badpages - 1;
	printf(_("Setting up swapspace version %d, size = %llu KiB\n"),
		version, goodpages * pagesize / 1024);

	write_signature(SWAP_SIGNATURE);
	write_uuid_and_label(uuid, opt_label);

	offset = 1024;
	if (lseek(DEV, offset, SEEK_SET) != offset)
		errx(EXIT_FAILURE, _("unable to rewind swap-device"));
	if (write_all(DEV, (char *) signature_page + offset,
				    pagesize - offset) == -1)
		err(EXIT_FAILURE,
			_("%s: unable to write signature page"),
			device_name);

#ifdef HAVE_LIBSELINUX
	if (S_ISREG(statbuf.st_mode) && is_selinux_enabled() > 0) {
		security_context_t context_string;
		security_context_t oldcontext;
		context_t newcontext;

		if (fgetfilecon(DEV, &oldcontext) < 0) {
			if (errno != ENODATA)
				err(EXIT_FAILURE,
					_("%s: unable to obtain selinux file label"),
					device_name);
			if (matchpathcon(device_name, statbuf.st_mode, &oldcontext))
				errx(EXIT_FAILURE, _("unable to matchpathcon()"));
		}
		if (!(newcontext = context_new(oldcontext)))
			errx(EXIT_FAILURE, _("unable to create new selinux context"));
		if (context_type_set(newcontext, SELINUX_SWAPFILE_TYPE))
			errx(EXIT_FAILURE, _("couldn't compute selinux context"));

		context_string = context_str(newcontext);

		if (strcmp(context_string, oldcontext)!=0) {
			if (fsetfilecon(DEV, context_string))
				err(EXIT_FAILURE, _("unable to relabel %s to %s"),
						device_name, context_string);
		}
		context_free(newcontext);
		freecon(oldcontext);
	}
#endif
	/*
	 * A subsequent swapon() will fail if the signature
	 * is not actually on disk. (This is a kernel bug.)
	 * The fsync() in close_fd() will take care of writing.
	 */
	if (close_fd(DEV) != 0)
		err(EXIT_FAILURE, _("write failed"));
	return EXIT_SUCCESS;
}
