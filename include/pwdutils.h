#ifndef UTIL_LINUX_PWDUTILS_H
#define UTIL_LINUX_PWDUTILS_H

#include <sys/types.h>
#include <pwd.h>

extern struct passwd *xgetpwnam(const char *username, char **pwdbuf);
extern struct passwd *xgetpwuid(uid_t uid, char **pwdbuf);
extern char *xgetlogin(void);

#endif /* UTIL_LINUX_PWDUTILS_H */

