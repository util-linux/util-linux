/*
 * mesg.c	The "mesg" utility. Gives / restrict access to
 *		your terminal by others.
 *
 * Usage:	mesg [y|n].
 *		Without arguments prints out the current settings.
 */
#include <stdio.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>

char *Version = "@(#) mesg 1.0 08-12-92 MvS";

int main(int argc, char **argv)
{
  struct stat st;

  if (!isatty(0)) {
	/* Or should we look in /etc/utmp? */
	fprintf(stderr, "stdin: is not a tty");
	return(1);
  }

  if (fstat(0, &st) < 0) {
	perror("fstat");
	return(1);
  }
  if (argc < 2) {
	printf("Is %s\n", ((st.st_mode & 022) == 022) ? "y" : "n");
	return(0);
  }
  if (argc > 2 || (argv[1][0] != 'y' && argv[1][0] != 'n')) {
	fprintf(stderr, "Usage: mesg [y|n]\n");
	return(1);
  }
  if (argv[1][0] == 'y')
	st.st_mode |= 022;
  else
	st.st_mode &= ~022;
  fchmod(0, st.st_mode);
  return(0);
}
