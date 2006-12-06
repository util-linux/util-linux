/*
 * losetup.c - setup and control loop devices
 */

/* 1999-02-22 Arkadiusz Mi¶kiewicz <misiek@misiek.eu.org>
 * - added Native Language Support
 */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <unistd.h>
#include <getopt.h>
#include <errno.h>
#include <sys/ioctl.h>

#include "loop.h"
#include "lomount.h"
#include "nls.h"

#ifdef LOOP_SET_FD

static char *progname;

struct crypt_type_struct {
	int id;
	char *name;
} crypt_type_tbl[] = {
	{ LO_CRYPT_NONE,"no" },
	{ LO_CRYPT_NONE,"none" },
	{ LO_CRYPT_XOR,	"xor" },
	{ LO_CRYPT_DES,	"DES" },
	{ -1,		NULL }
};


static char *crypt_name(int id)
{
	int i;

	for (i = 0; crypt_type_tbl[i].id != -1; i++)
		if (id == crypt_type_tbl[i].id)
			return crypt_type_tbl[i].name;
	return "undefined";
}


static int crypt_type(const char *name)
{
	int i;

	for (i = 0; crypt_type_tbl[i].id != -1; i++)
		if (!strcasecmp(name, crypt_type_tbl[i].name))
			return crypt_type_tbl[i].id;
	return -1;
}


static void show_loop(const char *device)
{
	struct	loop_info	loopinfo;
	int			fd;

	if ((fd = open(device, O_RDWR)) < 0) {
		perror(device);
		return;
	}
	if (ioctl(fd, LOOP_GET_STATUS, &loopinfo) < 0) {
		perror(_("Cannot get loop info"));
		close(fd);
		return;
	}
	printf(_("%s: [%04x]:%ld (%s) offset %d, %s encryption\n"),
	       device, loopinfo.lo_device, loopinfo.lo_inode,
	       loopinfo.lo_name, loopinfo.lo_offset,
	       crypt_name(loopinfo.lo_encrypt_type));
	close(fd);
}


int set_loop(const char *device, const char *file, int offset,
	      const char *encryption, int *loopro)
{
	struct loop_info loopinfo;
	int	fd, ffd, mode, i;
	char	*pass;

	mode = *loopro ? O_RDONLY : O_RDWR;
	if ((ffd = open (file, mode)) < 0 && !*loopro
	    && (errno != EROFS || (ffd = open (file, mode = O_RDONLY)) < 0)) {
	  perror (file);
	  return 1;
	}
	if ((fd = open (device, mode)) < 0) {
	  perror (device);
	  return 1;
	}
	*loopro = (mode == O_RDONLY);

	memset(&loopinfo, 0, sizeof(loopinfo));
	strncpy(loopinfo.lo_name, file, LO_NAME_SIZE);
	loopinfo.lo_name[LO_NAME_SIZE-1] = 0;
	if (encryption && (loopinfo.lo_encrypt_type = crypt_type(encryption))
	    < 0) {
		fprintf(stderr,_("Unsupported encryption type %s\n"),
			encryption);
		exit(1);
	}
	loopinfo.lo_offset = offset;
	switch (loopinfo.lo_encrypt_type) {
	case LO_CRYPT_NONE:
		loopinfo.lo_encrypt_key_size = 0;
		break;
	case LO_CRYPT_XOR:
		pass = getpass(_("Password: "));
		strncpy(loopinfo.lo_encrypt_key, pass, LO_KEY_SIZE);
		loopinfo.lo_encrypt_key[LO_KEY_SIZE-1] = 0;
		loopinfo.lo_encrypt_key_size = strlen(loopinfo.lo_encrypt_key);
		break;
	case LO_CRYPT_DES:
		pass = getpass(_("Password: "));
		strncpy(loopinfo.lo_encrypt_key, pass, 8);
		loopinfo.lo_encrypt_key[8] = 0;
		loopinfo.lo_encrypt_key_size = 8;
		pass = getpass(_("Init (up to 16 hex digits): "));
		for (i = 0; i < 16 && pass[i]; i++)
			if (isxdigit(pass[i]))
				loopinfo.lo_init[i >> 3] |= (pass[i] > '9' ?
				    (islower(pass[i]) ? toupper(pass[i]) :
				    pass[i])-'A'+10 : pass[i]-'0') << (i & 7)*4;
			else {
				fprintf(stderr,_("Non-hex digit '%c'.\n"),
					pass[i]);
				exit(1);
			}
		break;
	default:
		fprintf(stderr,
			_("Don't know how to get key for encryption system %d\n"),
			loopinfo.lo_encrypt_type);
		exit(1);
	}
	if (ioctl(fd, LOOP_SET_FD, ffd) < 0) {
		perror("ioctl: LOOP_SET_FD");
		exit(1);
	}
	if (ioctl(fd, LOOP_SET_STATUS, &loopinfo) < 0) {
		(void) ioctl(fd, LOOP_CLR_FD, 0);
		perror("ioctl: LOOP_SET_STATUS");
		exit(1);
	}
	close(fd);
	close(ffd);
	return 0;
}

int del_loop(const char *device)
{
	int fd;

	if ((fd = open(device, O_RDONLY)) < 0) {
		perror(device);
		exit(1);
	}
	if (ioctl(fd, LOOP_CLR_FD, 0) < 0) {
		perror("ioctl: LOOP_CLR_FD");
		exit(1);
	}
	return(0);
}


static int usage(void)
{
	fprintf(stderr, _("usage:\n\
  %s loop_device                                      # give info\n\
  %s -d loop_device                                   # delete\n\
  %s [ -e encryption ] [ -o offset ] loop_device file # setup\n"),
		progname, progname, progname);
	exit(1);
}

int main(int argc, char **argv)
{
	char *offset,*encryption;
	int delete,off,c;
	int res = 0;
	int ro = 0;

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

	delete = off = 0;
	offset = encryption = NULL;
	progname = argv[0];
	while ((c = getopt(argc,argv,"de:o:")) != EOF) {
		switch (c) {
			case 'd':
				delete = 1;
				break;
			case 'e':
				encryption = optarg;
				break;
			case 'o':
				offset = optarg;
				break;
			default:
				usage();
		}
	}
	if (argc == 1) usage();
	if ((delete && (argc != optind+1 || encryption || offset)) ||
	    (!delete && (argc < optind+1 || argc > optind+2)))
		usage();
	if (argc == optind+1) {
		if (delete)
			del_loop(argv[optind]);
		else
			show_loop(argv[optind]);
	} else {
		if (offset && sscanf(offset,"%d",&off) != 1)
			usage();
		res = set_loop(argv[optind],argv[optind+1],off,encryption,&ro);
	}
	return res;
}

#else /* LOOP_SET_FD not defined */

int main(int argc, char **argv) {
  fprintf(stderr,
         _("No loop support was available at compile time. Please recompile.\n"));
  return -1;
}
#endif
