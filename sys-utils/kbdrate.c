/*
From: faith@cs.unc.edu (Rik Faith)
Subject: User mode keyboard rate changer
Date: 27 Apr 92 13:44:26 GMT

I put together this program, called kbdrate.c, which will reset the keyboard
repeat rate and delay in user mode.  The program must have read/write
access to /dev/port, so if /dev/port is only read/writeable by group port,
then kbdrate must run setgid to group port (for example).

The "rate" is the rate in characters per second

The "delay" is the amount of time the key must remain depressed before it
will start to repeat.

Usage examples:

kbdrate                 set rate to IBM default (10.9 cps, 250mS delay)
kbdrate -r 30.0         set rate to 30 cps and delay to 250mS
kbdrate -r 20.0 -s      set rate to 20 cps (delay 250mS) -- don't print message
kbdrate -r 0 -d 0       set rate to 2.0 cps and delay to 250 mS

I find it useful to put kbdrate in my /etc/rc file so that the keyboard
rate is set to something that I find comfortable at boot time.  This sure
beats rebuilding the kernel!


  kbdrate.c -- Set keyboard typematic rate (and delay)
  Created: Thu Apr 23 12:24:30 1992
  Author: Rickard E. Faith, faith@cs.unc.edu

  Copyright 1992 Rickard E. Faith.  Distributed under the GPL.
  This program comes with ABSOLUTELY NO WARRANTY.
  Usage: kbdrate [-r rate] [-d delay] [-s]
         Rate can range from 2.0 to 30.0 (units are characters per second)
         Delay can range from 250 to 1000 (units are milliseconds)
         -s suppressed message
  Compiles under gcc 2.1 for Linux (tested with the pre-0.96 kernel)
 
  Wed Jun 22 21:35:43 1994, faith@cs.unc.edu:
            Changed valid_rates per suggestion by Andries.Brouwer@cwi.nl.
  Wed Jun 22 22:18:29 1994, faith@cs.unc.edu:
            Added patch for AUSTIN notebooks from John Bowman
            (bowman@hagar.ph.utexas.edu)
 
  Linux/68k modifications by Roman Hodek 
 				(Roman.Hodek@informatik.uni-erlangen.de):
 
  Reading/writing the Intel I/O ports via /dev/port is not the
  English way... Such hardware dependant stuff can never work on
  other architectures.
  
  Linux/68k has an new ioctl for setting the keyboard repeat rate
  and delay. Both values are counted in msecs, the kernel will do
  any rounding to values possible with the underlying hardware.
 
  kbdrate now first tries if the KDKBDREP ioctl is available. If it
  is, it is used, else the old method is applied.

*/

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <linux/version.h>
#if LINUX_VERSION_CODE >= 131072
/* kd.h is not available with all linux versions.  131072 is equivalent
   to linux 2.0.0 */
#include <linux/kd.h>
#endif

#define VERSION "1.3"

static int valid_rates[] = { 300, 267, 240, 218, 200, 185, 171, 160, 150,
                                   133, 120, 109, 100, 92, 86, 80, 75, 67,
                                   60, 55, 50, 46, 43, 40, 37, 33, 30, 27,
                                   25, 23, 21, 20 };
#define RATE_COUNT (sizeof( valid_rates ) / sizeof( int ))

static int valid_delays[] = { 250, 500, 750, 1000 };
#define DELAY_COUNT (sizeof( valid_delays ) / sizeof( int ))

#ifdef KDKBDREP
static int ioctl_possible = 0;
struct kbd_repeat kbdrep_s;
#endif

int main( int argc, char **argv )
{
   double      rate = 10.9;     /* Default rate */
   int         delay = 250;     /* Default delay */
   int         value = 0x7f;    /* Maximum delay with slowest rate */
                                /* DO NOT CHANGE this value */
   int         silent = 0;
   int         fd;
   char        data;
   int         c;
   int         i;
   extern char *optarg;

   while ( (c = getopt( argc, argv, "r:d:sv" )) != EOF )
         switch (c) {
         case 'r':
            rate = atof( optarg );
            break;
         case 'd':
            delay = atoi( optarg );
            break;
         case 's':
            silent = 1;
            break;
	 case 'v':
	    fprintf( stderr, "util-linux kbdrate " VERSION "\n");
	    exit(0);
         }

#ifdef KDKBDREP
   kbdrep_s.rate = -1; /* don't change, just test */
   kbdrep_s.delay = -1;
   if (ioctl( 0, KDKBDREP, &kbdrep_s )) {
     if (errno == EINVAL)
       ioctl_possible = 0;
     else {
       perror( "ioctl(KDKBDREP)" );
       exit( 1 );
     }
   } else ioctl_possible = 1;

   if (ioctl_possible) {
     if (rate == 0)				/* switch repeat off */
       kbdrep_s.rate = 0;
     else
       kbdrep_s.rate  = 1000.0 / rate;		/* convert cps to msec */
     if (kbdrep_s.rate < 1)
       kbdrep_s.rate = 1;
     kbdrep_s.delay = delay;
     if (kbdrep_s.delay < 1)
       kbdrep_s.delay = 1;
     
     if (ioctl( 0, KDKBDREP, &kbdrep_s )) {
       perror( "ioctl(KDKBDREP)" );
       exit( 1 );
     }
          
     if (!silent)
       printf( "Typematic Rate set to %.1f cps (delay = %d mS)\n",
	       1000.0 / (double)kbdrep_s.rate, kbdrep_s.delay );
   } else {
#endif

    /* The ioport way */

    for (i = 0; i < RATE_COUNT; i++)
      if (rate * 10 >= valid_rates[i]) {
	value &= 0x60;
	value |= i;
	break;
      }


      for (i = 0; i < DELAY_COUNT; i++)
	if (delay <= valid_delays[i]) {
	  value &= 0x1f;
	  value |= i << 5;
	  break;
	}

      if ( (fd = open( "/dev/port", O_RDWR )) < 0) {
	perror( "Cannot open /dev/port" );
	exit( 1 );
      }

      do {
	lseek( fd, 0x64, 0 );
	read( fd, &data, 1 );
      } while ((data & 2) == 2 );  /* wait */

      lseek( fd, 0x60, 0 );
      data = 0xf3;                 /* set typematic rate */
      write( fd, &data, 1 );

      do {
	lseek( fd, 0x64, 0 );
	read( fd, &data, 1 );
      } while ((data & 2) == 2 );  /* wait */

      lseek( fd, 0x60, 0 );
      sleep( 1 );
      write( fd, &value, 1 );

      close( fd );

      if (!silent) printf( "Typematic Rate set to %.1f cps (delay = %d mS)\n",
                           valid_rates[value & 0x1f] / 10.0,
                           valid_delays[ (value & 0x60) >> 5 ] );

#ifdef KDKBDREP
   }
#endif

   return 0;
}
