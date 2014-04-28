#ifndef UTIL_LINUX_CAREFUULPUTC_H
#define UTIL_LINUX_CAREFUULPUTC_H

/*
 * A putc() for use in write and wall (that sometimes are sgid tty).
 * It avoids control characters in our locale, and also ASCII control
 * characters.   Note that the locale of the recipient is unknown.
*/
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#define iso8859x_iscntrl(c) \
	(((c) & 0x7f) < 0x20 || (c) == 0x7f)

static inline int fputc_careful(int c, FILE *fp, const char fail) {
	int ret;

	if (c == '\007' || c == '\t' || c == '\r' || c == '\n' ||
	    (!iso8859x_iscntrl(c) && (isprint(c) || isspace(c))))
		ret = putc(c, fp);
	else if ((c & 0x80) || !isprint(c^0x40))
		ret = fprintf(fp, "\\%3o", (unsigned char) c);
	else {
		ret = putc(fail, fp);
		if (ret != EOF)
			ret = putc(c^0x40, fp);
	}
	return (ret < 0) ? EOF : 0;
}


static inline void fputs_quoted(const char *data, FILE *out)
{
	const char *p;

	fputc('"', out);
	for (p = data; p && *p; p++) {
		if ((unsigned char) *p == 0x22 ||		/* " */
		    (unsigned char) *p == 0x5c ||		/* \ */
		    !isprint((unsigned char) *p) ||
		    iscntrl((unsigned char) *p)) {

			fprintf(out, "\\x%02x", (unsigned char) *p);
		} else
			fputc(*p, out);
	}
	fputc('"', out);
}

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


#endif  /*  _CAREFUULPUTC_H  */
