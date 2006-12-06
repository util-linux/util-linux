/*  need.c

    Source file for  need  (init(8) dependency tool).

    Copyright (C) 2000  Richard Gooch

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

    Richard Gooch may be reached by email at  rgooch@atnf.csiro.au
    The postal address is:
      Richard Gooch, c/o ATNF, P. O. Box 76, Epping, N.S.W., 2121, Australia.
*/

/*
    This tool will request init(8) to start a service and will wait for that
    service to be available. If the service is already available, init(8) will
    not start it again.
    This tool may also be used to inspect the list of currently available
    services.


    Written by      Richard Gooch   28-FEB-2000

    Last updated by Richard Gooch   28-FEB-2000


*/
#include <unistd.h>
#include <stdio.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include "simpleinit.h"


static void signal_handler (int sig);


static int caught_signal = 0;


int main (int argc, char **argv)
{
    int fd, nbytes;
    struct sigaction sa;
    sigset_t ss;
    char *ptr;
    long buffer[COMMAND_SIZE / sizeof (long)];
    struct command_struct *command = (struct command_struct *) buffer;

    sigemptyset (&ss);
    sigaddset (&ss, SIG_PRESENT);
    sigaddset (&ss, SIG_NOT_PRESENT);
    sigaddset (&ss, SIG_FAILED);
    sigprocmask (SIG_BLOCK, &ss, NULL);
    sigemptyset (&sa.sa_mask);
    sa.sa_flags = 0;
    sa.sa_handler = signal_handler;
    sigaction (SIG_PRESENT, &sa, NULL);
    sigaction (SIG_NOT_PRESENT, &sa, NULL);
    sigaction (SIG_FAILED, &sa, NULL);
    command->pid = getpid ();
    if ( ( ptr = strrchr (argv[0], '/') ) == NULL ) ptr = argv[0];
    else ++ptr;
    if (strcmp (ptr, "display-services") == 0)
    {
	command->command = COMMAND_DUMP_LIST;
	if (tmpnam (command->name) == NULL)
	{
	    fprintf (stderr, "Unable to create a unique filename\t%s\n",
		     ERRSTRING);
	    exit (1);
	}
	if (mkfifo (command->name, S_IRUSR) != 0)
	{
	    fprintf (stderr, "Unable to create FIFO: \"%s\"\t%s\n",
		     command->name, ERRSTRING);
	    exit (1);
	}
    }
    else
    {
	if ( (argc > 1) && (strcmp (argv[1], "-r") == 0) )
	{
	    command->command = COMMAND_ROLLBACK;
	    --argc;
	    ++argv;
	}
	else command->command = COMMAND_NEED;
	if (argc == 2) strcpy (command->name, argv[1]);
	else
	{
	    if (command->command == COMMAND_ROLLBACK) command->name[0] = '\0';
	    else
	    {
		fprintf (stderr, "Usage:\tneed programme\n");
		exit (1);
	    }
	}
    }
    if ( ( fd = open ("/dev/initctl", O_WRONLY, 0) ) < 0 )
    {
	fprintf (stderr, "Error opening\t%s\n", ERRSTRING);
	exit (1);
    }
    if (write (fd, buffer, COMMAND_SIZE) < COMMAND_SIZE)
    {
	fprintf (stderr, "Error writing\t%s\n", ERRSTRING);
	exit (1);
    }
    close (fd);
    if (command->command != COMMAND_DUMP_LIST)
    {
	sigemptyset (&ss);
	while (caught_signal == 0) sigsuspend (&ss);
	switch (caught_signal)
	{
	  case SIG_PRESENT:
	    return 0;
	  case SIG_NOT_PRESENT:
	    return 2;
	  case SIG_FAILED:
	    return 1;
	}
	return 3;
    }
    /*  Read back the data and display it  */
    if ( ( fd = open (command->name, O_RDONLY, 0) ) < 0 )
    {
	fprintf (stderr, "Error opening:\"%s\"\t%s\n",
		 command->name, ERRSTRING);
	exit (1);
    }
    unlink (command->name);
    fflush (stdout);
    while ( ( nbytes = read (fd, buffer, COMMAND_SIZE) ) > 0 )
	write (1, buffer, nbytes);
    close (fd);
    return (0);
}   /*  End Function main  */

static void signal_handler (int sig)
{
    caught_signal = sig;
}   /*  End Function signal_handler  */
