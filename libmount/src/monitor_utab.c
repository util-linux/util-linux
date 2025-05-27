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
 *
 * libmount userspace monitor (based on /run/mount/utab inotify)
 */

#include "mountP.h"
#include "monitor.h"

#include "fileutils.h"

#include <sys/inotify.h>
#include <sys/epoll.h>

static int userspace_monitor_close_fd(
			struct libmnt_monitor *mn __attribute__((__unused__)),
			struct monitor_entry *me)
{
	assert(me);

	if (me->fd >= 0)
		close(me->fd);
	me->fd = -1;
	return 0;
}

static int userspace_add_watch(struct monitor_entry *me, int *final, int *fd)
{
	char *filename = NULL;
	int wd, rc = -EINVAL;

	assert(me);
	assert(me->path);

	/*
	 * libmount uses utab.event file to monitor and control utab updates
	 */
	if (asprintf(&filename, "%s.event", me->path) <= 0) {
		rc = -ENOMEM;
		goto done;
	}

	/* try event file if already exists */
	errno = 0;
	wd = inotify_add_watch(me->fd, filename, IN_CLOSE_WRITE);
	if (wd >= 0) {
		DBG(MONITOR, ul_debug(" added inotify watch for %s [fd=%d]", filename, wd));
		rc = 0;
		if (final)
			*final = 1;
		if (fd)
			*fd = wd;
		goto done;
	} else if (errno != ENOENT) {
		rc = -errno;
		goto done;
	}

	while (strchr(filename, '/')) {
		stripoff_last_component(filename);
		if (!*filename)
			break;

		/* try directory where is the event file */
		errno = 0;
		wd = inotify_add_watch(me->fd, filename, IN_CREATE|IN_ISDIR);
		if (wd >= 0) {
			DBG(MONITOR, ul_debug(" added inotify watch for %s [fd=%d]", filename, wd));
			rc = 0;
			if (fd)
				*fd = wd;
			break;
		}

		if (errno != ENOENT) {
			rc = -errno;
			break;
		}
	}
done:
	free(filename);
	return rc;
}

static int userspace_monitor_get_fd(struct libmnt_monitor *mn,
				    struct monitor_entry *me)
{
	int rc;

	if (!me || me->enabled == 0)	/* not-initialized or disabled */
		return -EINVAL;
	if (me->fd >= 0)
		return me->fd;		/* already initialized */

	assert(me->path);
	DBG(MONITOR, ul_debugobj(mn, " open userspace monitor for %s", me->path));

	me->fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
	if (me->fd < 0)
		goto err;

	if (userspace_add_watch(me, NULL, NULL) < 0)
		goto err;

	return me->fd;
err:
	rc = -errno;
	if (me->fd >= 0)
		close(me->fd);
	me->fd = -1;
	DBG(MONITOR, ul_debugobj(mn, "failed to create userspace monitor [rc=%d]", rc));
	return rc;
}

/*
 * verify and drain inotify buffer
 */
static int userspace_process_event(struct libmnt_monitor *mn,
					struct monitor_entry *me)
{
	char buf[sizeof(struct inotify_event) + NAME_MAX + 1];
	int status = 0;

	if (!me || me->fd < 0)
		return 0;

	DBG(MONITOR, ul_debugobj(mn, "process utab event"));

	/* the me->fd is non-blocking */
	do {
		ssize_t len;
		char *p;
		const struct inotify_event *e;

		len = read(me->fd, buf, sizeof(buf));
		if (len < 0)
			break;

		for (p = buf; p < buf + len;
		     p += sizeof(struct inotify_event) + e->len) {

			int fd = -1;

			e = (const struct inotify_event *) p;
			DBG(MONITOR, ul_debugobj(mn, " inotify event 0x%x [%s]\n", e->mask, e->len ? e->name : ""));

			if (e->mask & IN_CLOSE_WRITE)
				status = 1;
			else {
				/* add watch for the event file */
				userspace_add_watch(me, &status, &fd);

				if (fd != e->wd) {
					DBG(MONITOR, ul_debugobj(mn, " removing watch [fd=%d]", e->wd));
					inotify_rm_watch(me->fd, e->wd);
				}
			}
		}
	} while (1);

	DBG(MONITOR, ul_debugobj(mn, "%s", status == 1 ? " success" : " nothing"));
	return status;
}

/*
 * userspace monitor operations
 */
static const struct monitor_opers userspace_opers = {
	.op_get_fd		= userspace_monitor_get_fd,
	.op_close_fd		= userspace_monitor_close_fd,
	.op_process_event	= userspace_process_event
};

/**
 * mnt_monitor_enable_userspace:
 * @mn: monitor
 * @enable: 0 or 1
 * @filename: overwrites default
 *
 * Enables or disables userspace monitoring. If the userspace monitor does not
 * exist and enable=1 then allocates new resources necessary for the monitor.
 *
 * If the top-level monitor has been already created (by mnt_monitor_get_fd()
 * or mnt_monitor_wait()) then it's updated according to @enable.
 *
 * The @filename is used only the first time when you enable the monitor. It's
 * impossible to have more than one userspace monitor. The recommended is to
 * use NULL as filename.
 *
 * The userspace monitor is unsupported for systems with classic regular
 * /etc/mtab file.
 *
 * Return: 0 on success and <0 on error
 */
int mnt_monitor_enable_userspace(struct libmnt_monitor *mn, int enable, const char *filename)
{
	struct monitor_entry *me;
	int rc = 0;

	if (!mn)
		return -EINVAL;

	me = monitor_get_entry(mn, MNT_MONITOR_TYPE_USERSPACE, -1);
	if (me) {
		rc = monitor_modify_epoll(mn, me, enable);
		if (!enable)
			userspace_monitor_close_fd(mn, me);
		return rc;
	}
	if (!enable)
		return 0;

	DBG(MONITOR, ul_debugobj(mn, "allocate new userspace monitor"));

	if (!filename)
		filename = mnt_get_utab_path();		/* /run/mount/utab */
	if (!filename) {
		DBG(MONITOR, ul_debugobj(mn, "failed to get userspace mount table path"));
		return -EINVAL;
	}

	me = monitor_new_entry(mn);
	if (!me)
		goto err;

	me->type = MNT_MONITOR_TYPE_USERSPACE;
	me->opers = &userspace_opers;
	me->events = EPOLLIN;
	me->path = strdup(filename);
	if (!me->path)
		goto err;

	return monitor_modify_epoll(mn, me, TRUE);
err:
	rc = -errno;
	free_monitor_entry(me);
	DBG(MONITOR, ul_debugobj(mn, "failed to allocate userspace monitor [rc=%d]", rc));
	return rc;
}
