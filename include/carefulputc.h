#ifndef UTIL_LINUX_CAREFULPUTC_H
#define UTIL_LINUX_CAREFULPUTC_H

/*
 * A putc() for use in write and wall (that sometimes are sgid tty).
 * It avoids control characters in our locale, and also ASCII control
 * characters.   Note that the locale of the recipient is unknown.
*/
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "cctype.h"

static inline int fputc_careful(int c, FILE *fp, const char fail)
{
	int ret;

	if (isprint(c) || c == '\a' || c == '\t' || c == '\r' || c == '\n')
		ret = putc(c, fp);
	else if (!c_isascii(c))
		ret = fprintf(fp, "\\%3o", (unsigned char)c);
	else {
		ret = putc(fail, fp);
		if (ret != EOF)
			ret = putc(c ^ 0x40, fp);
	}
	return (ret < 0) ? EOF : 0;
}

static inline void fputs_quoted_case(const char *data, FILE *out, int dir)
{
	const char *p;

	fputc('"', out);
	for (p = data; p && *p; p++) {
		if ((unsigned char) *p == 0x22 ||		/* " */
		    (unsigned char) *p == 0x5c ||		/* \ */
		    (unsigned char) *p == 0x60 ||		/* ` */
		    (unsigned char) *p == 0x24 ||		/* $ */
		    !isprint((unsigned char) *p) ||
		    iscntrl((unsigned char) *p)) {

			fprintf(out, "\\x%02x", (unsigned char) *p);
		} else
			fputc(dir ==  1 ? toupper(*p) :
			      dir == -1 ? tolower(*p) :
			      *p, out);
	}
	fputc('"', out);
}

#define fputs_quoted(_d, _o)		fputs_quoted_case(_d, _o, 0)
#define fputs_quoted_upper(_d, _o)	fputs_quoted_case(_d, _o, 1)
#define fputs_quoted_lower(_d, _o)	fputs_quoted_case(_d, _o, -1)

static inline void fputs_nonblank(const char *data, FILE *out)
{
	const char *p;

	for (p = data; p && *p; p++) {
		if (isblank((unsigned char) *p) ||
		    (unsigned char) *p == 0x5c ||		/* \ */
		    !isprint((unsigned char) *p) ||
		    iscntrl((unsigned char) *p)) {

			fprintf(out, "\\x%02x", (unsigned char) *p);

		} else
			fputc(*p, out);
	}
}

#endif  /*  _CAREFULPUTC_H  */
