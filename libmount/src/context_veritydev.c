/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * This file is part of libmount from util-linux project.
 *
 * Copyright (C) 2019 Microsoft Corporation
 * Copyright (C) 2022 Karel Zak <kzak@redhat.com>
 *
 * libmount is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 */

#include "mountP.h"

#if defined(HAVE_CRYPTSETUP)

#ifdef CRYPTSETUP_VIA_DLOPEN
#include <dlfcn.h>
#endif
#include <libcryptsetup.h>
#include "path.h"
#include "strutils.h"

#ifdef CRYPTSETUP_VIA_DLOPEN
struct verity_opers {
	void (*crypt_set_debug_level)(int);
	void (*crypt_set_log_callback)(struct crypt_device *, void (*log)(int, const char *, void *), void *);
	int (*crypt_init_data_device)(struct crypt_device **, const char *, const char *);
	int (*crypt_load)(struct crypt_device *, const char *, void *);
	int (*crypt_get_volume_key_size)(struct crypt_device *);
# ifdef HAVE_CRYPT_ACTIVATE_BY_SIGNED_KEY
	int (*crypt_activate_by_signed_key)(struct crypt_device *, const char *, const char *, size_t, const char *, size_t, uint32_t);
# endif
	int (*crypt_activate_by_volume_key)(struct crypt_device *, const char *, const char *, size_t, uint32_t);
	void (*crypt_free)(struct crypt_device *);
	int (*crypt_init_by_name)(struct crypt_device **, const char *);
	int (*crypt_get_verity_info)(struct crypt_device *, struct crypt_params_verity *);
	int (*crypt_volume_key_get)(struct crypt_device *, int, char *, size_t *, const char *, size_t);

	int (*crypt_deactivate_by_name)(struct crypt_device *, const char *, uint32_t);
};

/* symbols for dlopen() */
struct verity_sym {
	const char *name;
	size_t offset;		/* offset of the symbol in verity_opers */
};

# define DEF_VERITY_SYM(_name) \
	{ \
		.name = # _name, \
		.offset = offsetof(struct verity_opers, _name), \
	}

static const struct verity_sym verity_symbols[] =
{
	DEF_VERITY_SYM( crypt_set_debug_level ),
	DEF_VERITY_SYM( crypt_set_log_callback ),
	DEF_VERITY_SYM( crypt_init_data_device ),
	DEF_VERITY_SYM( crypt_load ),
	DEF_VERITY_SYM( crypt_get_volume_key_size ),
# ifdef HAVE_CRYPT_ACTIVATE_BY_SIGNED_KEY
	DEF_VERITY_SYM( crypt_activate_by_signed_key ),
# endif
	DEF_VERITY_SYM( crypt_activate_by_volume_key ),
	DEF_VERITY_SYM( crypt_free ),
	DEF_VERITY_SYM( crypt_init_by_name ),
	DEF_VERITY_SYM( crypt_get_verity_info ),
	DEF_VERITY_SYM( crypt_volume_key_get ),

	DEF_VERITY_SYM( crypt_deactivate_by_name ),
};

static int verity_load_symbols(struct libmnt_context *cxt, void **dl, struct verity_opers *oprs)
{
	size_t i;

	if (!dl)
		return -EINVAL;
	/*
	 * dlopen()
	 */
	if (!*dl) {
		int flags = RTLD_LAZY | RTLD_LOCAL;

		/* glibc extension: mnt_context_deferred_delete_veritydev is called immediately after, don't unload on dl_close */
#ifdef RTLD_NODELETE
		flags |= RTLD_NODELETE;
#endif
		/* glibc extension: might help to avoid further symbols clashes */
#ifdef RTLD_DEEPBIND
		flags |= RTLD_DEEPBIND;
#endif
		*dl = dlopen("libcryptsetup.so.12", flags);
		if (!*dl) {
			DBG(VERITY, ul_debugobj(cxt, "veritydev specific options detected but cannot dlopen libcryptsetup"));
			return -ENOTSUP;
		}
	}

	/* clear errors first, then load all the libcryptsetup symbols */
	dlerror();

	/*
	 * dlsym()
	 */
	for (i = 0; i < ARRAY_SIZE(verity_symbols); i++) {
		char *errmsg;
		const struct verity_sym *def = &verity_symbols[i];
		void **sym;

		sym = (void **) ((char *) oprs + def->offset);
		*sym = dlsym(*dl, def->name);

		errmsg = dlerror();
		if (errmsg) {
			DBG(VERITY, ul_debugobj(cxt, "cannot obtain address of a '%s' symbol: %s", def->name, errmsg));
			return -ENOTSUP;
		}
	}
	return 0;
}

