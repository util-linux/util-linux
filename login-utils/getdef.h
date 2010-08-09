/* Copyright (C) 2003, 2005 Thorsten Kukuk
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

#ifndef _GETDEF_H_

#define _GETDEF_H_ 1

extern int getdef_bool (const char *name, int dflt);
extern long getdef_num (const char *name, long dflt);
extern unsigned long getdef_unum (const char *name, unsigned long dflt);
extern const char *getdef_str (const char *name, const char *dflt);

/* Free all data allocated by getdef_* calls before.  */
extern void free_getdef_data (void);

#endif /* _GETDEF_H_ */
