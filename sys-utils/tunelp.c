/****************************************************************************\
*	Copyright (C) 1992-1997 Michael K. Johnson, johnsonm@redhat.com      *
*									     *
*	This file is licensed under the terms of the GNU General             *
*	Public License, version 2, or any later version.  See file COPYING   *
*	for information on distribution conditions.			     *
\****************************************************************************/

/* $Id: tunelp.c,v 1.8 1997/07/06 00:14:06 aebr Exp $
 * $Log: tunelp.c,v $
 * Revision 1.8  1997/07/06 00:14:06  aebr
 * Fixes to silence -Wall.
 *
 * Revision 1.7  1997/06/20 16:10:38  janl
 * tunelp refreshed from authors archive.
 *
 * Revision 1.9  1997/06/20 12:56:43  johnsonm
 * Finished fixing license terms.
 *
 * Revision 1.8  1997/06/20 12:34:59  johnsonm
 * Fixed copyright and license.
 *
 * Revision 1.7  1995/03/29 11:16:23  johnsonm
 * TYPO fixed...
 *
 * Revision 1.6  1995/03/29  11:12:15  johnsonm
 * Added third argument to ioctl needed with new kernels
 *
 * Revision 1.5  1995/01/13  10:33:43  johnsonm
 * Chris's changes for new ioctl numbers and backwards compatibility
 * and the reset ioctl.
 *
 * Revision 1.4  1995/01/03  17:42:14  johnsonm
 * -s isn't supposed to take an argument; removed : after s in getopt...
 *
 * Revision 1.3  1995/01/03  07:36:49  johnsonm
 * Fixed typo
 *
 * Revision 1.2  1995/01/03  07:33:44  johnsonm
 * revisions for lp driver updates in Linux 1.1.76
 *
 *
 */

#include<unistd.h>
#include<stdio.h>
#include<fcntl.h>
#include<linux/lp.h>
#include<linux/fs.h>
#include<sys/ioctl.h>
#include<sys/stat.h>
#include<sys/types.h>
#include<malloc.h>
#include<string.h>
#include<errno.h>

struct command {
  long op;
  long val;
  struct command *next;
};




void print_usage(char *progname) {
  printf("Usage: %s <device> [ -i <IRQ> | -t <TIME> | -c <CHARS> | -w <WAIT> | \n"
         "          -a [on|off] | -o [on|off] | -C [on|off] | -q [on|off] | -s ]\n", progname);
  exit (1);
}





void *mylloc(long size) {
  void *ptr;
  if(!(ptr = (void*)malloc(size))) {
    perror("malloc error");
    exit(2);
  }
  return ptr;
}



long get_val(char *val) {
  long ret;
  if (!(sscanf(val, "%ld", &ret) == 1)) {
    perror("sscanf error");
    exit(3);
  }
  return ret;
}


long get_onoff(char *val) {
  if (!strncasecmp("on", val, 2))
    return 1;
  return 0;
}



