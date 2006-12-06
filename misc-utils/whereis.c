/*-
 * Copyright (c) 1980 The Regents of the University of California.
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

/* *:aeb */

/* 1999-02-22 Arkadiusz Mi¶kiewicz <misiek@pld.ORG.PL>
 * - added Native Language Support
 */

#include <sys/param.h>
#include <sys/dir.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "nls.h"

void zerof(void);
void getlist(int *, char ***, char ***, int *);
void lookup(char *);
void looksrc(char *);
void lookbin(char *);
void lookman(char *);
void findv(char **, int, char *);
void find(char **, char *);
void findin(char *, char *);
int itsit(char *, char *);

static char *bindirs[] = {
   "/bin",
   "/usr/bin",
   "/sbin",
   "/usr/sbin",
   "/etc",
   "/usr/etc",
   "/lib",
   "/usr/lib",
   "/usr/games",
   "/usr/games/bin",
   "/usr/games/lib",
   "/usr/emacs/etc",
   "/usr/lib/emacs/*/etc",
   "/usr/TeX/bin",
   "/usr/tex/bin",
   "/usr/interviews/bin/LINUX",
   
   "/usr/X11R6/bin",
   "/usr/X386/bin",
   "/usr/bin/X11",
   "/usr/X11/bin",
   "/usr/X11R5/bin",

   "/usr/local/bin",
   "/usr/local/sbin",
   "/usr/local/etc",
   "/usr/local/lib",
   "/usr/local/games",
   "/usr/local/games/bin",
   "/usr/local/emacs/etc",
   "/usr/local/TeX/bin",
   "/usr/local/tex/bin",
   "/usr/local/bin/X11",

   "/usr/contrib",
   "/usr/hosts",
   "/usr/include",

   "/usr/g++-include",

   "/usr/ucb",
   "/usr/old",
   "/usr/new",
   "/usr/local",
   "/usr/libexec",
   "/usr/share",

   "/opt/*/bin",

	0
};

static char *mandirs[] = {
	"/usr/man/*",
	"/usr/share/man/*",
	"/usr/X386/man/*",
	"/usr/X11/man/*",
	"/usr/TeX/man/*",
	"/usr/interviews/man/mann",
	0
};

static char *srcdirs[]  = {
	"/usr/src/*",
	"/usr/src/lib/libc/*",
	"/usr/src/lib/libc/net/*",
	"/usr/src/ucb/pascal",
	"/usr/src/ucb/pascal/utilities",
	"/usr/src/undoc",
	0
};

char	sflag = 1;
char	bflag = 1;
char	mflag = 1;
char	**Sflag;
int	Scnt;
char	**Bflag;
int	Bcnt;
char	**Mflag;
int	Mcnt;
char	uflag;
/*
 * whereis name
 * look for source, documentation and binaries
 */
int
main(int argc, char **argv) {
	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	

	argc--, argv++;
	if (argc == 0) {
usage:
		fprintf(stderr, _("whereis [ -sbmu ] [ -SBM dir ... -f ] name...\n"));
		exit(1);
	}
	do
		if (argv[0][0] == '-') {
			register char *cp = argv[0] + 1;
			while (*cp) switch (*cp++) {

			case 'f':
				break;

			case 'S':
				getlist(&argc, &argv, &Sflag, &Scnt);
				break;

			case 'B':
				getlist(&argc, &argv, &Bflag, &Bcnt);
				break;

			case 'M':
				getlist(&argc, &argv, &Mflag, &Mcnt);
				break;

			case 's':
				zerof();
				sflag++;
				continue;

			case 'u':
				uflag++;
				continue;

			case 'b':
				zerof();
				bflag++;
				continue;

			case 'm':
				zerof();
				mflag++;
				continue;

			default:
				goto usage;
			}
			argv++;
		} else
			lookup(*argv++);
	while (--argc > 0);
	return 0;
}

void
getlist(int *argcp, char ***argvp, char ***flagp, int *cntp) {
	(*argvp)++;
	*flagp = *argvp;
	*cntp = 0;
	for ((*argcp)--; *argcp > 0 && (*argvp)[0][0] != '-'; (*argcp)--)
		(*cntp)++, (*argvp)++;
	(*argcp)++;
	(*argvp)--;
}


