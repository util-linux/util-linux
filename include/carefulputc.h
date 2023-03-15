#ifndef UTIL_LINUX_CAREFULPUTC_H
#define UTIL_LINUX_CAREFULPUTC_H

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#ifdef HAVE_WIDECHAR
#include <wctype.h>
#endif
#include <stdbool.h>

#include "cctype.h"

/*
 * A puts() for use in write and wall (that sometimes are sgid tty).
 * It avoids control and invalid characters.
 * The locale of the recipient is nominally unknown,
 * but it's a solid bet that the encoding is compatible with the author's.
 */
static inline int fputs_careful(const char * s, FILE *fp, const char ctrl, bool cr_lf)
{
	int ret = 0;

	for (size_t slen = strlen(s); *s; ++s, --slen) {
		if (*s == '\n')
			ret = fputs(cr_lf ? "\r\n" : "\n", fp);
		else if (isprint(*s) || *s == '\a' || *s == '\t' || *s == '\r')
			ret = putc(*s, fp);
		else if (!c_isascii(*s)) {
#ifdef HAVE_WIDECHAR
			wchar_t w;
			size_t clen = mbtowc(&w, s, slen);
			switch(clen) {
				case (size_t)-2:  // incomplete
				case (size_t)-1:  // EILSEQ
					mbtowc(NULL, NULL, 0);
				nonprint:
					ret = fprintf(fp, "\\%3hho", *s);
					break;
				default:
					if(!iswprint(w))
						goto nonprint;
					ret = fwrite(s, 1, clen, fp);
					s += clen - 1;
					slen -= clen - 1;
					break;
			}
#else
			ret = fprintf(fp, "\\%3hho", *s);
#endif
		} else
			ret = fputs((char[]){ ctrl, *s ^ 0x40, '\0' }, fp);
		if (ret < 0)
			return EOF;
	}
	return 0;
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