/*
 * with dlopen --  call pointer to libcryptsetup function, the pointer is
 *                 stored in __verity_opers and initialized by dlsym() in
 *                 verity_load_symbols().
 */
# define verity_call(_func)	( __verity_opers._func )

#else /* !CRYPTSETUP_VIA_DLOPEN */

/*
 * without dlopen -- call linked libcryptsetup function
 */
# define verity_call(_func)	(_func)

#endif /* CRYPTSETUP_VIA_DLOPEN */


static void libcryptsetup_log(int level __attribute__((__unused__)), const char *msg, void *data)
{
	struct libmnt_context *cxt = (struct libmnt_context *)data;

	DBG(VERITY, ul_debugobj(cxt, "cryptsetup: %s", msg));
}

/* Taken from https://gitlab.com/cryptsetup/cryptsetup/blob/master/lib/utils_crypt.c#L225 */
static size_t crypt_hex_to_bytes(const char *hex, char **result)
{
	char buf[3] = "xx\0", *endp, *bytes;
	size_t i, len;

	len = strlen(hex);
	if (len % 2)
		return -EINVAL;
	len /= 2;

	bytes = malloc(len);
	if (!bytes)
		return -ENOMEM;

	for (i = 0; i < len; i++) {
		memcpy(buf, &hex[i * 2], 2);
		errno = 0;
		bytes[i] = strtoul(buf, &endp, 16);
		if (errno || endp != &buf[2]) {
			free(bytes);
			return -EINVAL;
		}
	}
	*result = bytes;
	return i;
}


