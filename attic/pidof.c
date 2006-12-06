/* pid -- display the process id of a running command

   Copyright (c) 1994 Salvatore Valente <svalente@mit.edu>
   Copyright (c) 1996 Bruno Haible <haible@ilog.fr>

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
#include <string.h>
#include <stdlib.h>

extern int* get_pids (char*, int);

char version_string[] = "pid 1.0";
char* program_name;

int usage (int status)
{
  FILE* fp = (status == 0 ? stdout : stderr);
  fprintf(fp, "Usage:  %s command ...\n", program_name);
  return status;
}

static int compar_int (const void* i1, const void* i2)
{
  return *((int *)i1) - *((int *)i2);
}

int main (int argc, char *argv[])
{
  int i;
  int *pids, *pids0;
  int num_allpids = 0;
  int *allpids = (int*)0;
  int allpids_size = 0;

  program_name = argv[0];

  /* Argument processing. */
  for (i = 1; i < argc; i++) {
    char* arg = argv[i];
    if (!strcmp(arg, "--help"))
      return usage(0);
    else if (!strcmp(arg, "--version")) {
      printf("%s\n", version_string);
      return 0;
    }
  }

  /* Gather the pids. */
  for (i = 1; i < argc; i++) {
    char* arg = argv[i];
    pids0 = pids = get_pids(arg, 1);
    if (pids) {
      while (*pids >= 0) {
        int pid = *pids++;
        if (num_allpids >= allpids_size) {
          allpids_size = 2*allpids_size+1;
          allpids = (int*) realloc(allpids, sizeof(int)*allpids_size);
          if (!allpids) {
            fprintf(stderr, "%s: out of memory\n", program_name);
            exit(1);
          }
        }
        allpids[num_allpids++] = pid;
      }
      free(pids0);
    }
  }

  /* Sort them. */
  if (num_allpids > 1)
    qsort(allpids, num_allpids, sizeof(int), compar_int);

  /* Print them. */
  for (pids = allpids, i = num_allpids; i > 0; pids++, i--)
    printf("%d\n", *pids);

  return 0;
}
