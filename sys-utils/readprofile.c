/*
 *  readprofile.c - used to read /proc/profile
 *
 *  Copyright (C) 1994 Alessandro Rubini
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>  /* getopt() */
#include <string.h>

#define RELEASE "1.1, Jan 1995"

#define S_LEN 128

static char *prgname;

/* These are the defaults and they cna be changed */
static char defaultmap1[]="/usr/src/linux/System.map";
static char defaultmap2[]="/usr/src/linux/zSystem.map";
static char defaultpro[]="/proc/profile";
static char optstring[]="m:p:itvarV";

void usage()
{
  fprintf(stderr,
		  "%s: Usage: \"%s [options]\n"
		  "\t -m <mapfile>  (default = \"%s\")\n"
		  "\t -p <pro-file> (default = \"%s\")\n"
		  "\t -i            print only info about the sampling step\n"
		  "\t -t            print terse data\n"
		  "\t -v            print verbose data\n"
		  "\t -a            print all symbols, even if count is 0\n"
		  "\t -r            reset all the counters (root only)\n"
		  "\t -V            print version and exit\n"
		  ,prgname,prgname,defaultmap1,defaultpro);
  exit(1);
}

FILE *myopen(char *name, char *mode, int *flag)
{
static char cmdline[S_LEN];

  if (!strcmp(name+strlen(name)-3,".gz"))
	{
	*flag=1;
	sprintf(cmdline,"zcat %s", name);
	return popen(cmdline,mode);
	}
  *flag=0;
  return fopen(name,mode);
}

int main (int argc, char **argv)
{
FILE *pro;
FILE *map;
unsigned long l;
char *proFile;
char *mapFile;
int add, step;
int fn_add[2];            /* current and next address */
char fn_name[2][S_LEN];   /* current and next name */
char mode[8];
int i,c,current=0;
int optAll=0, optInfo=0, optReset=0, optTerse=0, optVerbose=0;
char mapline[S_LEN];
int maplineno=1;
int popenMap, popenPro;   /* flags to tell if popen() is used */

#define next (current^1)

  prgname=argv[0];
  proFile=defaultpro;
  mapFile=defaultmap1;

  while ((c=getopt(argc,argv,optstring))!=-1)
    {
    switch(c)
      {
      case 'm': mapFile=optarg; break;
      case 'p': proFile=optarg; break;
      case 'a': optAll++;       break;
      case 'i': optInfo++;      break;
	  case 't': optTerse++;     break;
	  case 'r': optReset++;     break;
	  case 'v': optVerbose++;   break;
	  case 'V': printf("%s Version %s\n",prgname,RELEASE); exit(0);
      default: usage();
      }
    }

  if (optReset)
	{
	pro=fopen(defaultpro,"w");
	if (!pro)
	  {perror(proFile); exit(1);}
	fprintf(pro,"anything\n");
	fclose(pro);
    exit(0);
	}

  if (!(pro=myopen(proFile,"r",&popenPro))) 
    {fprintf(stderr,"%s: ",prgname);perror(proFile);exit(1);}

  /*
   * In opening the map file, try both the default names, but exit
   * at first fail if the filename was specified on cmdline
   */
  for (map=NULL; map==NULL; )
	{
	if (!(map=myopen(mapFile,"r",&popenMap)))
	  {
	  fprintf(stderr,"%s: ",prgname);perror(mapFile);
	  if (mapFile!=defaultmap1) exit(1);
	  mapFile=defaultmap2;
	  }
	}

#define NEXT_WORD(where) \
        (fread((void *)where, 1,sizeof(unsigned long),pro), feof(pro) ? 0 : 1)

  /*
   * Init the 'next' field
   */
  if (!fgets(mapline,S_LEN,map))
	{
	fprintf(stderr,"%s: %s(%i): premature EOF\n",prgname,mapFile,maplineno);
	exit(1);
	}
  if (sscanf(mapline,"%x %s %s",&(fn_add[next]),mode,fn_name[next])!=3)
	{
	fprintf(stderr,"%s: %s(%i): wrong map line\n",prgname,mapFile, maplineno);
	exit(1);
	}

  add=0;

  if (!NEXT_WORD(&step))
	{
    fprintf(stderr,"%s: %s: premature EOF\n",prgname,proFile);
    exit(1);
    }

  if (optInfo)
    {
    printf(optTerse ? "%i\n" : "The sampling step in the kernel is %i bytes\n",
		   step);
    exit(0);
    } 

  /*
   * The main loop is build around the mapfile
   */
  
  while(current^=1, maplineno++, fgets(mapline,S_LEN,map))
    {
    int fn_len;
    int count=0;


	if (sscanf(mapline,"%x %s %s",&(fn_add[next]),mode,fn_name[next])!=3)
	  {
	  fprintf(stderr,"%s: %s(%i): wrong map line\n",
			  prgname,mapFile, maplineno);
	  exit(1);
	  }

	if (!(fn_len=fn_add[next]-fn_add[current]))
	  continue;

    if (*mode=='d' || *mode=='D') break; /* only text is profiled */

    while (add<fn_add[next])
      {
      if (!NEXT_WORD(&l))
		{
		fprintf(stderr,"%s: %s: premature EOF\n",prgname,proFile);
		exit(1);
		}
      count+=l; add+=step;
      }

    if (count || optAll)
	  {
	  if (optTerse)
		printf("%i %s %lg\n",
			   count,fn_name[current],count/(double)fn_len);
	  else if (optVerbose)
		printf("%08x %-40s %6i %8.4lf\n",
			   fn_add[current],fn_name[current],count,count/(double)fn_len);
	  else
		printf("%6i %-40s %8.4lf\n",
			   count,fn_name[current],count/(double)fn_len);
	  }
	}

  if (feof(map))
	{
	fprintf(stderr,"%s: %s(%i): premature EOF\n",prgname,mapFile,maplineno);
	exit(1);
	}
	
  popenPro ? pclose(pro) : fclose(pro);
  popenMap ? pclose(map) : fclose(map);
  exit(0);
}


