/* Mitch DSouza - (m.dsouza@mrc-apu.cam.ac.uk) */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <getopt.h>

void main (int argc, char **argv)
{
    int verbose = 0;

    if(argc == 2 && strcmp(argv[1], "-v") == 0) {
	verbose = 1; 
	argc--;
	argv++;
    }

    if (argc==2) {
	if (sethostid(atoi(argv[1]))!=0) {
	    perror("sethostid");
	    exit(1);
	}
    } else if (argc==1)	{
	unsigned long id = gethostid();
	
	if(id && verbose) {
	    printf("Hostid is %lu (0x%lx)\n",id,id);
	} else if(id) {
	    printf("0x%lx\n", id);
	} else {
	    printf("Usage: %s hostid_number\n",*argv);
	}
    }
    exit(0);
}
