/* Copyright (C) 2003, 2004, 2005 Thorsten Kukuk
   Author: Thorsten Kukuk <kukuk@suse.de>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License version 2 or
   later as published by the Free Software Foundation.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#define _GNU_SOURCE

#include <errno.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "getdef.h"

struct item {
  char *name;         /* Name of the option.  */
  char *value;        /* Value of the option.  */
  struct item *next;  /* Pointer to next option.  */
};

static struct item *list = NULL;

void
free_getdef_data (void)
{
  struct item *ptr;

  ptr = list;
  while (ptr != NULL)
    {
      struct item *tmp;
      tmp = ptr->next;
      free (ptr->name);
      free (ptr->value);
      free (ptr);
      ptr = tmp;
    }

  list = NULL;
}

/* Add a new entry to the list.  */
static void
store (const char *name, const char *value)
{
  struct item *new = malloc (sizeof (struct item));

  if (new == NULL)
    abort ();

  if (name == NULL)
    abort ();

  new->name = strdup (name);
  new->value = strdup (value ?: "");
  new->next = list;
  list = new;
}

/* Search a special entry in the list and return the value.  */
static const char *
search (const char *name)
{
  struct item *ptr;

  ptr = list;
  while (ptr != NULL)
    {
      if (strcasecmp (name, ptr->name) == 0)
	return ptr->value;
      ptr = ptr->next;
    }

  return NULL;
}

/* Load the login.defs file (/etc/login.defs).  */
static void
load_defaults_internal (const char *filename)
{
  FILE *fp;
  char *buf = NULL;
  size_t buflen = 0;

  fp = fopen (filename, "r");
  if (NULL == fp)
    return;

  while (!feof (fp))
    {
      char *tmp, *cp;
#if defined(HAVE_GETLINE)
      ssize_t n = getline (&buf, &buflen, fp);
#elif defined (HAVE_GETDELIM)
      ssize_t n = getdelim (&buf, &buflen, '\n', fp);
#else
      ssize_t n;

      if (buf == NULL)
        {
          buflen = 8096;
          buf = malloc (buflen);
        }
      buf[0] = '\0';
      fgets (buf, buflen - 1, fp);
      if (buf != NULL)
        n = strlen (buf);
      else
        n = 0;
#endif /* HAVE_GETLINE / HAVE_GETDELIM */
      cp = buf;

      if (n < 1)
        break;

      tmp = strchr (cp, '#');  /* remove comments */
      if (tmp)
        *tmp = '\0';
      while (isspace ((unsigned char) *cp))    /* remove spaces and tabs */
        ++cp;
      if (*cp == '\0')        /* ignore empty lines */
        continue;

      if (cp[strlen (cp) - 1] == '\n')
        cp[strlen (cp) - 1] = '\0';

      tmp = strsep (&cp, " \t=");
      if (cp != NULL)
	while (isspace ((unsigned char) *cp) || *cp == '=')
	  ++cp;

      store (tmp, cp);
    }
  fclose (fp);

  if (buf)
    free (buf);
}

static void
load_defaults (void)
{
  load_defaults_internal ("/etc/default/su");
  load_defaults_internal ("/etc/login.defs");
}

int
getdef_bool (const char *name, int dflt)
{
  const char *val;

  if (list == NULL)
    load_defaults ();

  val = search (name);

  if (val == NULL)
    return dflt;

  return (strcasecmp (val, "yes") == 0);
}

long
getdef_num (const char *name, long dflt)
{
  const char *val;
  char *cp;
  long retval;

  if (list == NULL)
    load_defaults ();

  val = search (name);

  if (val == NULL)
    return dflt;

  errno = 0;
  retval = strtol (val, &cp, 0);
  if (*cp != '\0'
      || ((retval == LONG_MAX || retval == LONG_MIN) && errno == ERANGE))
    {
      fprintf (stderr,
	       "%s contains invalid numerical value: %s!\n",
	       name, val);
      retval = dflt;
    }
  return retval;
}

unsigned long
getdef_unum (const char *name, unsigned long dflt)
{
  const char *val;
  char *cp;
  unsigned long retval;

  if (list == NULL)
    load_defaults ();

  val = search (name);

  if (val == NULL)
    return dflt;

  errno = 0;
  retval = strtoul (val, &cp, 0);
  if (*cp != '\0' || (retval == ULONG_MAX && errno == ERANGE))
    {
      fprintf (stderr,
	       "%s contains invalid numerical value: %s!\n",
	       name, val);
      retval = dflt;
    }
  return retval;
}

const char *
getdef_str (const char *name, const char *dflt)
{
  const char *retval;

  if (list == NULL)
    load_defaults ();

  retval = search (name);

  return retval ?: dflt;
}

#if defined(TEST)

int
main ()
{
  printf ("CYPT=%s\n", getdef_str ("cRypt", "no"));
  printf ("LOG_UNKFAIL_ENAB=%s\n", getdef_str ("log_unkfail_enab",""));
  printf ("DOESNOTEXIST=%s\n", getdef_str ("DOESNOTEXIST","yes"));
  return 0;
}

#endif
