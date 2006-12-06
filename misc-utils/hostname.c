/* hostname -- set the host name or show the host/domain name

   Copyright (C) 1994 Peter Tobias

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

*/


#include <stdio.h>
#include <unistd.h>
#include <getopt.h>
#include <string.h>
#include <netdb.h>
#include <errno.h>
#include <sys/param.h>

#define NO_OPT -1

static char *program_name;
static const char *version_string = "hostname 1.6";

static void sethname(char *);
static void showhname(char *, int);
static void usage(void);

static void sethname(char *hname)
{
	if(sethostname(hname, strlen(hname))) {
		switch(errno) {
			case EPERM:
				fprintf(stderr,"%s: you must be root to change the host name\n", program_name);
				break;
			case EINVAL:
				fprintf(stderr,"%s: name too long\n", program_name);
				break;
			default:
		}
		exit(1);
	};
}

static void showhname(char *hname, int c)
{
	struct hostent *hp;
	register char *p;

	if (!(hp = gethostbyname(hname))) {
		herror(program_name);
		exit(1);
	}

    	if (!(p = strchr(hp->h_name, '.')) && (c == 'd')) return;

	switch(c) {
		case 'd':
			printf("%s\n", ++p);
			break;
		case 'f':
			printf("%s\n", hp->h_name);
			break;
		case 's':
			if (p != NULL) *p = '\0';
			printf("%s\n", hp->h_name);
			break;
		default:
	}
}

static void usage(void)
{
  printf("Usage: %s [OPTION]... [hostname]\n\n\
  -d, --domain                 display the DNS domain name\n\
  -F, --file filename          read the host name from file\n\
  -f, --fqdn, --long           display the long host name (FQDN)\n\
  -s, --short                  display the short host name\n\
  -h, --help                   display this help and exit\n\
  -v, --version                output version information and exit\n\
\n\
   When the program is called without any arguments, it displays the\n\
   current host name as set by the hostname command. If an argument\n\
   is given, the program will set the value of the host name to the\n\
   value specified.\n\
   Unless you are using bind or NIS for host lookups you can change the\n\
   FQDN (Fully Qualified Domain Name) and the DNS domain name (which is\n\
   part of the FQDN) in the /etc/hosts file.\n", program_name);
}

int main(int argc, char **argv)
{
	int c;
	int option_index = 0;

	char myname[MAXHOSTNAMELEN+1];

	static const struct option long_options[] =
	{
		{"domain", no_argument, 0, 'd'},
		{"file", required_argument, 0, 'F'},
		{"fqdn", no_argument, 0, 'f'},
		{"help", no_argument, 0, 'h'},
		{"long", no_argument, 0, 'f'},
		{"short", no_argument, 0, 's'},
		{"version", no_argument, 0, 'v'},
		{0, 0, 0, 0}
	};

	program_name = (rindex(argv[0], '/')) ? rindex(argv[0], '/') + 1 : argv[0];

	if (strcmp(program_name, "dnsdomainname") == 0) {
		if (argc > 1) {
			fprintf(stderr,"%s: You can't change the DNS domainname with this command\n", program_name);
			fprintf(stderr,"\nUnless you are using bind or NIS for host lookups you can change the DNS\n");
			fprintf(stderr,"domain name (which is part of the FQDN) in the /etc/hosts file.\n");
			exit(1);
		}
		c = 'd';
	} else
		c = getopt_long(argc, argv, "dfF:hsv", long_options, &option_index);

	gethostname(myname, sizeof(myname));

	switch(c)
	{
		case 'd':
		case 'f':
		case 's':
			showhname(myname, c);
			break;
		case 'F':
			{
			register FILE *fd;
			register char *p;
			char fline[MAXHOSTNAMELEN];

			if ((fd = fopen(optarg, "r")) != NULL) {
				while (fgets(fline, sizeof(fline), fd) != NULL)
					if ((p = index(fline, '\n')) != NULL) {
						*p = '\0';
						if (fline[0] == '#')
							continue;
						sethname(fline);
					}
				(void) fclose(fd);
			} else {
				fprintf(stderr,"%s: can't open `%s'\n",
				    program_name, optarg);
				exit(1);
			}
			}
			break;
		case 'h':
			usage();
			break;
		case 'v':
			printf("%s\n", version_string);
			break;
		case '?':
			fprintf(stderr,"Try `%s --help' for more information.\n", program_name);
			exit(1);
			break;
		case NO_OPT:
			if (optind < argc) {
				sethname(argv[optind]);
				exit(0);
			}
		default:
			printf("%s\n", myname);

	};
	exit(0);
}
