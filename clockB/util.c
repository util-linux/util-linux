/**************************************************************************

  This is a component of the hwclock program.

  This file contains the code for various basic utility routines
  needed by the other modules.

****************************************************************************/

#include <stdio.h>
#include <string.h>

#include "hwclock.h"

bool
is_in_cpuinfo(const char * const fmt, const char * const str) {
/*----------------------------------------------------------------------------
  Return true iff the /proc/cpuinfo file shows the value 'str' for the
  keyword 'fmt'.  Both arguments are null-terminated strings.

  If for any reason we can't read /proc/cpuinfo, return false.
-----------------------------------------------------------------------------*/
  FILE *cpuinfo;
  char field[256];
  char format[256];
  bool found;

  sprintf(format, "%s : %s", fmt, "%255s");

  found = FALSE;  /* initial value */
  if ((cpuinfo = fopen ("/proc/cpuinfo", "r")) != NULL) {
    while (!feof(cpuinfo)) {
      if (fscanf (cpuinfo, format, field) == 1) {
        if (strncmp(field, str, strlen(str)) == 0)
          found = TRUE;
        break;
      }
      fgets (field, 256, cpuinfo);
    }
    fclose(cpuinfo);
  }
  return found;
}



char *
ctime2(const time_t time) {
/*----------------------------------------------------------------------------
  Same as ctime() from the standard C library, except it takes a time_t
  as an argument instead of a pointer to a time_t, so it is much more
  useful.

  Also, don't include a newline at the end of the returned string.  If
  the user wants a newline, he can provide it himself.
  
  return value is in static storage within.
-----------------------------------------------------------------------------*/
  static char retval[30];

  strncpy(retval, ctime(&time), sizeof(retval));
  retval[sizeof(retval)-1] = '\0';

  /* Now chop off the last character, which is the newline */
  if (strlen(retval) >= 1)   /* for robustness */
    retval[strlen(retval)-1] = '\0';
  return(retval);

}



struct timeval
t2tv(time_t argument) {
/*----------------------------------------------------------------------------
   Convert from "time_t" format to "timeval" format.
-----------------------------------------------------------------------------*/
  struct timeval retval;

  retval.tv_sec = argument;
  retval.tv_usec = 0;
  return(retval);
}



float 
time_diff(struct timeval subtrahend, struct timeval subtractor) {
/*---------------------------------------------------------------------------
  The difference in seconds between two times in "timeval" format.
----------------------------------------------------------------------------*/
  return( (subtrahend.tv_sec - subtractor.tv_sec)
           + (subtrahend.tv_usec - subtractor.tv_usec) / 1E6 );
}


struct timeval
time_inc(struct timeval addend, float increment) {
/*----------------------------------------------------------------------------
  The time, in "timeval" format, which is <increment> seconds after
  the time <addend>.  Of course, <increment> may be negative.
-----------------------------------------------------------------------------*/
  struct timeval newtime;

  newtime.tv_sec = addend.tv_sec + (int) increment;
  newtime.tv_usec = addend.tv_usec + (increment - (int) increment) * 1E6;

  /* Now adjust it so that the microsecond value is between 0 and 1 million */
  if (newtime.tv_usec < 0) {
    newtime.tv_usec += 1E6;
    newtime.tv_sec -= 1;
  } else if (newtime.tv_usec >= 1E6) {
    newtime.tv_usec -= 1E6;
    newtime.tv_sec += 1;
  }
  return(newtime);
}



