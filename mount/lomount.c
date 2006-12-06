/* Originally from Ted's losetup.c */

#define LOOPMAJOR	7

/*
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

#include "loop.h"
#include "lomount.h"
#include "xstrncpy.h"
#include "nls.h"

extern int verbose;
extern char *progname;
extern char *xstrdup (const char *s);	/* not: #include "sundries.h" */
extern void error (const char *fmt, ...);	/* idem */

#ifdef LOOP_SET_FD

static int
loop_info64_to_old(const struct loop_info64 *info64, struct loop_info *info)
{
        memset(info, 0, sizeof(*info));
        info->lo_number = info64->lo_number;
        info->lo_device = info64->lo_device;
        info->lo_inode = info64->lo_inode;
        info->lo_rdevice = info64->lo_rdevice;
        info->lo_offset = info64->lo_offset;
        info->lo_encrypt_type = info64->lo_encrypt_type;
        info->lo_encrypt_key_size = info64->lo_encrypt_key_size;
        info->lo_flags = info64->lo_flags;
        info->lo_init[0] = info64->lo_init[0];
        info->lo_init[1] = info64->lo_init[1];
        if (info->lo_encrypt_type == LO_CRYPT_CRYPTOAPI)
                memcpy(info->lo_name, info64->lo_crypt_name, LO_NAME_SIZE);
        else
                memcpy(info->lo_name, info64->lo_file_name, LO_NAME_SIZE);
        memcpy(info->lo_encrypt_key, info64->lo_encrypt_key, LO_KEY_SIZE);

        /* error in case values were truncated */
        if (info->lo_device != info64->lo_device ||
            info->lo_rdevice != info64->lo_rdevice ||
            info->lo_inode != info64->lo_inode ||
            info->lo_offset != info64->lo_offset)
                return -EOVERFLOW;

        return 0;
}

#ifdef MAIN

static int
show_loop(char *device) {
	struct loop_info loopinfo;
	struct loop_info64 loopinfo64;
	int fd, errsv;

	if ((fd = open(device, O_RDONLY)) < 0) {
		int errsv = errno;
		fprintf(stderr, _("loop: can't open device %s: %s\n"),
			device, strerror (errsv));
		return 2;
	}

	if (ioctl(fd, LOOP_GET_STATUS64, &loopinfo64) == 0) {

		loopinfo64.lo_file_name[LO_NAME_SIZE-2] = '*';
		loopinfo64.lo_file_name[LO_NAME_SIZE-1] = 0;
		loopinfo64.lo_crypt_name[LO_NAME_SIZE-1] = 0;

		printf("%s: [%04llx]:%llu (%s)",
		       device, loopinfo64.lo_device, loopinfo64.lo_inode,
		       loopinfo64.lo_file_name);

		if (loopinfo64.lo_offset)
			printf(_(", offset %lld"), loopinfo64.lo_offset);

		if (loopinfo64.lo_sizelimit)
			printf(_(", sizelimit %lld"), loopinfo64.lo_sizelimit);

		if (loopinfo64.lo_encrypt_type ||
		    loopinfo64.lo_crypt_name[0]) {
			char *e = loopinfo64.lo_crypt_name;

			if (*e == 0 && loopinfo64.lo_encrypt_type == 1)
				e = "XOR";
			printf(_(", encryption %s (type %d)"),
			       e, loopinfo64.lo_encrypt_type);
		}
		printf("\n");
		close (fd);
		return 0;
	}

	if (ioctl(fd, LOOP_GET_STATUS, &loopinfo) == 0) {
		printf ("%s: [%04x]:%ld (%s)",
			device, loopinfo.lo_device, loopinfo.lo_inode,
			loopinfo.lo_name);

		if (loopinfo.lo_offset)
			printf(_(", offset %d"), loopinfo.lo_offset);

		if (loopinfo.lo_encrypt_type)
			printf(_(", encryption type %d\n"),
			       loopinfo.lo_encrypt_type);

		printf("\n");
		close (fd);
		return 0;
	}

	errsv = errno;
	fprintf(stderr, _("loop: can't get info on device %s: %s\n"),
		device, strerror (errsv));
	close (fd);
	return 1;
}
#endif

int
is_loop_device (const char *device) {
	struct stat statbuf;

	return (stat(device, &statbuf) == 0 &&
		S_ISBLK(statbuf.st_mode) &&
		major(statbuf.st_rdev) == LOOPMAJOR);
}

#define SIZE(a) (sizeof(a)/sizeof(a[0]))

