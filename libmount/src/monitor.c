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
	int			type;		/* MNT_MONITOR_TYPE_* */

	unsigned int		enable : 1;

	struct list_head	ents;
};

struct libmnt_monitor {
	int			refcount;

	struct list_head	ents;
};

static int monitor_enable_entry(struct libmnt_monitor *mn,
				struct monitor_entry *me, int enable);

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

	me->fd = -1;

	return me;
}

static struct monitor_entry *monitor_get_entry(struct libmnt_monitor *mn, int type)
{
	struct list_head *p;

	assert(mn);
	assert(type);

	list_for_each(p, &mn->ents) {
		struct monitor_entry *me;

		me = list_entry(p, struct monitor_entry, ents);
		if (me->type == type)
			return me;
	}

	return NULL;
}

static struct monitor_entry *monitor_get_entry_by_fd(struct libmnt_monitor *mn, int fd)
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
 * mnt_monitor_enable_userspace:
 * @mn: monitor
 * @enable: 0 or 1
 * @filename: overwrites default
 *
 * Enables or disables userspace monitor. If the monitor does not exist and
 * enable=1 then allocates new resources necessary for the monitor.
 *
 * If high-level monitor has been already initialized (by mnt_monitor_get_fd()
 * or mnt_wait_monitor()) then it's updated according to @enable.
 *
 * The @filename is used only first time when you enable the monitor. It's
 * impossible to have more than one userspace monitor.
 *
 * Note that the current implementation of the userspace monitor is based on
 * inotify. On systems (libc) without inotify_init1() the function return
 * -ENOSYS. The dependence on inotify is implemenation specific and may be
 * changed later.
 *
 * Return: 0 on success and <0 on error
 */
int mnt_monitor_enable_userspace(struct libmnt_monitor *mn, int enable, const char *filename)
{
	struct monitor_entry *me;
	int rc = 0;

	if (!mn)
		return -EINVAL;

	me = monitor_get_entry(mn, MNT_MONITOR_TYPE_USERSPACE);
	if (me)
		return monitor_enable_entry(mn, me, enable);
	if (!enable)
		return 0;

	DBG(MONITOR, ul_debugobj(mn, "allocate new userspace monitor"));

	/* create a new entry */
	if (!mnt_has_regular_mtab(&filename, NULL))	/* /etc/mtab */
		filename = mnt_get_utab_path();		/* /run/mount/utab */
	if (!filename) {
		DBG(MONITOR, ul_debugobj(mn, "failed to get userspace mount table path"));
		return -EINVAL;
	}

	me = monitor_new_entry(mn);
	if (!me)
		goto err;

	me->type = MNT_MONITOR_TYPE_USERSPACE;
	me->path = strdup(filename);
	if (!me->path)
		goto err;

	DBG(MONITOR, ul_debugobj(mn, "allocate new userspace monitor: OK"));
	return monitor_enable_entry(mn, me, 1);
err:
	rc = -errno;
	free_monitor_entry(me);
	return rc;
}

/**
 * mnt_monitor_userspace_get_fd:
 * @mn: monitor pointer
 *
 * Returns: file descriptor to previously enabled userspace monitor or <0 on error.
 */
#ifdef HAVE_INOTIFY_INIT1
int mnt_monitor_userspace_get_fd(struct libmnt_monitor *mn)
{
	struct monitor_entry *me;
	int wd, rc;
	char *dirname, *sep;

	assert(mn);

	me = monitor_get_entry(mn, MNT_MONITOR_TYPE_USERSPACE);
	if (!me || me->enable == 0)	/* not-initialized or disabled */
		return -EINVAL;

	if (me->fd >= 0)
		return me->fd;		/* already initialized */

	assert(me->path);
	DBG(MONITOR, ul_debugobj(mn, "open userspace monitor for %s", me->path));

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

	DBG(MONITOR, ul_debugobj(mn, "new fd=%d", me->fd));
	return me->fd;
err:
	return -errno;
}

static int monitor_userspace_is_changed(struct libmnt_monitor *mn,
					struct monitor_entry *me)
{
	char wanted[NAME_MAX + 1];
	char buf[sizeof(struct inotify_event) + NAME_MAX + 1];
	struct inotify_event *event;
	char *p;
	ssize_t r;
	int rc = 0;

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

	return rc;
}

#else /* HAVE_INOTIFY_INIT1 */
int mnt_monitor_enable_userspace(
		struct libmnt_monitor *mn  __attribute__((unused)),
		int enable  __attribute__((unused)),
		const char *filename  __attribute__((unused)))
{
	return -ENOSYS;
}
int mnt_monitor_userspace_get_fd(
		struct libmnt_monitor *mn __attribute__((unused)))
{
	return -ENOSYS;
}
#endif

static int monitor_enable_entry(struct libmnt_monitor *mn,
				struct monitor_entry *me, int enable)
{
	assert(mn);
	assert(me);

	me->enable = enable ? 1 : 0;

	/* TODO : remove / add me->fd to high-level*/
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
	struct monitor_entry *me = monitor_get_entry_by_fd(mn, fd);

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
	struct monitor_entry *me = monitor_get_entry_by_fd(mn, fd);
	int rc = 0;

	if (!me)
		return 0;

	switch (me->type) {
	case MNT_MONITOR_TYPE_USERSPACE:
		rc = monitor_userspace_is_changed(mn, me);
		break;
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

	if (mnt_monitor_enable_userspace(mn, TRUE, NULL)) {
		warn("failed to initialize userspace monitor");
		goto done;
	}

	fd = mnt_monitor_userspace_get_fd(mn);
	if (fd < 0) {
		warn("failed to initialize userspace monitor fd");
		goto done;
	}

	efd = epoll_create1(EPOLL_CLOEXEC);
	if (efd < 0) {
		warn("failed to create epoll");
		goto done;
	}

	ev.events = EPOLLPRI | EPOLLIN;

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
		{ "--low-userspace", test_monitor, "tests low-level userspace monitor" },
		{ NULL }
	};

	return mnt_run_test(tss, argc, argv);
}

#endif /* TEST_PROGRAM */
