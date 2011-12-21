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
	A_CREATE,		/* setup a new device */
	A_DELETE,		/* delete one or all devices */
	A_SHOW,			/* list devices */
	A_FIND_FREE,		/* find first unused */
	A_SET_CAPACITY,		/* set device capacity */
};

static int verbose;

static int is_associated(int dev, struct stat *file, unsigned long long offset, int isoff);

#define LOOPMAJOR		7
#define NLOOPS_DEFAULT		8	/* /dev/loop[0-7] */

struct looplist {
	int		flag;		/* scanning options */
	FILE		*proc;		/* /proc/partitions */
	int		ncur;		/* current position */
	int		*minors;	/* ary of minor numbers (when scan whole /dev) */
	int		nminors;	/* number of items in *minors */
	char		name[128];	/* device name */
	int		ct_perm;	/* count permission problems */
	int		ct_succ;	/* count number of successfully
					   detected devices */
};

#define LLFLG_USEDONLY	(1 << 1)	/* return used devices only */
#define LLFLG_FREEONLY	(1 << 2)	/* return non-used devices */
#define LLFLG_DONE	(1 << 3)	/* all is done */
#define LLFLG_PROCFS	(1 << 4)	/* try to found used devices in /proc/partitions */
#define LLFLG_SUBDIR	(1 << 5)	/* /dev/loop/N */
#define LLFLG_DFLT	(1 << 6)	/* directly try to check default loops */

#define SETLOOP_RDONLY     (1<<0)  /* Open loop read-only */
#define SETLOOP_AUTOCLEAR  (1<<1)  /* Automatically detach loop on close (2.6.25?) */

static int del_loop (const char *device);

/* TODO: move to lib/sysfs.c */
static char *loopfile_from_sysfs(const char *device)
{
	FILE *f;
	struct stat st;
	char buf[PATH_MAX], *res = NULL;

	if (stat(device, &st) || !S_ISBLK(st.st_mode))
		return NULL;

	snprintf(buf, sizeof(buf), _PATH_SYS_DEVBLOCK "/%d:%d/loop/backing_file",
			major(st.st_rdev), minor(st.st_rdev));

	f = fopen(buf, "r");
	if (!f)
		return NULL;

	if (fgets(buf, sizeof(buf), f)) {
		size_t sz = strlen(buf);
		if (sz) {
			buf[sz - 1] = '\0';
			res = xstrdup(buf);
		}
	}

	fclose(f);
	return res;
}

char *loopdev_get_loopfile(const char *device)
{
	char *res = loopfile_from_sysfs(device);

	if (!res) {
		struct loop_info64 lo64;
		int fd;

		if ((fd = open(device, O_RDONLY)) < 0)
			return NULL;

		if (ioctl(fd, LOOP_GET_STATUS64, &lo64) == 0) {
			lo64.lo_file_name[LO_NAME_SIZE-2] = '*';
			lo64.lo_file_name[LO_NAME_SIZE-1] = 0;
			res = xstrdup((char *) lo64.lo_file_name);

		}
		close(fd);
	}
	return res;
}

int
is_loop_device (const char *device) {
	struct stat st;

	return (stat(device, &st) == 0 &&
		S_ISBLK(st.st_mode) &&
		major(st.st_rdev) == LOOPMAJOR);
}

static int
is_loop_used(int fd)
{
	struct loop_info64 li;

	errno = 0;
	if (ioctl (fd, LOOP_GET_STATUS64, &li) < 0 && errno == ENXIO)
		return 0;
	return 1;
}

static int
is_loopfd_autoclear(int fd)
{
	struct loop_info64 lo64;

	if (ioctl(fd, LOOP_GET_STATUS64, &lo64) == 0) {
		if (lo64.lo_flags & LO_FLAGS_AUTOCLEAR)
			return 1;

	}
	return 0;
}

int
is_loop_autoclear(const char *device)
{
	int fd, rc;

	if ((fd = open(device, O_RDONLY)) < 0)
		return 0;
	rc = is_loopfd_autoclear(fd);

	close(fd);
	return rc;
}

