#ifndef UTIL_LINUX_SELINUX_UTILS_H
#define UTIL_LINUX_SELINUX_UTILS_H

extern access_vector_t get_access_vector(const char *tclass, const char *op);
extern int setupDefaultContext(char *orig_file);

#endif