int main (int argc, char ** argv) {
  int c, fd, irq, status, show_irq, offset = 0, retval;
  char *progname;
  char *filename;
  struct stat statbuf;
  struct command *cmds, *cmdst;


  progname = argv[0];
  if (argc < 2) print_usage(progname);

  filename = strdup(argv[1]);
  fd = open(filename, O_WRONLY|O_NONBLOCK, 0);
  /* Need to open O_NONBLOCK in case ABORTOPEN is already set and
     printer is off or off-line or in an error condition.  Otherwise
     we would abort... */
  if (fd < 0) {
    perror(argv[1]);
    return -1;
  }

  fstat(fd, &statbuf);

  if((!S_ISCHR(statbuf.st_mode)) || (MAJOR(statbuf.st_rdev) != 6 )
     || (MINOR(statbuf.st_rdev) > 3)) {
    printf("%s: %s not an lp device.\n", argv[0], argv[1]);
    print_usage(progname);
  }

  cmdst = cmds = mylloc(sizeof(struct command));
  cmds->next = 0;

  show_irq = 1;
  while ((c = getopt(argc, argv, "t:c:w:a:i:ho:C:sq:r")) != EOF) {
    switch (c) {
    case 'h':
      print_usage(progname);
      break;
    case 'i':
      cmds->op = LPSETIRQ;
      cmds->val = get_val(optarg);
      cmds->next = mylloc(sizeof(struct command));
      cmds = cmds->next; cmds->next = 0;
      break;
    case 't':
      cmds->op = LPTIME;
      cmds->val = get_val(optarg);
      cmds->next = mylloc(sizeof(struct command));
      cmds = cmds->next; cmds->next = 0;
      break;
    case 'c':
      cmds->op = LPCHAR;
      cmds->val = get_val(optarg);
      cmds->next = mylloc(sizeof(struct command));
      cmds = cmds->next; cmds->next = 0;
      break;
    case 'w':
      cmds->op = LPWAIT;
      cmds->val = get_val(optarg);
      cmds->next = mylloc(sizeof(struct command));
      cmds = cmds->next; cmds->next = 0;
      break;
    case 'a':
      cmds->op = LPABORT;
      cmds->val = get_onoff(optarg);
      cmds->next = mylloc(sizeof(struct command));
      cmds = cmds->next; cmds->next = 0;
      break;
    case 'q':
      if (get_onoff(optarg)) {
        show_irq=1;
      } else {
        show_irq=0;
      }
#ifdef LPGETSTATUS
    case 'o':
      cmds->op = LPABORTOPEN;
      cmds->val = get_onoff(optarg);
      cmds->next = mylloc(sizeof(struct command));
      cmds = cmds->next; cmds->next = 0;
      break;
    case 'C':
      cmds->op = LPCAREFUL;
      cmds->val = get_onoff(optarg);
      cmds->next = mylloc(sizeof(struct command));
      cmds = cmds->next; cmds->next = 0;
      break;
    case 's':
      show_irq = 0;
      cmds->op = LPGETSTATUS;
      cmds->val = 0;
      cmds->next = mylloc(sizeof(struct command));
      cmds = cmds->next; cmds->next = 0;
      break;
#endif
#ifdef LPRESET
    case 'r':
      cmds->op = LPRESET;
      cmds->val = 0;
      cmds->next = mylloc(sizeof(struct command));
      cmds = cmds->next; cmds->next = 0;
      break;
#endif
    default: print_usage(progname);
    }
  }

  /* Allow for binaries compiled under a new kernel to work on the old ones */
  /* The irq argument to ioctl isn't touched by the old kernels, but we don't */
  /*  want to cause the kernel to complain if we are using a new kernel */
  if (LPGETIRQ >= 0x0600 && ioctl(fd, LPGETIRQ, &irq) < 0 && errno == EINVAL)
    offset = 0x0600;	/* We don't understand the new ioctls */

  cmds = cmdst;
  while (cmds->next) {
#ifdef LPGETSTATUS
    if (cmds->op == LPGETSTATUS) {
      status = 0xdeadbeef;
      retval = ioctl(fd, LPGETSTATUS - offset, &status);
      if (retval < 0)
      	perror("LPGETSTATUS error");
      else {
        if (status == 0xdeadbeef)	/* a few 1.1.7x kernels will do this */
          status = retval;
	printf("%s status is %d", filename, status);
	if (!(status & LP_PBUSY)) printf(", busy");
	if (!(status & LP_PACK)) printf(", ready");
	if ((status & LP_POUTPA)) printf(", out of paper");
	if ((status & LP_PSELECD)) printf(", on-line");
	if (!(status & LP_PERRORP)) printf(", error");
	printf("\n");
      }
    } else
#endif /* LPGETSTATUS */
    if (ioctl(fd, cmds->op - offset, cmds->val) < 0) {
      perror("tunelp: ioctl");
    }
    cmdst = cmds;
    cmds = cmds->next;
    free(cmdst);
  }

  if (show_irq) {
    irq = 0xdeadbeef;
    retval = ioctl(fd, LPGETIRQ - offset, &irq);
    if (retval == -1) {
      perror("LPGETIRQ error");
      exit(4);
    }
    if (irq == 0xdeadbeef)		/* up to 1.1.77 will do this */
      irq = retval;
    if (irq)
      printf("%s using IRQ %d\n", filename, irq);
    else
      printf("%s using polling\n", filename);
  }

  close(fd);

  return 0;
}
