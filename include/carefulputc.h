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

static inline int fputc_careful(int c, FILE *fp, const char fail)
{
	int ret;

	if (isprint(c) || c == '\a' || c == '\t' || c == '\r' || c == '\n')
		ret = putc(c, fp);
	else if (!isascii(c))
		ret = fprintf(fp, "\\%3o", (unsigned char)c);
	else {
		ret = putc(fail, fp);
		if (ret != EOF)
			ret = putc(c ^ 0x40, fp);
	}
	return (ret < 0) ? EOF : 0;
}

static inline void fputs_quoted_case(const char *data, FILE *out, int dir, int json)
{
	const char *p;

	fputc('"', out);
	for (p = data; p && *p; p++) {
		if (json == 0) {
			if ((unsigned char) *p == 0x22 ||		/* " */
				(unsigned char) *p == 0x5c ||		/* \ */
				(unsigned char) *p == 0x60 ||		/* ` */
				(unsigned char) *p == 0x24)			/* $ */
			{
				fprintf(out, "\\x%02x", (unsigned char) *p);
			} else {
				fputc(dir ==  1 ? toupper(*p) :
					  dir == -1 ? tolower(*p) :
					  *p, out);
			}
		} else {
			unsigned char c = (unsigned char) *p;
			// From http://www.json.org
			if (((unsigned char) *p) < 0x20) {
				if (c == '"' || c == '\\') { fputc('\\', out); fputc(c, out); }
				else if (c == '\r') { fputs("\\r", out); }
				else if (c == '\n') { fputs("\\n", out); }
				else if (c == '\t') { fputs("\\t", out); }
				else if (c == '\v') { fputs("\\v", out); }
				else if (c == '\b') { fputs("\\b", out); }
				else if (c == '\f') { fputs("\\f", out); }
				// single-quotes don't break double-quoted strings; readability better without escapes.
				//else if (c == '\'') { fputs("\\'", out); }
				// forward-slashes don't break double-quoted strings; readability better without escapes.
				//else if (c == '/') { fputs("\\/", out); }
				else {
					// Other assorted control characters
					fprintf(out, "\\u00%02x", (unsigned char) *p);
				}
			} else {
				// All other characters OK
				fputc(dir ==  1 ? toupper(*p) :
					  dir == -1 ? tolower(*p) :
					  *p, out);
			}
		}
	}
	fputc('"', out);
}

#define fputs_quoted(_d, _o)		fputs_quoted_case(_d, _o, 0, 0)
#define fputs_quoted_json(_d, _o)		fputs_quoted_case(_d, _o, 0, 1)
#define fputs_quoted_upper(_d, _o)	fputs_quoted_case(_d, _o, 1, 0)
#define fputs_quoted_lower(_d, _o)	fputs_quoted_case(_d, _o, -1, 0)
#define fputs_quoted_lower_json(_d, _o)	fputs_quoted_case(_d, _o, -1, 1)

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
