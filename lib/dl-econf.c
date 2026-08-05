/*
 * No copyright is claimed.  This code is in the public domain; do with
 * it what you wish.
 */
#include <dlfcn.h>

#include "c.h"
#include "dl-econf.h"

#ifdef HAVE_LIBECONF

UL_ELF_NOTE_DLOPEN("econf",
		    "Support for libeconf",
		    UL_ELF_NOTE_DLOPEN_PRIORITY_RECOMMENDED,
		    "libeconf.so.0");

struct ul_econf_opers ul_econf;

static const struct ul_dlsym ul_econf_symbols[] =
{
	UL_DLSYM( ul_econf_opers, econf_readFile ),
	UL_DLSYM( ul_econf_opers, econf_mergeFiles ),
#ifdef HAVE_ECONF_READCONFIG
	UL_DLSYM( ul_econf_opers, econf_readConfig ),
#endif
	UL_DLSYM( ul_econf_opers, econf_readDirs ),
	UL_DLSYM( ul_econf_opers, econf_getKeys ),
	UL_DLSYM( ul_econf_opers, econf_getStringValue ),
	UL_DLSYM( ul_econf_opers, econf_getBoolValue ),
	UL_DLSYM( ul_econf_opers, econf_getUInt64Value ),
	UL_DLSYM( ul_econf_opers, econf_errString ),

	UL_DLSYM( ul_econf_opers, econf_freeFile ),
	UL_DLSYM( ul_econf_opers, econf_freeArray ),
};

int ul_dlopen_libeconf(void)
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
	status = ul_dlopen_symbols("libeconf.so.0", flags,
				   ul_econf_symbols,
				   ARRAY_SIZE(ul_econf_symbols),
				   &ul_econf, &dl) == 0 ? 1 : -1;

	return status > 0 ? 0 : -ENOSYS;
}

#endif /* HAVE_LIBECONF */
