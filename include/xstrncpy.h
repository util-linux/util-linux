/* NUL-terminated version of strncpy() */
#include <string.h>

/* caller guarantees n > 0 */
static inline void xstrncpy(char *dest, const char *src, size_t n) {
	strncpy(dest, src, n-1);
	dest[n-1] = 0;
}
