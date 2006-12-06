/* `sln' program to create links between files.
   Copyright (C) 1986, 1989, 1990, 1991, 1993 Free Software Foundation, Inc.

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
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.  */

/* Written by Mike Parker and David MacKenzie. */

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

#if !defined(S_ISDIR) && defined(S_IFDIR)
#define	S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#endif

int
main (int argc, char **argv) {
  struct stat stats;

  if (argc != 3) return 1;

  /* Destination must not be a directory. */
#if 0
  if (stat (argv [2], &stats) == 0 && S_ISDIR (stats.st_mode))
    {
      return 1;
    }
#endif

  if (lstat (argv [2], &stats) == 0)
    {
      if (S_ISDIR (stats.st_mode))
	{
	  return 1;
	}
      else if (unlink (argv [2]) && errno != ENOENT)
	{
	  return 1;
	}
    }
  else if (errno != ENOENT)
    {
      return 1;
    }
       
#ifdef S_ISLNK
  if (symlink (argv [1], argv [2]) == 0)
#else
  if (link (argv [1], argv [2]) == 0)
#endif
    {
      return 0;
    }

  return 1;
}
