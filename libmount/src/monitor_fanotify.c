/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * This file is part of libmount from util-linux project.
 *
 * Copyright (C) 2014-2025 Karel Zak <kzak@redhat.com>
 *
 * libmount is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * libmount kernel mount table monitor (based on fanotify)
 */
#include "mountP.h"
#include "monitor.h"

#include "strutils.h"
#include "pathnames.h"

#include <sys/fanotify.h>
#include <sys/epoll.h>

#ifndef FAN_MNT_ATTACH
struct fanotify_event_info_mnt {
	struct fanotify_event_info_header hdr;
	uint64_t mnt_id;
};
# define FAN_MNT_ATTACH 0x01000000 /* Mount was attached */
#endif

#ifndef FAN_MNT_DETACH
# define FAN_MNT_DETACH 0x02000000 /* Mount was detached */
#endif

#ifndef FAN_REPORT_MNT
# define FAN_REPORT_MNT 0x00004000 /* Report mount events */
#endif

#ifndef FAN_MARK_MNTNS
# define FAN_MARK_MNTNS 0x00000110
#endif

/* private monitor data */
struct monitor_entrydata {
	int	ns_fd;		/* namespace file descriptor */

	char	buf[BUFSIZ];
	char	*current;	/* first unprocessed event in buf[] */
	size_t	remaining;	/* unprocessed events in buf[]  */
};

static int fanotify_close_fd(
			struct libmnt_monitor *mn __attribute__((__unused__)),
			struct monitor_entry *me)
{
	assert(me);

	if (me->fd >= 0)
		close(me->fd);
	me->fd = -1;
	return 0;
}

static int fanotify_free_data(struct monitor_entry *me)
{
	struct monitor_entrydata *data;

	if (!me || !me->data)
		return 0;

	data = (struct monitor_entrydata *) me->data;

	/* The namespace FD may be used as a monitor identifier. In this case, it's
	 * opened by the application, and the library should not close it. */
	if (data->ns_fd >= 0 && me->id != data->ns_fd)
		close(data->ns_fd);

	free(me->data);
	me->data = NULL;
	return 0;
}

/* returns FD or <0 on error */
static int fanotify_get_fd(struct libmnt_monitor *mn, struct monitor_entry *me)
{
	struct monitor_entrydata *data;
	int rc;

	if (!me || !me->enabled)	/* not-initialized or disabled */
		return -EINVAL;
	if (me->fd >= 0)
		return me->fd;		/* already initialized */

	assert(me->path);
	assert(me->data);

	data = (struct monitor_entrydata *) me->data;
	assert(data->ns_fd >= 0);
	DBG(MONITOR, ul_debugobj(mn, " opening fanotify for %s", me->path));

	me->fd = fanotify_init(FAN_REPORT_MNT | FAN_CLOEXEC | FAN_NONBLOCK, 0);

	if (me->fd >= 0 &&
	    fanotify_mark(me->fd, FAN_MARK_ADD | FAN_MARK_MNTNS,
			FAN_MNT_ATTACH | FAN_MNT_DETACH, data->ns_fd, NULL) == 0) {

		return me->fd; /* success */
	}

	/* failed */
	rc = -errno;
	if (me->fd >= 0) {
		close(me->fd);
		me->fd = -1;
	}
	DBG(MONITOR, ul_debugobj(mn, "failed to open fanotify FD [rc=%d]", rc));
	return rc;
}

/* Returns: <0 error; 0 success; 1 nothing */
static int fanotify_process_event(
			struct libmnt_monitor *mn,
			struct monitor_entry *me)
{
	struct monitor_entrydata *data;
	ssize_t len;

	if (!mn || !me || me->fd < 0 || !me->data)
		return -EINVAL;

	DBG(MONITOR, ul_debugobj(mn, "reading fanotify event"));

	data = (struct monitor_entrydata *) me->data;
	data->remaining = 0;
	data->current = data->buf;

	if (mn->kernel_veiled && access(MNT_PATH_UTAB ".act", F_OK) == 0) {
		DBG(MONITOR, ul_debugobj(mn, " kernel event veiled"));

		/* just drain out */
		do {
			len = read(me->fd, data->buf, sizeof(data->buf));
			if (len < 0)
				break;
		} while (1);

		return 1;
	}

	len = read(me->fd, data->buf, sizeof(data->buf));
	if (len <= 0)
		return 1;	/* nothing */

	data->remaining = (size_t) len;
	DBG(MONITOR, ul_debugobj(mn, " fanotify event [len=%zu %p]",
				data->remaining, data->current));

	return 0;
}

/* Returns: <0 error; 0 success; 1 nothing */
static int fanotify_next_fs(struct libmnt_monitor *mn, struct monitor_entry *me,
			struct libmnt_fs *fs)
{
	struct monitor_entrydata *data;
	size_t thislen;

	struct fanotify_event_metadata *meta;
	struct fanotify_event_info_mnt *mnt;

	if (!mn || !me || me->fd < 0 || !me->data)
		return -EINVAL;

