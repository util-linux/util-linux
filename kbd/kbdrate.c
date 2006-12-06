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

kbdrate                 set rate to IBM default (10.9 cps, 250ms delay)
kbdrate -r 30.0         set rate to 30 cps and delay to 250ms
kbdrate -r 20.0 -s      set rate to 20 cps (delay 250ms) -- don't print message
kbdrate -r 0 -d 0       set rate to 2.0 cps and delay to 250 ms

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

  1999-02-22 Arkadiusz Mi¶kiewicz <misiek@misiek.eu.org>
  - added Native Language Support

  1999-03-17
  Linux/SPARC modifications by Jeffrey Connell <ankh@canuck.gen.nz>:
  It seems that the KDKBDREP ioctl is not available on this platform.
  However, Linux/SPARC has its own ioctl for this, with yet another
  different measurement system.  Thus, try for KIOCSRATE, too.

*/

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/file.h>
#include <sys/ioctl.h>

#include "../defines.h"
#ifdef HAVE_kd_h
#include <linux/kd.h>
#endif
#ifdef __sparc__
#include <asm/param.h>
#include <asm/kbio.h>
#endif

#include "nls.h"

static int valid_rates[] = { 300, 267, 240, 218, 200, 185, 171, 160, 150,
                                   133, 120, 109, 100, 92, 86, 80, 75, 67,
                                   60, 55, 50, 46, 43, 40, 37, 33, 30, 27,
                                   25, 23, 21, 20 };
#define RATE_COUNT (sizeof( valid_rates ) / sizeof( int ))

static int valid_delays[] = { 250, 500, 750, 1000 };
#define DELAY_COUNT (sizeof( valid_delays ) / sizeof( int ))

static int
KDKBDREP_ioctl_ok(double rate, int delay, int silent) {
#ifdef KDKBDREP
     /* This ioctl is defined in <linux/kd.h> but is not
	implemented anywhere - must be in some m68k patches. */
   struct kbd_repeat kbdrep_s;

   /* don't change, just test */
   kbdrep_s.rate = -1;
   kbdrep_s.delay = -1;
   if (ioctl( 0, KDKBDREP, &kbdrep_s )) {
     if (errno == EINVAL)
       return 0;
     perror( "ioctl(KDKBDREP)" );
     exit( 1 );
   }

   /* do the change */
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

   /* report */
   if (kbdrep_s.rate == 0)
     rate = 0;
   else
     rate = 1000.0 / (double) kbdrep_s.rate;

   if (!silent)
     printf( _("Typematic Rate set to %.1f cps (delay = %d ms)\n"),
	     rate, kbdrep_s.delay );

   return 1;			/* success! */

#else /* no KDKBDREP */
   return 0;
#endif /* KDKBDREP */
}

static int
KIOCSRATE_ioctl_ok(double rate, int delay, int silent) {
#ifdef KIOCSRATE
   struct kbd_rate kbdrate_s;
   int fd;

   fd = open("/dev/kbd", O_RDONLY);
   if (fd == -1) {
     perror( "open(/dev/kbd)" );
     exit( 1 );
   }

   kbdrate_s.rate = (int) (rate + 0.5);  /* must be integer, so round up */
   kbdrate_s.delay = delay * HZ / 1000;  /* convert ms to Hz */
   if (kbdrate_s.rate > 50)
     kbdrate_s.rate = 50;

   if (ioctl( fd, KIOCSRATE, &kbdrate_s )) {
     perror( "ioctl(KIOCSRATE)" );
     exit( 1 );
   }
   close( fd );

   if (!silent)
     printf( "Typematic Rate set to %d cps (delay = %d ms)\n",
	     kbdrate_s.rate, kbdrate_s.delay * 1000 / HZ );

   return 1;
#else /* no KIOCSRATE */
   return 0;
#endif /* KIOCSRATE */
}

int main( int argc, char **argv )
{
#ifdef __sparc__
   double      rate = 5.0;      /* Default rate */
   int         delay = 200;     /* Default delay */
#else
   double      rate = 10.9;     /* Default rate */
   int         delay = 250;     /* Default delay */
#endif
   int         value = 0x7f;    /* Maximum delay with slowest rate */
                                /* DO NOT CHANGE this value */
   int         silent = 0;
   int         fd;
   char        data;
   int         c;
   int         i;
   extern char *optarg;

   setlocale(LC_ALL, "");
   bindtextdomain(PACKAGE, LOCALEDIR);
   textdomain(PACKAGE);
   

   while ( (c = getopt( argc, argv, "r:d:svVh?" )) != EOF ) {
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
	 case 'V':
	    fprintf( stderr, "util-linux %s kbdrate\n", UTIL_LINUX_VERSION);
	    exit(0);
	 case 'h':
	 case '?':
	    fprintf( stderr,
		  _("Usage: kbdrate [-V] [-s] [-r rate] [-d delay]\n"));
	    exit(0);
         }
   }

   if(KDKBDREP_ioctl_ok(rate, delay, silent)) 	/* m68k? */
	return 0;

   if(KIOCSRATE_ioctl_ok(rate, delay, silent))	/* sparc? */
	return 0;


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
	perror( _("Cannot open /dev/port") );
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

      if (!silent) printf( _("Typematic Rate set to %.1f cps (delay = %d ms)\n"),
                           valid_rates[value & 0x1f] / 10.0,
                           valid_delays[ (value & 0x60) >> 5 ] );

   return 0;
}
