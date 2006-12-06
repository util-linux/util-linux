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

#ifndef lint
char copyright[] =
"@(#) Copyright (c) 1980 The Regents of the University of California.\n\
 All rights reserved.\n";
#endif /* not lint */

#ifndef lint
static char sccsid[] = "@(#)whereis.c	5.5 (Berkeley) 4/18/91";
#endif /* not lint */

#include <sys/param.h>
#include <sys/dir.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

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
#ifdef __linux__
   "/bin",
   "/usr/bin",
   "/etc",
   "/usr/etc",
   "/sbin",
   "/usr/sbin",
   "/usr/games",
   "/usr/games/bin",
   "/usr/emacs/etc",
   "/usr/lib/emacs/19.22/etc",
   "/usr/lib/emacs/19.23/etc",
   "/usr/lib/emacs/19.24/etc",
   "/usr/lib/emacs/19.25/etc",
   "/usr/lib/emacs/19.26/etc",
   "/usr/lib/emacs/19.27/etc",
   "/usr/lib/emacs/19.28/etc",
   "/usr/lib/emacs/19.29/etc",
   "/usr/lib/emacs/19.30/etc",
   "/usr/lib/emacs/19.31/etc",
   "/usr/lib/emacs/19.32/etc",
   "/usr/TeX/bin",
   "/usr/tex/bin",
   "/usr/interviews/bin/LINUX",
   
   "/usr/bin/X11",
   "/usr/X11/bin",
   "/usr/X11R5/bin",
   "/usr/X11R6/bin",
   "/usr/X386/bin",

   "/usr/local/bin",
   "/usr/local/etc",
   "/usr/local/sbin",
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
#else
	"/bin",
	"/sbin",
	"/usr/ucb",
	"/usr/bin",
	"/usr/sbin",
	"/usr/old",
	"/usr/contrib",
	"/usr/games",
	"/usr/local",
	"/usr/libexec",
	"/usr/include",
	"/usr/hosts",
	"/usr/share", /*?*/
	"/etc",
#ifdef notdef
	/* before reorg */
	"/etc",
	"/bin",
	"/usr/bin",
	"/usr/games",
	"/lib",
	"/usr/ucb",
	"/usr/lib",
	"/usr/local",
	"/usr/new",
	"/usr/old",
	"/usr/hosts",
	"/usr/include",
#endif
#endif
	0
};
/* This needs to be redone - man pages live with sources */
static char *mandirs[] = {
	"/usr/man/man1",
	"/usr/man/man2",
	"/usr/man/man3",
	"/usr/man/man4",
	"/usr/man/man5",
	"/usr/man/man6",
	"/usr/man/man7",
	"/usr/man/man8",
#ifdef __linux__
	"/usr/man/man9",
#endif
	"/usr/man/manl",
	"/usr/man/mann",
	"/usr/man/mano",
#ifdef __linux__
	"/usr/X386/man/man1",
	"/usr/X386/man/man2",
	"/usr/X386/man/man3",
	"/usr/X386/man/man4",
	"/usr/X386/man/man5",
	"/usr/X386/man/man6",
	"/usr/X386/man/man7",
	"/usr/X386/man/man8",
	"/usr/X11/man/man1",
	"/usr/X11/man/man2",
	"/usr/X11/man/man3",
	"/usr/X11/man/man4",
	"/usr/X11/man/man5",
	"/usr/X11/man/man6",
	"/usr/X11/man/man7",
	"/usr/X11/man/man8",
	"/usr/TeX/man/man1",
	"/usr/TeX/man/man2",
	"/usr/TeX/man/man3",
	"/usr/TeX/man/man4",
	"/usr/TeX/man/man5",
	"/usr/TeX/man/man6",
	"/usr/TeX/man/man7",
	"/usr/TeX/man/man8",
	"/usr/interviews/man/mann",
#endif
	0
};
static char *srcdirs[]  = {
	"/usr/src/bin",
	"/usr/src/sbin",
	"/usr/src/etc",
	"/usr/src/pgrm",
	"/usr/src/usr.bin",
	"/usr/src/usr.sbin",
	"/usr/src/usr.ucb",
	"/usr/src/usr.new",
	"/usr/src/usr.lib",
	"/usr/src/libexec",
	"/usr/src/libdata",
	"/usr/src/share",
	"/usr/src/contrib",
	"/usr/src/athena",
	"/usr/src/devel",
	"/usr/src/games",
	"/usr/src/local",
	"/usr/src/man",
	"/usr/src/root",
	"/usr/src/old",
	"/usr/src/include",
	/* still need libs */
#ifdef notdef /* before reorg */
	"/usr/src/bin",
	"/usr/src/usr.bin",
	"/usr/src/etc",
	"/usr/src/ucb",
	"/usr/src/games",
	"/usr/src/usr.lib",
	"/usr/src/lib",
	"/usr/src/local",
	"/usr/src/new",
	"/usr/src/old",
	"/usr/src/include",
	"/usr/src/lib/libc/gen",
	"/usr/src/lib/libc/stdio",
	"/usr/src/lib/libc/sys",
	"/usr/src/lib/libc/net/common",
	"/usr/src/lib/libc/net/inet",
	"/usr/src/lib/libc/net/misc",
	"/usr/src/ucb/pascal",
	"/usr/src/ucb/pascal/utilities",
	"/usr/src/undoc",
#endif
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
main(argc, argv)
	int argc;
	char *argv[];
{

	argc--, argv++;
	if (argc == 0) {
usage:
		fprintf(stderr, "whereis [ -sbmu ] [ -SBM dir ... -f ] name...\n");
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
getlist(argcp, argvp, flagp, cntp)
	char ***argvp;
	int *argcp;
	char ***flagp;
	int *cntp;
{

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
lookup(cp)
	register char *cp;
{
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
looksrc(cp)
	char *cp;
{
	if (Sflag == 0) {
		find(srcdirs, cp);
	} else
		findv(Sflag, Scnt, cp);
}

void
lookbin(cp)
	char *cp;
{
	if (Bflag == 0)
		find(bindirs, cp);
	else
		findv(Bflag, Bcnt, cp);
}

void
lookman(cp)
	char *cp;
{
	if (Mflag == 0) {
		find(mandirs, cp);
	} else
		findv(Mflag, Mcnt, cp);
}

void
findv(dirv, dirc, cp)
	char **dirv;
	int dirc;
	char *cp;
{

	while (dirc > 0)
		findin(*dirv++, cp), dirc--;
}

void
find(dirs, cp)
	char **dirs;
	char *cp;
{

	while (*dirs)
		findin(*dirs++, cp);
}

void
findin(dir, cp)
	char *dir, *cp;
{
	DIR *dirp;
	struct direct *dp;

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
itsit(cp, dp)
	register char *cp, *dp;
{
	register int i = strlen(dp);

	if (dp[0] == 's' && dp[1] == '.' && itsit(cp, dp+2))
		return (1);
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
