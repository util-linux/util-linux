/*
 * mkfs.c - make a linux (minix) file-system.
 *
 * (C) 1991 Linus Torvalds. This file may be redistributed as per
 * the Linux copyright.
 */

/*
 * DD.MM.YY
 *
 * 24.11.91  -	Time began. Used the fsck sources to get started.
 *
 * 25.11.91  -	Corrected some bugs. Added support for ".badblocks"
 *		The algorithm for ".badblocks" is a bit weird, but
 *		it should work. Oh, well.
 *
 * 25.01.92  -	Added the -l option for getting the list of bad blocks
 *		out of a named file. (Dave Rivers, rivers@ponds.uucp)
 *
 * 28.02.92  -	Added %-information when using -c.
 *
 * 28.02.93  -	Added support for other namelengths than the original
 *		14 characters so that I can test the new kernel routines..
 *
 * 09.10.93  -	Make exit status conform to that required by fsutil
 *		(Rik Faith, faith@cs.unc.edu)
 *
 * 31.10.93  -	Added inode request feature, for backup floppies: use
 *		32 inodes, for a news partition use more.
 *		(Scott Heavner, sdh@po.cwru.edu)
 *
 * 03.01.94  -	Added support for file system valid flag.
 *		(Dr. Wettstein, greg%wind.uucp@plains.nodak.edu)
 *
 * 30.10.94 - added support for v2 filesystem
 *	      (Andreas Schwab, schwab@issan.informatik.uni-dortmund.de)
 * 
 * 09.11.94  -	Added test to prevent overwrite of mounted fs adapted
 *		from Theodore Ts'o's (tytso@athena.mit.edu) mke2fs
 *		program.  (Daniel Quinlan, quinlan@yggdrasil.com)
 *
 * 03.20.95  -	Clear first 512 bytes of filesystem to make certain that
 *		the filesystem is not misidentified as a MS-DOS FAT filesystem.
 *		(Daniel Quinlan, quinlan@yggdrasil.com)
 *
 * 02.07.96  -  Added small patch from Russell King to make the program a
 *		good deal more portable (janl@math.uio.no)
 *
 * Usage:  mkfs [-c | -l filename ] [-v] [-nXX] [-iXX] device [size-in-blocks]
 *
 *	-c for readablility checking (SLOW!)
 *      -l for getting a list of bad blocks from a file.
 *	-n for namelength (currently the kernel only uses 14 or 30)
 *	-i for number of inodes
 *	-v for v2 filesystem
 *
 * The device may be a block device or a image of one, but this isn't
 * enforced (but it's not much fun on a character device :-). 
 */

#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <ctype.h>
#include <stdlib.h>
#include <termios.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <mntent.h>

#include <linux/fs.h>
#include <linux/minix_fs.h>

#ifdef MINIX2_SUPER_MAGIC2
#define HAVE_MINIX2 1
#endif

#ifndef __GNUC__
#error "needs gcc for the bitop-__asm__'s"
#endif

#ifndef __linux__
#define volatile
#endif

#define MINIX_ROOT_INO 1
#define MINIX_BAD_INO 2

#define TEST_BUFFER_BLOCKS 16
#define MAX_GOOD_BLOCKS 512

#define UPPER(size,n) ((size+((n)-1))/(n))
#define INODE_SIZE (sizeof(struct minix_inode))
#ifdef HAVE_MINIX2
#define INODE_SIZE2 (sizeof(struct minix2_inode))
#define INODE_BLOCKS UPPER(INODES, (version2 ? MINIX2_INODES_PER_BLOCK \
				    : MINIX_INODES_PER_BLOCK))
#else
#define INODE_BLOCKS UPPER(INODES, (MINIX_INODES_PER_BLOCK))
#endif
#define INODE_BUFFER_SIZE (INODE_BLOCKS * BLOCK_SIZE)

#define BITS_PER_BLOCK (BLOCK_SIZE<<3)

