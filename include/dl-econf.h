/*
 * No copyright is claimed.  This code is in the public domain; do with
 * it what you wish.
 */
#ifndef UTIL_LINUX_DL_ECONF_H
#define UTIL_LINUX_DL_ECONF_H

#ifdef HAVE_LIBECONF

#include <libeconf.h>

#include "dl-utils.h"

/* Pointers to libeconf functions (initialized by dlsym()) */
struct ul_econf_opers {
	econf_err (*econf_readFile)(econf_file **, const char *,
			const char *, const char *);
	econf_err (*econf_mergeFiles)(econf_file **, econf_file *, econf_file *);
#ifdef HAVE_ECONF_READCONFIG
	econf_err (*econf_readConfig)(econf_file **, const char *,
			const char *, const char *, const char *,
			const char *, const char *);
#endif
	econf_err (*econf_readDirs)(econf_file **, const char *,
			const char *, const char *, const char *,
			const char *, const char *);
	econf_err (*econf_getKeys)(econf_file *, const char *,
			size_t *, char ***);
	econf_err (*econf_getStringValue)(econf_file *, const char *,
			const char *, char **);
	econf_err (*econf_getBoolValue)(econf_file *, const char *,
			const char *, bool *);
	econf_err (*econf_getUInt64Value)(econf_file *, const char *,
			const char *, uint64_t *);
	const char *(*econf_errString)(const econf_err);

	/* the econf_free() macro dispatches to these depending on the type */
	econf_file *(*econf_freeFile)(econf_file *);
	char **(*econf_freeArray)(char **);
};

typedef struct ul_econf_opers ul_econf_opers;

extern struct ul_econf_opers ul_econf;

extern int ul_dlopen_libeconf(void);

#define econf_call(_func)	(ul_econf._func)

#endif /* HAVE_LIBECONF */
#endif /* UTIL_LINUX_DL_ECONF_H */