char *
find_unused_loop_device (void) {
	/* Just creating a device, say in /tmp, is probably a bad idea -
	   people might have problems with backup or so.
	   So, we just try /dev/loop[0-7]. */
	char dev[20];
	char *loop_formats[] = { "/dev/loop%d", "/dev/loop/%d" };
	int i, j, fd, somedev = 0, someloop = 0, permission = 0;
	struct stat statbuf;
	struct loop_info loopinfo;

	for (j = 0; j < SIZE(loop_formats); j++) {
	    for(i = 0; i < 256; i++) {
		sprintf(dev, loop_formats[j], i);
		if (stat (dev, &statbuf) == 0 && S_ISBLK(statbuf.st_mode)) {
			somedev++;
			fd = open (dev, O_RDONLY);
			if (fd >= 0) {
				if(ioctl (fd, LOOP_GET_STATUS, &loopinfo) == 0)
					someloop++;		/* in use */
				else if (errno == ENXIO) {
					close (fd);
					return xstrdup(dev);/* probably free */
				}
				close (fd);
			} else if (errno == EACCES)
				permission++;

			continue;/* continue trying as long as devices exist */
		}
		break;
	    }
	}

	if (!somedev)
		error(_("%s: could not find any device /dev/loop#"), progname);
	else if (!someloop && permission)
		error(_("%s: no permission to look at /dev/loop#"), progname);
	else if (!someloop)
		error(_(
		    "%s: Could not find any loop device. Maybe this kernel "
		    "does not know\n"
		    "       about the loop device? (If so, recompile or "
		    "`modprobe loop'.)"), progname);
	else
		error(_("%s: could not find any free loop device"), progname);
	return 0;
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
				error("Out of memory while reading passphrase");
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

int
set_loop(const char *device, const char *file, unsigned long long offset,
	 const char *encryption, int pfd, int *loopro) {
	struct loop_info64 loopinfo64;
	int fd, ffd, mode, i;
	char *pass;

	mode = (*loopro ? O_RDONLY : O_RDWR);
	if ((ffd = open(file, mode)) < 0) {
		if (!*loopro && errno == EROFS)
			ffd = open(file, mode = O_RDONLY);
		if (ffd < 0) {
			perror(file);
			return 1;
		}
	}
	if ((fd = open(device, mode)) < 0) {
		perror (device);
		return 1;
	}
	*loopro = (mode == O_RDONLY);

	memset(&loopinfo64, 0, sizeof(loopinfo64));

	xstrncpy(loopinfo64.lo_file_name, file, LO_NAME_SIZE);

	if (encryption && *encryption) {
		if (digits_only(encryption)) {
			loopinfo64.lo_encrypt_type = atoi(encryption);
		} else {
			loopinfo64.lo_encrypt_type = LO_CRYPT_CRYPTOAPI;
			snprintf(loopinfo64.lo_crypt_name, LO_NAME_SIZE,
				 "%s", encryption);
		}
	}

	loopinfo64.lo_offset = offset;

#ifdef MCL_FUTURE  
	/*
	 * Oh-oh, sensitive data coming up. Better lock into memory to prevent
	 * passwd etc being swapped out and left somewhere on disk.
	 */
                                                
	if(mlockall(MCL_CURRENT | MCL_FUTURE)) {
		perror("memlock");
		fprintf(stderr, _("Couldn't lock into memory, exiting.\n"));
		exit(1);
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
		xstrncpy(loopinfo64.lo_encrypt_key, pass, LO_KEY_SIZE);
		memset(pass, 0, strlen(pass));
		loopinfo64.lo_encrypt_key_size = LO_KEY_SIZE;
	}

	if (ioctl(fd, LOOP_SET_FD, ffd) < 0) {
		perror("ioctl: LOOP_SET_FD");
		return 1;
	}
	close (ffd);

	i = ioctl(fd, LOOP_SET_STATUS64, &loopinfo64);
	if (i) {
		struct loop_info loopinfo;
		int errsv = errno;

		i = loop_info64_to_old(&loopinfo64, &loopinfo);
		if (i) {
			errno = errsv;
			perror("ioctl: LOOP_SET_STATUS64");
		} else {
			i = ioctl(fd, LOOP_SET_STATUS, &loopinfo);
			if (i)
				perror("ioctl: LOOP_SET_STATUS");
		}
		memset(&loopinfo, 0, sizeof(loopinfo));
	}
	memset(&loopinfo64, 0, sizeof(loopinfo64));

	if (i) {
		ioctl (fd, LOOP_CLR_FD, 0);
		close (fd);
		return 1;
	}
	close (fd);

	if (verbose > 1)
		printf(_("set_loop(%s,%s,%llu): success\n"),
		       device, file, offset);
	return 0;
}

int 
del_loop (const char *device) {
	int fd;

	if ((fd = open (device, O_RDONLY)) < 0) {
		int errsv = errno;
		fprintf(stderr, _("loop: can't delete device %s: %s\n"),
			device, strerror (errsv));
		return 1;
	}
	if (ioctl (fd, LOOP_CLR_FD, 0) < 0) {
		perror ("ioctl: LOOP_CLR_FD");
		return 1;
	}
	close (fd);
	if (verbose > 1)
		printf(_("del_loop(%s): success\n"), device);
	return 0;
}

#else /* no LOOP_SET_FD defined */
static void
mutter(void) {
	fprintf(stderr,
		_("This mount was compiled without loop support. "
		  "Please recompile.\n"));
}  

int
set_loop (const char *device, const char *file, unsigned long long offset,
	  const char *encryption, int *loopro) {
	mutter();
	return 1;
}

int
del_loop (const char *device) {
	mutter();
	return 1;
}

char *
find_unused_loop_device (void) {
	mutter();
	return 0;
}

#endif

#ifdef MAIN

#ifdef LOOP_SET_FD

#include <getopt.h>
#include <stdarg.h>

int verbose = 0;
char *progname;

static void
usage(void) {
	fprintf(stderr, _("usage:\n\
  %s loop_device                                       # give info\n\
  %s -d loop_device                                    # delete\n\
  %s -f                                                # find unused\n\
  %s [-e encryption] [-o offset] {-f|loop_device} file # setup\n"),
		progname, progname, progname, progname);
	exit(1);
}

char *
xstrdup (const char *s) {
	char *t;

	if (s == NULL)
		return NULL;

	t = strdup (s);

	if (t == NULL) {
		fprintf(stderr, _("not enough memory"));
		exit(1);
	}

	return t;
}

void
error (const char *fmt, ...) {
	va_list args;

	va_start (args, fmt);
	vfprintf (stderr, fmt, args);
	va_end (args);
	fprintf (stderr, "\n");
}

int
main(int argc, char **argv) {
	char *p, *offset, *encryption, *passfd, *device, *file;
	int delete, find, c;
	int res = 0;
	int ro = 0;
	int pfd = -1;
	unsigned long long off;

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

	delete = find = 0;
	off = 0;
	offset = encryption = passfd = NULL;

	progname = argv[0];
	if ((p = strrchr(progname, '/')) != NULL)
		progname = p+1;

	while ((c = getopt(argc, argv, "de:E:fo:p:v")) != -1) {
		switch (c) {
		case 'd':
			delete = 1;
			break;
		case 'E':
		case 'e':
			encryption = optarg;
			break;
		case 'f':
			find = 1;
			break;
		case 'o':
			offset = optarg;
			break;
		case 'p':
			passfd = optarg;
			break;
		case 'v':
			verbose = 1;
			break;
		default:
			usage();
		}
	}

	if (argc == 1) {
		usage();
	} else if (delete) {
		if (argc != optind+1 || encryption || offset || find)
			usage();
	} else if (find) {
		if (argc < optind || argc > optind+1)
			usage();
	} else {
		if (argc < optind+1 || argc > optind+2)
			usage();
	}

	if (find) {
		device = find_unused_loop_device();
		if (device == NULL)
			return -1;
		if (verbose)
			printf("Loop device is %s\n", device);
		if (argc == optind) {
			printf("%s\n", device);
			return 0;
		}
		file = argv[optind];
	} else {
		device = argv[optind];
		if (argc == optind+1)
			file = NULL;
		else
			file = argv[optind+1];
	}

	if (delete)
		res = del_loop(device);
	else if (file == NULL)
		res = show_loop(device);
	else {
		if (offset && sscanf(offset, "%llu", &off) != 1)
			usage();
		if (passfd && sscanf(passfd, "%d", &pfd) != 1)
			usage();
		res = set_loop(device, file, off, encryption, pfd, &ro);
	}
	return res;
}

#else /* LOOP_SET_FD not defined */

int
main(int argc, char **argv) {
	fprintf(stderr,
		_("No loop support was available at compile time. "
		  "Please recompile.\n"));
	return -1;
}
#endif
#endif
