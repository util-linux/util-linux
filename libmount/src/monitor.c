/*
 * Copyright (C) 2014 Karel Zak <kzak@redhat.com>
 *
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 */

/**
 * SECTION: monitor
 * @title: Monitor
 * @short_description: interface to monitor mount tables
 *
 */

#include "fileutils.h"
#include "mountP.h"

#include <sys/inotify.h>
#include <sys/epoll.h>


enum {
	MNT_MONITOR_TYPE_NONE	= 0,
	MNT_MONITOR_TYPE_USERSPACE
};

struct monitor_entry {
	int			fd;		/* public file descriptor */
	char			*path;		/* path to the monitored file */
	unsigned int		events;		/* epoll events or zero */
	int			type;		/* MNT_MONITOR_TYPE_* */

	struct list_head	ents;
};

struct libmnt_monitor {
	int			refcount;

	struct list_head	ents;
};

/**
 * mnt_new_monitor:
 *
 * The initial refcount is 1, and needs to be decremented to
 * release the resources of the filesystem.
 *
 * Returns: newly allocated struct libmnt_monitor.
 */
struct libmnt_monitor *mnt_new_monitor(void)
{
	struct libmnt_monitor *mn = calloc(1, sizeof(*mn));
	if (!mn)
		return NULL;

	mn->refcount = 1;
	INIT_LIST_HEAD(&mn->ents);

	DBG(MONITOR, ul_debugobj(mn, "alloc"));
	return mn;
}

/**
 * mnt_ref_monitor:
 * @mn: monitor pointer
 *
 * Increments reference counter.
 */
void mnt_ref_monitor(struct libmnt_monitor *mn)
{
	if (mn)
		mn->refcount++;
}

static void free_monitor_entry(struct monitor_entry *me)
{
	if (!me)
		return;
	list_del(&me->ents);
	if (me->fd >= 0)
		close(me->fd);
	free(me->path);
	free(me);
}

static void free_monitor(struct libmnt_monitor *mn)
{

	while (!list_empty(&mn->ents)) {
		struct monitor_entry *me = list_entry(mn->ents.next,
				                  struct monitor_entry, ents);
		free_monitor_entry(me);
	}
}

/**
 * mnt_unref_monitor:
 * @mn: monitor pointer
 *
 * De-increments reference counter, on zero the @mn is automatically
 * deallocated.
 */
void mnt_unref_monitor(struct libmnt_monitor *mn)
{
	if (mn) {
		mn->refcount--;
		if (mn->refcount <= 0)
			free_monitor(mn);
	}
}

static struct monitor_entry *monitor_new_entry(struct libmnt_monitor *mn)
{
	struct monitor_entry *me;

	assert(mn);

	me = calloc(1, sizeof(*me));
	if (!me)
		return NULL;
        INIT_LIST_HEAD(&me->ents);
	list_add_tail(&me->ents, &mn->ents);

	return me;
}

/**
 * mnt_monitor_userspace_get_fd:
 * @mn: monitor pointer
 * @filename: overwrites default
 *
 * The kernel mount tables (/proc/mounts and /proc/self/mountinfo) are possible
 * to monitor by [e]poll(). This function provides the same for userspace mount
 * table.
 *
 * The userspace mount table is originaly /etc/mtab or on systems without mtab
 * it's private libmount utab file.
 *
 * The userspace mount tables are updated by rename(2), this requires that all
 * dictionary with the mount table is monitored. Be careful on systems with
 * regular /etc (see mnt_has_regular_mtab()).
 *
 * Use mnt_monitor_userspace_get_events() to get epoll events mask (e.g
 * EPOLLIN, ...).
 * 
 * Use mnt_monitor_is_changed() to verify that events on the @fd are really
 * relevant for userspace moutn table.
 *
 * If the change is detected then you can use mnt_table_parse_mtab() to parse
 * the file and mnt_diff_tables() to compare old and new version of the file.
 *
 * Returns: <0 on error or file descriptor.
 */
#ifdef HAVE_INOTIFY_INIT1
int mnt_monitor_userspace_get_fd(struct libmnt_monitor *mn, const char *filename)
{
	struct monitor_entry *me;
	int rc = 0, wd;
	char *dirname, *sep;

	assert(mn);

	if (!filename) {
		if (!mnt_has_regular_mtab(&filename, NULL))	/* /etc/mtab */
			filename = mnt_get_utab_path();		/* /run/mount/utab */
		if (!filename) {
			DBG(MONITOR, ul_debugobj(mn, "failed to get userspace mount table path"));
			return -EINVAL;
		}
	}

	DBG(MONITOR, ul_debugobj(mn, "new userspace monitor for %s requested", filename));

	me = monitor_new_entry(mn);
	if (!me)
		goto err;

	me->type = MNT_MONITOR_TYPE_USERSPACE;
	me->path = strdup(filename);
	if (!me->path)
		goto err;

	dirname = me->path;
	sep = stripoff_last_component(dirname);	/* add \0 between dir/filename */

	/* make sure the directory exists */
	rc = mkdir(dirname, S_IWUSR|
			    S_IRUSR|S_IRGRP|S_IROTH|
			    S_IXUSR|S_IXGRP|S_IXOTH);
	if (rc && errno != EEXIST)
		goto err;

	/* initialize inotify stuff */
	me->fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
	if (me->fd < 0)
		goto err;

	/*
	 * libmount uses rename(2) to atomically update utab/mtab, the finame
	 * change is possible to detect by IN_MOVE_TO inotify event.
	 */
	wd = inotify_add_watch(me->fd, dirname, IN_MOVED_TO);
	if (wd < 0)
		goto err;

	if (sep && sep > dirname)
		*(sep - 1) = '/';		/* set '/' back to the path */

	me->events = EPOLLIN | EPOLLPRI;

	DBG(MONITOR, ul_debugobj(mn, "new fd=%d", me->fd));
	return me->fd;
err:
	rc = -errno;
	free_monitor_entry(me);
	return rc;
}
#else /* HAVE_INOTIFY_INIT1 */
int mnt_monitor_userspace_get_fd(struct libmnt_monitor *mn __attribute__((unused)),
				const char *filename  __attribute__((unused)))
{
	return -ENOSYS;
}
#endif

