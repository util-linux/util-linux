#include <sys/types.h>
#include <stdarg.h>

extern void *xmalloc(size_t size);
extern void *xrealloc(void *p, size_t size);
extern char *xstrdup(const char *s);
extern void die(int err, const char *fmt, ...);
extern void (*at_die)(void);
