/*
 * cramfsck - check a cramfs file system
 *
 * Copyright (C) 2000-2001 Transmeta Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * 1999/12/03: Linus Torvalds (cramfs tester and unarchive program)
 * 2000/06/03: Daniel Quinlan (CRC and length checking program)
 * 2000/06/04: Daniel Quinlan (merged programs, added options, support
 *                            for special files, preserve permissions and
 *                            ownership, cramfs superblock v2, bogus mode
 *                            test, pathname length test, etc.)
 * 2000/06/06: Daniel Quinlan (support for holes, pretty-printing,
 *                            symlink size test)
 * 2000/07/11: Daniel Quinlan (file length tests, start at offset 0 or 512,
 *                            fsck-compatible exit codes)
 * 2000/07/15: Daniel Quinlan (initial support for block devices)
 */

/* compile-time options */
#define INCLUDE_FS_TESTS	/* include cramfs checking and extraction */

#include <stdio.h>
#include <unistd.h>
#include <dirent.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <getopt.h>
#include <utime.h>
#include <fcntl.h>
#include <zlib.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/sysmacros.h>	/* for major, minor */

#include "cramfs.h"
#include "../defines.h"		/* for HAVE_lchown */
#include "nls.h"

#define BLKGETSIZE _IO(0x12,96) /* return device size */

static const char *progname = "cramfsck";

static int fd;			/* ROM image file descriptor */
static char *filename;		/* ROM image filename */
struct cramfs_super *super;	/* just find the cramfs superblock once */
static int opt_verbose = 0;	/* 1 = verbose (-v), 2+ = very verbose (-vv) */

#ifdef INCLUDE_FS_TESTS
static int opt_extract = 0;	/* extract cramfs (-x) */
char *extract_dir = NULL;	/* extraction directory (-x) */

unsigned long start_inode = 1 << 28;	/* start of first non-root inode */
unsigned long end_inode = 0;		/* end of the directory structure */
unsigned long start_data = 1 << 28;	/* start of the data (256 MB = max) */
unsigned long end_data = 0;		/* end of the data */
/* true?  cramfs_super < start_inode < end_inode <= start_data <= end_data */
static uid_t euid;			/* effective UID */

#define PAD_SIZE 512

#include <asm/page.h>
#ifdef PAGE_SIZE
#define PAGE_CACHE_SIZE ((int) PAGE_SIZE)
#elif defined __ia64__
#define PAGE_CACHE_SIZE (16384)
#elif defined __alpha__
#define PAGE_CACHE_SIZE (8192)
#else
#define PAGE_CACHE_SIZE (4096)
#endif

/* Guarantee access to at least 8kB at a time */
#define ROMBUFFER_BITS	13
#define ROMBUFFERSIZE	(1 << ROMBUFFER_BITS)
#define ROMBUFFERMASK	(ROMBUFFERSIZE-1)
static char read_buffer[ROMBUFFERSIZE * 2];
static unsigned long read_buffer_block = ~0UL;

/* Uncompressing data structures... */
static char outbuffer[PAGE_CACHE_SIZE*2];
z_stream stream;

#endif /* INCLUDE_FS_TESTS */

/* Input status of 0 to print help and exit without an error. */
static void usage(int status)
{
	FILE *stream = status ? stderr : stdout;

	fprintf(stream, _("usage: %s [-hv] [-x dir] file\n"
		" -h         print this help\n"
		" -x dir     extract into dir\n"
		" -v         be more verbose\n"
		" file       file to test\n"), progname);

	exit(status);
}

#ifdef INCLUDE_FS_TESTS
static void print_node(char type, struct cramfs_inode *i, char *name)
{
	char info[10];

	if (S_ISCHR(i->mode) || (S_ISBLK(i->mode))) {
		/* major/minor numbers can be as high as 2^12 or 4096 */
		snprintf(info, 10, "%4d,%4d", major(i->size), minor(i->size));
	}
	else {
		/* size be as high as 2^24 or 16777216 */
		snprintf(info, 10, "%9d", i->size);
	}

	printf("%c %04o %s %5d:%-3d %s\n",
	       type, i->mode & ~S_IFMT, info, i->uid, i->gid, name);
}

/*
 * Create a fake "blocked" access
 */
static void *romfs_read(unsigned long offset)
{
	unsigned int block = offset >> ROMBUFFER_BITS;
	if (block != read_buffer_block) {
		read_buffer_block = block;
		lseek(fd, block << ROMBUFFER_BITS, SEEK_SET);
		read(fd, read_buffer, ROMBUFFERSIZE * 2);
	}
	return read_buffer + (offset & ROMBUFFERMASK);
}

