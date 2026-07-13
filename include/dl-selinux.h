/*
 * No copyright is claimed.  This code is in the public domain; do with
 * it what you wish.
 */
#ifndef UTIL_LINUX_DL_SELINUX_H
#define UTIL_LINUX_DL_SELINUX_H

#ifdef HAVE_LIBSELINUX

#include <selinux/selinux.h>
#include <selinux/context.h>
#include <selinux/label.h>
#include <selinux/get_context_list.h>

#include "dl-utils.h"

/* Pointers to libselinux functions (initialized by dlsym()) */
struct ul_selinux_opers {
	int (*is_selinux_enabled)(void);

	int (*getfilecon)(const char *, char **);
	int (*getfilecon_raw)(const char *, char **);
	int (*fgetfilecon)(int, char **);
	int (*lgetfilecon)(const char *, char **);

	int (*getcon)(char **);
	int (*getprevcon)(char **);
	int (*getpidcon)(pid_t, char **);

	int (*getseuserbyname)(const char *, char **, char **);

	int (*setfilecon)(const char *, const char *);
	int (*fsetfilecon)(int, const char *);
	int (*setfscreatecon)(const char *);
	int (*setexeccon)(const char *);

	void (*freecon)(char *);

	int (*selinux_check_access)(const char *, const char *,
			const char *, const char *, void *);
	int (*security_compute_relabel)(const char *, const char *,
			security_class_t, char **);
#ifdef HAVE_SECURITY_GET_INITIAL_CONTEXT
	int (*security_get_initial_context)(const char *, char **);
#endif
	int (*selinux_file_context_cmp)(const char *, const char *);
	int (*selinux_trans_to_raw_context)(const char *, char **);
	security_class_t (*string_to_security_class)(const char *);

	int (*get_default_context_with_level)(const char *, const char *,
			context_t, char **);

	struct selabel_handle *(*selabel_open)(unsigned int,
			const struct selinux_opt *, unsigned);
	int (*selabel_lookup)(struct selabel_handle *, char **,
			const char *, int);
	void (*selabel_close)(struct selabel_handle *);

	context_t (*context_new)(const char *);
	int (*context_type_set)(context_t, const char *);
	const char *(*context_str)(context_t);
	void (*context_free)(context_t);
};

typedef struct ul_selinux_opers ul_selinux_opers;

extern struct ul_selinux_opers ul_selinux;

extern int ul_dlopen_libselinux(void);

#define selinux_call(_func)	(ul_selinux._func)

#endif /* HAVE_LIBSELINUX */
#endif /* UTIL_LINUX_DL_SELINUX_H */
