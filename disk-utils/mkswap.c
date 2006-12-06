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
 * 1999-02-22 Arkadiusz Mi¶kiewicz <misiek@pld.ORG.PL>
 * - added Native Language Support
 * 
 */

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/ioctl.h>		/* for _IO */
#include <sys/utsname.h>
#include <sys/stat.h>
#include "swapheader.h"
#include "xstrncpy.h"
#include "nls.h"

#ifdef HAVE_UUID_UUID_H
#include <uuid/uuid.h>
#endif

/* Try to get PAGE_SIZE from libc or kernel includes */
#ifdef HAVE_SYS_USER_H
				/* Note: <sys/user.h> says: for gdb only */
#include <sys/user.h>		/* for PAGE_SIZE and PAGE_SHIFT */
#else
#ifdef HAVE_ASM_PAGE_H
#include <asm/page.h>		/* for PAGE_SIZE and PAGE_SHIFT */
				/* we also get PAGE_SIZE via getpagesize() */
#endif
#endif

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
static unsigned long PAGES = 0;
static unsigned long badpages = 0;
static int check = 0;
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

#ifdef __sparc__
# ifdef __arch64__
#  define is_sparc64() 1
#  define is_be64() 1
# else /* sparc32 */
static int
is_sparc64(void) {
	struct utsname un;
	static int sparc64 = -1;

	if (sparc64 != -1) return sparc64;
	sparc64 = 0;

	if (uname(&un) < 0) return 0;
	if (! strcmp(un.machine, "sparc64")) {
		sparc64 = 1;
		return 1;
	}
	if (strcmp(un.machine, "sparc"))
		return 0; /* Should not happen */

#ifdef HAVE_PERSONALITY
	{
		extern int personality(unsigned long);
		int oldpers;
#define PERS_LINUX          0x00000000
#define PERS_LINUX_32BIT    0x00800000
#define PERS_LINUX32        0x00000008

		oldpers = personality(PERS_LINUX_32BIT);
		if (oldpers != -1) {
			if (personality(PERS_LINUX) != -1) {
				uname(&un);
				if (! strcmp(un.machine, "sparc64")) {
					sparc64 = 1;
					oldpers = PERS_LINUX32;
				}
			}
			personality(oldpers);
		}
	}
#endif

	return sparc64;
}
#  define is_be64() is_sparc64()
# endif /* sparc32 */
#else /* !sparc */
# define is_be64() 0
#endif

/*
 * The definition of the union swap_header uses the constant PAGE_SIZE.
 * Unfortunately, on some architectures this depends on the hardware model,
 * and can only be found at run time -- we use getpagesize(), so that
 * we do not need separate binaries e.g. for sun4, sun4c/d/m and sun4u.
 *
 * Even more unfortunately, getpagesize() does not always return
 * the right information. For example, libc4 and libc5 do not use
 * the system call but invent a value themselves
 * (EXEC_PAGESIZE or NBPG * CLSIZE or NBPC), and thus it may happen
 * that e.g. on a sparc PAGE_SIZE=4096 and getpagesize() returns 8192.
 * What to do? Let us allow the user to specify the pagesize explicitly.
 */

static int user_pagesize = 0;
static int kernel_pagesize;	   /* obtained via getpagesize(); */
static int defined_pagesize = 0;   /* PAGE_SIZE, when that exists */
static int pagesize;
static long *signature_page;
struct swap_header_v1 *p;

static void
init_signature_page(void) {

#ifdef PAGE_SIZE
	defined_pagesize = PAGE_SIZE;
#endif
	kernel_pagesize = getpagesize();
	pagesize = kernel_pagesize;

	if (user_pagesize) {
		if ((user_pagesize & (user_pagesize-1)) ||
		    user_pagesize < 1024) {
			fprintf(stderr, _("Bad user-specified page size %d\n"),
				user_pagesize);
			exit(1);
		}
		pagesize = user_pagesize;
	}

	if (user_pagesize && user_pagesize != kernel_pagesize &&
	    user_pagesize != defined_pagesize)
		fprintf(stderr, _("Using user-specified page size %d, "
				  "instead of the system values %d/%d\n"),
			pagesize, kernel_pagesize, defined_pagesize);
	else if (defined_pagesize && pagesize != defined_pagesize)
		fprintf(stderr, _("Assuming pages of size %d (not %d)\n"),
			pagesize, defined_pagesize);

	signature_page = (long *) malloc(pagesize);
	memset(signature_page, 0, pagesize);
	p = (struct swap_header_v1 *) signature_page;
}