static struct cramfs_inode *cramfs_iget(struct cramfs_inode * i)
{
	struct cramfs_inode *inode = malloc(sizeof(struct cramfs_inode));
	*inode = *i;
	return inode;
}

static struct cramfs_inode *iget(unsigned int ino)
{
	return cramfs_iget(romfs_read(ino));
}

#if 0
static void iput(struct cramfs_inode *inode)
{
	free(inode);
}
#endif

/*
 * Return the offset of the root directory,
 * or 0 if none.
 */
static struct cramfs_inode *read_super(void)
{
	unsigned long offset;

	offset = super->root.offset << 2;
	if (super->magic != CRAMFS_MAGIC)
		return NULL;
	if (memcmp(super->signature, CRAMFS_SIGNATURE, sizeof(super->signature)) != 0)
		return NULL;
	if (offset < sizeof(super))
		return NULL;
	return cramfs_iget(&super->root);
}

static int uncompress_block(void *src, int len)
{
	int err;

	stream.next_in = src;
	stream.avail_in = len;

	stream.next_out = (unsigned char *) outbuffer;
	stream.avail_out = PAGE_CACHE_SIZE*2;

	inflateReset(&stream);

	err = inflate(&stream, Z_FINISH);
	if (err != Z_STREAM_END) {
		fprintf(stderr,
			_("%s: error %d while decompressing! %p(%d)\n"),
			filename, err, src, len);
		exit(4);
	}
	return stream.total_out;
}

#ifdef HAVE_lchown
#define my_lchown lchown
#else
#define my_lchown chown
#endif

static void change_file_status(char *path, struct cramfs_inode *i)
{
	struct utimbuf epoch = { 0, 0 };

	if (euid == 0) {
		if (my_lchown(path, i->uid, i->gid) < 0) {
			perror(path);
			exit(8);
		}
		if (S_ISLNK(i->mode))
			return;
		if ((S_ISUID | S_ISGID) & i->mode) {
			if (chmod(path, i->mode) < 0) {
				perror(path);
				exit(8);
			}
		}
	}
	if (S_ISLNK(i->mode))
		return;
	if (utime(path, &epoch) < 0) {
		perror(path);
		exit(8);
	}
}

static void do_symlink(char *path, struct cramfs_inode *i)
{
	unsigned long offset = i->offset << 2;
	unsigned long curr = offset + 4;
	unsigned long next = *(u32 *) romfs_read(offset);
	unsigned long size;

	if (next > end_data) {
		end_data = next;
	}

	size = uncompress_block(romfs_read(curr), next - curr);
	if (size != i->size) {
		fprintf(stderr, _("%s: size error in symlink `%s'\n"),
			filename, path);
		exit(4);
	}
	outbuffer[size] = 0;
	if (opt_verbose) {
		char *str;

		str = malloc(strlen(outbuffer) + strlen(path) + 5);
		strcpy(str, path);
		strncat(str, " -> ", 4);
		strncat(str, outbuffer, size);

		print_node('l', i, str);
		if (opt_verbose > 1) {
			printf(_("  uncompressing block at %ld "
				 "to %ld (%ld)\n"),
			       curr, next, next - curr);
		}
	}
	if (opt_extract) {
		symlink(outbuffer, path);
		change_file_status(path, i);
	}
}

static void do_special_inode(char *path, struct cramfs_inode *i)
{
	dev_t devtype = 0;
	char type;

	if (S_ISCHR(i->mode)) {
		devtype = i->size;
		type = 'c';
	}
	else if (S_ISBLK(i->mode)) {
		devtype = i->size;
		type = 'b';
	}
	else if (S_ISFIFO(i->mode))
		type = 'p';
	else if (S_ISSOCK(i->mode))
		type = 's';
	else {
		fprintf(stderr, _("%s: bogus mode on `%s' (%o)\n"),
			filename, path, i->mode);
		exit(4);
	}

	if (opt_verbose) {
		print_node(type, i, path);
	}

	if (opt_extract) {
		if (mknod(path, i->mode, devtype) < 0) {
			perror(path);
			exit(8);
		}
		change_file_status(path, i);
	}
}

