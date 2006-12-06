#ifndef lint
#ifndef NOID
static char	elsieid[] = "@(#)optind.c	7.3";
#endif /* !defined NOID */
#endif /* !defined lint */

int	opterr = 1,		/* if error message should be printed */
	optind = 1;		/* index into parent argv vector */
char	*optarg;		/* argument associated with option */
int     optopt;
