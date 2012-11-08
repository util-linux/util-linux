/*
 * consoles.h	    Header file for routines to detect the system consoles
 *
 * Copyright (c) 2011 SuSE LINUX Products GmbH, All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *  
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program (see the file COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 *
 * Author: Werner Fink <werner@suse.de>
 */

#include <sys/types.h>
#include <stdint.h>
#include <stdio.h>
#include <termios.h>

struct chardata {
	uint8_t	erase;
	uint8_t kill;
	uint8_t eol;
	uint8_t parity;
};
struct console {
	char *tty;
	FILE *file;
	uint32_t flags;
	int fd, id;
#define	CON_SERIAL	0x0001
#define	CON_NOTTY	0x0002
	pid_t pid;
	struct chardata cp;
	struct termios tio;
	struct console *next;
};
extern struct console *consoles;
extern int detect_consoles(const char *, int);
