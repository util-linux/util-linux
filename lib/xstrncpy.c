/* NUL-terminated version of strncpy() */
#include <string.h>
#include "xstrncpy.h"

void
xstrncpy(char *dest, const char *src, size_t n) {
	strncpy(dest, src, n-1);
	dest[n-1] = 0;
}