static int
looplist_open(struct looplist *ll, int flag)
{
	struct stat st;

	memset(ll, 0, sizeof(*ll));
	ll->flag = flag;
	ll->ncur = -1;

	if (stat(_PATH_DEV, &st) == -1 || (!S_ISDIR(st.st_mode)))
		return -1;			/* /dev doesn't exist */

	if (stat(_PATH_DEV_LOOP, &st) == 0 && S_ISDIR(st.st_mode))
		ll->flag |= LLFLG_SUBDIR;	/* /dev/loop/ exists */

	if ((ll->flag & LLFLG_USEDONLY) &&
			stat(_PATH_PROC_PARTITIONS, &st) == 0)
		ll->flag |= LLFLG_PROCFS;	/* try /proc/partitions */

	ll->flag |= LLFLG_DFLT;			/* required! */
	return 0;
}

static void
looplist_close(struct looplist *ll)
{
	free(ll->minors);
	if (ll->proc)
		fclose(ll->proc);
	ll->minors = NULL;
	ll->proc = NULL;
	ll->ncur = -1;
	ll->flag |= LLFLG_DONE;
}

static int
looplist_open_dev(struct looplist *ll, int lnum)
{
	struct stat st;
	int used;
	int fd;

	/* create a full device path */
	snprintf(ll->name, sizeof(ll->name),
		ll->flag & LLFLG_SUBDIR ?
			_PATH_DEV_LOOP "/%d" :
			_PATH_DEV "loop%d",
		lnum);

	fd = open(ll->name, O_RDONLY);
	if (fd == -1) {
		if (errno == EACCES)
			ll->ct_perm++;
		return -1;
	}
	if (fstat(fd, &st) == -1)
		goto error;
	if (!S_ISBLK(st.st_mode) || major(st.st_rdev) != LOOPMAJOR)
		goto error;

	ll->ct_succ++;

	/* check if the device is wanted */
	if (!(ll->flag & (LLFLG_USEDONLY | LLFLG_FREEONLY)))
		return fd;

	used = is_loop_used(fd);

	if ((ll->flag & LLFLG_USEDONLY) && used)
		return fd;
	if ((ll->flag & LLFLG_FREEONLY) && !used)
		return fd;
error:
	close(fd);
	return -1;
}

/* returns <N> from "loop<N>" */
static int
name2minor(int hasprefix, const char *name)
{
	int n;
	char *end;

	if (hasprefix) {
		if (strncmp(name, "loop", 4))
			return -1;
		name += 4;
	}
	n = strtol(name, &end, 10);
	if (end && end != name && *end == '\0' && n >= 0)
		return n;
	return -1;
}

static int
cmpnum(const void *p1, const void *p2)
{
	return (*(int *) p1 > *(int *) p2) - (*(int *) p1 < *(int *) p2);
}

/*
 * The classic scandir() is more expensive and less portable.
 * We needn't full loop device names -- minor numbers (loop<N>)
 * are enough.
 */
static int
loop_scandir(const char *dirname, int **ary, int hasprefix)
{
	DIR *dir;
	struct dirent *d;
	int n, count = 0, arylen = 0;

	if (!dirname || !ary)
		return -1;
	dir = opendir(dirname);
	if (!dir)
		return -1;

	*ary = NULL;

	while((d = readdir(dir))) {
		if (d->d_type != DT_BLK && d->d_type != DT_UNKNOWN && d->d_type != DT_LNK)
			continue;
		n = name2minor(hasprefix, d->d_name);
		if (n == -1 || n < NLOOPS_DEFAULT)
			continue;
		if (count + 1 > arylen) {
			int *tmp;

			arylen += 1;

			tmp = realloc(*ary, arylen * sizeof(int));
			if (!tmp) {
				free(*ary);
				return -1;
			}
			*ary = tmp;
		}
		(*ary)[count++] = n;
	}
	if (count)
		qsort(*ary, count, sizeof(int), cmpnum);

	closedir(dir);
	return count;
}

