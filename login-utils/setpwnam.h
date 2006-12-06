/*
 *  setpwnam.h --
 *  define several paths
 *
 *  (c) 1994 Martin Schulze <joey@infodrom.north.de>
 *  This file is based on setpwnam.c which is
 *  (c) 1994 Salvatore Valente <svalente@mit.edu>
 *
 *  This file is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public License as
 *  published by the Free Software Foundation; either version 2 of the
 *  License, or (at your option) any later version.
 */

#include "pathnames.h"

#define false 0
#define true 1

typedef int boolean;

#ifndef DEBUG
#define PASSWD_FILE    _PATH_PASSWD
#define PTMP_FILE      _PATH_PTMP
#define PTMPTMP_FILE   _PATH_PTMPTMP

#define GROUP_FILE     _PATH_GROUP
#define GTMP_FILE      _PATH_GTMP
#define GTMPTMP_FILE   _PATH_GTMPTMP
#else
#define PASSWD_FILE    "/tmp/passwd"
#define PTMP_FILE      "/tmp/ptmp"
#define PTMPTMP_FILE   "/tmp/ptmptmp"

#define GROUP_FILE     "/tmp/group"
#define GTMP_FILE      "/tmp/gtmp"
#define GTMPTMP_FILE   "/tmp/gtmptmp"
#endif