static char * program_name = "mkfs";
static char * device_name = NULL;
static int DEV = -1;
static long BLOCKS = 0;
static int check = 0;
static int badblocks = 0;
static int namelen = 30;	/* default (changed to 30, per Linus's
				   suggestion, Sun Nov 21 08:05:07 1993) */
static int dirsize = 32;
static int magic = MINIX_SUPER_MAGIC2;
static int version2 = 0;

static char root_block[BLOCK_SIZE] = "\0";

static char * inode_buffer = NULL;
#define Inode (((struct minix_inode *) inode_buffer)-1)
#ifdef HAVE_MINIX2
#define Inode2 (((struct minix2_inode *) inode_buffer)-1)
#endif
static char super_block_buffer[BLOCK_SIZE];
static char boot_block_buffer[512];
#define Super (*(struct minix_super_block *)super_block_buffer)
#define INODES ((unsigned long)Super.s_ninodes)
#ifdef HAVE_MINIX2
#define ZONES ((unsigned long)(version2 ? Super.s_zones : Super.s_nzones))
#else
#define ZONES ((unsigned long)(Super.s_nzones))
#endif
#define IMAPS ((unsigned long)Super.s_imap_blocks)
#define ZMAPS ((unsigned long)Super.s_zmap_blocks)
#define FIRSTZONE ((unsigned long)Super.s_firstdatazone)
#define ZONESIZE ((unsigned long)Super.s_log_zone_size)
#define MAXSIZE ((unsigned long)Super.s_max_size)
#define MAGIC (Super.s_magic)
#define NORM_FIRSTZONE (2+IMAPS+ZMAPS+INODE_BLOCKS)

static char inode_map[BLOCK_SIZE * MINIX_I_MAP_SLOTS];
static char zone_map[BLOCK_SIZE * MINIX_Z_MAP_SLOTS];

static unsigned short good_blocks_table[MAX_GOOD_BLOCKS];
static int used_good_blocks = 0;
static unsigned long req_nr_inodes = 0;

#include "bitops.h"

#define inode_in_use(x) (bit(inode_map,(x)))
#define zone_in_use(x) (bit(zone_map,(x)-FIRSTZONE+1))

#define mark_inode(x) (setbit(inode_map,(x)))
#define unmark_inode(x) (clrbit(inode_map,(x)))

#define mark_zone(x) (setbit(zone_map,(x)-FIRSTZONE+1))
#define unmark_zone(x) (clrbit(zone_map,(x)-FIRSTZONE+1))

/*
 * Volatile to let gcc know that this doesn't return. When trying
 * to compile this under minix, volatile gives a warning, as
 * exit() isn't defined as volatile under minix.
 */
volatile void fatal_error(const char * fmt_string,int status)
{
	fprintf(stderr,fmt_string,program_name,device_name);
	exit(status);
}

#define usage() fatal_error("Usage: %s [-c | -l filename] [-nXX] [-iXX] /dev/name [blocks]\n",16)
#define die(str) fatal_error("%s: " str "\n",8)

/*
 * Check to make certain that our new filesystem won't be created on
 * an already mounted partition.  Code adapted from mke2fs, Copyright
 * (C) 1994 Theodore Ts'o.  Also licensed under GPL.
 */