static void
write_signature(char *sig) {
	char *sp = (char *) signature_page;

	strncpy(sp+pagesize-10, sig, 10);
}

#if 0
static int
tohex(int a) {
	return ((a < 10) ? '0'+a : 'a'+a-10);
}

static void
uuid_unparse(unsigned char *uuid, char *s) {
	int i;

	for (i=0; i<16; i++) {
		if (i == 4 || i == 6 || i == 8 || i == 10)
			*s++ = '-';
		*s++ = tohex((uuid[i] >> 4) & 0xf);
		*s++ = tohex(uuid[i] & 0xf);
	}
	*s = 0;
}
#endif

static void
write_uuid_and_label(char *uuid, char *volume_name) {
	struct swap_header_v1_2 *h;

	/* Sanity check */
	if (sizeof(struct swap_header_v1) !=
	    sizeof(struct swap_header_v1_2)) {
		fprintf(stderr,
			_("Bad swap header size, no label written.\n"));
		return;
	}

	h = (struct swap_header_v1_2 *) signature_page;
	if (uuid)
		memcpy(h->uuid, uuid, sizeof(h->uuid));
	if (volume_name) {
		xstrncpy(h->volume_name, volume_name, sizeof(h->volume_name));
		if (strlen(volume_name) > strlen(h->volume_name))
			fprintf(stderr, _("Label was truncated.\n"));
	}
	if (uuid || volume_name) {
		if (volume_name)
			printf("LABEL=%s, ", h->volume_name);
		else
			printf(_("no label, "));
#ifdef HAVE_UUID_UUID_H
		if (uuid) {
			char uuid_string[37];
			uuid_unparse(uuid, uuid_string);
			printf("UUID=%s\n", uuid_string);
		} else
#endif
			printf(_("no uuid\n"));
	}
}

/*
 * Find out what the maximum amount of swap space is that the kernel will
 * handle.  This wouldn't matter if the kernel just used as much of the
 * swap space as it can handle, but until 2.3.4 it would return an error
 * to swapon() if the swapspace was too large.
 */
#define V0_MAX_PAGES	(8 * (pagesize - 10))
/* Before 2.2.0pre9 */
#define V1_OLD_MAX_PAGES	((0x7fffffff / pagesize) - 1)
/* Since 2.2.0pre9, before 2.3.4:
   error if nr of pages >= SWP_OFFSET(SWP_ENTRY(0,~0UL))
   with variations on
	#define SWP_ENTRY(type,offset) (((type) << 1) | ((offset) << 8))
	#define SWP_OFFSET(entry) ((entry) >> 8)
   on the various architectures. Below the result - yuk.

   Machine	pagesize	SWP_ENTRY	SWP_OFFSET	bound+1	oldbound+2
   i386		2^12		o<<8		e>>8		1<<24	1<<19
   mips		2^12		o<<15		e>>15		1<<17	1<<19
   alpha	2^13		o<<40		e>>40		1<<24	1<<18
   m68k		2^12		o<<12		e>>12		1<<20	1<<19
   sparc	2^{12,13}	(o&0x3ffff)<<9	(e>>9)&0x3ffff	1<<18	1<<{19,18}
   sparc64	2^13		o<<13		e>>13		1<<51	1<<18
   ppc		2^12		o<<8		e>>8		1<<24	1<<19
   armo		2^{13,14,15}	o<<8		e>>8		1<<24	1<<{18,17,16}
   armv		2^12		o<<9		e>>9		1<<23	1<<19

   assuming that longs have 64 bits on alpha and sparc64 and 32 bits elsewhere.

   The bad part is that we need to know this since the kernel will
   refuse a swap space if it is too large.
*/
/* patch from jj - why does this differ from the above? */
/* 32bit kernels have a second limitation of 2GB, sparc64 is limited by
   the size of virtual address space allocation for vmalloc */
