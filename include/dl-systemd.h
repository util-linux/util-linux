/*
 * No copyright is claimed.  This code is in the public domain; do with
 * it what you wish.
 */
#ifndef UTIL_LINUX_DL_SYSTEMD_H
#define UTIL_LINUX_DL_SYSTEMD_H

#ifdef HAVE_LIBSYSTEMD

#include <sys/uio.h>

#include <systemd/sd-daemon.h>
#include <systemd/sd-login.h>
/* keep sd_journal_sendv as a plain symbol (not the _with_location macro) */
#ifndef SD_JOURNAL_SUPPRESS_LOCATION
# define SD_JOURNAL_SUPPRESS_LOCATION
#endif
#include <systemd/sd-journal.h>
#if HAVE_DECL_SD_DEVICE_NEW_FROM_SYSPATH
# include <systemd/sd-device.h>
# include <systemd/sd-event.h>
#endif

#include "dl-utils.h"

/* Pointers to libsystemd functions (initialized by dlsym()) */
struct ul_systemd_opers {
	/* sd-daemon */
	int (*sd_booted)(void);
	int (*sd_listen_fds)(int);

	/* sd-login */
	int (*sd_get_sessions)(char ***);
#if HAVE_DECL_SD_SESSION_GET_USERNAME
	int (*sd_session_get_username)(const char *, char **);
	int (*sd_session_get_tty)(const char *, char **);
#endif

	/* sd-journal */
	int (*sd_journal_open)(sd_journal **, int);
	int (*sd_journal_open_directory)(sd_journal **, const char *, int);
	void (*sd_journal_close)(sd_journal *);
	int (*sd_journal_next)(sd_journal *);
	int (*sd_journal_previous_skip)(sd_journal *, uint64_t);
	int (*sd_journal_get_realtime_usec)(sd_journal *, uint64_t *);
	int (*sd_journal_get_data)(sd_journal *, const char *,
			const void **, size_t *);
	int (*sd_journal_add_match)(sd_journal *, const void *, size_t);
	void (*sd_journal_flush_matches)(sd_journal *);
	int (*sd_journal_seek_tail)(sd_journal *);
	int (*sd_journal_sendv)(const struct iovec *, int);

#if HAVE_DECL_SD_DEVICE_NEW_FROM_SYSPATH
	/* sd-device */
	int (*sd_device_new_from_syspath)(sd_device **, const char *);
	int (*sd_device_get_property_value)(sd_device *, const char *,
			const char **);
	sd_device *(*sd_device_ref)(sd_device *);
	sd_device *(*sd_device_unref)(sd_device *);
	int (*sd_device_get_action)(sd_device *, sd_device_action_t *);
	int (*sd_device_get_devname)(sd_device *, const char **);
	int (*sd_device_get_is_initialized)(sd_device *);

	/* sd-device monitor */
	int (*sd_device_monitor_new)(sd_device_monitor **);
	sd_device_monitor *(*sd_device_monitor_unref)(sd_device_monitor *);
	sd_event *(*sd_device_monitor_get_event)(sd_device_monitor *);
	int (*sd_device_monitor_start)(sd_device_monitor *,
			sd_device_monitor_handler_t, void *);
	int (*sd_device_monitor_filter_add_match_subsystem_devtype)(
			sd_device_monitor *, const char *, const char *);

	/* sd-event */
	int (*sd_event_add_time_relative)(sd_event *, sd_event_source **,
			clockid_t, uint64_t, uint64_t,
			sd_event_time_handler_t, void *);
	int (*sd_event_loop)(sd_event *);
	int (*sd_event_exit)(sd_event *, int);
#endif
#if HAVE_DECL_SD_DEVICE_OPEN
	/* sd_device_open() and sd_device_new_from_devname() are both since systemd-251 */
	int (*sd_device_open)(sd_device *, int);
	int (*sd_device_new_from_devname)(sd_device **, const char *);
#endif
};

typedef struct ul_systemd_opers ul_systemd_opers;

extern struct ul_systemd_opers ul_systemd;

extern int ul_dlopen_libsystemd(void);

#define systemd_call(_func)	(ul_systemd._func)

#endif /* HAVE_LIBSYSTEMD */
#endif /* UTIL_LINUX_DL_SYSTEMD_H */