static int
looplist_next(struct looplist *ll)
{
	int fd, n;

	if (ll->flag & LLFLG_DONE)
		return -1;

	/* A) Look for used loop devices in /proc/partitions ("losetup -a" only)
	 */
	if (ll->flag & LLFLG_PROCFS) {
		char buf[BUFSIZ];

		if (!ll->proc)
			ll->proc = fopen(_PATH_PROC_PARTITIONS, "r");

		while (ll->proc && fgets(buf, sizeof(buf), ll->proc)) {
			int m;
			unsigned long long sz;
			char name[128];

			if (sscanf(buf, " %d %d %llu %128[^\n ]",
						   &m, &n, &sz, name) != 4)
				continue;
			if (m != LOOPMAJOR)
				continue;
			/* unfortunately, real minor numbers needn't to match
			 * loop<N> device name. We have to follow device name.
			 */
			n = name2minor(1, name);
			fd = looplist_open_dev(ll, n);
			if (fd != -1)
				return fd;
		}
		goto done;
	}


	/* B) Classic way, try first eight loop devices (default number
	 *    of loop devices). This is enough for 99% of all cases.
	 */
	if (ll->flag & LLFLG_DFLT) {
		for (++ll->ncur; ll->ncur < NLOOPS_DEFAULT; ll->ncur++) {
			fd = looplist_open_dev(ll, ll->ncur);
			if (fd != -1)
				return fd;
		}
		ll->flag &= ~LLFLG_DFLT;
	}

	/* C) the worst possibility, scan all /dev or /dev/loop
	 */
	if (!ll->minors) {
		ll->nminors = (ll->flag & LLFLG_SUBDIR) ?
			loop_scandir(_PATH_DEV_LOOP, &ll->minors, 0) :
			loop_scandir(_PATH_DEV, &ll->minors, 1);
		ll->ncur = -1;
	}
	for (++ll->ncur; ll->ncur < ll->nminors; ll->ncur++) {
		fd = looplist_open_dev(ll, ll->minors[ll->ncur]);
		if (fd != -1)
			return fd;
	}

done:
	looplist_close(ll);
	return -1;
}

/* Find loop device associated with given @filename. Used for unmounting loop
 * device specified by associated backing file.
 *
 * returns: 1 no such device/error
 *          2 more than one loop device associated with @filename
 *          0 exactly one loop device associated with @filename
 *            (@loopdev points to string containing full device name)
 */
int
find_loopdev_by_backing_file(const char *filename, char **loopdev)
{
	struct looplist ll;
	struct stat filestat;
	int fd;
	int devs_n = 0;		/* number of loop devices found */
	char* devname = NULL;

	if (stat(filename, &filestat) == -1) {
		perror(filename);
		return 1;
	}

	if (looplist_open(&ll, LLFLG_USEDONLY) == -1) {
		warnx(_("/dev directory does not exist."));
		return 1;
	}

	while((devs_n < 2) && (fd = looplist_next(&ll)) != -1) {
		if (is_associated(fd, &filestat, 0, 0) == 1) {
			if (!devname)
				devname = xstrdup(ll.name);
			devs_n++;
		}
		close(fd);
	}
	looplist_close(&ll);

	if (devs_n == 1) {
		*loopdev = devname;
		return 0;		/* exactly one loopdev */
	}
	free(devname);
	return devs_n ? 2 : 1;		/* more loopdevs or error */
}

static int
set_capacity(const char *device)
{
	int errsv;
	int fd = open(device, O_RDONLY);

	if (fd == -1)
		goto err;

	if (ioctl(fd, LOOP_SET_CAPACITY) != 0)
		goto err;

	return 0;
err:
	errsv = errno;
	fprintf(stderr, _("loop: can't set capacity on device %s: %s\n"),
					device, strerror (errsv));
	if (fd != -1)
		close(fd);
	return 2;
}