static void do_uncompress(int fd, unsigned long offset, unsigned long size)
{
	unsigned long curr = offset + 4 * ((size + PAGE_CACHE_SIZE - 1) / PAGE_CACHE_SIZE);

	do {
		unsigned long out = PAGE_CACHE_SIZE;
		unsigned long next = *(u32 *) romfs_read(offset);

		if (next > end_data)
			end_data = next;

		offset += 4;
		if (curr == next) {
			if (opt_verbose > 1) {
				printf(_("  hole at %ld (%d)\n"),
				       curr, PAGE_CACHE_SIZE);
			}
			if (size < PAGE_CACHE_SIZE)
				out = size;
			memset(outbuffer, 0x00, out);
		}
		else {
			if (opt_verbose > 1) {
				printf(_("  uncompressing block at %ld "
					 "to %ld (%ld)\n"),
					 curr, next, next - curr);
			}
			out = uncompress_block(romfs_read(curr), next - curr);
		}
		if (size >= PAGE_CACHE_SIZE) {
			if (out != PAGE_CACHE_SIZE) {
				fprintf(stderr,
					_("%s: Non-block (%ld) bytes\n"),
					filename, out);
				exit(4);
			}
		} else {
			if (out != size) {
				fprintf(stderr, _("%s: Non-size (%ld vs %ld) "
						  "bytes\n"),
					filename, out, size);
				exit(4);
			}
		}
		size -= out;
		if (opt_extract) {
			write(fd, outbuffer, out);
		}
		curr = next;
	} while (size);
}

static void expand_fs(int pathlen, char *path, struct cramfs_inode *inode)
{
	if (S_ISDIR(inode->mode)) {
		int count = inode->size;
		unsigned long offset = inode->offset << 2;
		char *newpath = malloc(pathlen + 256);

		if (count > 0 && offset < start_inode) {
			start_inode = offset;
		}
		/* XXX - need to check end_inode for empty case? */
		memcpy(newpath, path, pathlen);
		newpath[pathlen] = '/';
		pathlen++;
		if (opt_verbose) {
			print_node('d', inode, path);
		}
		if (opt_extract) {
			mkdir(path, inode->mode);
			change_file_status(path, inode);
		}
		while (count > 0) {
			struct cramfs_inode *child = iget(offset);
			int size;
			int newlen = child->namelen << 2;

			size = sizeof(struct cramfs_inode) + newlen;
			count -= size;

			offset += sizeof(struct cramfs_inode);

			memcpy(newpath + pathlen, romfs_read(offset), newlen);
			newpath[pathlen + newlen] = 0;
			if ((pathlen + newlen) - strlen(newpath) > 3) {
				fprintf(stderr,
					_("%s: invalid cramfs--bad "
					  "path length\n"),
					filename);
				exit(4);
			}
			expand_fs(strlen(newpath), newpath, child);

			offset += newlen;

			if (offset > end_inode) {
				end_inode = offset;
			}
		}
		return;
	}
	if (S_ISREG(inode->mode)) {
		int fd = 0;
		unsigned long offset = inode->offset << 2;

		if (offset > 0 && offset < start_data) {
			start_data = offset;
		}
		if (opt_verbose) {
			print_node('f', inode, path);
		}
		if (opt_extract) {
			fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, inode->mode);
		}
		if (inode->size) {
			do_uncompress(fd, offset, inode->size);
		}
		if (opt_extract) {
			close(fd);
			change_file_status(path, inode);
		}
		return;
	}
	if (S_ISLNK(inode->mode)) {
		unsigned long offset = inode->offset << 2;

		if (offset < start_data) {
			start_data = offset;
		}
		do_symlink(path, inode);
		return;
	}
	else {
		do_special_inode(path, inode);
		return;
	}
}
#endif /* INCLUDE_FS_TESTS */