int mnt_context_setup_veritydev(struct libmnt_context *cxt)
{
	struct libmnt_optlist *ol;
	struct libmnt_opt *opt;
	const char *backing_file, *hash_device = NULL, *root_hash_file = NULL, *fec_device = NULL,
	           *root_hash_sig_file = NULL;
	char *key = NULL, *root_hash_binary = NULL, *mapper_device = NULL,
		*mapper_device_full = NULL, *backing_file_basename = NULL, *root_hash = NULL,
		*hash_sig = NULL;
	size_t hash_size, hash_sig_size = 0, keysize = 0;
	struct crypt_params_verity crypt_params = {};
	struct crypt_device *crypt_dev = NULL;
	int rc = 0;
	/* Use the same default for FEC parity bytes as cryptsetup uses */
	uint64_t offset = 0, fec_offset = 0, fec_roots = 2;
	uint32_t crypt_activate_flags = CRYPT_ACTIVATE_READONLY;
	struct stat hash_sig_st;
#ifdef CRYPTSETUP_VIA_DLOPEN
	void *dl = NULL;
	struct verity_opers __verity_opers = { NULL } ;	/* see verity_call() macro */
#endif
	assert(cxt);
	assert(cxt->fs);
	assert((cxt->flags & MNT_FL_MOUNTFLAGS_MERGED));

	ol = mnt_context_get_optlist(cxt);
	if (!ol)
		return -ENOMEM;

	/* dm-verity volumes are read-only, and mount will fail if not set */
	mnt_optlist_append_flags(ol, MS_RDONLY, cxt->map_linux);

	backing_file = mnt_fs_get_srcpath(cxt->fs);
	if (!backing_file)
		return -EINVAL;

	/* To avoid clashes, prefix libmnt_ to all mapper devices */
	backing_file_basename = basename(backing_file);
	mapper_device = calloc(strlen(backing_file_basename) + strlen("libmnt_") + 1, sizeof(char));
	if (!mapper_device)
		return -ENOMEM;
	strcat(mapper_device, "libmnt_");
	strcat(mapper_device, backing_file_basename);

	DBG(VERITY, ul_debugobj(cxt, "trying to setup verity device for %s", backing_file));

	/*
	 * verity.hashdevice=
	 */
	if (!rc && (opt = mnt_optlist_get_opt(ol, MNT_MS_HASH_DEVICE, cxt->map_userspace)))
		hash_device = mnt_opt_get_value(opt);

	/*
	 * verity.roothash=
	 */
	if (!rc && (opt = mnt_optlist_get_opt(ol, MNT_MS_ROOT_HASH, cxt->map_userspace))) {
		root_hash = strdup(mnt_opt_get_value(opt));
		rc = root_hash ? 0 : -ENOMEM;
	}

	/*
	 * verity.hashoffset=
	 */
	if (!rc && (opt = mnt_optlist_get_opt(ol, MNT_MS_HASH_OFFSET, cxt->map_userspace))
	    && mnt_opt_has_value(opt)) {
		if (strtosize(mnt_opt_get_value(opt), &offset)) {
			DBG(VERITY, ul_debugobj(cxt, "failed to parse verity.hashoffset="));
			rc = -MNT_ERR_MOUNTOPT;
		}
	}

	/*
	 * verity.roothashfile=
	 */
	if (!rc && (opt = mnt_optlist_get_opt(ol, MNT_MS_ROOT_HASH_FILE, cxt->map_userspace)))
		root_hash_file = mnt_opt_get_value(opt);

	/*
	 * verity.fecdevice=
	 */
	if (!rc && (opt = mnt_optlist_get_opt(ol, MNT_MS_FEC_DEVICE, cxt->map_userspace)))
		fec_device = mnt_opt_get_value(opt);

	/*
	 * verity.fecoffset=
	 */
	if (!rc && (opt = mnt_optlist_get_opt(ol, MNT_MS_FEC_OFFSET, cxt->map_userspace))
	    && mnt_opt_has_value(opt)) {
		if (strtosize(mnt_opt_get_value(opt), &fec_offset)) {
			DBG(VERITY, ul_debugobj(cxt, "failed to parse verity.fecoffset="));
			rc = -MNT_ERR_MOUNTOPT;
		}
	}

	/*
	 * verity.fecroots=
	 */
	if (!rc && (opt = mnt_optlist_get_opt(ol, MNT_MS_FEC_ROOTS, cxt->map_userspace))
	    && mnt_opt_has_value(opt)) {
		if (strtosize(mnt_opt_get_value(opt), &fec_roots)) {
			DBG(VERITY, ul_debugobj(cxt, "failed to parse verity.fecroots="));
			rc = -MNT_ERR_MOUNTOPT;
		}
	}

	/*
	 * verity.roothashsig=
	 */
	if (!rc && (opt = mnt_optlist_get_opt(ol, MNT_MS_ROOT_HASH_SIG, cxt->map_userspace))
	    && mnt_opt_has_value(opt)) {
		root_hash_sig_file = mnt_opt_get_value(opt);

		DBG(VERITY, ul_debugobj(cxt, "checking %s", root_hash_sig_file));

		rc = ul_path_stat(NULL, &hash_sig_st, 0, root_hash_sig_file);
		if (rc == 0)
			rc = S_ISREG(hash_sig_st.st_mode) && hash_sig_st.st_size ? 0 : -EINVAL;

		if (rc == 0) {
			hash_sig_size = hash_sig_st.st_size;
			hash_sig = malloc(hash_sig_size);
			rc = hash_sig ? 0 : -ENOMEM;
		}

		if (rc == 0) {
			rc = ul_path_read(NULL, hash_sig, hash_sig_size, root_hash_sig_file);
			rc = rc < (int)hash_sig_size ? -1 : 0;
		}
	}

	/*
	 * verity.oncorruption=
	 */
	if (!rc && (opt = mnt_optlist_get_opt(ol, MNT_MS_VERITY_ON_CORRUPTION, cxt->map_userspace))
	    && mnt_opt_has_value(opt)) {
		const char *val =  mnt_opt_get_value(opt);
		if (!strcmp(val, "ignore"))
			crypt_activate_flags |= CRYPT_ACTIVATE_IGNORE_CORRUPTION;
		else if (!strcmp(val, "restart"))
			crypt_activate_flags |= CRYPT_ACTIVATE_RESTART_ON_CORRUPTION;
		else if (!strcmp(val, "panic"))
			/* Added by libcryptsetup v2.3.4 - ignore on lower versions, as with other optional features */
#ifdef CRYPT_ACTIVATE_PANIC_ON_CORRUPTION
			crypt_activate_flags |= CRYPT_ACTIVATE_PANIC_ON_CORRUPTION;
#else
			DBG(VERITY, ul_debugobj(cxt, "verity.oncorruption=panic not supported by libcryptsetup, ignoring"));
#endif
		else {
			DBG(VERITY, ul_debugobj(cxt, "failed to parse verity.oncorruption="));
			rc = -MNT_ERR_MOUNTOPT;
		}
	}

	if (!rc && root_hash && root_hash_file) {
		DBG(VERITY, ul_debugobj(cxt, "verity.roothash and verity.roothashfile are mutually exclusive"));
		rc = -EINVAL;
	} else if (!rc && root_hash_file) {
		rc = ul_path_read_string(NULL, &root_hash, root_hash_file);
		rc = rc < 1 ? rc : 0;
	}

	if (!rc && (!hash_device || !root_hash)) {
		DBG(VERITY, ul_debugobj(cxt, "verity.hashdevice and one of verity.roothash or verity.roothashfile are mandatory"));
		rc = -EINVAL;
	}

#ifdef CRYPTSETUP_VIA_DLOPEN
	if (rc == 0)
		rc = verity_load_symbols(cxt, &dl, &__verity_opers);
#endif
	if (rc)
		goto done;

	if (mnt_context_is_verbose(cxt))
		verity_call( crypt_set_debug_level(CRYPT_DEBUG_ALL) );

	verity_call( crypt_set_log_callback(NULL, libcryptsetup_log, cxt) );

	rc = verity_call( crypt_init_data_device(&crypt_dev, hash_device, backing_file) );
	if (rc)
		goto done;

	memset(&crypt_params, 0, sizeof(struct crypt_params_verity));
	crypt_params.hash_area_offset = offset;
	crypt_params.fec_area_offset = fec_offset;
	crypt_params.fec_roots = fec_roots;
	crypt_params.fec_device = fec_device;
	crypt_params.flags = 0;

	rc = verity_call( crypt_load(crypt_dev, CRYPT_VERITY, &crypt_params) );
	if (rc < 0)
		goto done;

	hash_size = verity_call( crypt_get_volume_key_size(crypt_dev) );
	if (crypt_hex_to_bytes(root_hash, &root_hash_binary) != hash_size) {
		DBG(VERITY, ul_debugobj(cxt, "root hash %s is not of length %zu", root_hash, hash_size));
		rc = -EINVAL;
		goto done;
	}

	if (hash_sig) {
#ifdef HAVE_CRYPT_ACTIVATE_BY_SIGNED_KEY
		rc = verity_call( crypt_activate_by_signed_key(crypt_dev, mapper_device, root_hash_binary, hash_size,
				hash_sig, hash_sig_size, crypt_activate_flags) );
#else
		rc = -EINVAL;
		DBG(VERITY, ul_debugobj(cxt, "verity.roothashsig=%s passed but libcryptsetup does not provide crypt_activate_by_signed_key()", hash_sig));
#endif
	} else
		rc = verity_call( crypt_activate_by_volume_key(crypt_dev, mapper_device, root_hash_binary, hash_size,
				crypt_activate_flags) );

	/*
	 * If the mapper device already exists, and if libcryptsetup supports it, get the root
	 * hash associated with the existing one and compare it with the parameter passed by
	 * the user. If they match, then we can be sure the user intended to mount the exact
	 * same device, and simply reuse it and return success.
	 * The kernel does the refcounting for us.
	 * If libcryptsetup does not support getting the root hash out of an existing device,
	 * then return an error and tell the user that the device is already in use.
	 * Pass through only OOM errors or mismatching root hash errors.
	 */
	if (rc == -EEXIST) {
		DBG(VERITY, ul_debugobj(cxt, "%s already in use as /dev/mapper/%s", backing_file, mapper_device));
		verity_call( crypt_free(crypt_dev) );
		rc = verity_call( crypt_init_by_name(&crypt_dev, mapper_device) );
		if (!rc) {
			rc = verity_call( crypt_get_verity_info(crypt_dev, &crypt_params) );
			if (!rc) {
				key = calloc(hash_size, 1);
				if (!key) {
					rc = -ENOMEM;
					goto done;
				}
			}
			if (!rc) {
				keysize = hash_size;
				rc = verity_call( crypt_volume_key_get(crypt_dev, CRYPT_ANY_SLOT, key, &keysize, NULL, 0) );
			}
			if (!rc) {
				DBG(VERITY, ul_debugobj(cxt, "comparing root hash of existing device with %s", root_hash));
				if (memcmp(key, root_hash_binary, hash_size)) {
					DBG(VERITY, ul_debugobj(cxt, "existing device's hash does not match with %s", root_hash));
					rc = -EINVAL;
					goto done;
				}
			} else {
				DBG(VERITY, ul_debugobj(cxt, "libcryptsetup does not support extracting root hash of existing device"));
			}
		}
		if (rc) {
			rc = -EEXIST;
		} else {
			/*
			 * Ensure that, if signatures are supported, we only reuse the device if the previous mount
			 * used the same settings, so that a previous unsigned mount will not be reused if the user
			 * asks to use signing for the new one, and viceversa.
			 */
#ifdef HAVE_CRYPT_ACTIVATE_BY_SIGNED_KEY
			if (!!hash_sig != !!(crypt_params.flags & CRYPT_VERITY_ROOT_HASH_SIGNATURE)) {
				rc = -EINVAL;
				DBG(VERITY, ul_debugobj(cxt, "existing device and new mount have to either be both opened with signature or both without"));
				goto done;
			}
#endif
			DBG(VERITY, ul_debugobj(cxt, "root hash of %s matches %s, reusing device", mapper_device, root_hash));
		}
	}

	if (!rc) {
		cxt->flags |= MNT_FL_VERITYDEV_READY;
		mapper_device_full = calloc(strlen(mapper_device) + strlen("/dev/mapper/") + 1, sizeof(char));
		if (!mapper_device_full)
			rc = -ENOMEM;
		else {
			strcat(mapper_device_full, "/dev/mapper/");
			strcat(mapper_device_full, mapper_device);
			rc = mnt_fs_set_source(cxt->fs, mapper_device_full);
		}
	}

done:
	verity_call( crypt_free(crypt_dev) );
#ifdef CRYPTSETUP_VIA_DLOPEN
	if (dl)
		dlclose(dl);
#endif
	free(root_hash_binary);
	free(mapper_device_full);
	free(mapper_device);
	free(root_hash);
	free(hash_sig);
	free(key);
	return rc;
}