static int
show_loop_fd(int fd, char *device) {
	struct loop_info64 loopinfo64;
	int errsv;

	if (ioctl(fd, LOOP_GET_STATUS64, &loopinfo64) == 0) {

		char *lofile = NULL;

		loopinfo64.lo_file_name[LO_NAME_SIZE-2] = '*';
		loopinfo64.lo_file_name[LO_NAME_SIZE-1] = 0;
		loopinfo64.lo_crypt_name[LO_NAME_SIZE-1] = 0;

		/* ioctl has limited buffer for backing file name, since
		 * kernel 2.6.37 the filename is available in sysfs too
		 */
		if (strlen((char *) loopinfo64.lo_file_name) == LO_NAME_SIZE - 1)
			lofile = loopfile_from_sysfs(device);
		if (!lofile)
			lofile = (char *) loopinfo64.lo_file_name;

		printf("%s: [%04" PRIx64 "]:%" PRIu64 " (%s)",
		       device, loopinfo64.lo_device, loopinfo64.lo_inode,
		       lofile);

		if (lofile != (char *) loopinfo64.lo_file_name)
			free(lofile);

		if (loopinfo64.lo_offset)
			printf(_(", offset %" PRIu64 ), loopinfo64.lo_offset);

		if (loopinfo64.lo_sizelimit)
			printf(_(", sizelimit %" PRIu64 ), loopinfo64.lo_sizelimit);

		if (loopinfo64.lo_encrypt_type ||
		    loopinfo64.lo_crypt_name[0]) {
			char *e = (char *)loopinfo64.lo_crypt_name;

			if (*e == 0 && loopinfo64.lo_encrypt_type == 1)
				e = "XOR";
			printf(_(", encryption %s (type %" PRIu32 ")"),
			       e, loopinfo64.lo_encrypt_type);
		}
		printf("\n");
		return 0;
	}

	errsv = errno;
	fprintf(stderr, _("loop: can't get info on device %s: %s\n"),
		device, strerror (errsv));
	return 1;
}

static int
show_loop(char *device) {
	int ret, fd;

	if ((fd = open(device, O_RDONLY)) < 0) {
		int errsv = errno;
		fprintf(stderr, _("loop: can't open device %s: %s\n"),
			device, strerror (errsv));
		return 2;
	}
	ret = show_loop_fd(fd, device);
	close(fd);
	return ret;
}


static int
show_used_loop_devices (void) {
	struct looplist ll;
	int fd;

	if (looplist_open(&ll, LLFLG_USEDONLY) == -1) {
		warnx(_("/dev directory does not exist."));
		return 1;
	}

	while((fd = looplist_next(&ll)) != -1) {
		show_loop_fd(fd, ll.name);
		close(fd);
	}
	looplist_close(&ll);

	if (!ll.ct_succ && ll.ct_perm) {
		warnx(_("no permission to look at /dev/loop%s<N>"),
				(ll.flag & LLFLG_SUBDIR) ? "/" : "");
		return 1;
	}
	return 0;
}

/* check if the loopfile is already associated with the same given
 * parameters.
 *
 * returns:  0 unused / error
 *           1 loop device already used
 */
static int
is_associated(int dev, struct stat *file, unsigned long long offset, int isoff)
{
	struct loop_info64 linfo64;
	int ret = 0;

	if (ioctl(dev, LOOP_GET_STATUS64, &linfo64) == 0) {
		if (file->st_dev == linfo64.lo_device &&
	            file->st_ino == linfo64.lo_inode &&
		    (isoff == 0 || offset == linfo64.lo_offset))
			ret = 1;

	}

	return ret;
}

/* check if the loop file is already used with the same given
 * parameters. We check for device no, inode and offset.
 * returns: associated devname or NULL
 */
char *
loopfile_used (const char *filename, unsigned long long offset) {
	struct looplist ll;
	char *devname = NULL;
	struct stat filestat;
	int fd;

	if (stat(filename, &filestat) == -1) {
		perror(filename);
		return NULL;
	}

	if (looplist_open(&ll, LLFLG_USEDONLY) == -1) {
		warnx(_("/dev directory does not exist."));
		return NULL;
	}

	while((fd = looplist_next(&ll)) != -1) {
		int res = is_associated(fd, &filestat, offset, 1);
		close(fd);
		if (res == 1) {
			devname = xstrdup(ll.name);
			break;
		}
	}
	looplist_close(&ll);

	return devname;
}