int main(int argc, char **argv)
{
	void *buf;
	size_t length;
	struct stat st;
	u32 crc_old, crc_new;
#ifdef INCLUDE_FS_TESTS
	struct cramfs_inode *root;
#endif /* INCLUDE_FS_TESTS */
	int c;			/* for getopt */
	int start = 0;

	if (argc)
		progname = argv[0];

	/* command line options */
	while ((c = getopt(argc, argv, "hx:v")) != EOF) {
		switch (c) {
		case 'h':
			usage(0);
		case 'x':
#ifdef INCLUDE_FS_TESTS
			opt_extract = 1;
			extract_dir = malloc(strlen(optarg) + 1);
			strcpy(extract_dir, optarg);
			break;
#else /*  not INCLUDE_FS_TESTS */
			fprintf(stderr, _("%s: compiled without -x support\n"),
				progname);
			exit(16);
#endif /* not INCLUDE_FS_TESTS */
		case 'v':
			opt_verbose++;
			break;
		}
	}

	if ((argc - optind) != 1)
		usage(16);
	filename = argv[optind];

	/* find the physical size of the file or block device */
	if (lstat(filename, &st) < 0) {
		perror(filename);
		exit(8);
	}
	fd = open(filename, O_RDONLY);
	if (fd < 0) {
		perror(filename);
		exit(8);
	}
	if (S_ISBLK(st.st_mode)) {
		if (ioctl(fd, BLKGETSIZE, &length) < 0) {
			fprintf(stderr, _("%s: warning--unable to determine "
					  "filesystem size \n"), filename);
			exit(4);
		}
		length = length * 512;
	}
	else if (S_ISREG(st.st_mode)) {
		length = st.st_size;
	}
	else {
		fprintf(stderr, _("%s is not a block device or file\n"),
			filename);
		exit(8);
	}

	if (length < sizeof(struct cramfs_super)) {
		fprintf(stderr, _("%s: invalid cramfs--file length "
				  "too short\n"), filename);
		exit(4);
	}

	if (S_ISBLK(st.st_mode)) {
		/* nasty because mmap of block devices fails */
		buf = mmap(NULL, length, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		read(fd, buf, length);
	}
	else {
		/* nice and easy */
		buf = mmap(NULL, length, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
	}

	/* XXX - this could be cleaner... */
	if (((struct cramfs_super *) buf)->magic == CRAMFS_MAGIC) {
		start = 0;
		super = (struct cramfs_super *) buf;
	}
	else if (length >= (PAD_SIZE + sizeof(struct cramfs_super)) &&
		 ((((struct cramfs_super *) (buf + PAD_SIZE))->magic == CRAMFS_MAGIC)))
	{
		start = PAD_SIZE;
		super = (struct cramfs_super *) (buf + PAD_SIZE);
	}
	else {
		fprintf(stderr, _("%s: invalid cramfs--wrong magic\n"),
			filename);
		exit(4);
	}

	if (super->flags & CRAMFS_FLAG_FSID_VERSION_2) {
		/* length test */
		if (length < super->size) {
			fprintf(stderr, _("%s: invalid cramfs--file length "
					  "too short\n"), filename);
			exit(4);
		}
		else if (length > super->size) {
			fprintf(stderr, _("%s: warning--file length too long, "
					  "padded image?\n"), filename);
		}

		/* CRC test */
		crc_old = super->fsid.crc;
		super->fsid.crc = crc32(0L, Z_NULL, 0);
		crc_new = crc32(0L, Z_NULL, 0);
		crc_new = crc32(crc_new, (unsigned char *) buf+start, super->size - start);
		if (crc_new != crc_old) {
			fprintf(stderr, _("%s: invalid cramfs--crc error\n"),
				filename);
			exit(4);
		}
	}
	else {
		fprintf(stderr, _("%s: warning--old cramfs image, no CRC\n"),
			filename);
	}

#ifdef INCLUDE_FS_TESTS
	super = (struct cramfs_super *) malloc(sizeof(struct cramfs_super));
	if (((struct cramfs_super *) buf)->magic == CRAMFS_MAGIC) {
		memcpy(super, buf, sizeof(struct cramfs_super));
	}
	else if (length >= (PAD_SIZE + sizeof(struct cramfs_super)) &&
		 ((((struct cramfs_super *) (buf + PAD_SIZE))->magic == CRAMFS_MAGIC)))
	{
		memcpy(super, (buf + PAD_SIZE), sizeof(struct cramfs_super));
	}

	munmap(buf, length);

	/* file format test, uses fake "blocked" accesses */
	root = read_super();
	umask(0);
	euid = geteuid();
	if (!root) {
		fprintf(stderr, _("%s: invalid cramfs--bad superblock\n"),
			filename);
		exit(4);
	}
	stream.next_in = NULL;
	stream.avail_in = 0;
	inflateInit(&stream);

	if (!extract_dir)
		extract_dir = "root";

	expand_fs(strlen(extract_dir), extract_dir, root);
	inflateEnd(&stream);

	if (start_data != 1 << 28  && end_inode != start_data) {
		fprintf(stderr,
			_("%s: invalid cramfs--directory data end "
			  "(%ld) != file data start (%ld)\n"),
			filename, end_inode, start_data);
		exit(4);
	}
	if (super->flags & CRAMFS_FLAG_FSID_VERSION_2) {
		if (end_data > super->size) {
			fprintf(stderr,
				_("%s: invalid cramfs--invalid file "
				  "data offset\n"),
				filename);
			exit(4);
		}
	}
#endif /* INCLUDE_FS_TESTS */

	exit(0);
}
