/*
 * No copyright is claimed.  This code is in the public domain; do with
 * it what you wish.
 */
#ifndef UTIL_LINUX_DL_CRYPTSETUP_H
#define UTIL_LINUX_DL_CRYPTSETUP_H

#ifdef HAVE_CRYPTSETUP

#include <libcryptsetup.h>

#include "dl-utils.h"

/* Pointers to libcryptsetup functions (initialized by dlsym()) */
struct ul_cryptsetup_opers {
	void (*crypt_set_debug_level)(int);
	void (*crypt_set_log_callback)(struct crypt_device *,
			void (*log)(int, const char *, void *), void *);
	int (*crypt_init_data_device)(struct crypt_device **,
			const char *, const char *);
	int (*crypt_load)(struct crypt_device *, const char *, void *);
	int (*crypt_get_volume_key_size)(struct crypt_device *);
#ifdef HAVE_CRYPT_ACTIVATE_BY_SIGNED_KEY
	int (*crypt_activate_by_signed_key)(struct crypt_device *,
			const char *, const char *, size_t,
			const char *, size_t, uint32_t);
#endif
	int (*crypt_activate_by_volume_key)(struct crypt_device *,
			const char *, const char *, size_t, uint32_t);
	void (*crypt_free)(struct crypt_device *);
	int (*crypt_init_by_name)(struct crypt_device **, const char *);
	int (*crypt_get_verity_info)(struct crypt_device *,
			struct crypt_params_verity *);
	int (*crypt_volume_key_get)(struct crypt_device *, int,
			char *, size_t *, const char *, size_t);
	int (*crypt_deactivate_by_name)(struct crypt_device *,
			const char *, uint32_t);
};

typedef struct ul_cryptsetup_opers ul_cryptsetup_opers;

extern struct ul_cryptsetup_opers ul_cryptsetup;

extern int ul_dlopen_libcryptsetup(void);

#define cryptsetup_call(_func)	(ul_cryptsetup._func)

#endif /* HAVE_CRYPTSETUP */
#endif /* UTIL_LINUX_DL_CRYPTSETUP_H */
