/* Taken from Ted's losetup.c - Mitch <m.dsouza@mrc-apu.cam.ac.uk> */
/* Added vfs mount options - aeb - 960223 */
/* Removed lomount - aeb - 960224 */

/* 1999-02-22 Arkadiusz Mi¶kiewicz <misiek@misiek.eu.org>
 * - added Native Language Support
 * Sun Mar 21 1999 - Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 * - fixed strerr(errno) in gettext calls
 */

#define PROC_DEVICES	"/proc/devices"

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

#include "loop.h"
#include "lomount.h"
#include "nls.h"

char *xstrdup (const char *s);		/* not: #include "sundries.h" */
void error (const char *fmt, ...);	/* idem */

#ifdef LOOP_SET_FD
struct crypt_type_struct {
  int id;
  char *name;
} crypt_type_tbl[] = {
  { LO_CRYPT_NONE, "no" },
  { LO_CRYPT_NONE, "none" },
  { LO_CRYPT_XOR, "xor" },
  { LO_CRYPT_DES, "DES" },
  { -1, NULL   }
};

static int 
crypt_type (const char *name)
{
  int i;

  if (name)
    for (i = 0; crypt_type_tbl[i].id != -1; i++)
      if (!strcasecmp (name, crypt_type_tbl[i].name))
	return crypt_type_tbl[i].id;
  return -1;
}

#if 0
static char *
crypt_name (int id)
{
  int i;

  for (i = 0; crypt_type_tbl[i].id != -1; i++)
    if (id == crypt_type_tbl[i].id)
      return crypt_type_tbl[i].name;
  return "undefined";
}

static void 
show_loop (char *device)
{
  struct loop_info loopinfo;
  int fd;

  if ((fd = open (device, O_RDONLY)) < 0) {
    int errsv = errno;
    fprintf(stderr, _("loop: can't open device %s: %s\n"),
	    device, strerror (errsv));
    return;
  }
  if (ioctl (fd, LOOP_GET_STATUS, &loopinfo) < 0) {
    int errsv = errno;
    fprintf(stderr, _("loop: can't get info on device %s: %s\n"),
	    device, strerror (errsv));
    close (fd);
    return;
  }
  printf (_("%s: [%04x]:%ld (%s) offset %d, %s encryption\n"),
	  device, loopinfo.lo_device, loopinfo.lo_inode,
	  loopinfo.lo_name, loopinfo.lo_offset,
	  crypt_name (loopinfo.lo_encrypt_type));
  close (fd);
}
#endif

char *
find_unused_loop_device (void)
{
    /* Just creating a device, say in /tmp, is probably a bad idea -
       people might have problems with backup or so.
       So, we just try /dev/loop[0-7]. */
    char dev[20];
    int i, fd, somedev = 0, someloop = 0, loop_known = 0;
    struct stat statbuf;
    struct loop_info loopinfo;
    FILE *procdev;

    for(i = 0; i < 256; i++) {
      sprintf(dev, "/dev/loop%d", i);
      if (stat (dev, &statbuf) == 0 && S_ISBLK(statbuf.st_mode)) {
	somedev++;
	fd = open (dev, O_RDONLY);
	if (fd >= 0) {
	  if(ioctl (fd, LOOP_GET_STATUS, &loopinfo) == 0)
	    someloop++;			/* in use */
	  else if (errno == ENXIO) {
	    close (fd);
	    return xstrdup(dev);	/* probably free */
	  }
	  close (fd);
        }
	continue;		/* continue trying as long as devices exist */
      }
      if (i >= 7)
	break;
    }

    /* Nothing found. Why not? */
    if ((procdev = fopen(PROC_DEVICES, "r")) != NULL) {
      char line[100];
      while (fgets (line, sizeof(line), procdev))
	if (strstr (line, " loop\n")) {
	  loop_known = 1;
	  break;
	}
      fclose(procdev);
      if (!loop_known)
	loop_known = -1;
    }

    if (!somedev)
      error(_("mount: could not find any device /dev/loop#"));
    else if(!someloop) {
      if (loop_known == 1)
	error(_(
"mount: Could not find any loop device.\n"
"       Maybe /dev/loop# has a wrong major number?"));
      else if (loop_known == -1)
	error(_(
"mount: Could not find any loop device, and, according to %s,\n"
"       this kernel does not know about the loop device.\n"
"       (If so, then recompile or `insmod loop.o'.)"), PROC_DEVICES);
      else
	error(_(
"mount: Could not find any loop device. Maybe this kernel does not know\n"
"       about the loop device (then recompile or `insmod loop.o'), or\n"
"       maybe /dev/loop# has the wrong major number?"));
    } else
      error(_("mount: could not find any free loop device"));
    return 0;
}

