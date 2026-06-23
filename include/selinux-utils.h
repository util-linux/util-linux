/*
 * No copyright is claimed.  This code is in the public domain; do with
 * it what you wish.
 */
#ifndef UTIL_LINUX_SELINUX_UTILS_H
#define UTIL_LINUX_SELINUX_UTILS_H

#ifdef HAVE_LIBSELINUX
#include <selinux/selinux.h>
#include <selinux/context.h>
#include <selinux/label.h>
#include <selinux/get_context_list.h>

/* libselinux is an optional runtime dependency: it is loaded on demand with
 * dlopen() rather than linked, so that the util-linux tools and libraries do
 * not pull it in when SELinux is not actually used. The library functions are
 * resolved with dlsym() into the 'ul_selinux' table below.
 *
 * Call ul_load_libselinux() before using any of the functions. It returns 0
 * when libselinux is available (and all the symbols have been resolved), or a
 * negative value otherwise, so that callers can gracefully behave as if SELinux
 * was disabled when the library is not available at runtime. The result is
 * cached, so the library is opened at most once.
 *
 * The functions are called through the selinux_call() macro, for example:
 *
 *	if (ul_load_libselinux() == 0 && selinux_call(is_selinux_enabled)() > 0)
 *		selinux_call(getfilecon)(path, &con);
 */

/* Pointers to libselinux functions (initialized by dlsym()) */
struct ul_selinux_opers {
	__typeof__(is_selinux_enabled)			*is_selinux_enabled;
	__typeof__(getfilecon)				*getfilecon;
	__typeof__(getfilecon_raw)			*getfilecon_raw;
	__typeof__(fgetfilecon)				*fgetfilecon;
	__typeof__(lgetfilecon)				*lgetfilecon;
	__typeof__(getcon)				*getcon;
	__typeof__(getprevcon)				*getprevcon;
	__typeof__(getpidcon)				*getpidcon;
	__typeof__(getseuserbyname)			*getseuserbyname;
	__typeof__(setfilecon)				*setfilecon;
	__typeof__(fsetfilecon)				*fsetfilecon;
	__typeof__(setfscreatecon)			*setfscreatecon;
	__typeof__(setexeccon)				*setexeccon;
	__typeof__(freecon)				*freecon;
	__typeof__(selinux_check_access)		*selinux_check_access;
	__typeof__(security_compute_relabel)		*security_compute_relabel;
	__typeof__(security_get_initial_context)	*security_get_initial_context;
	__typeof__(selinux_file_context_cmp)		*selinux_file_context_cmp;
	__typeof__(selinux_trans_to_raw_context)	*selinux_trans_to_raw_context;
	__typeof__(string_to_security_class)		*string_to_security_class;
	__typeof__(get_default_context_with_level)	*get_default_context_with_level;
	__typeof__(selabel_open)			*selabel_open;
	__typeof__(selabel_lookup)			*selabel_lookup;
	__typeof__(selabel_close)			*selabel_close;
	__typeof__(context_new)				*context_new;
	__typeof__(context_type_set)			*context_type_set;
	__typeof__(context_str)				*context_str;
	__typeof__(context_free)			*context_free;
};

extern struct ul_selinux_opers ul_selinux;

extern int ul_load_libselinux(void);

/* libselinux call -- libselinux is always loaded with dlopen() */
#define selinux_call(_func)	(ul_selinux._func)

#endif /* HAVE_LIBSELINUX */

extern int ul_setfscreatecon_from_file(char *orig_file);
extern int ul_selinux_has_access(const char *classstr, const char *perm, char **user_cxt);
extern int ul_selinux_get_default_context(const char *path, int st_mode, char **cxt);

#endif