int mnt_context_deferred_delete_veritydev(struct libmnt_context *cxt)
{
	const char *src;
	/* If mounting failed delete immediately, otherwise setup auto cleanup for user umount */
	uint32_t flags = mnt_context_get_status(cxt) ? CRYPT_DEACTIVATE_DEFERRED : 0;
#ifdef CRYPTSETUP_VIA_DLOPEN
	void *dl = NULL;
	struct verity_opers __verity_opers = { NULL } ;	/* see verity_call() macro */
#endif
	int rc = 0;

	assert(cxt);
	assert(cxt->fs);

	if (!(cxt->flags & MNT_FL_VERITYDEV_READY))
		return 0;

	src = mnt_fs_get_srcpath(cxt->fs);
	if (!src)
		return -EINVAL;

#ifdef CRYPTSETUP_VIA_DLOPEN
	rc = verity_load_symbols(cxt, &dl, &__verity_opers);
#endif
	if (!rc) {
		if (mnt_context_is_verbose(cxt))
			verity_call( crypt_set_debug_level(CRYPT_DEBUG_ALL) );

		verity_call( crypt_set_log_callback(NULL, libcryptsetup_log, cxt) );

		rc = verity_call( crypt_deactivate_by_name(NULL, src, flags) );
		if (!rc)
			cxt->flags &= ~MNT_FL_VERITYDEV_READY;
	}

#ifdef CRYPTSETUP_VIA_DLOPEN
	dlclose(dl);
#endif
	DBG(VERITY, ul_debugobj(cxt, "deleted [rc=%d]", rc));
	return rc;
}