static void check_mount(void)
{
	FILE * f;
	struct mntent * mnt;

	if ((f = setmntent (MOUNTED, "r")) == NULL)
		return;
	while ((mnt = getmntent (f)) != NULL)
		if (strcmp (device_name, mnt->mnt_fsname) == 0)
			break;
	endmntent (f);
	if (!mnt)
		return;

	die("%s is mounted; will not make a filesystem here!");
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

static int count_blocks (int fd)
{
	int high, low;

	low = 0;
	for (high = 1; valid_offset (fd, high); high *= 2)
		low = high;
	while (low < high - 1)
	{
		const int mid = (low + high) / 2;

		if (valid_offset (fd, mid))
			low = mid;
		else
			high = mid;
	}
	valid_offset (fd, 0);
	return (low + 1);
}

static int get_size(const char  *file)
{
	int	fd;
	int	size;

	fd = open(file, O_RDWR);
	if (fd < 0) {
		perror(file);
		exit(1);
	}
	if (ioctl(fd, BLKGETSIZE, &size) >= 0) {
		close(fd);
		return (size * 512);
	}
		
	size = count_blocks(fd);
	close(fd);
	return size;
}

void write_tables(void)
{
	/* Mark the super block valid. */
	Super.s_state |= MINIX_VALID_FS;
	Super.s_state &= ~MINIX_ERROR_FS;

	if (lseek(DEV, 0, SEEK_SET))
		die("seek to boot block failed in write_tables");
	if (512 != write(DEV, boot_block_buffer, 512))
		die("unable to clear boot sector");
	if (BLOCK_SIZE != lseek(DEV, BLOCK_SIZE, SEEK_SET))
		die("seek failed in write_tables");
	if (BLOCK_SIZE != write(DEV, super_block_buffer, BLOCK_SIZE))
		die("unable to write super-block");
	if (IMAPS*BLOCK_SIZE != write(DEV,inode_map,IMAPS*BLOCK_SIZE))
		die("unable to write inode map");
	if (ZMAPS*BLOCK_SIZE != write(DEV,zone_map,ZMAPS*BLOCK_SIZE))
		die("unable to write zone map");
	if (INODE_BUFFER_SIZE != write(DEV,inode_buffer,INODE_BUFFER_SIZE))
		die("unable to write inodes");
	
}

void write_block(int blk, char * buffer)
{
	if (blk*BLOCK_SIZE != lseek(DEV, blk*BLOCK_SIZE, SEEK_SET))
		die("seek failed in write_block");
	if (BLOCK_SIZE != write(DEV, buffer, BLOCK_SIZE))
		die("write failed in write_block");
}

int get_free_block(void)
{
	int blk;

	if (used_good_blocks+1 >= MAX_GOOD_BLOCKS)
		die("too many bad blocks");
	if (used_good_blocks)
		blk = good_blocks_table[used_good_blocks-1]+1;
	else
		blk = FIRSTZONE;
	while (blk < ZONES && zone_in_use(blk))
		blk++;
	if (blk >= ZONES)
		die("not enough good blocks");
	good_blocks_table[used_good_blocks] = blk;
	used_good_blocks++;
	return blk;
}

void mark_good_blocks(void)
{
	int blk;

	for (blk=0 ; blk < used_good_blocks ; blk++)
		mark_zone(good_blocks_table[blk]);
}

inline int next(int zone)
{
	if (!zone)
		zone = FIRSTZONE-1;
	while (++zone < ZONES)
		if (zone_in_use(zone))
			return zone;
	return 0;
}

void make_bad_inode(void)
{
	struct minix_inode * inode = &Inode[MINIX_BAD_INO];
	int i,j,zone;
	int ind=0,dind=0;
	unsigned short ind_block[BLOCK_SIZE>>1];
	unsigned short dind_block[BLOCK_SIZE>>1];

#define NEXT_BAD (zone = next(zone))

	if (!badblocks)
		return;
	mark_inode(MINIX_BAD_INO);
	inode->i_nlinks = 1;
	inode->i_time = time(NULL);
	inode->i_mode = S_IFREG + 0000;
	inode->i_size = badblocks*BLOCK_SIZE;
	zone = next(0);
	for (i=0 ; i<7 ; i++) {
		inode->i_zone[i] = zone;
		if (!NEXT_BAD)
			goto end_bad;
	}
	inode->i_zone[7] = ind = get_free_block();
	memset(ind_block,0,BLOCK_SIZE);
	for (i=0 ; i<512 ; i++) {
		ind_block[i] = zone;
		if (!NEXT_BAD)
			goto end_bad;
	}
	inode->i_zone[8] = dind = get_free_block();
	memset(dind_block,0,BLOCK_SIZE);
	for (i=0 ; i<512 ; i++) {
		write_block(ind,(char *) ind_block);
		dind_block[i] = ind = get_free_block();
		memset(ind_block,0,BLOCK_SIZE);
		for (j=0 ; j<512 ; j++) {
			ind_block[j] = zone;
			if (!NEXT_BAD)
				goto end_bad;
		}
	}
	die("too many bad blocks");
end_bad:
	if (ind)
		write_block(ind, (char *) ind_block);
	if (dind)
		write_block(dind, (char *) dind_block);
}

#ifdef HAVE_MINIX2
void
make_bad_inode2 (void)
{
	struct minix2_inode *inode = &Inode2[MINIX_BAD_INO];
	int i, j, zone;
	int ind = 0, dind = 0;
	unsigned long ind_block[BLOCK_SIZE >> 2];
	unsigned long dind_block[BLOCK_SIZE >> 2];

	if (!badblocks)
		return;
	mark_inode (MINIX_BAD_INO);
	inode->i_nlinks = 1;
	inode->i_atime = inode->i_mtime = inode->i_ctime = time (NULL);
	inode->i_mode = S_IFREG + 0000;
	inode->i_size = badblocks * BLOCK_SIZE;
	zone = next (0);
	for (i = 0; i < 7; i++) {
		inode->i_zone[i] = zone;
		if (!NEXT_BAD)
			goto end_bad;
	}
	inode->i_zone[7] = ind = get_free_block ();
	memset (ind_block, 0, BLOCK_SIZE);
	for (i = 0; i < 256; i++) {
		ind_block[i] = zone;
		if (!NEXT_BAD)
			goto end_bad;
	}
	inode->i_zone[8] = dind = get_free_block ();
	memset (dind_block, 0, BLOCK_SIZE);
	for (i = 0; i < 256; i++) {
		write_block (ind, (char *) ind_block);
		dind_block[i] = ind = get_free_block ();
		memset (ind_block, 0, BLOCK_SIZE);
		for (j = 0; j < 256; j++) {
			ind_block[j] = zone;
			if (!NEXT_BAD)
				goto end_bad;
		}
	}
	/* Could make triple indirect block here */
	die ("too many bad blocks");
 end_bad:
	if (ind)
		write_block (ind, (char *) ind_block);
	if (dind)
		write_block (dind, (char *) dind_block);
}
#endif

void make_root_inode(void)
{
	struct minix_inode * inode = &Inode[MINIX_ROOT_INO];

	mark_inode(MINIX_ROOT_INO);
	inode->i_zone[0] = get_free_block();
	inode->i_nlinks = 2;
	inode->i_time = time(NULL);
	if (badblocks)
		inode->i_size = 3*dirsize;
	else {
		root_block[2*dirsize] = '\0';
		root_block[2*dirsize+1] = '\0';
		inode->i_size = 2*dirsize;
	}
	inode->i_mode = S_IFDIR + 0755;
	write_block(inode->i_zone[0],root_block);
}

#ifdef HAVE_MINIX2
void
make_root_inode2 (void)
{
	struct minix2_inode *inode = &Inode2[MINIX_ROOT_INO];

	mark_inode (MINIX_ROOT_INO);
	inode->i_zone[0] = get_free_block ();
	inode->i_nlinks = 2;
	inode->i_atime = inode->i_mtime = inode->i_ctime = time (NULL);
	if (badblocks)
		inode->i_size = 3 * dirsize;
	else {
		root_block[2 * dirsize] = '\0';
		root_block[2 * dirsize + 1] = '\0';
		inode->i_size = 2 * dirsize;
	}
	inode->i_mode = S_IFDIR + 0755;
	write_block (inode->i_zone[0], root_block);
}
#endif

void setup_tables(void)
{
	int i;
	unsigned long inodes;

	memset(inode_map,0xff,sizeof(inode_map));
	memset(zone_map,0xff,sizeof(zone_map));
	memset(super_block_buffer,0,BLOCK_SIZE);
	memset(boot_block_buffer,0,512);
	MAGIC = magic;
	ZONESIZE = 0;
	MAXSIZE = version2 ? 0x7fffffff : (7+512+512*512)*1024;
	ZONES = BLOCKS;
/* some magic nrs: 1 inode / 3 blocks */
	if ( req_nr_inodes == 0 ) 
		inodes = BLOCKS/3;
	else
		inodes = req_nr_inodes;
	if (inodes > 65535)
		inodes = 65535;
	INODES = inodes;
/* I don't want some off-by-one errors, so this hack... */
	if ((INODES & 8191) > 8188)
		INODES -= 5;
	if ((INODES & 8191) < 10)
		INODES -= 20;
	IMAPS = UPPER(INODES,BITS_PER_BLOCK);
	ZMAPS = 0;
	while (ZMAPS != UPPER(BLOCKS - NORM_FIRSTZONE,BITS_PER_BLOCK))
		ZMAPS = UPPER(BLOCKS - NORM_FIRSTZONE,BITS_PER_BLOCK);
	if (ZMAPS > 64)
		die ("Filesystem too big for Linux to handle");
	FIRSTZONE = NORM_FIRSTZONE;
	for (i = FIRSTZONE ; i<ZONES ; i++)
		unmark_zone(i);
	for (i = MINIX_ROOT_INO ; i<INODES ; i++)
		unmark_inode(i);
	inode_buffer = malloc(INODE_BUFFER_SIZE);
	if (!inode_buffer)
		die("unable to allocate buffer for inodes");
	memset(inode_buffer,0,INODE_BUFFER_SIZE);
	printf("%ld inodes\n",INODES);
	printf("%ld blocks\n",ZONES);
	printf("Firstdatazone=%ld (%ld)\n",FIRSTZONE,NORM_FIRSTZONE);
	printf("Zonesize=%d\n",BLOCK_SIZE<<ZONESIZE);
	printf("Maxsize=%ld\n\n",MAXSIZE);
}

/*
 * Perform a test of a block; return the number of
 * blocks readable/writeable.
 */
long do_check(char * buffer, int try, unsigned int current_block) 
{
	long got;
	
	/* Seek to the correct loc. */
	if (lseek(DEV, current_block * BLOCK_SIZE, SEEK_SET) !=
                       current_block * BLOCK_SIZE ) {
                 die("seek failed during testing of blocks");
	}


	/* Try the read */
	got = read(DEV, buffer, try * BLOCK_SIZE);
	if (got < 0) got = 0;	
	if (got & (BLOCK_SIZE - 1 )) {
		printf("Weird values in do_check: probably bugs\n");
	}
	got /= BLOCK_SIZE;
	return got;
}

static unsigned int currently_testing = 0;

void alarm_intr(int alnum)
{
	if (currently_testing >= ZONES)
		return;
	signal(SIGALRM,alarm_intr);
	alarm(5);
	if (!currently_testing)
		return;
	printf("%d ...", currently_testing);
	fflush(stdout);
}

void check_blocks(void)
{
	int try,got;
	static char buffer[BLOCK_SIZE * TEST_BUFFER_BLOCKS];

	currently_testing=0;
	signal(SIGALRM,alarm_intr);
	alarm(5);
	while (currently_testing < ZONES) {
		if (lseek(DEV,currently_testing*BLOCK_SIZE,SEEK_SET) !=
		currently_testing*BLOCK_SIZE)
			die("seek failed in check_blocks");
		try = TEST_BUFFER_BLOCKS;
		if (currently_testing + try > ZONES)
			try = ZONES-currently_testing;
		got = do_check(buffer, try, currently_testing);
		currently_testing += got;
		if (got == try)
			continue;
		if (currently_testing < FIRSTZONE)
			die("bad blocks before data-area: cannot make fs");
		mark_zone(currently_testing);
		badblocks++;
		currently_testing++;
	}
	if (badblocks)
		printf("%d bad block%s\n",badblocks,(badblocks>1)?"s":"");
}

void get_list_blocks(filename)
char *filename;

{
  FILE *listfile;
  unsigned long blockno;

  listfile=fopen(filename,"r");
  if(listfile == (FILE *)NULL) {
    die("can't open file of bad blocks");
  }
  while(!feof(listfile)) {
    fscanf(listfile,"%ld\n", &blockno);
    mark_zone(blockno);
    badblocks++;
  }
  if(badblocks) {
    printf("%d bad block%s\n", badblocks, (badblocks>1)?"s":"");
  }
}

int main(int argc, char ** argv)

{
  int i;
  char * tmp;
  struct stat statbuf;
  char * listfile = NULL;

  if (argc && *argv)
    program_name = *argv;
  if (INODE_SIZE * MINIX_INODES_PER_BLOCK != BLOCK_SIZE)
    die("bad inode size");
#ifdef HAVE_MINIX2
  if (INODE_SIZE2 * MINIX2_INODES_PER_BLOCK != BLOCK_SIZE)
    die("bad inode size");
#endif
  while (argc-- > 1) {
    argv++;
    if (argv[0][0] != '-')
      if (device_name) {
	BLOCKS = strtol(argv[0],&tmp,0);
	if (*tmp) {
	  printf("strtol error: number of"
		 " blocks not specified");
	  usage();
	}
      } else
	device_name = argv[0];
    else { 
      if(argv[0][1] == 'l') {
	listfile = argv[1];
	argv++;
	if (!(argc--))
	  usage();
      } else {
	if(argv[0][1] == 'i') {
	  req_nr_inodes
	    = (unsigned long)atol(argv[1]);
	  argv++;
	  if (!(argc--))
	    usage();
	} else while (*(++argv[0])) {
	  switch (argv[0][0]) {
	  case 'c': check=1; break;
	  case 'n':
	    i = strtoul(argv[0]+1,&tmp,0);
	    if (*tmp)
	      usage();
	    argv[0][1] = '\0';
	    if (i == 14)
	      magic = MINIX_SUPER_MAGIC;
	    else if (i == 30)
	      magic = MINIX_SUPER_MAGIC2;
	    else
	      usage();
	    namelen = i;
	    dirsize = i+2;
	    break;
	  case 'v':
#ifdef HAVE_MINIX2
	    version2 = 1;
#else
	    fatal_error("%s: not compiled with minix v2 support\n",-1);
#endif
	    break;
	  default: usage();
	  }
	}
      }
    }
  }
  if (device_name && !BLOCKS)
    BLOCKS = get_size (device_name) / 1024;
  if (!device_name || BLOCKS<10) {
    usage();
  }
#ifdef HAVE_MINIX2
  if (version2) {
    if (namelen == 14)
      magic = MINIX2_SUPER_MAGIC;
    else
      magic = MINIX2_SUPER_MAGIC2;
  } else
#endif
    if (BLOCKS > 65535)
      BLOCKS = 65535;
  check_mount();		/* is it already mounted? */
  tmp = root_block;
  *(short *)tmp = 1;
  strcpy(tmp+2,".");
  tmp += dirsize;
  *(short *)tmp = 1;
  strcpy(tmp+2,"..");
  tmp += dirsize;
  *(short *)tmp = 2;
  strcpy(tmp+2,".badblocks");
  DEV = open(device_name,O_RDWR );
  if (DEV<0)
    die("unable to open %s");
  if (fstat(DEV,&statbuf)<0)
    die("unable to stat %s");
  if (!S_ISBLK(statbuf.st_mode))
    check=0;
  else if (statbuf.st_rdev == 0x0300 || statbuf.st_rdev == 0x0340)
    die("will not try to make filesystem on '%s'");
  setup_tables();
  if (check)
    check_blocks();
  else if (listfile)
    get_list_blocks(listfile);
#ifdef HAVE_MINIX2
  if (version2) {
    make_root_inode2 ();
    make_bad_inode2 ();
  } else
#endif
    {
      make_root_inode();
      make_bad_inode();
    }
  mark_good_blocks();
  write_tables();
  return 0;
}
