/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * This file is part of libmount from util-linux project.
 *
 * Copyright (C) 2019 Microsoft Corporation
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

#ifdef CRYPTSETUP_VIA_DLOPEN
static void *get_symbol(struct libmnt_context *cxt, void *dl, const char *name, int *rc)
{
	char *dl_error = NULL;
	void *sym = dlsym(dl, name);

	*rc = 0;
	if ((dl_error = dlerror()) == NULL)
		return sym;

	DBG(VERITY, ul_debugobj(cxt, "veritydev specific options detected but cannot dlopen symbol %s: %s", name, dl_error));
	*rc = -ENOTSUP;

	return NULL;
}
#endif

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
		bytes[i] = strtoul(buf, &endp, 16);
		if (endp != &buf[2]) {
			free(bytes);
			return -EINVAL;
		}
	}
	*result = bytes;
	return i;
}


int mnt_context_setup_veritydev(struct libmnt_context *cxt)
{
	const char *backing_file, *optstr;
	char *val = NULL, *key = NULL, *root_hash_binary = NULL, *mapper_device = NULL,
		*mapper_device_full = NULL, *backing_file_basename = NULL, *root_hash = NULL,
		*hash_device = NULL, *root_hash_file = NULL, *fec_device = NULL, *hash_sig = NULL;
	size_t len, hash_size, hash_sig_size = 0, keysize = 0;
	struct crypt_params_verity crypt_params = {};
	struct crypt_device *crypt_dev = NULL;
	int rc = 0;
	/* Use the same default for FEC parity bytes as cryptsetup uses */
	uint64_t offset = 0, fec_offset = 0, fec_roots = 2;
	struct stat hash_sig_st;
#ifdef CRYPTSETUP_VIA_DLOPEN
	/* To avoid linking libmount to libcryptsetup, and keep the default dependencies list down, use dlopen */
	void *dl = NULL;
	int (*sym_crypt_init_data_device)(struct crypt_device **, const char *, const char *) = NULL;
	int (*sym_crypt_load)(struct crypt_device *, const char *, void *) = NULL;
	int (*sym_crypt_get_volume_key_size)(struct crypt_device *) = NULL;
#ifdef HAVE_CRYPT_ACTIVATE_BY_SIGNED_KEY
	int (*sym_crypt_activate_by_signed_key)(struct crypt_device *, const char *, const char *, size_t, const char *, size_t, uint32_t) = NULL;
#endif
	int (*sym_crypt_activate_by_volume_key)(struct crypt_device *, const char *, const char *, size_t, uint32_t) = NULL;
	void (*sym_crypt_free)(struct crypt_device *) = NULL;
	int (*sym_crypt_init_by_name)(struct crypt_device **, const char *) = NULL;
	int (*sym_crypt_get_verity_info)(struct crypt_device *, struct crypt_params_verity *) = NULL;
	int (*sym_crypt_volume_key_get)(struct crypt_device *, int, char *, size_t *, const char *, size_t) = NULL;
#else
	int (*sym_crypt_init_data_device)(struct crypt_device **, const char *, const char *) = &crypt_init_data_device;
	int (*sym_crypt_load)(struct crypt_device *, const char *, void *) = &crypt_load;
	int (*sym_crypt_get_volume_key_size)(struct crypt_device *) = &crypt_get_volume_key_size;
#ifdef HAVE_CRYPT_ACTIVATE_BY_SIGNED_KEY
	int (*sym_crypt_activate_by_signed_key)(struct crypt_device *, const char *, const char *, size_t, const char *, size_t, uint32_t) = &crypt_activate_by_signed_key;
#endif
	int (*sym_crypt_activate_by_volume_key)(struct crypt_device *, const char *, const char *, size_t, uint32_t) = &crypt_activate_by_volume_key;
	void (*sym_crypt_free)(struct crypt_device *) = &crypt_free;
	int (*sym_crypt_init_by_name)(struct crypt_device **, const char *) = &crypt_init_by_name;
	int (*sym_crypt_get_verity_info)(struct crypt_device *, struct crypt_params_verity *) = &crypt_get_verity_info;
	int (*sym_crypt_volume_key_get)(struct crypt_device *, int, char *, size_t *, const char *, size_t) = &crypt_volume_key_get;
#endif

	assert(cxt);
	assert(cxt->fs);
	assert((cxt->flags & MNT_FL_MOUNTFLAGS_MERGED));

	/* dm-verity volumes are read-only, and mount will fail if not set */
	mnt_context_set_mflags(cxt, (cxt->mountflags | MS_RDONLY));

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

	optstr = mnt_fs_get_user_options(cxt->fs);

	/*
	 * verity.hashdevice=
	 */
	if (rc == 0 && (cxt->user_mountflags & MNT_MS_HASH_DEVICE) &&
	    mnt_optstr_get_option(optstr, "verity.hashdevice", &val, &len) == 0 && val) {
		hash_device = strndup(val, len);
		rc = hash_device ? 0 : -ENOMEM;
	}

	/*
	 * verity.roothash=
	 */
	if (rc == 0 && (cxt->user_mountflags & MNT_MS_ROOT_HASH) &&
	    mnt_optstr_get_option(optstr, "verity.roothash", &val, &len) == 0 && val) {
		root_hash = strndup(val, len);
		rc = root_hash ? 0 : -ENOMEM;
	}

	/*
	 * verity.hashoffset=
	 */
	if (rc == 0 && (cxt->user_mountflags & MNT_MS_HASH_OFFSET) &&
	    mnt_optstr_get_option(optstr, "verity.hashoffset", &val, &len) == 0) {
		rc = mnt_parse_offset(val, len, &offset);
		if (rc) {
			DBG(VERITY, ul_debugobj(cxt, "failed to parse verity.hashoffset="));
			rc = -MNT_ERR_MOUNTOPT;
		}
	}

	/*
	 * verity.roothashfile=
	 */
	if (rc == 0 && (cxt->user_mountflags & MNT_MS_ROOT_HASH_FILE) &&
	    mnt_optstr_get_option(optstr, "verity.roothashfile", &val, &len) == 0 && val) {
		root_hash_file = strndup(val, len);
		rc = root_hash_file ? 0 : -ENOMEM;
	}

	/*
	 * verity.fecdevice=
	 */
	if (rc == 0 && (cxt->user_mountflags & MNT_MS_FEC_DEVICE) &&
	    mnt_optstr_get_option(optstr, "verity.fecdevice", &val, &len) == 0 && val) {
		fec_device = strndup(val, len);
		rc = fec_device ? 0 : -ENOMEM;
	}

	/*
	 * verity.fecoffset=
	 */
	if (rc == 0 && (cxt->user_mountflags & MNT_MS_FEC_OFFSET) &&
	    mnt_optstr_get_option(optstr, "verity.fecoffset", &val, &len) == 0) {
		rc = mnt_parse_offset(val, len, &fec_offset);
		if (rc) {
			DBG(VERITY, ul_debugobj(cxt, "failed to parse verity.fecoffset="));
			rc = -MNT_ERR_MOUNTOPT;
		}
	}

	/*
	 * verity.fecroots=
	 */
	if (rc == 0 && (cxt->user_mountflags & MNT_MS_FEC_ROOTS) &&
	    mnt_optstr_get_option(optstr, "verity.fecroots", &val, &len) == 0) {
		rc = mnt_parse_offset(val, len, &fec_roots);
		if (rc) {
			DBG(VERITY, ul_debugobj(cxt, "failed to parse verity.fecroots="));
			rc = -MNT_ERR_MOUNTOPT;
		}
	}

	/*
	 * verity.roothashsig=
	 */
	if (rc == 0 && (cxt->user_mountflags & MNT_MS_ROOT_HASH_SIG) &&
	    mnt_optstr_get_option(optstr, "verity.roothashsig", &val, &len) == 0 && val) {
		rc = ul_path_stat(NULL, &hash_sig_st, val);
		if (rc == 0)
			rc = !S_ISREG(hash_sig_st.st_mode) || !hash_sig_st.st_size ? -EINVAL : 0;
		if (rc == 0) {
			hash_sig_size = hash_sig_st.st_size;
			hash_sig = malloc(hash_sig_size);
			rc = hash_sig ? 0 : -ENOMEM;
		}
		if (rc == 0) {
			rc = ul_path_read(NULL, hash_sig, hash_sig_size, val);
			rc = rc < (int)hash_sig_size ? -1 : 0;
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
	if (rc == 0) {
		int dl_flags = RTLD_LAZY | RTLD_LOCAL;
		/* glibc extension: mnt_context_deferred_delete_veritydev is called immediately after, don't unload on dl_close */
#ifdef RTLD_NODELETE
		dl_flags |= RTLD_NODELETE;
#endif
		/* glibc extension: might help to avoid further symbols clashes */
#ifdef RTLD_DEEPBIND
		dl_flags |= RTLD_DEEPBIND;
#endif
		dl = dlopen("libcryptsetup.so.12", dl_flags);
		if (!dl) {
			DBG(VERITY, ul_debugobj(cxt, "veritydev specific options detected but cannot dlopen libcryptsetup"));
			rc = -ENOTSUP;
		}
	}

	/* clear errors first, then load all the libcryptsetup symbols */
	dlerror();

	if (rc == 0)
		*(void **)(&sym_crypt_init_data_device) = get_symbol(cxt, dl, "crypt_init_data_device", &rc);
	if (rc == 0)
		*(void **)(&sym_crypt_load) = get_symbol(cxt, dl, "crypt_load", &rc);
	if (rc == 0)
		*(void **)(&sym_crypt_get_volume_key_size) = get_symbol(cxt, dl, "crypt_get_volume_key_size", &rc);
#ifdef HAVE_CRYPT_ACTIVATE_BY_SIGNED_KEY
	if (rc == 0)
		*(void **)(&sym_crypt_activate_by_signed_key) = get_symbol(cxt, dl, "crypt_activate_by_signed_key", &rc);
#endif
	if (rc == 0)
		*(void **)(&sym_crypt_activate_by_volume_key) = get_symbol(cxt, dl, "crypt_activate_by_volume_key", &rc);
	if (rc == 0)
		*(void **)(&sym_crypt_free) = get_symbol(cxt, dl, "crypt_free", &rc);
	if (rc == 0)
		*(void **)(&sym_crypt_init_by_name) = get_symbol(cxt, dl, "crypt_init_by_name", &rc);
	if (rc == 0)
		*(void **)(&sym_crypt_get_verity_info) = get_symbol(cxt, dl, "crypt_get_verity_info", &rc);
	if (rc == 0)
		*(void **)(&sym_crypt_volume_key_get) = get_symbol(cxt, dl, "crypt_volume_key_get", &rc);
#endif
	if (rc)
		goto done;

	rc = (*sym_crypt_init_data_device)(&crypt_dev, hash_device, backing_file);
	if (rc)
		goto done;

	memset(&crypt_params, 0, sizeof(struct crypt_params_verity));
	crypt_params.hash_area_offset = offset;
	crypt_params.fec_area_offset = fec_offset;
	crypt_params.fec_roots = fec_roots;
	crypt_params.fec_device = fec_device;
	crypt_params.flags = 0;
	rc = (*sym_crypt_load)(crypt_dev, CRYPT_VERITY, &crypt_params);
	if (rc < 0)
		goto done;

	hash_size = (*sym_crypt_get_volume_key_size)(crypt_dev);
	if (crypt_hex_to_bytes(root_hash, &root_hash_binary) != hash_size) {
		DBG(VERITY, ul_debugobj(cxt, "root hash %s is not of length %zu", root_hash, hash_size));
		rc = -EINVAL;
		goto done;
	}
	if (hash_sig) {
#ifdef HAVE_CRYPT_ACTIVATE_BY_SIGNED_KEY
		rc = (*sym_crypt_activate_by_signed_key)(crypt_dev, mapper_device, root_hash_binary, hash_size,
				hash_sig, hash_sig_size, CRYPT_ACTIVATE_READONLY);
#else
		rc = -EINVAL;
		DBG(VERITY, ul_debugobj(cxt, "verity.roothashsig=%s passed but libcryptsetup does not provide crypt_activate_by_signed_key()", hash_sig));
#endif
	} else
		rc = (*sym_crypt_activate_by_volume_key)(crypt_dev, mapper_device, root_hash_binary, hash_size,
				CRYPT_ACTIVATE_READONLY);
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
		(*sym_crypt_free)(crypt_dev);
		rc = (*sym_crypt_init_by_name)(&crypt_dev, mapper_device);
		if (!rc) {
			rc = (*sym_crypt_get_verity_info)(crypt_dev, &crypt_params);
			if (!rc) {
				key = calloc(hash_size, 1);
				if (!key) {
					rc = -ENOMEM;
					goto done;
				}
			}
			if (!rc) {
				keysize = hash_size;
				rc = (*sym_crypt_volume_key_get)(crypt_dev, CRYPT_ANY_SLOT, key, &keysize, NULL, 0);
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
	if (sym_crypt_free)
		(*sym_crypt_free)(crypt_dev);
#ifdef CRYPTSETUP_VIA_DLOPEN
	if (dl)
		dlclose(dl);
#endif
	free(root_hash_binary);
	free(mapper_device_full);
	free(mapper_device);
	free(hash_device);
	free(root_hash);
	free(root_hash_file);
	free(fec_device);
	free(hash_sig);
	free(key);
	return rc;
}

int mnt_context_deferred_delete_veritydev(struct libmnt_context *cxt)
{
	const char *src;
	struct crypt_device *crypt_dev = NULL;
	/* If mounting failed delete immediately, otherwise setup auto cleanup for user umount */
	uint32_t flags = mnt_context_get_status(cxt) ? CRYPT_DEACTIVATE_DEFERRED : 0;
#ifdef CRYPTSETUP_VIA_DLOPEN
	void *dl = NULL;
	int dl_flags = RTLD_LAZY | RTLD_LOCAL;
	/* glibc extension: might help to avoid further symbols clashes */
#ifdef RTLD_DEEPBIND
	dl_flags |= RTLD_DEEPBIND;
#endif
	int (*sym_crypt_init_by_name)(struct crypt_device **, const char *) = NULL;
	int (*sym_crypt_deactivate_by_name)(struct crypt_device *, const char *, uint32_t) = NULL;
	void (*sym_crypt_free)(struct crypt_device *) = NULL;
#else
	int (*sym_crypt_init_by_name)(struct crypt_device **, const char *) = &crypt_init_by_name;
	int (*sym_crypt_deactivate_by_name)(struct crypt_device *, const char *, uint32_t) = &crypt_deactivate_by_name;
	void (*sym_crypt_free)(struct crypt_device *) = &crypt_free;
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
	dl = dlopen("libcryptsetup.so.12", dl_flags);
	if (!dl) {
		DBG(VERITY, ul_debugobj(cxt, "veritydev specific options detected but cannot dlopen libcryptsetup"));
		return -ENOTSUP;
	}

	/* clear errors first */
	dlerror();

	if (!rc)
		*(void **)(&sym_crypt_init_by_name) = get_symbol(cxt, dl, "crypt_init_by_name", &rc);
	if (!rc)
		*(void **)(&sym_crypt_deactivate_by_name) = get_symbol(cxt, dl, "crypt_deactivate_by_name", &rc);
	if (!rc)
		*(void **)(&sym_crypt_free) = get_symbol(cxt, dl, "crypt_free", &rc);
#endif
	if (!rc) {
		rc = (*sym_crypt_init_by_name)(&crypt_dev, src);
		if (!rc) {
			rc = (*sym_crypt_deactivate_by_name)(crypt_dev, src, flags);
			if (!rc)
				cxt->flags &= ~MNT_FL_VERITYDEV_READY;
		}

		(*sym_crypt_free)(crypt_dev);
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

	assert(cxt);

	/* The mount flags have to be merged, otherwise we have to use
	 * expensive mnt_context_get_user_mflags() instead of cxt->user_mountflags. */
	assert((cxt->flags & MNT_FL_MOUNTFLAGS_MERGED));

	if (!cxt->fs)
		return 0;
	src = mnt_fs_get_srcpath(cxt->fs);
	if (!src)
		return 0;		/* backing file not set */

	if (cxt->user_mountflags & (MNT_MS_HASH_DEVICE |
				    MNT_MS_ROOT_HASH |
				    MNT_MS_HASH_OFFSET)) {
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
