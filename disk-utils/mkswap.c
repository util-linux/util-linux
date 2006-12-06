/*
 * mkswap.c - set up a linux swap device
 *
 * (C) 1991 Linus Torvalds. This file may be redistributed as per
 * the Linux copyright.
 */

/*
 * 20.12.91  -	time began. Got VM working yesterday by doing this by hand.
 *
 * Usuage: mkswap [-c] [-vN] [-f] device [size-in-blocks]
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
 */

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/ioctl.h>		/* for _IO */
#include <sys/utsname.h>
#include <sys/stat.h>
#include <asm/page.h>		/* for PAGE_SIZE and PAGE_SHIFT */

#ifndef _IO
/* pre-1.3.45 */
#define BLKGETSIZE 0x1260
#else
/* same on i386, m68k, arm; different on alpha, mips, sparc, ppc */
#define BLKGETSIZE _IO(0x12,96)
#endif

static char * program_name = "mkswap";
static char * device_name = NULL;
static int DEV = -1;
static long PAGES = 0;
static int check = 0;
static int badpages = 0;
static int version = -1;

#define MAKE_VERSION(p,q,r)	(65536*(p) + 256*(q) + (r))

static int
linux_version_code(void) {
	struct utsname my_utsname;
	int p, q, r;

	if (uname(&my_utsname) == 0) {
		p = atoi(strtok(my_utsname.release, "."));
		q = atoi(strtok(NULL, "."));
		r = atoi(strtok(NULL, "."));
		return MAKE_VERSION(p,q,r);
	}
	return 0;
}

/*
 * The definition of the union swap_header uses the constant PAGE_SIZE.
 * Unfortunately, on some architectures this depends on the hardware model,
 * and can only be found at run time -- we use getpagesize().
 */

static int pagesize;
static int *signature_page;

struct swap_header_v1 {
        char         bootbits[1024];    /* Space for disklabel etc. */
	unsigned int version;
	unsigned int last_page;
	unsigned int nr_badpages;
	unsigned int padding[125];
	unsigned int badpages[1];
} *p;

static void
init_signature_page() {
	pagesize = getpagesize();

#ifdef PAGE_SIZE
	if (pagesize != PAGE_SIZE)
		fprintf(stderr, "Assuming pages of size %d\n", pagesize);
#endif
	signature_page = (int *) malloc(pagesize);
	memset(signature_page,0,pagesize);
	p = (struct swap_header_v1 *) signature_page;
}

static void
write_signature(char *sig) {
	char *sp = (char *) signature_page;

	strncpy(sp+pagesize-10, sig, 10);
}

#define V0_MAX_PAGES	(8 * (pagesize - 10))
#define V1_MAX_PAGES	((0x7fffffff / pagesize) - 1)

#define MAX_BADPAGES	((pagesize-1024-128*sizeof(int)-10)/sizeof(int))

static void bit_set (unsigned int *addr, unsigned int nr)
{
	unsigned int r, m;

	addr += nr / (8 * sizeof(int));
	r = *addr;
	m = 1 << (nr & (8 * sizeof(int) - 1));
	*addr = r | m;
}

static int bit_test_and_clear (unsigned int *addr, unsigned int nr)
{
	unsigned int r, m;

	addr += nr / (8 * sizeof(int));
	r = *addr;
	m = 1 << (nr & (8 * sizeof(int) - 1));
	*addr = r & ~m;
	return (r & m) != 0;
}

void fatal_error(const char * fmt_string)
{
	fprintf(stderr,fmt_string,program_name,device_name);
	exit(1);
}

#define usage() fatal_error("Usage: %s [-c] [-v0|-v1] /dev/name [blocks]\n")
#define die(str) fatal_error("%s: " str "\n")

void
page_ok(int page) {
	if (version==0)
		bit_set(signature_page, page);
}

void
page_bad(int page) {
	if (version == 0)
		bit_test_and_clear(signature_page, page);
	else {
		if (badpages == MAX_BADPAGES)
			die("too many bad pages");
		p->badpages[badpages] = page;
	}
	badpages++;
}

void
check_blocks(void) {
	unsigned int current_page;
	int do_seek = 1;
	char *buffer;

	buffer = malloc(pagesize);
	if (!buffer)
		die("Out of memory");
	current_page = 0;
	while (current_page < PAGES) {
		if (!check) {
			page_ok(current_page++);
			continue;
		}
		if (do_seek && lseek(DEV,current_page*pagesize,SEEK_SET) !=
		    current_page*pagesize)
			die("seek failed in check_blocks");
		if ((do_seek = (pagesize != read(DEV, buffer, pagesize)))) {
			page_bad(current_page++);
			continue;
		}
		page_ok(current_page++);
	}
	if (badpages)
		printf("%d bad page%s\n",badpages,(badpages>1)?"s":"");
}

static long valid_offset (int fd, int offset)
{
	char ch;

	if (lseek (fd, offset, 0) < 0)
		return 0;
	if (read (fd, &ch, 1) < 1)
		return 0;
	return 1;
}