#if defined(__alpha__)
#define V1_MAX_PAGES           ((1 << 24) - 1)
#elif defined(__mips__)
#define V1_MAX_PAGES           ((1 << 17) - 1)
#elif defined(__sparc__)
#define V1_MAX_PAGES           (is_sparc64() ? ((3 << 29) - 1) : ((1 << 18) - 1))
#elif defined(__ia64__)
/* 
 * The actual size will depend on the amount of virtual address space
 * available to vmalloc the swap map.
 */
#define V1_MAX_PAGES          ((1UL << 54) - 1)
#else
#define V1_MAX_PAGES           V1_OLD_MAX_PAGES
#endif
/* man page now says:
The maximum useful size of a swap area now depends on the architecture.
It is roughly 2GB on i386, PPC, m68k, ARM, 1GB on sparc, 512MB on mips,
128GB on alpha and 3TB on sparc64.
*/

#define MAX_BADPAGES	((pagesize-1024-128*sizeof(int)-10)/sizeof(int))

/*
 * One more point of lossage - Linux swapspace really is a mess.
 * The definition of the bitmap used is architecture dependent,
 * and requires one to know whether the machine is bigendian,
 * and if so, whether it will use 32-bit or 64-bit units in
 * test_bit().
 * davem writes: "... is based upon an unsigned long type of
 * the cpu and the native endianness".
 * So, it seems we can write `unsigned long' below.
 * However, sparc64 uses 64-bit units in the kernel, while
 * mkswap may have been translated with 32-bit longs. Thus,
 * we need an explicit test for version 0 swap on sparc64.
 */

static void
bit_set (unsigned long *addr, unsigned int nr) {
	unsigned int r, m;

	if(is_be64()) {
		unsigned long long *bitmap = (unsigned long long *) addr;
		unsigned long long bitnum = (unsigned long long) nr;
		unsigned long long rl, ml;

		bitmap += bitnum / (8 * sizeof(long long));
		rl = *bitmap;
		ml = 1ULL << (bitnum & (8ULL * sizeof(long long) - 1ULL));
		*bitmap = rl | ml;
		return;
	}

	addr += nr / (8 * sizeof(unsigned long));
	r = *addr;
	m = 1 << (nr & (8 * sizeof(unsigned long) - 1));
	*addr = r | m;
}

static int
bit_test_and_clear (unsigned long *addr, unsigned int nr) {
	unsigned int r, m;

	if(is_be64()) {
		unsigned long long *bitmap = (unsigned long long *) addr;
		unsigned long long bitnum = (unsigned long long) nr;
		unsigned long long rl, ml;

		bitmap += bitnum / (8 * sizeof(long long));
		rl = *bitmap;
		ml = 1ULL << (bitnum & (8ULL * sizeof(long long) - 1ULL));
		*bitmap = rl & ~ml;
		return ((rl & ml) != 0ULL);
	}

	addr += nr / (8 * sizeof(unsigned long));
	r = *addr;
	m = 1 << (nr & (8 * sizeof(unsigned long) - 1));
	*addr = r & ~m;
	return (r & m) != 0;
}

static void
usage(void) {
	fprintf(stderr,
		_("Usage: %s [-c] [-v0|-v1] [-pPAGESZ] [-L label] /dev/name [blocks]\n"),
		program_name);
	exit(1);
}

static void
die(const char *str) {
	fprintf(stderr, "%s: %s\n", program_name, str);
	exit(1);
}

static void
page_ok(int page) {
	if (version==0)
		bit_set(signature_page, page);
}

static void
page_bad(int page) {
	if (version == 0)
		bit_test_and_clear(signature_page, page);
	else {
		if (badpages == MAX_BADPAGES)
			die(_("too many bad pages"));
		p->badpages[badpages] = page;
	}
	badpages++;
}

