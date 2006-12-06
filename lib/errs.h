#ifndef _ERR_H_
#define	_ERR_H_

#include <stdarg.h>

void	err_nomsg (int);
void	err (int, const char *, ...);
void	verr (int, const char *, va_list);
void	errx (int, const char *, ...);
void	verrx (int, const char *, va_list);
void	warn (const char *, ...);
void	vwarn (const char *, va_list);
void	warnx (const char *, ...);
void	vwarnx (const char *, va_list);

#endif /* !_ERR_H_ */
