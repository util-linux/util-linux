#ifndef UTIL_LINUX_MANGLE_H
#define UTIL_LINUX_MANGLE_H

/*
 * Functions for \oct encoding used in mtab/fstab/swaps/etc.
 */

extern char *mangle(const char *s);

extern void unmangle_to_buffer(char *s, char *buf, size_t len);
extern char *unmangle(char *s);

#endif /* UTIL_LINUX_MANGLE_H */

