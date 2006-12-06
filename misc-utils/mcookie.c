/* mcookie.c -- Generates random numbers for xauth
 * Created: Fri Feb  3 10:42:48 1995 by faith@cs.unc.edu
 * Revised: Sun Feb 12 20:29:58 1995 by faith@cs.unc.edu
 * Public Domain 1995 Rickard E. Faith (faith@cs.unc.edu)
 * This program comes with ABSOLUTELY NO WARRANTY.
 * 
 * mcookie.c,v 1.1.1.1 1995/02/22 19:09:16 faith Exp
 */

#define SECURE 1

#include <stdio.h>
#include <stdlib.h>
#if SECURE
#include <sys/time.h>
#include <unistd.h>
#endif

int main( void )
{
   int      i;
#if SECURE
   struct timeval  tv;
   struct timezone tz;

   gettimeofday( &tv, &tz );
   srand( tv.tv_sec + tv.tv_usec );
#else
   long int t;
   
   time( &t );
   srand( t );
#endif

   for (i = 0; i < 32; i++) {
      int r = (rand() & 0x0f0) >> 4;

      if (r < 10) putchar( '0' + r );
      else        putchar( 'a' + r - 10 );
   }
   putchar ( '\n' );
   
   return 0;
}