	DBG(MONITOR, ul_debugobj(mn, "next fanotify fs"));

	data = (struct monitor_entrydata *) me->data;
	if (!fs || !data->remaining)
		goto nothing;
	 else {
		struct libmnt_statmnt *tmp = fs->stmnt;
		fs->stmnt = NULL;
		mnt_reset_fs(fs);
		fs->stmnt = tmp;
	}

	meta = (struct fanotify_event_metadata *) data->current;

	if (!FAN_EVENT_OK(meta, data->remaining) ||
	    meta->vers != FANOTIFY_METADATA_VERSION)
		goto nothing;

	thislen = meta->event_len - sizeof(*meta);

	mnt = (struct fanotify_event_info_mnt *)
			(data->current + meta->event_len - thislen);


	mnt_fs_set_uniq_id(fs, mnt->mnt_id);

	if (meta->mask & FAN_MNT_ATTACH)
		fs->flags |= MNT_FS_STATUS_ATTACH;
	if (meta->mask & FAN_MNT_DETACH)
		fs->flags |= MNT_FS_STATUS_DETACH;

	DBG(MONITOR, ul_debugobj(mn, "fanotify fs id=%ju %s\n",
		(uintmax_t) mnt_fs_get_uniq_id(fs),
		mnt_fs_is_attached(fs) ? "ATTACHED" :
		mnt_fs_is_detached(fs) ? "DETACHED" :
		mnt_fs_is_moved(fs) ? "MOVED" : "???"));

	data->current = (char *) FAN_EVENT_NEXT(meta, data->remaining);
	return 0;

nothing:
	data->remaining = 0;
	return 1;
}

/*
 * fanotify kernel monitor operations
 */
static const struct monitor_opers fanotify_opers = {
	.op_get_fd		= fanotify_get_fd,
	.op_close_fd		= fanotify_close_fd,
	.op_free_data		= fanotify_free_data,
	.op_process_event	= fanotify_process_event,
	.op_next_fs		= fanotify_next_fs
};

/**
 * mnt_monitor_enable_fanotify:
 * @mn: monitor
 * @enable: 0 or 1
 * @ns: namespace file descritor (use -1 for default /proc/self/ns/mnt)
 *
 * Enables or disables kernel VFS monitoring based on fanotify (since Linux
 * 6.15). This monitor can return mount IDs of changed mount points. It's also
 * possible to enable multiple monitors for different namespaces.  If you do
 * not need this advanced functionality, use the classic
 * mnt_monitor_enable_mountinfo().
 *
 * If the monitor does not exist and enable=1, it allocates new resources
 * necessary for the monitor.
 *
 * If the top-level monitor has already been created (by mnt_monitor_get_fd()
 * or mnt_monitor_wait()), it is updated according to @enable.
 *
 * The function mnt_monitor_next_change() returns the namespace filename for
 * this monitor (default is "/proc/self/ns/mnt").
 *
 * The function mnt_monitor_event_next_fs() can return filesystems associated
 * with the last event.
 *
 * Return: 0 on success and <0 on error
 */
int mnt_monitor_enable_fanotify(struct libmnt_monitor *mn, int enable, int ns)
{
	struct monitor_entry *me;
	struct monitor_entrydata *data;
	int rc = 0;

	if (!mn)
		return -EINVAL;

	me = monitor_get_entry(mn, MNT_MONITOR_TYPE_FANOTIFY, ns);
	if (me) {
		rc = monitor_modify_epoll(mn, me, enable);
		if (!enable)
			fanotify_close_fd(mn, me);
		return rc;
	}
	if (!enable)
		return 0;

	DBG(MONITOR, ul_debugobj(mn, "allocate new fanotify monitor"));

	/* create a new entry */
	me = monitor_new_entry(mn);
	if (!me)
		goto err;

	data = calloc(1, sizeof(*data));
	if (!data)
		goto err;

	data->ns_fd = ns;

	me->data = data;
	me->id = ns;

	if (data->ns_fd < 0) {
		/* In this case, NS differs from ID, meaning that NS is private
		 * and will be closed by fanotify_free_data() (called from
		 * free_monitor_entry()).
		 */
		data->ns_fd = open(_PATH_PROC_NSDIR "/mnt", O_RDONLY);
		if (data->ns_fd < 0)
			goto err;

		/* The path is just a placeholder to have something returned by
		 * mnt_monitor_next_change().
		 */
		me->path = strdup(_PATH_PROC_NSDIR "/mnt");
		if (!me->path)
			goto err;
	} else {
		if (asprintf(&me->path, _PATH_PROC_FDDIR "/%d", data->ns_fd) < 0)
			goto err;
	}

	me->events = EPOLLIN;
	me->type = MNT_MONITOR_TYPE_FANOTIFY;
	me->opers = &fanotify_opers;

	return monitor_modify_epoll(mn, me, TRUE);
err:
	rc = -errno;
	free_monitor_entry(me);
	DBG(MONITOR, ul_debugobj(mn, "failed to allocate fanotify monitor [rc=%d]", rc));
	return rc;
}
