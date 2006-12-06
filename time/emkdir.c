#ifndef lint
#ifndef NOID
static char	elsieid[] = "@(#)emkdir.c	8.23";
#endif /* !defined NOID */
#endif /* !defined lint */

#ifndef emkdir

/*LINTLIBRARY*/

#include "private.h"

extern char *	imalloc P((int n));
extern void	ifree P((char * p));

static char *
quoted(name)
register const char *	name;
{
	register char *	result;
	register char *	cp;
	register int	c;

	if (name == NULL)
		name = "";
	result = imalloc((int) (4 * strlen(name) + 3));
	if (result == NULL)
		return NULL;
	cp = result;
#ifdef unix
	*cp++ = '\'';
	while ((c = *name++) != '\0')
		if (c == '\'') {
			*cp++ = c;
			*cp++ = '\\';
			*cp++ = c;
			*cp++ = c;
		} else	*cp++ = c;
	*cp++ = '\'';
#endif /* defined unix */
#ifndef unix
	while ((c = *name++) != '\0')
		if (c == '/')
			*cp++ = '\\';
		else	*cp++ = c;
#endif /* !defined unix */
	*cp = '\0';
	return result;
}

int
emkdir(name, mode)
const char *	name;
const int	mode;
{
	register int		result;
	register const char *	format;
	register char *		command;
	register char *		qname;

	if ((qname = quoted(name)) == NULL)
		return -1;
#ifdef unix
	format = "mkdir 2>&- %s && chmod 2>&- %o %s";
#endif /* defined unix */
#ifndef unix
	format = "mkdir %s";
#endif /* !defined unix */
	command = imalloc((int) (strlen(format) + 2 * strlen(qname) + 20 + 1));
	if (command == NULL) {
		ifree(qname);
		return -1;
	}
	(void) sprintf(command, format, qname, mode, qname);
	ifree(qname);
	result = system(command);
	ifree(command);
	return (result == 0) ? 0 : -1;
}

/*
** UNIX was a registered trademark of UNIX System Laboratories in 1993.
*/

#endif /* !defined emkdir */
