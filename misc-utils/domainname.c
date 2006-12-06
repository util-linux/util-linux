/* domainname.c - poe@daimi.aau.dk */

#include <sys/types.h>
#include <sys/param.h>
#include <stdio.h>
#include <unistd.h>

#define MAXDNAME 64

int main(int argc, char *argv[])
{
	char hn[MAXDNAME + 1];
	
	if(argc >= 2) {
		if(geteuid() || getuid()) {
			puts("You must be root to change the domainname");
			exit(1);
		}
		if(strlen(argv[1]) > MAXDNAME) {
			puts("That name is too long.");
			exit(1);
		}
		setdomainname(argv[1], strlen(argv[1]));
	} else {
		getdomainname(hn, MAXDNAME);
		puts(hn);
	}
	exit(0);
}