void
zerof()
{
	if (sflag && bflag && mflag)
		sflag = bflag = mflag = 0;
}

int	count;
int	print;

void
lookup(char *cp) {
	register char *dp;

	for (dp = cp; *dp; dp++)
		continue;
	for (; dp > cp; dp--) {
		if (*dp == '.') {
			*dp = 0;
			break;
		}
	}
	for (dp = cp; *dp; dp++)
		if (*dp == '/')
			cp = dp + 1;
	if (uflag) {
		print = 0;
		count = 0;
	} else
		print = 1;
again:
	if (print)
		printf("%s:", cp);
	if (sflag) {
		looksrc(cp);
		if (uflag && print == 0 && count != 1) {
			print = 1;
			goto again;
		}
	}
	count = 0;
	if (bflag) {
		lookbin(cp);
		if (uflag && print == 0 && count != 1) {
			print = 1;
			goto again;
		}
	}
	count = 0;
	if (mflag) {
		lookman(cp);
		if (uflag && print == 0 && count != 1) {
			print = 1;
			goto again;
		}
	}
	if (print)
		printf("\n");
}

void
looksrc(char *cp) {
	if (Sflag == 0) {
		find(srcdirs, cp);
	} else
		findv(Sflag, Scnt, cp);
}

void
lookbin(char *cp) {
	if (Bflag == 0)
		find(bindirs, cp);
	else
		findv(Bflag, Bcnt, cp);
}

void
lookman(char *cp) {
	if (Mflag == 0) {
		find(mandirs, cp);
	} else
		findv(Mflag, Mcnt, cp);
}

void
findv(char **dirv, int dirc, char *cp) {
	while (dirc > 0)
		findin(*dirv++, cp), dirc--;
}

void
find(char **dirs, char *cp) {
	while (*dirs)
		findin(*dirs++, cp);
}

void
findin(char *dir, char *cp) {
	DIR *dirp;
	struct direct *dp;
	char *d, *dd;
	int l;
	char dirbuf[1024];
	struct stat statbuf;

	dd = index(dir, '*');
	if (!dd)
		goto noglob;

	l = strlen(dir);
	if (l < sizeof(dirbuf)) { 	/* refuse excessively long names */
		strcpy (dirbuf, dir);
		d = index(dirbuf, '*');
		*d = 0;
		dirp = opendir(dirbuf);
		if (dirp == NULL)
			return;
		while ((dp = readdir(dirp)) != NULL) {
			if (!strcmp(dp->d_name, ".") ||
			    !strcmp(dp->d_name, ".."))
				continue;
			if (strlen(dp->d_name) + l > sizeof(dirbuf))
				continue;
			sprintf(d, "%s", dp->d_name);
			if (stat(dirbuf, &statbuf))
				continue;
			if (!S_ISDIR(statbuf.st_mode))
				continue;
			strcat(d, dd+1);
			findin(dirbuf, cp);
		}
		closedir(dirp);
	}
	return;

    noglob:
	dirp = opendir(dir);
	if (dirp == NULL)
		return;
	while ((dp = readdir(dirp)) != NULL) {
		if (itsit(cp, dp->d_name)) {
			count++;
			if (print)
				printf(" %s/%s", dir, dp->d_name);
		}
	}
	closedir(dirp);
}

int
itsit(char *cp, char *dp) {
	int i = strlen(dp);

	if (dp[0] == 's' && dp[1] == '.' && itsit(cp, dp+2))
		return (1);
	if (!strcmp(dp+i-2, ".Z"))
		i -= 2;
	else if (!strcmp(dp+i-3, ".gz"))
		i -= 3;
	else if (!strcmp(dp+i-4, ".bz2"))
		i -= 4;
	while (*cp && *dp && *cp == *dp)
		cp++, dp++, i--;
	if (*cp == 0 && *dp == 0)
		return (1);
	while (isdigit(*dp))
		dp++;
	if (*cp == 0 && *dp++ == '.') {
		--i;
		while (i > 0 && *dp)
			if (--i, *dp++ == '.')
				return (*dp++ == 'C' && *dp++ == 0);
		return (1);
	}
	return (0);
}
