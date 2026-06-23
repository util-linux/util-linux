/*
 * No copyright is claimed.  This code is in the public domain; do with
 * it what you wish.
 *
 * Written by Karel Zak <kzak@redhat.com> [January 2021]
 */
#include "selinux-utils.h"

#ifdef HAVE_LIBSELINUX
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <errno.h>
#include <dlfcn.h>

#ifdef HAVE_SYSTEMD_SD_DLOPEN_H
#include <systemd/sd-dlopen.h>
#endif

#include "c.h"

/* The ELF .note.dlopen note advertises the optional libselinux dependency to
 * package managers and other tooling, see SD_ELF_NOTE_DLOPEN(3) and
 * https://uapi-group.org/specifications/specs/elf_dlopen_metadata/
 */
#ifdef HAVE_SYSTEMD_SD_DLOPEN_H
SD_ELF_NOTE_DLOPEN("selinux",
		   "Support for SELinux",
		   SD_ELF_NOTE_DLOPEN_PRIORITY_RECOMMENDED,
		   "libselinux.so.1");
#endif

/* Pointers to libselinux functions (initialized by dlsym()) */
struct ul_selinux_opers ul_selinux;

static void *libselinux_dl;

/* libselinux function names and offsets in 'struct ul_selinux_opers' */
struct selinux_sym {
	const char *name;
	size_t offset;		/* offset of the symbol in ul_selinux_opers */
};

#define DEF_SELINUX_SYM(_name) \
	{ \
		.name = # _name, \
		.offset = offsetof(struct ul_selinux_opers, _name), \
	}

/* All required symbols */
static const struct selinux_sym selinux_symbols[] = {
	DEF_SELINUX_SYM( is_selinux_enabled ),
	DEF_SELINUX_SYM( getfilecon ),
	DEF_SELINUX_SYM( getfilecon_raw ),
	DEF_SELINUX_SYM( fgetfilecon ),
	DEF_SELINUX_SYM( lgetfilecon ),
	DEF_SELINUX_SYM( getcon ),
	DEF_SELINUX_SYM( getprevcon ),
	DEF_SELINUX_SYM( getpidcon ),
	DEF_SELINUX_SYM( getseuserbyname ),
	DEF_SELINUX_SYM( setfilecon ),
	DEF_SELINUX_SYM( fsetfilecon ),
	DEF_SELINUX_SYM( setfscreatecon ),
	DEF_SELINUX_SYM( setexeccon ),
	DEF_SELINUX_SYM( freecon ),
	DEF_SELINUX_SYM( selinux_check_access ),
	DEF_SELINUX_SYM( security_compute_relabel ),
	DEF_SELINUX_SYM( security_get_initial_context ),
	DEF_SELINUX_SYM( selinux_file_context_cmp ),
	DEF_SELINUX_SYM( selinux_trans_to_raw_context ),
	DEF_SELINUX_SYM( string_to_security_class ),
	DEF_SELINUX_SYM( get_default_context_with_level ),
	DEF_SELINUX_SYM( selabel_open ),
	DEF_SELINUX_SYM( selabel_lookup ),
	DEF_SELINUX_SYM( selabel_close ),
	DEF_SELINUX_SYM( context_new ),
	DEF_SELINUX_SYM( context_type_set ),
	DEF_SELINUX_SYM( context_str ),
	DEF_SELINUX_SYM( context_free ),
};

/*
 * dlopen() libselinux and resolve all the symbols listed in selinux_symbols[]
 * into the global 'ul_selinux' table. The result is cached, so the library is
 * opened at most once. Returns 0 on success and a negative value when the
 * library (or any required symbol) is not available, so that callers can
 * gracefully behave as if SELinux was disabled.
 */
int ul_load_libselinux(void)
{
	static int status;	/* 0 = not tried yet, 1 = loaded, -1 = failed */
	size_t i;
	int flags = RTLD_LAZY | RTLD_LOCAL;

	if (status)
		return status > 0 ? 0 : -ENOSYS;

#ifdef RTLD_NODELETE
	/* the handle is cached for the whole process lifetime, never unload it */
	flags |= RTLD_NODELETE;
#endif
	libselinux_dl = dlopen("libselinux.so.1", flags);
	if (!libselinux_dl) {
		status = -1;
		return -ENOSYS;
	}

	/* clear errors first, then load all the libselinux symbols */
	dlerror();

	for (i = 0; i < ARRAY_SIZE(selinux_symbols); i++) {
		const struct selinux_sym *def = &selinux_symbols[i];
		void **sym;

		sym = (void **) ((char *) (&ul_selinux) + def->offset);
		*sym = dlsym(libselinux_dl, def->name);

		if (dlerror()) {
			dlclose(libselinux_dl);
			libselinux_dl = NULL;
			status = -1;
			return -ENOSYS;
		}
	}

	status = 1;
	return 0;
}

/* set the SELinux security context used for _creating_ a new file system object
 *
 * returns 0 on success,
 *     or <0 on error
 */
int ul_setfscreatecon_from_file(char *orig_file)
{
	if (ul_load_libselinux() == 0 && selinux_call(is_selinux_enabled)() > 0) {
		char *scontext = NULL;

		if (selinux_call(getfilecon)(orig_file, &scontext) < 0)
			return -1;
		if (selinux_call(setfscreatecon)(scontext) < 0) {
			selinux_call(freecon)(scontext);
			return -1;
		}
		selinux_call(freecon)(scontext);
	}
	return 0;
}

/* returns 1 if user has access to @class and @perm ("passwd", "chfn")
 *	or 0 on error,
 *	or 0 if has no access -- in this case sets @user_cxt to user-context
 */
int ul_selinux_has_access(const char *classstr, const char *perm, char **user_cxt)
{
	char *user;
	int rc;

	if (user_cxt)
		*user_cxt = NULL;

	if (ul_load_libselinux() != 0)
		return 0;

	if (selinux_call(getprevcon)(&user) != 0)
		return 0;

	rc = selinux_call(selinux_check_access)(user, user, classstr, perm, NULL);
	if (rc != 0 && user_cxt)
		*user_cxt = user;
	else
		selinux_call(freecon)(user);

	return rc == 0 ? 1 : 0;
}

/* Gets the default context for @path and @st_mode.
 *
 * returns 0 on success,
 *     or <0 on error
 */
int ul_selinux_get_default_context(const char *path, int st_mode, char **cxt)
{
	struct selabel_handle *hnd;
	struct selinux_opt options[SELABEL_NOPT] = {};
	int rc = 0;

	*cxt = NULL;

	if (ul_load_libselinux() != 0)
		return -ENOSYS;

	hnd = selinux_call(selabel_open)(SELABEL_CTX_FILE, options, SELABEL_NOPT);
	if (!hnd)
		return -errno;

	if (selinux_call(selabel_lookup)(hnd, cxt, path, st_mode) != 0)
		rc = -errno
			;
	selinux_call(selabel_close)(hnd);

	return rc;
}

#endif /* HAVE_LIBSELINUX */