#else

int mnt_context_setup_veritydev(struct libmnt_context *cxt __attribute__ ((__unused__)))
{
	return 0;
}

int mnt_context_deferred_delete_veritydev(struct libmnt_context *cxt __attribute__ ((__unused__)))
{
	return 0;
}
#endif

int mnt_context_is_veritydev(struct libmnt_context *cxt)
{
	const char *src;
	unsigned long flags = 0;
	struct libmnt_optlist *ol;

	assert(cxt);
	assert((cxt->flags & MNT_FL_MOUNTFLAGS_MERGED));

	if (cxt->action != MNT_ACT_MOUNT)
		return 0;
	if (!cxt->fs)
		return 0;
	src = mnt_fs_get_srcpath(cxt->fs);
	if (!src)
		return 0;		/* backing file not set */

	ol = mnt_context_get_optlist(cxt);
	if (!ol)
		return 0;
	if (mnt_optlist_is_bind(ol)
	    || mnt_optlist_is_move(ol)
	    || mnt_context_propagation_only(cxt))
		return 0;

	if (mnt_context_get_user_mflags(cxt, &flags))
		return 0;

	if (flags & (MNT_MS_HASH_DEVICE | MNT_MS_ROOT_HASH | MNT_MS_HASH_OFFSET)) {
#ifndef HAVE_CRYPTSETUP
		DBG(VERITY, ul_debugobj(cxt, "veritydev specific options detected but libmount built without libcryptsetup"));
		return -ENOTSUP;
#else
		DBG(VERITY, ul_debugobj(cxt, "veritydev specific options detected"));
		return 1;
#endif
	}

	if (!strncmp(src, "/dev/mapper/libmnt_", strlen("/dev/mapper/libmnt_"))) {
#ifndef HAVE_CRYPTSETUP
		DBG(VERITY, ul_debugobj(cxt, "veritydev prefix detected in source device but libmount built without libcryptsetup"));
		return -ENOTSUP;
#else
		DBG(VERITY, ul_debugobj(cxt, "veritydev prefix detected in source device"));
		return 1;
#endif
	}

	return 0;
}
