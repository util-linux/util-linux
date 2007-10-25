#ifndef MOUNT_XMALLOC_H
#define MOUNT_XMALLOC_H

extern void *xmalloc(size_t size);
extern void *xrealloc(void *p, size_t size);
extern char *xstrdup(const char *s);

#endif  /* MOUNT_XMALLOC_H */
