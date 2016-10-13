#ifndef UTIL_LINUX_LOGINDEFS_H
#define UTIL_LINUX_LOGINDEFS_H

extern void logindefs_load_file(const char *filename);
extern void logindefs_set_loader(void (*loader)(void *data), void *data);
extern int getlogindefs_bool(const char *name, int dflt);
extern unsigned long getlogindefs_num(const char *name, long dflt);
extern const char *getlogindefs_str(const char *name, const char *dflt);
extern void free_getlogindefs_data(void);
extern int logindefs_setenv(const char *name, const char *conf, const char *dflt);
extern int effective_access(const char *path, int mode);
extern int get_hushlogin_status(struct passwd *pwd, int force_check);

#endif /* UTIL_LINUX_LOGINDEFS_H */
