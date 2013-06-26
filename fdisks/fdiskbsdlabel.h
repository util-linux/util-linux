#ifndef FDISK_BSD_LABEL_H
#define FDISK_BSD_LABEL_H

/*
 * Copyright (c) 1987, 1988 Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <stdint.h>
#include "pt-bsd.h"

#ifdef DKTYPENAMES
static char *xbsd_dktypenames[] = {
	"unknown",
	"SMD",
	"MSCP",
	"old DEC",
	"SCSI",
	"ESDI",
	"ST506",
	"HP-IB",
	"HP-FL",
	"type 9",
	"floppy",
	0
};
#define BSD_DKMAXTYPES	(sizeof(xbsd_dktypenames) / sizeof(xbsd_dktypenames[0]) - 1)
#endif

#ifdef	DKTYPENAMES
static struct fdisk_parttype xbsd_fstypes[] = {
        {BSD_FS_UNUSED, "unused"},
	{BSD_FS_SWAP,   "swap"},
	{BSD_FS_V6,     "Version 6"},
	{BSD_FS_V7,     "Version 7"},
	{BSD_FS_SYSV,   "System V"},
	{BSD_FS_V71K,   "4.1BSD"},
	{BSD_FS_V8,     "Eighth Edition"},
	{BSD_FS_BSDFFS, "4.2BSD"},
#ifdef __alpha__
	{BSD_FS_EXT2,   "ext2"},
#else
	{BSD_FS_MSDOS,  "MS-DOS"},
#endif
	{BSD_FS_BSDLFS, "4.4LFS"},
	{BSD_FS_OTHER,  "unknown"},
	{BSD_FS_HPFS,   "HPFS"},
	{BSD_FS_ISO9660,"ISO-9660"},
	{BSD_FS_BOOT,   "boot"},
	{BSD_FS_ADOS,   "ADOS"},
	{BSD_FS_HFS,    "HFS"},
	{BSD_FS_ADVFS,	"AdvFS"},
	{ 0, NULL }
};
#define BSD_FSMAXTYPES (ARRAY_SIZE(xbsd_fstypes)-1)

#endif


extern void bsd_command_prompt(struct fdisk_context *cxt);
extern int btrydev(struct fdisk_context *cxt);
extern void xbsd_print_disklabel(struct fdisk_context *cxt, int);

#endif /* FDISK_BSD_LABEL_H */
