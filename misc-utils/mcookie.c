/* mcookie.c -- Generates random numbers for xauth
 * Created: Fri Feb  3 10:42:48 1995 by faith@cs.unc.edu
 * Revised: Mon Sep 25 23:44:43 1995 by r.faith@ieee.org
 * Public Domain 1995 Rickard E. Faith (faith@cs.unc.edu)
 * This program comes with ABSOLUTELY NO WARRANTY.
 * 
 * $Id: mcookie.c,v 1.2 1995/10/07 01:32:00 faith Exp $
 *
 * This program gathers some random bits of data and used the MD5
 * message-digest algorithm to generate a 128-bit hexadecimal number for
 * use with xauth(1).
 *
 * NOTE: Unless /dev/random is available, this program does not actually
 * gather 128 bits of random information, so the magic cookie generated
 * will be considerably easier to guess than one might expect.
 */

#ifdef __linux__
#define HAVE_GETTIMEOFDAY 1
#endif

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include "md5.h"
#if HAVE_GETTIMEOFDAY
#include <sys/time.h>
#include <unistd.h>
#endif

#define MAXBUFFERSIZE 512

struct rngs {
   const char *path;
   int        length;
} rngs[] = {
   { "/dev/random",              16 },
   { "/dev/urandom",            128 },
   { "/proc/stat",    MAXBUFFERSIZE },
   { "/proc/loadavg", MAXBUFFERSIZE },
   { "/dev/audio",    MAXBUFFERSIZE },
};
#define RNGS (sizeof(rngs)/sizeof(struct rngs))

int Verbose = 0;

int main( int argc, char **argv )
{
   int               i;
   struct MD5Context ctx;
   unsigned char     digest[16];
   unsigned char     buf[MAXBUFFERSIZE];
   int               fd;
   int               c;
   pid_t             pid;
   char              *file = NULL;
   int               r;
#if HAVE_GETTIMEOFDAY
   struct timeval    tv;
   struct timezone   tz;
#else
   long int          t;
#endif

   while ((c = getopt( argc, argv, "vf:" )) != EOF)
      switch (c) {
      case 'v': ++Verbose;     break;
      case 'f': file = optarg; break;
      }

   MD5Init( &ctx );
   
#if HAVE_GETTIMEOFDAY
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
      
      if (file[0] == '-' && !file[1]) fd = fileno(stdin);
      else if ((fd = open( file, O_RDONLY )) <0) {
	 fprintf( stderr, "Could not open %s\n" );
      }

      while ((r = read( fd, buf, sizeof( buf ) )) > 0) {
	 MD5Update( &ctx, buf, r );
	 count += r;
      }
      if (Verbose)
	 fprintf( stderr, "Got %d bytes from %s\n", count, file );
      
      if (file[0] != '-' || file[1]) close( fd );
   }

   for (i = 0; i < RNGS; i++) {
      if ((fd = open( rngs[i].path, O_RDONLY )) >= 0) {
	 r = read( fd, buf, sizeof( buf ) );
	 MD5Update( &ctx, buf, r );
	 close( fd );
	 if (Verbose)
	    fprintf( stderr, "Got %d bytes from %s\n", r, rngs[i].path );
	 if (r >= rngs[i].length) break;
      } else if (Verbose)
	 fprintf( stderr, "Could not open %s\n", rngs[i].path );
   }

   MD5Final( digest, &ctx );
   for (i = 0; i < 16; i++) printf( "%02x", digest[i] );
   putchar ( '\n' );
   
   return 0;
}