static struct monitor_entry *get_monitor_entry(struct libmnt_monitor *mn, int fd)
{
	struct list_head *p;

	assert(mn);

	if (fd < 0)
		return NULL;

	list_for_each(p, &mn->ents) {
		struct monitor_entry *me;

		me = list_entry(p, struct monitor_entry, ents);
		if (me->fd == fd)
			return me;
	}

	DBG(MONITOR, ul_debugobj(mn, "failed to get entry for fd=%d", fd));
	return NULL;
}

/**
 * mnt_monitor_get_events:
 * @mn: monitor
 * @fd: event file descriptor
 * @event: returns epoll event mask
 *
 * Returns: on on success, <0 on error.
 */
int mnt_monitor_get_events(struct libmnt_monitor *mn, int fd, unsigned int *event)
{
	struct monitor_entry *me = get_monitor_entry(mn, fd);

	if (!me || !event)
		return -EINVAL;
	*event = me->events;
	return 0;
}

/**
 * mnt_monitor_get_filename:
 * @mn: monitor
 * @fd: event file descriptor
 *
 * Returns: filename monitored by @fd or NULL on error.
 */
const char *mnt_monitor_get_filename(struct libmnt_monitor *mn, int fd)
{
	struct monitor_entry *me = get_monitor_entry(mn, fd);

	if (!me)
		return NULL;
	return me->path;
}

/**
 * mnt_monitor_is_changed:
 * @mn: monitor
 * @fd: event file descriptor
 *
 * Returns: 1 of the file monitored by @fd has been changed
 */
int mnt_monitor_is_changed(struct libmnt_monitor *mn, int fd)
{
	struct monitor_entry *me = get_monitor_entry(mn, fd);
	int rc = 0;

	if (!me)
		return 0;


	switch (me->type) {
#ifdef HAVE_INOTIFY_INIT1
	case MNT_MONITOR_TYPE_USERSPACE:
	{
		char wanted[NAME_MAX + 1];
		char buf[sizeof(struct inotify_event) + NAME_MAX + 1];
		struct inotify_event *event;
                char *p;
		ssize_t r;

		DBG(MONITOR, ul_debugobj(mn, "checking fd=%d for userspace changes", me->fd));

		p = strrchr(me->path, '/');
		if (!p)
			p = me->path;
		else
			p++;
		strncpy(wanted, p, sizeof(wanted) - 1);
		wanted[sizeof(wanted) - 1] = '\0';
		rc = 0;

		DBG(MONITOR, ul_debugobj(mn, "wanted file: '%s'", wanted));

                while ((r = read(me->fd, buf, sizeof(buf))) > 0) {
			for (p = buf; p < buf + r; ) {
				event = (struct inotify_event *) p;

				if (strcmp(event->name, wanted) == 0)
					rc = 1;
				p += sizeof(struct inotify_event) + event->len;
                        }
			if (rc)
				break;
                }
		break;
	}
#endif
	default:
		return 0;
	}

	DBG(MONITOR, ul_debugobj(mn, "fd=%d %s", me->fd, rc ? "changed" : "unchanged"));
	return rc;
}


#ifdef TEST_PROGRAM

int test_monitor(struct libmnt_test *ts, int argc, char *argv[])
{
	struct libmnt_monitor *mn;
	int fd, efd = -1, rc = -1;
	struct epoll_event ev = { .events = 0 };

	mn = mnt_new_monitor();
	if (!mn) {
		warn("failed to allocate monitor");
		goto done;
	}

	/* monitor userspace mount table changes */
	fd = mnt_monitor_userspace_get_fd(mn, NULL);
	if (fd < 0) {
		warn("failed to initialize userspace mount table fd");
		goto done;
	}

	efd = epoll_create1(EPOLL_CLOEXEC);
	if (efd < 0) {
		warn("failed to create epoll");
		goto done;
	}

	mnt_monitor_get_events(mn, fd, &ev.events);

	/* set data is necessary only if you want to use epoll for more file
	 * descriptors, then epoll_wait() returns data associated with the file
	 * descriptor. */
	ev.data.fd = fd;

	rc = epoll_ctl(efd, EPOLL_CTL_ADD, fd, &ev);
	if (rc < 0) {
		warn("failed to add fd to epoll");
		goto done;
	}

	printf("waiting for changes...\n");
	do {
		struct epoll_event events[1];
		int n, nfds = epoll_wait(efd, events, 1, -1);

		if (nfds < 0) {
			rc = -errno;
			warn("polling error");
			goto done;
		}

		for (n = 0; n < nfds; n++) {
			if (events[n].data.fd == fd &&
			    mnt_monitor_is_changed(mn, fd) == 1)
				printf("%s: change detected\n",
						mnt_monitor_get_filename(mn, fd));
		}
	} while (1);

	rc = 0;
done:
	printf("done");
	mnt_unref_monitor(mn);
	if (efd >= 0)
		close(efd);
	return rc;
}

int main(int argc, char *argv[])
{
	struct libmnt_test tss[] = {
		{ "--monitor", test_monitor, "print change" },
		{ NULL }
	};

	return mnt_run_test(tss, argc, argv);
}

#endif /* TEST_PROGRAM */
