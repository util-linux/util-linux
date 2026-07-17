/*
 * No copyright is claimed.  This code is in the public domain; do with
 * it what you wish.
 */
#include <dlfcn.h>

#include "c.h"
#include "dl-systemd.h"

#ifdef HAVE_LIBSYSTEMD

UL_ELF_NOTE_DLOPEN("systemd",
		    "Support for systemd",
		    UL_ELF_NOTE_DLOPEN_PRIORITY_RECOMMENDED,
		    "libsystemd.so.0");

struct ul_systemd_opers ul_systemd;

static const struct ul_dlsym ul_systemd_symbols[] =
{
	UL_DLSYM( ul_systemd_opers, sd_booted ),
	UL_DLSYM( ul_systemd_opers, sd_listen_fds ),

	UL_DLSYM( ul_systemd_opers, sd_get_sessions ),
#if HAVE_DECL_SD_SESSION_GET_USERNAME
	UL_DLSYM( ul_systemd_opers, sd_session_get_username ),
	UL_DLSYM( ul_systemd_opers, sd_session_get_tty ),
#endif

	UL_DLSYM( ul_systemd_opers, sd_journal_open ),
	UL_DLSYM( ul_systemd_opers, sd_journal_open_directory ),
	UL_DLSYM( ul_systemd_opers, sd_journal_close ),
	UL_DLSYM( ul_systemd_opers, sd_journal_next ),
	UL_DLSYM( ul_systemd_opers, sd_journal_previous_skip ),
	UL_DLSYM( ul_systemd_opers, sd_journal_get_realtime_usec ),
	UL_DLSYM( ul_systemd_opers, sd_journal_get_data ),
	UL_DLSYM( ul_systemd_opers, sd_journal_add_match ),
	UL_DLSYM( ul_systemd_opers, sd_journal_flush_matches ),
	UL_DLSYM( ul_systemd_opers, sd_journal_seek_tail ),
	UL_DLSYM( ul_systemd_opers, sd_journal_sendv ),

#if HAVE_DECL_SD_DEVICE_NEW_FROM_SYSPATH
	UL_DLSYM( ul_systemd_opers, sd_device_new_from_syspath ),
	UL_DLSYM( ul_systemd_opers, sd_device_get_property_value ),
	UL_DLSYM( ul_systemd_opers, sd_device_ref ),
	UL_DLSYM( ul_systemd_opers, sd_device_unref ),
	UL_DLSYM( ul_systemd_opers, sd_device_get_action ),
	UL_DLSYM( ul_systemd_opers, sd_device_get_devname ),
	UL_DLSYM( ul_systemd_opers, sd_device_get_is_initialized ),

	UL_DLSYM( ul_systemd_opers, sd_device_monitor_new ),
	UL_DLSYM( ul_systemd_opers, sd_device_monitor_unref ),
	UL_DLSYM( ul_systemd_opers, sd_device_monitor_get_event ),
	UL_DLSYM( ul_systemd_opers, sd_device_monitor_start ),
	UL_DLSYM( ul_systemd_opers, sd_device_monitor_filter_add_match_subsystem_devtype ),

	UL_DLSYM( ul_systemd_opers, sd_event_add_time_relative ),
	UL_DLSYM( ul_systemd_opers, sd_event_loop ),
	UL_DLSYM( ul_systemd_opers, sd_event_exit ),
#endif
#if HAVE_DECL_SD_DEVICE_OPEN
	UL_DLSYM( ul_systemd_opers, sd_device_open ),
	UL_DLSYM( ul_systemd_opers, sd_device_new_from_devname ),
#endif
};

int ul_dlopen_libsystemd(void)
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
				   ul_systemd_symbols,
				   ARRAY_SIZE(ul_systemd_symbols),
				   &ul_systemd, &dl) == 0 ? 1 : -1;

	return status > 0 ? 0 : -ENOSYS;
}

#endif /* HAVE_LIBSYSTEMD */
