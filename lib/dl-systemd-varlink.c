/*
 * No copyright is claimed.  This code is in the public domain; do with
 * it what you wish.
 */
#include <dlfcn.h>

#include "c.h"
#include "dl-systemd-varlink.h"

#if defined(HAVE_SYSTEMD_VARLINK) && defined(USE_DLOPEN_SYSTEMD)

UL_ELF_NOTE_DLOPEN("systemd",
		    "Support for systemd",
		    UL_ELF_NOTE_DLOPEN_PRIORITY_RECOMMENDED,
		    "libsystemd.so.0");

struct ul_systemd_varlink_opers ul_systemd_varlink;

static const struct ul_dlsym ul_systemd_varlink_symbols[] =
{
	UL_DLSYM( ul_systemd_varlink_opers, sd_varlink_connect_address ),
	UL_DLSYM( ul_systemd_varlink_opers, sd_varlink_set_allow_fd_passing_output ),
	UL_DLSYM( ul_systemd_varlink_opers, sd_varlink_push_dup_fd ),
	UL_DLSYM( ul_systemd_varlink_opers, sd_varlink_callb ),
	UL_DLSYM( ul_systemd_varlink_opers, sd_varlink_close_unref ),
	UL_DLSYM( ul_systemd_varlink_opers, sd_json_variant_unref ),
};

int ul_dlopen_libsystemd_varlink(void)
{
	/* 0 = not tried, 1 = loaded, -1 = failed */
	static int status = 0;
	static void *dl = NULL;
	int flags = RTLD_LAZY | RTLD_LOCAL;

	if (status)
		return status > 0 ? 0 : -ENOSYS;

#ifdef RTLD_NODELETE
	flags |= RTLD_NODELETE;
#endif
	status = ul_dlopen_symbols("libsystemd.so.0", flags,
				   ul_systemd_varlink_symbols,
				   ARRAY_SIZE(ul_systemd_varlink_symbols),
				   &ul_systemd_varlink, &dl) == 0 ? 1 : -1;

	return status > 0 ? 0 : -ENOSYS;
}

#endif /* HAVE_SYSTEMD_VARLINK && USE_DLOPEN_SYSTEMD */