int
loopfile_used_with(char *devname, const char *filename, unsigned long long offset)
{
	struct stat statbuf;
	int fd, ret;

	if (!is_loop_device(devname))
		return 0;

	if (stat(filename, &statbuf) == -1)
		return 0;

	fd = open(devname, O_RDONLY);
	if (fd == -1)
		return 0;

	ret = is_associated(fd, &statbuf, offset, 1);
	close(fd);
	return ret;
}

char *
find_unused_loop_device (void) {
	struct looplist ll;
	char *devname = NULL;
	int fd;

	if (looplist_open(&ll, LLFLG_FREEONLY) == -1) {
		warnx(_("/dev directory does not exist."));
		return NULL;
	}

	if ((fd = looplist_next(&ll)) != -1) {
		close(fd);
		devname = xstrdup(ll.name);
	}
	looplist_close(&ll);
	if (devname)
		return devname;

	if (!ll.ct_succ && ll.ct_perm)
		warnx(_("no permission to look at /dev/loop%s<N>"),
				(ll.flag & LLFLG_SUBDIR) ? "/" : "");
	else if (ll.ct_succ)
		warnx(_("could not find any free loop device"));
	else
		warnx(_(
		    "Could not find any loop device. Maybe this kernel "
		    "does not know\n"
		    "       about the loop device? (If so, recompile or "
		    "`modprobe loop'.)"));
	return NULL;
}

/*
 * A function to read the passphrase either from the terminal or from
 * an open file descriptor.
 */
