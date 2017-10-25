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

/*
 * Requirements enumerated via testing (V8, Firefox, IE11):
 *
 * var charsToEscape = [];
 * for (var i = 0; i < 65535; i += 1) {
 *	try {
 *		JSON.parse('{"sample": "' + String.fromCodePoint(i) + '"}');
 *	} catch (e) {
 *		charsToEscape.push(i);
 *	}
 * }
 */
static inline void fputs_quoted_case_json(const char *data, FILE *out, int dir)
{
	const char *p;

	fputc('"', out);
	for (p = data; p && *p; p++) {

		const unsigned char c = (unsigned char) *p;

		/* From http://www.json.org
		 *
		 * The double-quote and backslashes would break out a string or
		 * init an escape sequence if not escaped.
		 *
		 * Note that single-quotes and forward slashes, while they're
		 * in the JSON spec, don't break double-quoted strings.
		 */
		if (c == '"' || c == '\\') {
			fputc('\\', out);
			fputc(c, out);
			continue;
		}

		/* All non-control characters OK; do the case swap as required. */
		if (c >= 0x20) {
			fputc(dir ==  1 ? toupper(c) :
			      dir == -1 ? tolower(c) : *p, out);
			continue;
		}

		/* In addition, all chars under ' ' break Node's/V8/Chrome's, and
		 * Firefox's JSON.parse function
		 */
		switch (c) {
			/* Handle short-hand cases to reduce output size.  C
			 * has most of the same stuff here, so if there's an
			 * "Escape for C" function somewhere in the STL, we
			 * should probably be using it.
			 */
			case '\b':
				fputs("\\b", out);
				break;
			case '\t':
				fputs("\\t", out);
				break;
			case '\n':
				fputs("\\n", out);
				break;
			case '\f':
				fputs("\\f", out);
				break;
			case '\r':
				fputs("\\r", out);
				break;
			default:
				/* Other assorted control characters */
				fprintf(out, "\\u00%02x", c);
				break;
		}
	}
	fputc('"', out);
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

#define fputs_quoted_json(_d, _o)       fputs_quoted_case_json(_d, _o, 0)
#define fputs_quoted_json_upper(_d, _o) fputs_quoted_case_json(_d, _o, 1)
#define fputs_quoted_json_lower(_d, _o) fputs_quoted_case_json(_d, _o, -1)

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
