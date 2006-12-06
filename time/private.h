#ifndef PRIVATE_H

#define PRIVATE_H

/*
** This header is for use ONLY with the time conversion code.
** There is no guarantee that it will remain unchanged,
** or that it will remain at all.
** Do NOT copy it to any system include directory.
** Thank you!
*/

/*
** ID
*/

#ifndef lint
#ifndef NOID
static char	privatehid[] = "@(#)private.h	7.10";
#endif /* !defined NOID */
#endif /* !defined lint */

/*
** const
*/

#ifndef const
#ifndef __STDC__
#define const
#endif /* !defined __STDC__ */
#endif /* !defined const */

/*
** void
*/

#ifndef void
#ifndef __STDC__
#ifndef vax
#ifndef sun
#define void	char
#endif /* !defined sun */
#endif /* !defined vax */
#endif /* !defined __STDC__ */
#endif /* !defined void */

/*
** INITIALIZE
*/

#ifndef GNUC_or_lint
#ifdef lint
#define GNUC_or_lint
#endif /* defined lint */
#ifdef __GNUC__
#define GNUC_or_lint
#endif /* defined __GNUC__ */
#endif /* !defined GNUC_or_lint */

#ifndef INITIALIZE
#ifdef GNUC_or_lint
#define INITIALIZE(x)	((x) = 0)
#endif /* defined GNUC_or_lint */
#ifndef GNUC_or_lint
#define INITIALIZE(x)
#endif /* !defined GNUC_or_lint */
#endif /* !defined INITIALIZE */

/*
** P((args))
*/

#ifndef P
#ifdef __STDC__
#define P(x)	x
#endif /* defined __STDC__ */
#ifndef __STDC__
#define P(x)	()
#endif /* !defined __STDC__ */
#endif /* !defined P */

/*
** genericptr_T
*/

#ifdef __STDC__
typedef void *		genericptr_T;
#endif /* defined __STDC__ */
#ifndef __STDC__
typedef char *		genericptr_T;
#endif /* !defined __STDC__ */

#include "sys/types.h"	/* for time_t */
#include "stdio.h"
#include "ctype.h"
#include "errno.h"
#include "string.h"
#include "limits.h"	/* for CHAR_BIT */
#ifndef _TIME_
#include "time.h"
#endif /* !defined _TIME_ */

#ifndef remove
extern int	unlink P((const char * filename));
#define remove	unlink
#endif /* !defined remove */

#ifndef FILENAME_MAX

#ifndef MAXPATHLEN
#ifdef unix
#include "sys/param.h"
#endif /* defined unix */
#endif /* !defined MAXPATHLEN */

#ifdef MAXPATHLEN
#define FILENAME_MAX	MAXPATHLEN
#endif /* defined MAXPATHLEN */
#ifndef MAXPATHLEN
#define FILENAME_MAX	1024		/* Pure guesswork */
#endif /* !defined MAXPATHLEN */

#endif /* !defined FILENAME_MAX */

#ifndef EXIT_SUCCESS
#define EXIT_SUCCESS	0
#endif /* !defined EXIT_SUCCESS */

#ifndef EXIT_FAILURE
#define EXIT_FAILURE	1
#endif /* !defined EXIT_FAILURE */

#ifdef __STDC__

#define alloc_size_T	size_t
#define qsort_size_T	size_t
#define fwrite_size_T	size_t

#endif /* defined __STDC__ */
#ifndef __STDC__

#ifndef alloc_size_T
#define alloc_size_T	unsigned
#endif /* !defined alloc_size_T */

#ifndef qsort_size_T
#ifdef USG
#define qsort_size_T	unsigned
#endif /* defined USG */
#ifndef USG
#define qsort_size_T	int
#endif /* !defined USG */
#endif /* !defined qsort_size_T */

#ifndef fwrite_size_T
#define fwrite_size_T	int
#endif /* !defined fwrite_size_T */

#ifndef USG
extern char *		sprintf P((char * buf, const char * format, ...));
#endif /* !defined USG */

#endif /* !defined __STDC__ */

/*
** Ensure that these are declared--redundantly declaring them shouldn't hurt.
*/

extern char *		getenv P((const char * name));
extern genericptr_T	malloc P((alloc_size_T size));
extern genericptr_T	calloc P((alloc_size_T nelem, alloc_size_T elsize));
extern genericptr_T	realloc P((genericptr_T oldptr, alloc_size_T newsize));

#ifdef USG
extern void		exit P((int s));
extern void		qsort P((genericptr_T base, qsort_size_T nelem,
				qsort_size_T elsize, int (*comp)()));
extern void		perror P((const char * string));
extern void		free P((char * buf));
#endif /* defined USG */

#ifndef TRUE
#define TRUE	1
#endif /* !defined TRUE */

#ifndef FALSE
#define FALSE	0
#endif /* !defined FALSE */

#ifndef INT_STRLEN_MAXIMUM
/*
** 302 / 1000 is log10(2.0) rounded up.
** Subtract one for the sign bit;
** add one for integer division truncation;
** add one more for a minus sign.
*/
#define INT_STRLEN_MAXIMUM(type) \
	((sizeof(type) * CHAR_BIT - 1) * 302 / 1000 + 2)
#endif /* !defined INT_STRLEN_MAXIMUM */

#ifndef LOCALE_HOME
#define LOCALE_HOME	"/usr/lib/locale"
#endif /* !defined LOCALE_HOME */

/*
** UNIX was a registered trademark of UNIX System Laboratories in 1993.
** VAX is a trademark of Digital Equipment Corporation.
*/

#endif /* !defined PRIVATE_H */
