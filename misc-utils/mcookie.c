/* mcookie.c -- Generates random numbers for xauth
 * Created: Fri Feb  3 10:42:48 1995 by faith@cs.unc.edu
 * Revised: Fri Mar 19 07:48:01 1999 by faith@acm.org
 * Public Domain 1995, 1999 Rickard E. Faith (faith@acm.org)
 * This program comes with ABSOLUTELY NO WARRANTY.
 * 
 * $Id: mcookie.c,v 1.5 1997/07/06 00:13:06 aebr Exp $
 *
 * This program gathers some random bits of data and used the MD5
 * message-digest algorithm to generate a 128-bit hexadecimal number for
 * use with xauth(1).
 *
 * NOTE: Unless /dev/random is available, this program does not actually
 * gather 128 bits of random information, so the magic cookie generated
 * will be considerably easier to guess than one might expect.
 *
 * 1999-02-22 Arkadiusz Mi¶kiewicz <misiek@pld.ORG.PL>
 * - added Native Language Support
 * 1999-03-21 aeb: Added some fragments of code from Colin Plumb.
 *
 */

#ifdef __linux__
#define HAVE_GETTIMEOFDAY 1
#endif

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include "md5.h"
#ifdef HAVE_GETTIMEOFDAY
#include <sys/time.h>
#include <unistd.h>
#endif
#include "nls.h"

#define BUFFERSIZE 4096

struct rngs {
   const char *path;
   int minlength, maxlength;
} rngs[] = {
   { "/dev/random",              16,  16 }, /* 16 bytes = 128 bits suffice */
   { "/proc/interrupts",          0,   0 },
   { "/proc/slabinfo",            0,   0 },
   { "/proc/stat",                0,   0 },
   { "/dev/urandom",             32,  64 },
};
#define RNGS (sizeof(rngs)/sizeof(struct rngs))

int Verbose = 0;

/* The basic function to hash a file */
static off_t
hash_file(struct MD5Context *ctx, int fd)
{
   off_t count = 0;
   ssize_t r;
   unsigned char buf[BUFFERSIZE];

   while ((r = read(fd, buf, sizeof(buf))) > 0) {
      MD5Update(ctx, buf, r);
      count += r;
   }
   /* Separate files with a null byte */
   buf[0] = 0;
   MD5Update(ctx, buf, 1);
   return count;
}

int main( int argc, char **argv )
{
   int               i;
   struct MD5Context ctx;
   unsigned char     digest[16];
   unsigned char     buf[BUFFERSIZE];
   int               fd;
   int               c;
   pid_t             pid;
   char              *file = NULL;
   int               r;
#ifdef HAVE_GETTIMEOFDAY
   struct timeval    tv;
   struct timezone   tz;
#else
   long int          t;
#endif

   setlocale(LC_ALL, "");
   bindtextdomain(PACKAGE, LOCALEDIR);
   textdomain(PACKAGE);

   while ((c = getopt( argc, argv, "vf:" )) != -1)
      switch (c) {
      case 'v': ++Verbose;     break;
      case 'f': file = optarg; break;
      }

   MD5Init( &ctx );
   
#ifdef HAVE_GETTIMEOFDAY
   gettimeofday( &tv, &tz );
   MD5Update( &ctx, (unsigned char *)&tv, sizeof( tv ) );
#else
   time( &t );
   MD5Update( &ctx, (unsigned char *)&t, sizeof( t ) );
#endif
   pid = getppid();
   MD5Update( &ctx, (unsigned char *)&pid, sizeof( pid ));
   pid = getpid();
   MD5Update( &ctx, (unsigned char *)&pid, sizeof( pid ));

   if (file) {
      int count = 0;
      
      if (file[0] == '-' && !file[1])
	 fd = fileno(stdin);
      else
	 fd = open( file, O_RDONLY );

      if (fd < 0) {
	 fprintf( stderr, _("Could not open %s\n"), file );
      } else {
         count = hash_file( &ctx, fd );
	 if (Verbose)
	    fprintf( stderr, _("Got %d bytes from %s\n"), count, file );

	 if (file[0] != '-' || file[1]) close( fd );
      }
   }

   for (i = 0; i < RNGS; i++) {
      if ((fd = open( rngs[i].path, O_RDONLY|O_NONBLOCK )) >= 0) {
	 int count = sizeof(buf);

	 if (rngs[i].maxlength && count > rngs[i].maxlength)
	    count = rngs[i].maxlength;
	 r = read( fd, buf, count );
	 if (r > 0)
	    MD5Update( &ctx, buf, r );
	 else
	    r = 0;
	 close( fd );
	 if (Verbose)
	    fprintf( stderr, _("Got %d bytes from %s\n"), r, rngs[i].path );
	 if (rngs[i].minlength && r >= rngs[i].minlength)
	    break;
      } else if (Verbose)
	 fprintf( stderr, _("Could not open %s\n"), rngs[i].path );
   }

   MD5Final( digest, &ctx );
   for (i = 0; i < 16; i++) printf( "%02x", digest[i] );
   putchar ( '\n' );
   
   /*
    * The following is important for cases like disk full, so shell scripts
    * can bomb out properly rather than think they succeeded.
    */
   if (fflush(stdout) < 0 || fclose(stdout) < 0)
      return 1;

   return 0;
}
