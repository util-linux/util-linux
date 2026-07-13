/*
 * No copyright is claimed.  This code is in the public domain; do with
 * it what you wish.
 */
#include <dlfcn.h>
#include <errno.h>

#include "dl-utils.h"

/*
 * Open @libname with dlopen() and resolve all symbols listed in @syms
 * into the caller-provided @opers struct.  On success the handle is
 * stored in *@dl_handle and 0 is returned.  On failure *@dl_handle is
 * set to NULL and a negative errno value is returned (usually -ENOTSUP).
 */
int ul_dlopen_symbols(const char *libname, int flags,
		      const struct ul_dlsym *syms, size_t nsyms,
		      void *opers, void **dl_handle)
{
	size_t i;

	errno = 0;

	*dl_handle = dlopen(libname, flags);
	if (!*dl_handle)
		goto failed;

	dlerror();

	for (i = 0; i < nsyms; i++) {
		const struct ul_dlsym *def = &syms[i];
		void **sym;

		sym = (void **) ((char *) opers + def->offset);
		*sym = dlsym(*dl_handle, def->name);

		if (dlerror())
			goto failed;
	}

	return 0;
failed:
	if (*dl_handle) {
		dlclose(*dl_handle);
		*dl_handle = NULL;
	}
	if (!errno)
		errno = ENOTSUP;
	return -errno;
}