static void
check_blocks(void) {
	unsigned int current_page;
	int do_seek = 1;
	char *buffer;

	buffer = malloc(pagesize);
	if (!buffer)
		die(_("Out of memory"));
	current_page = 0;
	while (current_page < PAGES) {
		if (!check) {
			page_ok(current_page++);
			continue;
		}
		if (do_seek && lseek(DEV,current_page*pagesize,SEEK_SET) !=
		    current_page*pagesize)
			die(_("seek failed in check_blocks"));
		if ((do_seek = (pagesize != read(DEV, buffer, pagesize)))) {
			page_bad(current_page++);
			continue;
		}
		page_ok(current_page++);
	}
	if (badpages == 1)
		printf(_("one bad page\n"));
	else if (badpages > 1)
		printf(_("%lu bad pages\n"), badpages);
}

static long
valid_offset (int fd, off_t offset) {
	char ch;

	if (lseek (fd, offset, 0) < 0)
		return 0;
	if (read (fd, &ch, 1) < 1)
		return 0;
	return 1;
}

static off_t
find_size (int fd) {
	off_t high, low;

	low = 0;
	for (high = 1; high > 0 && valid_offset (fd, high); high *= 2)
		low = high;
	while (low < high - 1) {
		const off_t mid = (low + high) / 2;

		if (valid_offset (fd, mid))
			low = mid;
		else
			high = mid;
	}
	return (low + 1);
}

