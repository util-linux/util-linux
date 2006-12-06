/* dmesg.c -- Print out the contents of the kernel ring buffer
 * Created: Sat Oct  9 16:19:47 1993
 * Revised: Thu Oct 28 21:52:17 1993 by faith@cs.unc.edu
 * Copyright 1993 Theodore Ts'o (tytso@athena.mit.edu)
 * This program comes with ABSOLUTELY NO WARRANTY.
 * Modifications by Rick Sladkey (jrs@world.std.com)
 */

#include <linux/unistd.h>
#include <stdio.h>
#include <getopt.h>

#define __NR_klog __NR_syslog

static inline _syscall3(int,klog,int,type,char *,b,int,len)

static char *progname;

usage()
{
   fprintf( stderr, "Usage: %s [-c] [-n level]\n", progname );
}

int main( int argc, char *argv[] )
{
   char buf[4096];
   int  i;
   int  n;
   int  c;
   int  level;
   int  lastc;
   int  cmd = 3;

   progname = argv[0];
   while ((c = getopt( argc, argv, "cn:" )) != EOF) {
      switch (c) {
      case 'c':
	 cmd = 4;
	 break;
      case 'n':
	 cmd = 8;
	 level = atoi(optarg);
	 break;
      case '?':
      default:
	 usage();
	 exit(1);
      }
   }
   argc -= optind;
   argv += optind;
   
   if (argc > 1) {
      usage();
      exit(1);
   }

   if (cmd == 8) {
      n = klog( cmd, NULL, level );
      if (n < 0) {
	 perror( "klog" );
	 exit( 1 );
      }
      exit( 0 );
   }

   n = klog( cmd, buf, sizeof( buf ) );
   if (n < 0) {
      perror( "klog" );
      exit( 1 );
   }

   lastc = '\n';
   for (i = 0; i < n; i++) {
      if ((i == 0 || buf[i - 1] == '\n') && buf[i] == '<') {
	 i++;
	 while (buf[i] >= '0' && buf[i] <= '9')
	    i++;
	 if (buf[i] == '>')
	    i++;
      }
      lastc = buf[i];
      putchar( lastc );
   }
   if (lastc != '\n')
      putchar( '\n' );
   return 0;
}
