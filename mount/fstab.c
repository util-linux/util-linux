/* /home/faith/cvs/util-linux/mount/fstab.c,v 1.1.1.1 1995/02/22 19:09:21 faith Exp */

#include "fstab.h"
#include <stdio.h>

#define streq(s, t)	(strcmp ((s), (t)) == 0)

/* These routines are superceded by mntent(3), but I use them for
   convenience.  Mntent(3) is used in the implementation, so be
   very careful about the static buffers that are returned.  */


static FILE *F_fstab = NULL;

/* Open fstab or rewind if already open.  */
int
setfsent (void)
{
  if (F_fstab)
    return (fseek (F_fstab, 0L, SEEK_SET) == 0);

  F_fstab = setmntent (_PATH_FSTAB, "r");
  return (F_fstab != NULL);
}

/* Close fstab.  */
void
endfsent (void)
{
  endmntent (F_fstab);
}

/* Return next entry in fstab, skipping ignore entries.  I also put
   in some ugly hacks here to skip comments and blank lines.  */
struct mntent *
getfsent (void)
{
  struct mntent *fstab;

  if (!F_fstab && !setfsent())
    return 0;

  for (;;)
    {
      fstab = getmntent (F_fstab);
      if (fstab == NULL)
	{
	  if (!feof (F_fstab) && !ferror (F_fstab))
	    continue;
	  else
	    break;
	}
      else if ((*fstab->mnt_fsname != '#')
	       && !streq (fstab->mnt_type, MNTTYPE_IGNORE))
	break;
    }
  return fstab;
}

/* Find the dir FILE in fstab.  */
struct mntent *
getfsfile (const char *file)
{
  struct mntent *fstab;

  /* Open or rewind fstab.  */
  if (!setfsent ())
    return 0;

  while ((fstab = getfsent ()))
    if (streq (fstab->mnt_dir, file))
      break;

  return fstab;
}

/* Find the device SPEC in fstab.  */
struct mntent *
getfsspec (const char *spec)
{
  struct mntent *fstab;

  /* Open or rewind fstab.  */
  if (!setfsent())
    return 0;

  while ((fstab = getfsent ()))
    if (streq (fstab->mnt_fsname, spec))
      break;

  return fstab;
}