static int
find_size (int fd)
{
	unsigned int high, low;

	low = 0;
	for (high = 1; high > 0 && valid_offset (fd, high); high *= 2)
		low = high;
	while (low < high - 1)
	{
		const int mid = (low + high) / 2;

		if (valid_offset (fd, mid))
			low = mid;
		else
			high = mid;
	}
	return (low + 1);
}

/* return size in pages, to avoid integer overflow */
static int
get_size(const char  *file)
{
	int	fd;
	int	size;

	fd = open(file, O_RDONLY);
	if (fd < 0) {
		perror(file);
		exit(1);
	}
	if (ioctl(fd, BLKGETSIZE, &size) >= 0) {
		int sectors_per_page = pagesize/512;
		size /= sectors_per_page;
	} else {
		size = find_size(fd) / pagesize;
	}
	close(fd);
	return size;
}

int main(int argc, char ** argv)
{
	char * tmp;
	struct stat statbuf;
	int maxpages;
	int goodpages;
	int offset;
	int force = 0;

	if (argc && *argv)
		program_name = *argv;

	init_signature_page();	/* get pagesize */

	while (argc-- > 1) {
		argv++;
		if (argv[0][0] != '-') {
			if (device_name) {
				int blocks_per_page = pagesize/1024;
				PAGES = strtol(argv[0],&tmp,0)/blocks_per_page;
				if (*tmp)
					usage();
			} else
				device_name = argv[0];
		} else {
			switch (argv[0][1]) {
				case 'c':
					check=1;
					break;
				case 'f':
					force=1;
					break;
				case 'v':
					version=atoi(argv[0]+2);
					break;
				default:
					usage();
			}
		}
	}
	if (!device_name) {
		fprintf(stderr,
			"%s: error: Nowhere to set up swap on?\n",
			program_name);
		usage();
	}
	if (!PAGES) {
		PAGES = get_size(device_name);
	}

	if (version == -1) {
		if (PAGES <= V0_MAX_PAGES)
			version = 0;
		else if (linux_version_code() < MAKE_VERSION(2,1,117))
			version = 0;
		else if (pagesize < 2048)
			version = 0;
		else
			version = 1;
	}
	if (version != 0 && version != 1) {
		fprintf(stderr, "%s: error: unknown version %d\n",
			program_name, version);
		usage();
	}
	if (PAGES < 10) {
		fprintf(stderr,
			"%s: error: swap area needs to be at least %ldkB\n",
			program_name, (long)(10 * pagesize / 1024));
		usage();
	}
	maxpages = ((version == 0) ? V0_MAX_PAGES : V1_MAX_PAGES);
	if (PAGES > maxpages) {
		PAGES = maxpages;
		fprintf(stderr, "%s: warning: truncating swap area to %ldkB\n",
			program_name, PAGES * pagesize / 1024);
	}

	DEV = open(device_name,O_RDWR);
	if (DEV < 0 || fstat(DEV, &statbuf) < 0) {
		perror(device_name);
		exit(1);
	}
	if (!S_ISBLK(statbuf.st_mode))
		check=0;
	else if (statbuf.st_rdev == 0x0300 || statbuf.st_rdev == 0x0340)
		die("Will not try to make swapdevice on '%s'");

#ifdef __sparc__
	if (!force && version == 0) {
		/* Don't overwrite partition table unless forced */
		unsigned char *buffer = (unsigned char *)signature_page;
		unsigned short *q, sum;

		if (read(DEV, buffer, 512) != 512)
			die("fatal: first page unreadable");
		if (buffer[508] == 0xDA && buffer[509] == 0xBE) {
			q = (unsigned short *)(buffer + 510);
			for (sum = 0; q >= (unsigned short *) buffer;)
				sum ^= *q--;
			if (!sum) {
				fprintf(stderr, "\
%s: Device '%s' contains a valid Sun disklabel.\n\
This probably means creating v0 swap would destroy your partition table\n\
No swap created. If you really want to create swap v0 on that device, use\n\
the -f option to force it.\n",
					program_name, device_name);
				exit(1);
			}
		}
	}
#endif

	if (version == 0 || check)
		check_blocks();
	if (version == 0 && !bit_test_and_clear(signature_page,0))
		die("fatal: first page unreadable");
	if (version == 1) {
		p->version = version;
		p->last_page = PAGES-1;
		p->nr_badpages = badpages;
	}

	goodpages = PAGES - badpages - 1;
	if (goodpages <= 0)
		die("Unable to set up swap-space: unreadable");
	printf("Setting up swapspace version %d, size = %ld bytes\n",
		version, (long)(goodpages*pagesize));
	write_signature((version == 0) ? "SWAP-SPACE" : "SWAPSPACE2");

	offset = ((version == 0) ? 0 : 1024);
	if (lseek(DEV, offset, SEEK_SET) != offset)
		die("unable to rewind swap-device");
	if (write(DEV,(char*)signature_page+offset, pagesize-offset)
	    != pagesize-offset)
		die("unable to write signature page");

	/*
	 * A subsequent swapon() will fail if the signature
	 * is not actually on disk. (This is a kernel bug.)
	 */
	if (fsync(DEV))
		 die("fsync failed");
	return 0;
}
