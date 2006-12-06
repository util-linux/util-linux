#ifndef lint
#ifndef NOID
static char	elsieid[] = "@(#)getopt.c	7.5";
/* Modified from the UCB version with the SCCS ID appearing below. */
#endif /* !defined NOID */
#endif /* !defined lint */

/*LINTLIBRARY*/

/*
 * Copyright (c) 1987 Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that this notice is preserved and that due credit is given
 * to the University of California at Berkeley. The name of the University
 * may not be used to endorse or promote products derived from this
 * software without specific written prior permission. This software
 * is provided ``as is'' without express or implied warranty.
 */

#ifdef LIBC_SCCS
#ifndef lint
static char sccsid[] = "@(#)getopt.c	4.5 (Berkeley) 11/24/87";
#endif /* !defined lint */
#endif /* defined LIBC_SCCS */

#include <stdio.h>

/*
 * get option letter from argument vector
 */
extern int	opterr;		/* if error message should be printed */
extern int	optind; 	/* index into parent argv vector */
extern int	optopt;		/* character checked for validity */
extern char *	optarg;		/* argument associated with option */

#define BADCH	(int)'?'
static char	EMSG[1];
#define tell(s)	{ \
	if (opterr) { \
		(void) fputs(*nargv, stderr); \
		(void) fputs(s, stderr); \
		(void) fputc(optopt, stderr); \
		(void) fputc((int)'\n', stderr); \
	} \
	return(BADCH); \
}

extern char *	strchr();

int
getopt(nargc, nargv, ostr)
	int	nargc;
	char	**nargv, *ostr;
{
	static char	*place = EMSG;		/* option letter processing */
	register char	*oli;			/* option letter list index */

	if (!*place) {				/* update scanning pointer */
		if (optind >= nargc || *(place = nargv[optind]) != '-' ||
			!*++place)
				return(EOF);
		if (*place == '-') {		/* found "--" */
			++optind;
			return(EOF);
		}
	}					/* option letter okay? */
	if ((optopt = (int)*place++) == (int)':' ||
		!(oli = strchr(ostr, optopt))) {
			if (!*place)
				++optind;
			tell(": illegal option -- ");
	}
	if (*++oli != ':') {			/* don't need argument */
		optarg = NULL;
		if (!*place)
			++optind;
	}
	else {					/* need an argument */
		if (*place)			/* no white space */
			optarg = place;
		else if (nargc <= ++optind) {	/* no arg */
			place = EMSG;
			tell(": option requires an argument -- ");
		}
	 	else				/* white space */
			optarg = nargv[optind];
		place = EMSG;
		++optind;
	}
	return(optopt);				/* dump back option letter */
}
