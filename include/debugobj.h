#ifndef UTIL_LINUX_DEBUGOBJ_H
#define UTIL_LINUX_DEBUGOBJ_H

/*
 * Include *after* debug.h and after UL_DEBUG_CURRENT_MASK define.
 */

static inline void __attribute__ ((__format__ (__printf__, 2, 3)))
ul_debugobj(const void *handler, const char *mesg, ...)
{
	va_list ap;

	if (handler && !(UL_DEBUG_CURRENT_MASK & __UL_DEBUG_FL_NOADDR))
		fprintf(stderr, "[%p]: ", handler);

	va_start(ap, mesg);
	vfprintf(stderr, mesg, ap);
	va_end(ap);
	fputc('\n', stderr);
}

#endif /* UTIL_LINUX_DEBUGOBJ_H */
