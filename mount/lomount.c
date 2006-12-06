/* Taken from Ted's losetup.c - Mitch <m.dsouza@mrc-apu.cam.ac.uk> */

/*
 * losetup.c - setup and control loop devices
 */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/fs.h>
#include "loop.h"

char *crypt_name (int);
int crypt_type (char *);
void show_loop (char *);
int del_loop (const char *);
int set_loop (const char *, const char *, int offset, char *);
int lomount (const char *, const char *, const char *, char **,
	     int *, char **, char **);

struct crypt_type_struct {
  int id;
  char *name;
} crypt_type_tbl[] = {

  {
    LO_CRYPT_NONE, "no"
  },
  {
    LO_CRYPT_NONE, "none"
  },
  {
    LO_CRYPT_XOR, "xor"
  },
  {
    LO_CRYPT_DES, "DES"
  },
  {
    -1, NULL
  }
};

char *
crypt_name (int id)
{
  int i;

  for (i = 0; crypt_type_tbl[i].id != -1; i++)
    if (id == crypt_type_tbl[i].id)
      return crypt_type_tbl[i].name;
  return "undefined";
}

int 
crypt_type (char *name)
{
  int i;

  for (i = 0; crypt_type_tbl[i].id != -1; i++)
    if (!strcasecmp (name, crypt_type_tbl[i].name))
      return crypt_type_tbl[i].id;
  return -1;
}

void 
show_loop (char *device)
{
  struct loop_info loopinfo;
  int fd;

  if ((fd = open (device, O_RDWR)) < 0) {
    fprintf(stderr, "loop: can't open device %s: %s\n",
	    device, strerror (errno));
    return;
  }
  if (ioctl (fd, LOOP_GET_STATUS, &loopinfo) < 0) {
    fprintf(stderr, "loop: can't get info on device %s: %s\n",
	    device, strerror (errno));
    close (fd);
    return;
  }
  printf ("%s: [%04x]:%ld (%s) offset %d, %s encryption\n",
	  device, loopinfo.lo_device, loopinfo.lo_inode,
	  loopinfo.lo_name, loopinfo.lo_offset,
	  crypt_name (loopinfo.lo_encrypt_type));
  close (fd);
}

int
set_loop (const char *device, const char *file, int offset, char *encryption)
{
  struct loop_info loopinfo;
  int fd,
    ffd,
    i;
  char *pass;

  if ((fd = open (device, O_RDWR)) < 0) {
    perror (device);
    return 1;
  }
  if ((ffd = open (file, O_RDWR)) < 0) {
    perror (file);
    return 1;
  }
  memset (&loopinfo, 0, sizeof (loopinfo));
  strncpy (loopinfo.lo_name, file, LO_NAME_SIZE);
  loopinfo.lo_name[LO_NAME_SIZE - 1] = 0;
  if (encryption && (loopinfo.lo_encrypt_type = crypt_type (encryption))
      < 0) {
    fprintf (stderr, "Unsupported encryption type %s", encryption);
    return 1;
  }
  loopinfo.lo_offset = offset;
  switch (loopinfo.lo_encrypt_type) {
  case LO_CRYPT_NONE:
    loopinfo.lo_encrypt_key_size = 0;
    break;
  case LO_CRYPT_XOR:
    pass = getpass ("Password: ");
    strncpy (loopinfo.lo_encrypt_key, pass, LO_KEY_SIZE);
    loopinfo.lo_encrypt_key[LO_KEY_SIZE - 1] = 0;
    loopinfo.lo_encrypt_key_size = strlen (loopinfo.lo_encrypt_key);
    break;
  case LO_CRYPT_DES:
    pass = getpass ("Password: ");
    strncpy (loopinfo.lo_encrypt_key, pass, 8);
    loopinfo.lo_encrypt_key[8] = 0;
    loopinfo.lo_encrypt_key_size = 8;
    pass = getpass ("Init (up to 16 hex digits): ");
    for (i = 0; i < 16 && pass[i]; i++)
      if (isxdigit (pass[i]))
	loopinfo.lo_init[i >> 3] |= (pass[i] > '9' ?
				     (islower (pass[i]) ? toupper (pass[i]) :
			pass[i]) - 'A' + 10 : pass[i] - '0') << (i & 7) * 4;
      else {
	fprintf (stderr, "Non-hex digit '%c'.\n", pass[i]);
	return 1;
      }
    break;
  default:
    fprintf (stderr,
	     "Don't know how to get key for encryption system %d\n",
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
  return 0;
}

int 
del_loop (const char *device)
{
  int fd;

  if ((fd = open (device, O_RDONLY)) < 0) {
    fprintf(stderr, "loop: can't delete device %s: %s\n",
	    device, strerror (errno));
    return 1;
  }
  if (ioctl (fd, LOOP_CLR_FD, 0) < 0) {
#if 0
    perror ("ioctl: LOOP_CLR_FD");
#endif
    return 1;
  }
  return 0;
}


int 
lomount (const char *spec, const char *node, const char *device, char **type,
	 int *flags, char **extra_opts, char **mount_opts)
{
  char *opt,
   *opteq;
  int val;
  char *encryption = NULL, *vfs = NULL;
  int offset = 0, err;
  char new_opts[1024];

  for (opt = strtok (*extra_opts, ","); opt; opt = strtok (NULL, ",")) {
    if ((opteq = strchr (opt, '='))) {
      val = atoi (opteq + 1);
      *opteq = '\0';
      if (!strcmp (opt, "encryption"))
	encryption = strdup(opteq + 1);
      else if (!strcmp (opt, "vfs"))
	vfs = strdup(opteq + 1);
      else if (!strcmp (opt, "offset"))
	offset = val;
      else {
	printf ("unknown loop mount parameter: "
		"%s=%d (%s)\n", opt, val, opteq+1);
	return 1;
      }
    } else {
      printf ("unknown loop mount parameter: "
	      "%s\n", opt);
      return 1;
    }
  }
  err = set_loop (device, spec, offset, encryption);
  sprintf(new_opts, "vfs=%s,offset=%d,encryption=%s",
	  *type = vfs ? vfs : FSTYPE_DEFAULT, offset,
	  encryption=crypt_type(encryption)<0?"none":encryption);
  *extra_opts=strdup(new_opts);
  return err;
}
