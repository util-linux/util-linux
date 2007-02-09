#ifndef REALPATH_H
#define REALPATH_H

#include <limits.h>
#ifndef PATH_MAX
# define PATH_MAX 4096
#endif

extern char *myrealpath(const char *path, char *resolved_path, int m);

#endif /* REALPATH_H */