/* return size in pages, to avoid integer overflow */
static unsigned long
get_size(const char  *file) {
	int	fd;
	unsigned long	size;

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

static int
isnzdigit(char c) {
	return (c >= '1' && c <= '9');
}

int
main(int argc, char ** argv) {
	struct stat statbuf;
	int i;
	unsigned long maxpages;
	unsigned long goodpages;
	unsigned long sz;
	off_t offset;
	int force = 0;
	char *block_count = 0;
	char *pp;
	char *opt_label = NULL;
	char *uuid = NULL;
#ifdef HAVE_UUID_UUID_H
	uuid_t uuid_dat;
#endif

	program_name = (argc && *argv) ? argv[0] : "mkswap";
	if ((pp = strrchr(program_name, '/')) != NULL)
		program_name = pp+1;

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

	if (argc == 2 &&
	    (!strcmp(argv[1], "-V") || !strcmp(argv[1], "--version"))) {
		printf(_("%s (%s)\n"), program_name, PACKAGE_STRING);
		exit(0);
	}

	for (i=1; i<argc; i++) {
		if (argv[i][0] == '-') {
			switch (argv[i][1]) {
				case 'c':
					check=1;
					break;
				case 'f':
					force=1;
					break;
				case 'p':
					pp = argv[i]+2;
					if (!*pp && i+1 < argc)
						pp = argv[++i];
					if (isnzdigit(*pp))
						user_pagesize = atoi(pp);
					else
						usage();
					break;
			        case 'L':
					pp = argv[i]+2;
					if (!*pp && i+1 < argc)
						pp = argv[++i];
					opt_label = pp;
				        break;
				case 'v':
					version = atoi(argv[i]+2);
					break;
				default:
					usage();
			}
		} else if (!device_name) {
			device_name = argv[i];
		} else if (!block_count) {
			block_count = argv[i];
		} else
			usage();
	}

#ifdef HAVE_UUID_UUID_H
	uuid_generate(uuid_dat);
	uuid = uuid_dat;
#endif

	init_signature_page();	/* get pagesize */

	if (!device_name) {
		fprintf(stderr,
			_("%s: error: Nowhere to set up swap on?\n"),
			program_name);
		usage();
	}
	if (block_count) {
		/* this silly user specified the number of blocks
		   explicitly */
		char *tmp;
		int blocks_per_page = pagesize/1024;
		PAGES = strtoul(block_count,&tmp,0)/blocks_per_page;
		if (*tmp)
			usage();
	}
	sz = get_size(device_name);
	if (!PAGES) {
		PAGES = sz;
	} else if (PAGES > sz && !force) {
		fprintf(stderr,
			_("%s: error: "
			  "size %lu is larger than device size %lu\n"),
			program_name,
			PAGES*(pagesize/1024), sz*(pagesize/1024));
		exit(1);
	}

	if (version == -1) {
		/* labels only for v1 */
		if (opt_label)
			version = 1;
		else
		/* use version 1 as default, if possible */
		if (PAGES <= V0_MAX_PAGES && PAGES > V1_MAX_PAGES)
			version = 0;
		else if (linux_version_code() < MAKE_VERSION(2,1,117))
			version = 0;
		else if (pagesize < 2048)
			version = 0;
		else
			version = 1;
	}
	if (version != 0 && version != 1) {
		fprintf(stderr, _("%s: error: unknown version %d\n"),
			program_name, version);
		usage();
	}

	if (PAGES < 10) {
		fprintf(stderr,
			_("%s: error: swap area needs to be at least %ldkB\n"),
			program_name, (long)(10 * pagesize / 1000));
		usage();
	}

	if (version == 0)
		maxpages = V0_MAX_PAGES;
	else if (linux_version_code() >= MAKE_VERSION(2,3,4))
		maxpages = PAGES;
	else if (linux_version_code() >= MAKE_VERSION(2,2,1))
		maxpages = V1_MAX_PAGES;
	else
		maxpages = V1_OLD_MAX_PAGES;

	if (PAGES > maxpages) {
		PAGES = maxpages;
		fprintf(stderr,
			_("%s: warning: truncating swap area to %ldkB\n"),
			program_name, PAGES * pagesize / 1000);
	}

	if (opt_label && version == 0) {
		fprintf(stderr,
			_("%s: error: label only with v1 swap area\n"),
			program_name);
		usage();
	}

	DEV = open(device_name,O_RDWR);
	if (DEV < 0 || fstat(DEV, &statbuf) < 0) {
		perror(device_name);
		exit(1);
	}

	/* Want a block device. Probably not /dev/hda or /dev/hdb. */
	if (!S_ISBLK(statbuf.st_mode))
		check=0;
	else if (statbuf.st_rdev == 0x0300 || statbuf.st_rdev == 0x0340)
		die(_("Will not try to make swapdevice on '%s'"));

#ifdef __sparc__
	if (!force && version == 0) {
		/* Don't overwrite partition table unless forced */
		unsigned char *buffer = (unsigned char *)signature_page;
		unsigned short *q, sum;

		if (read(DEV, buffer, 512) != 512)
			die(_("fatal: first page unreadable"));
		if (buffer[508] == 0xDA && buffer[509] == 0xBE) {
			q = (unsigned short *)(buffer + 510);
			for (sum = 0; q >= (unsigned short *) buffer;)
				sum ^= *q--;
			if (!sum) {
				fprintf(stderr, _("\
%s: Device '%s' contains a valid Sun disklabel.\n\
This probably means creating v0 swap would destroy your partition table\n\
No swap created. If you really want to create swap v0 on that device, use\n\
the -f option to force it.\n"),
					program_name, device_name);
				exit(1);
			}
		}
	}
#endif

	if (version == 0 || check)
		check_blocks();
	if (version == 0 && !bit_test_and_clear(signature_page,0))
		die(_("fatal: first page unreadable"));
	if (version == 1) {
		p->version = version;
		p->last_page = PAGES-1;
		p->nr_badpages = badpages;
	}

	goodpages = PAGES - badpages - 1;
	if ((long) goodpages <= 0)
		die(_("Unable to set up swap-space: unreadable"));
	printf(_("Setting up swapspace version %d, size = %llu kB\n"),
		version, (unsigned long long)goodpages * pagesize / 1000);
	write_signature((version == 0) ? "SWAP-SPACE" : "SWAPSPACE2");

	if (version == 1)
		write_uuid_and_label(uuid, opt_label);

	offset = ((version == 0) ? 0 : 1024);
	if (lseek(DEV, offset, SEEK_SET) != offset)
		die(_("unable to rewind swap-device"));
	if (write(DEV,(char*)signature_page+offset, pagesize-offset)
	    != pagesize-offset)
		die(_("unable to write signature page"));

	/*
	 * A subsequent swapon() will fail if the signature
	 * is not actually on disk. (This is a kernel bug.)
	 */
#ifdef HAVE_FSYNC
	if (fsync(DEV))
		 die(_("fsync failed"));
#endif
	return 0;
}
