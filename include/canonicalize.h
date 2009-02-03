#ifndef CANONICALIZE_H
#define CANONICALIZE_H

#include <limits.h>
#ifndef PATH_MAX
# define PATH_MAX 4096
#endif

extern char *canonicalize_path(const char *path);

#endif /* CANONICALIZE_H */
