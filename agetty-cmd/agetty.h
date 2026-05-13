#ifndef UTIL_LINUX_AGETTY_H
#define UTIL_LINUX_AGETTY_H

extern void agetty_exit_slowly(int code) __attribute__((__noreturn__));
extern void agetty_log_err(const char *, ...) __attribute__((__noreturn__))
				       __attribute__((__format__(printf, 1, 2)));
extern void agetty_log_warn(const char *, ...)
				__attribute__((__format__(printf, 1, 2)));

#endif /* UTIL_LINUX_AGETTY_H */
