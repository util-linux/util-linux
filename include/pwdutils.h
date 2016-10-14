#ifndef UTIL_LINUX_PWDUTILS_H
#define UTIL_LINUX_PWDUTILS_H

#include <sys/types.h>
#include <pwd.h>

extern struct passwd *xgetpwnam(const char *username, char **pwdbuf);
extern char *xgetlogin(void);

#endif /* UTIL_LINUX_PWDUTILS_H */

