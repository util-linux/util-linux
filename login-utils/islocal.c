/* 
   islocal.c - returns true if user is registered in the local
   /etc/passwd file. Written by Alvaro Martinez Echevarria, 
   alvaro@enano.etsit.upm.es, to allow peaceful coexistence with yp. Nov 94.

   Hacked a bit by poe@daimi.aau.dk
   See also ftp://ftp.daimi.aau.dk/pub/linux/poe/admutil*

   Hacked by Peter Breitenlohner, peb@mppmu.mpg.de,
     to distinguish user names where one is a prefix of the other,
     and to use "pathnames.h". Oct 5, 96.   

   1999-02-22 Arkadiusz Mi¶kiewicz <misiek@pld.ORG.PL>
   - added Native Language Support
     

*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nls.h"
#include "pathnames.h"
#include "islocal.h"

#define MAX_LENGTH	1024

int
is_local(char *user)
{
	FILE *fd;
	char line[MAX_LENGTH];
	int local = 0;
	int len;

        if(!(fd = fopen(_PATH_PASSWD, "r"))) {
                fprintf(stderr,_("Can't read %s, exiting."),_PATH_PASSWD);
                exit(1);
        }

	len = strlen(user);
        while(fgets(line, MAX_LENGTH, fd) > 0) {
                if(!strncmp(line, user, len) && line[len] == ':') {
			local = 1;
			break;
                }
	}
	fclose(fd);
	return local;
}

