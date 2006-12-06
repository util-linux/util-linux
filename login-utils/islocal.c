/* islocal.c - returns true if user is registered in the local
   /etc/passwd file. Written by Alvaro Martinez Echevarria, 
   alvaro@enano.etsit.upm.es, to allow peaceful coexistence with yp. Nov 94.
   Hacked a bit by poe@daimi.aau.dk
   See also ftp://ftp.daimi.aau.dk/pub/linux/poe/admutil*
*/

#include <stdio.h>
#include <string.h>

#define MAX_LENGTH	1024

int
is_local(char *user)
{
	FILE *fd;
	char line[MAX_LENGTH];
	int local = 0;

        if(!(fd = fopen("/etc/passwd", "r"))) {
                puts("Can't read /etc/passwd, exiting.");
                exit(1);
        }

        while(fgets(line, MAX_LENGTH, fd) > 0) {
                if(!strncmp(line, user, strlen(user))) {
			local = 1;
			break;
                }
	}
	fclose(fd);
	return local;
}