int
set_loop (const char *device, const char *file, int offset,
	  const char *encryption, int *loopro)
{
  struct loop_info loopinfo;
  int fd, ffd, mode, i;
  char *pass;

  mode = (*loopro ? O_RDONLY : O_RDWR);
  if ((ffd = open (file, mode)) < 0) {
       if (!*loopro && errno == EROFS)
	    ffd = open (file, mode = O_RDONLY);
       if (ffd < 0) {
	    perror (file);
	    return 1;
       }
  }
  if ((fd = open (device, mode)) < 0) {
    perror (device);
    return 1;
  }
  *loopro = (mode == O_RDONLY);
  memset (&loopinfo, 0, sizeof (loopinfo));
  strncpy (loopinfo.lo_name, file, LO_NAME_SIZE);
  loopinfo.lo_name[LO_NAME_SIZE - 1] = 0;
  if (encryption && (loopinfo.lo_encrypt_type = crypt_type (encryption))
      < 0) {
    fprintf (stderr, _("Unsupported encryption type %s\n"), encryption);
    return 1;
  }
  loopinfo.lo_offset = offset;
  switch (loopinfo.lo_encrypt_type) {
  case LO_CRYPT_NONE:
    loopinfo.lo_encrypt_key_size = 0;
    break;
  case LO_CRYPT_XOR:
    pass = getpass (_("Password: "));
    strncpy (loopinfo.lo_encrypt_key, pass, LO_KEY_SIZE);
    loopinfo.lo_encrypt_key[LO_KEY_SIZE - 1] = 0;
    loopinfo.lo_encrypt_key_size = strlen (loopinfo.lo_encrypt_key);
    break;
  case LO_CRYPT_DES:
    pass = getpass (_("Password: "));
    strncpy (loopinfo.lo_encrypt_key, pass, 8);
    loopinfo.lo_encrypt_key[8] = 0;
    loopinfo.lo_encrypt_key_size = 8;
    pass = getpass (_("Init (up to 16 hex digits): "));
    for (i = 0; i < 16 && pass[i]; i++)
      if (isxdigit (pass[i]))
	loopinfo.lo_init[i >> 3] |= (pass[i] > '9' ?
				     (islower (pass[i]) ? toupper (pass[i]) :
			pass[i]) - 'A' + 10 : pass[i] - '0') << (i & 7) * 4;
      else {
	fprintf (stderr, _("Non-hex digit '%c'.\n"), pass[i]);
	return 1;
      }
    break;
  default:
    fprintf (stderr,
	     _("Don't know how to get key for encryption system %d\n"),
	     loopinfo.lo_encrypt_type);
    return 1;
  }
  if (ioctl (fd, LOOP_SET_FD, ffd) < 0) {
    perror ("ioctl: LOOP_SET_FD");
    return 1;
  }
  if (ioctl (fd, LOOP_SET_STATUS, &loopinfo) < 0) {
    (void) ioctl (fd, LOOP_CLR_FD, 0);
    perror ("ioctl: LOOP_SET_STATUS");
    return 1;
  }
  close (fd);
  close (ffd);
  if (verbose > 1)
    printf(_("set_loop(%s,%s,%d): success\n"), device, file, offset);
  return 0;
}

int 
del_loop (const char *device)
{
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
	  _("This mount was compiled without loop support. Please recompile.\n"));
}  

int
set_loop (const char *device, const char *file, int offset,
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