static char *
xgetpass(int pfd, const char *prompt) {
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

static int
digits_only(const char *s) {
	while (*s)
		if (!isdigit(*s++))
			return 0;
	return 1;
}

/*
 * return codes:
 *	0	- success
 *	1	- error
 *	2	- error (EBUSY)
 */
int
set_loop(const char *device, const char *file, unsigned long long offset,
	 unsigned long long sizelimit, const char *encryption, int pfd, int *options) {
	struct loop_info64 loopinfo64;
	int fd, ffd, mode, i;
	char *pass;
	char *filename;

	if (verbose) {
		char *xdev = loopfile_used(file, offset);

		if (xdev) {
			printf(_("warning: %s is already associated with %s\n"),
					file, xdev);
			free(xdev);
		}
	}

	mode = (*options & SETLOOP_RDONLY) ? O_RDONLY : O_RDWR;
	if ((ffd = open(file, mode)) < 0) {
		if (!(*options & SETLOOP_RDONLY) &&
		    (errno == EROFS || errno == EACCES))
			ffd = open(file, mode = O_RDONLY);
		if (ffd < 0) {
			perror(file);
			return 1;
		}
		if (verbose)
			printf(_("warning: %s: is write-protected, using read-only.\n"),
					file);
		*options |= SETLOOP_RDONLY;
	}
	if ((fd = open(device, mode)) < 0) {
		perror (device);
		close(ffd);
		return 1;
	}
	memset(&loopinfo64, 0, sizeof(loopinfo64));

	if (!(filename = canonicalize_path(file)))
		filename = (char *) file;
	xstrncpy((char *)loopinfo64.lo_file_name, filename, LO_NAME_SIZE);

	if (encryption && *encryption) {
		if (digits_only(encryption)) {
			loopinfo64.lo_encrypt_type = atoi(encryption);
		} else {
			loopinfo64.lo_encrypt_type = LO_CRYPT_CRYPTOAPI;
			snprintf((char *)loopinfo64.lo_crypt_name, LO_NAME_SIZE,
				 "%s", encryption);
		}
	}

	loopinfo64.lo_offset = offset;
	loopinfo64.lo_sizelimit = sizelimit;

#ifdef MCL_FUTURE
	/*
	 * Oh-oh, sensitive data coming up. Better lock into memory to prevent
	 * passwd etc being swapped out and left somewhere on disk.
	 */
	if (loopinfo64.lo_encrypt_type != LO_CRYPT_NONE) {
		if(mlockall(MCL_CURRENT | MCL_FUTURE)) {
			perror("memlock");
			fprintf(stderr, _("Couldn't lock into memory, exiting.\n"));
			exit(1);
		}
	}
#endif

	switch (loopinfo64.lo_encrypt_type) {
	case LO_CRYPT_NONE:
		loopinfo64.lo_encrypt_key_size = 0;
		break;
	case LO_CRYPT_XOR:
		pass = getpass(_("Password: "));
		goto gotpass;
	default:
		pass = xgetpass(pfd, _("Password: "));
	gotpass:
		memset(loopinfo64.lo_encrypt_key, 0, LO_KEY_SIZE);
		xstrncpy((char *)loopinfo64.lo_encrypt_key, pass, LO_KEY_SIZE);
		memset(pass, 0, strlen(pass));
		loopinfo64.lo_encrypt_key_size = LO_KEY_SIZE;
	}

	if (ioctl(fd, LOOP_SET_FD, ffd) < 0) {
		int rc = 1;

		if (errno == EBUSY) {
			if (verbose)
				printf(_("ioctl LOOP_SET_FD failed: %m\n"));
			rc = 2;
		} else
			perror("ioctl: LOOP_SET_FD");

		close(fd);
		close(ffd);
		if (file != filename)
			free(filename);
		return rc;
	}
	close (ffd);

	if (*options & SETLOOP_AUTOCLEAR)
		loopinfo64.lo_flags = LO_FLAGS_AUTOCLEAR;

	i = ioctl(fd, LOOP_SET_STATUS64, &loopinfo64);
	if (i)
		perror("ioctl: LOOP_SET_STATUS64");

	if ((*options & SETLOOP_AUTOCLEAR) && !is_loopfd_autoclear(fd))
		/* kernel doesn't support loop auto-destruction */
		*options &= ~SETLOOP_AUTOCLEAR;

	memset(&loopinfo64, 0, sizeof(loopinfo64));

	if (i) {
		ioctl (fd, LOOP_CLR_FD, 0);
		close (fd);
		if (file != filename)
			free(filename);
		return 1;
	}

	/*
	 * HACK: here we're leaking a file descriptor,
	 * but mount is a short-lived process anyway.
	 */
	if (!(*options & SETLOOP_AUTOCLEAR))
		close (fd);

	if (verbose)
		printf(_("set_loop(%s,%s,%llu,%llu): success\n"),
		       device, filename, offset, sizelimit);
	if (file != filename)
		free(filename);
	return 0;
}

static int
delete_all_devices (void)
{
	struct looplist ll;
	int fd;
	int ok = 0;

	if (looplist_open(&ll, LLFLG_USEDONLY) == -1) {
		warnx(_("/dev directory does not exist."));
		return 1;
	}

	while((fd = looplist_next(&ll)) != -1) {
		close(fd);
		ok |= del_loop(ll.name);
	}
	looplist_close(&ll);

	if (!ll.ct_succ && ll.ct_perm) {
		warnx(_("no permission to look at /dev/loop%s<N>"),
				(ll.flag & LLFLG_SUBDIR) ? "/" : "");
		return 1;
	}
	return ok;
}

static int
del_loop (const char *device) {
	int fd, errsv;

	if ((fd = open (device, O_RDONLY)) < 0) {
		errsv = errno;
		goto error;
	}
	if (ioctl (fd, LOOP_CLR_FD, 0) < 0) {
		errsv = errno;
		goto error;
	}
	close (fd);
	if (verbose)
		printf(_("del_loop(%s): success\n"), device);
	return 0;

error:
	fprintf(stderr, _("loop: can't delete device %s: %s\n"),
		 device, strerror(errsv));
	if (fd >= 0)
		close(fd);
	return 1;
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
		return EXIT_FAILURE;

	if (!file || stat(file, st))
		st = NULL;

	while (loopcxt_next(lc) == 0) {

		if (file && !loopcxt_is_used(lc, st, file, offset, flags))
			continue;

		printf_loopdev(lc);
	}
	return EXIT_SUCCESS;
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
	int act = 0, flags = 0;
	char *file = NULL;
	uint64_t offset = 0;

	char *p, *sizelimit, *encryption, *passfd, *device;
	int delete, delete_all, find, c, capacity;
	int res = 0;
	int showdev = 0;
	int ro = 0;
	int pfd = -1;
	uintmax_t slimit = 0;

	loopcxt_init(&lc, 0);
	/*loopcxt_enable_debug(&lc, TRUE);*/

	static const struct option longopts[] = {
		{ "all", 0, 0, 'a' },
		{ "set-capacity", 0, 0, 'c' },
		{ "detach", 0, 0, 'd' },
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

	capacity = delete = delete_all = find = 0;
	sizelimit = encryption = passfd = NULL;

	while ((c = getopt_long(argc, argv, "acdDe:E:fhj:o:p:rsv",
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
			capacity = 1;
			break;
		case 'r':
			ro = 1;
			break;
		case 'd':
			delete = 1;
			break;
		case 'D':
			delete_all = 1;
			break;
		case 'E':
		case 'e':
			encryption = optarg;
			break;
		case 'f':
			find = 1;
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
			passfd = optarg;
			break;
		case 's':
			showdev = 1;
			break;
		case 'v':
			verbose = 1;
			break;

	        case 128:			/* --sizelimit */
			sizelimit = optarg;
                        break;

		default:
			usage(stderr);
		}
	}

	if (argc == 1)
		usage(stderr);

	switch (act) {
	case A_SHOW:
		res = show_all_loops(&lc, file, offset, flags);
		break;
	default:
		break;
	}

	loopcxt_deinit(&lc);

	if (act)
		return res;

	if (delete) {
		if (argc < optind+1 || encryption || sizelimit ||
		    capacity || find || showdev || ro)
			usage(stderr);
	} else if (delete_all) {
		if (argc > optind || encryption || sizelimit ||
		    capacity || find || showdev || ro)
			usage(stderr);
	} else if (find) {
		if (capacity || argc < optind || argc > optind+1)
			usage(stderr);
	} else if (capacity) {
		if (argc != optind + 1 || encryption || sizelimit ||
		    showdev || ro)
			usage(stderr);
	} else {
		if (argc < optind+1 || argc > optind+2)
			usage(stderr);
	}

	if (sizelimit && strtosize(sizelimit, &slimit)) {
		warnx(_("invalid sizelimit '%s' specified"), sizelimit);
		usage(stderr);
	}

	if (delete_all)
		return delete_all_devices();
	else if (find) {
		device = find_unused_loop_device();
		if (device == NULL)
			return -1;
		if (argc == optind) {
			if (verbose)
				printf(_("Loop device is %s\n"), device);
			printf("%s\n", device);
			return 0;
		}
		file = argv[optind];
	} else if (!delete) {
		device = argv[optind];
		if (argc == optind+1)
			file = NULL;
		else
			file = argv[optind+1];
	}

	if (delete) {
		while (optind < argc)
			res += del_loop(argv[optind++]);
	} else if (capacity) {
		res = set_capacity(device);
	} else if (file == NULL)
		res = show_loop(device);
	else {
		if (passfd && sscanf(passfd, "%d", &pfd) != 1)
			usage(stderr);
		do {
			res = set_loop(device, file, offset, slimit, encryption, pfd, &ro);
			if (res == 2 && find) {
				if (verbose)
					printf(_("stolen loop=%s...trying again\n"),
						device);
				free(device);
				if (!(device = find_unused_loop_device()))
					return -1;
			}
		} while (find && res == 2);

		if (device) {
			if (res == 2)
				warnx(_("%s: device is busy"), device);
			else if (res == 0) {
				if (verbose)
					printf(_("Loop device is %s\n"), device);
				if (showdev && find)
					printf("%s\n", device);
			}
		}
	}
	return res;
}

