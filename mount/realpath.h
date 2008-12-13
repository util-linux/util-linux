#ifndef REALPATH_H
#define REALPATH_H

#include <limits.h>
#ifndef PATH_MAX
# define PATH_MAX 4096
#endif

extern char *myrealpath(const char *path, char *resolved_path, int m);
extern char *canonicalize (const char *path);
extern char *canonicalize_spec (const char *path);
extern int is_pseudo_fs(const char *type);

#endif /* REALPATH_H */
