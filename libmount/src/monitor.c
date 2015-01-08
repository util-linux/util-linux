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
#include "pathnames.h"

#include <sys/inotify.h>
#include <sys/epoll.h>


struct monitor_opers;

struct monitor_entry {
	int			fd;		/* private entry file descriptor */
	char			*path;		/* path to the monitored file */
	int			type;		/* MNT_MONITOR_TYPE_* */
	uint32_t		events;		/* wanted epoll events */

	const struct monitor_opers *opers;

	unsigned int		enable : 1,
				changed : 1;

	struct list_head	ents;
};

struct libmnt_monitor {
	int			refcount;
	int			fd;		/* public monitor file descriptor */

	struct list_head	ents;
};

struct monitor_opers {
	int (*op_get_fd)(struct libmnt_monitor *, struct monitor_entry *);
	int (*op_close_fd)(struct libmnt_monitor *, struct monitor_entry *);
	void (*op_event_cleanup)(struct libmnt_monitor *, struct monitor_entry *);
};

static int monitor_modify_epoll(struct libmnt_monitor *mn,
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
	mn->fd = -1;
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

/**
 * mnt_unref_monitor:
 * @mn: monitor pointer
 *
 * De-increments reference counter, on zero the @mn is automatically
 * deallocated.
 */
void mnt_unref_monitor(struct libmnt_monitor *mn)
{
	if (!mn)
		return;

	mn->refcount--;
	if (mn->refcount <= 0) {
		mnt_monitor_close_fd(mn);	/* destroys all file descriptors */

		while (!list_empty(&mn->ents)) {
			struct monitor_entry *me = list_entry(mn->ents.next,
						  struct monitor_entry, ents);
			free_monitor_entry(me);
		}

		free(mn);
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

static int monitor_next_entry(struct libmnt_monitor *mn,
			      struct libmnt_iter *itr,
			      struct monitor_entry **me)
{
	int rc = 1;

	assert(mn);
	assert(itr);
	assert(me);

	if (!mn || !itr || !me)
		return -EINVAL;

	*me = NULL;

	if (!itr->head)
		MNT_ITER_INIT(itr, &mn->ents);
	if (itr->p != itr->head) {
		MNT_ITER_ITERATE(itr, *me, struct monitor_entry, ents);
		rc = 0;
	}

	return rc;
}

/* returns entry by type */
static struct monitor_entry *monitor_get_entry(struct libmnt_monitor *mn, int type)
{
	struct libmnt_iter itr;
	struct monitor_entry *me;

	mnt_reset_iter(&itr, MNT_ITER_FORWARD);
	while (monitor_next_entry(mn, &itr, &me) == 0) {
		if (me->type == type)
			return me;
	}
	return NULL;
}


/*
 * Userspace monitor
 */

static int userspace_monitor_close_fd(struct libmnt_monitor *mn,
				    struct monitor_entry *me)
{
	assert(mn);
	assert(me);

	if (me->fd >= 0)
		close(me->fd);
	me->fd = -1;
	return 0;
}

static int userspace_monitor_get_fd(struct libmnt_monitor *mn,
				    struct monitor_entry *me)
{
	int wd, rc, dummy_fd;
	char *dirname, *sep;
	char *filename = NULL;

	assert(mn);
	assert(me);

	if (!me || me->enable == 0)	/* not-initialized or disabled */
		return -EINVAL;
	if (me->fd >= 0)
		return me->fd;		/* already initialized */

	assert(me->path);
	DBG(MONITOR, ul_debugobj(mn, " open userspace monitor for %s", me->path));

	dirname = me->path;
	sep = stripoff_last_component(dirname);	/* add \0 between dir/filename */

	/* make sure the directory exists */
	rc = mkdir(dirname, S_IWUSR|
			    S_IRUSR|S_IRGRP|S_IROTH|
			    S_IXUSR|S_IXGRP|S_IXOTH);

	if (sep && sep > dirname)
		*(sep - 1) = '/';		/* set '/' back to the path */

	if (rc && errno != EEXIST)
		goto err;

	/*
	 * libmount uses rename(2) to atomically update utab/mtab, monitor
	 * rename changes is too tricky. It seems better to monitor utab
	 * lockfile close.
	 */
	if (asprintf(&filename, "%s.lock", me->path) <= 0)
		goto err;

	/* make sure the file exists */
	dummy_fd = open(filename, O_RDONLY|O_CREAT|O_CLOEXEC,
				  S_IWUSR|S_IRUSR|S_IRGRP|S_IROTH);
	if (dummy_fd < 0)
		goto err;
	close(dummy_fd);

	/* initialize inotify stuff */
	me->fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
	if (me->fd < 0)
		goto err;


	wd = inotify_add_watch(me->fd, filename, IN_CLOSE_NOWRITE);
	if (wd < 0)
		goto err;

	free(filename);
	return me->fd;
err:
	rc = -errno;
	free(filename);
	if (me->fd)
		close(me->fd);
	me->fd = -1;
	DBG(MONITOR, ul_debugobj(mn, "failed to create userspace monitor [rc=%d]", rc));
	return rc;
}

/*
 * drain inotify buffer
 */
static void userspace_event_cleanup(struct libmnt_monitor *mn,
					struct monitor_entry *me)
{
	char buf[sizeof(struct inotify_event) + NAME_MAX + 1];

	if (!me || me->fd < 0)
		return;

	DBG(MONITOR, ul_debugobj(mn, "drain userspace monitor inotify"));

	/* the 'fd' is non-blocking */
	while (read(me->fd, buf, sizeof(buf)) > 0);
}

/*
 * userspace monitor operations
 */
static const struct monitor_opers userspace_opers = {
	.op_get_fd		= userspace_monitor_get_fd,
	.op_close_fd		= userspace_monitor_close_fd,
	.op_event_cleanup	= userspace_event_cleanup
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
 * The @filename is used only first time when you enable the monitor. It's
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

	me = monitor_get_entry(mn, MNT_MONITOR_TYPE_USERSPACE);
	if (me) {
		rc = monitor_modify_epoll(mn, me, enable);
		if (!enable)
			userspace_monitor_close_fd(mn, me);
		return rc;
	}
	if (!enable)
		return 0;

	DBG(MONITOR, ul_debugobj(mn, "allocate new userspace monitor"));

	/* create a new entry */
	if (mnt_has_regular_mtab(NULL, NULL))
		return -ENOSYS;

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
	me->events = EPOLLPRI | EPOLLIN;
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


/*
 * Kernel monitor
 */

static int kernel_monitor_close_fd(struct libmnt_monitor *mn,
				   struct monitor_entry *me)
{
	assert(mn);
	assert(me);

	if (me->fd >= 0)
		close(me->fd);
	me->fd = -1;
	return 0;
}

static int kernel_monitor_get_fd(struct libmnt_monitor *mn,
				 struct monitor_entry *me)
{
	int rc;

	assert(mn);
	assert(me);

	if (!me || me->enable == 0)	/* not-initialized or disabled */
		return -EINVAL;
	if (me->fd >= 0)
		return me->fd;		/* already initialized */

	assert(me->path);
	DBG(MONITOR, ul_debugobj(mn, " open kernel monitor for %s", me->path));

	me->fd = open(me->path, O_RDONLY|O_CLOEXEC);
	if (me->fd < 0)
		goto err;

	return me->fd;
err:
	rc = -errno;
	DBG(MONITOR, ul_debugobj(mn, "failed to create kernel  monitor [rc=%d]", rc));
	return rc;
}

/*
 * kernel monitor operations
 */
static const struct monitor_opers kernel_opers = {
	.op_get_fd		= kernel_monitor_get_fd,
	.op_close_fd		= kernel_monitor_close_fd,
};

/**
 * mnt_monitor_enable_kernel:
 * @mn: monitor
 * @enable: 0 or 1
 *
 * Enables or disables kernel VFS monitoring. If the monitor does not exist and
 * enable=1 then allocates new resources necessary for the monitor.
 *
 * If the top-level monitor has been already created (by mnt_monitor_get_fd()
 * or mnt_monitor_wait()) then it's updated according to @enable.
 *
 * Return: 0 on success and <0 on error
 */
int mnt_monitor_enable_kernel(struct libmnt_monitor *mn, int enable)
{
	struct monitor_entry *me;
	int rc = 0;

	if (!mn)
		return -EINVAL;

	me = monitor_get_entry(mn, MNT_MONITOR_TYPE_KERNEL);
	if (me) {
		rc = monitor_modify_epoll(mn, me, enable);
		if (!enable)
			kernel_monitor_close_fd(mn, me);
		return rc;
	}
	if (!enable)
		return 0;

	DBG(MONITOR, ul_debugobj(mn, "allocate new kernel monitor"));

	/* create a new entry */
	me = monitor_new_entry(mn);
	if (!me)
		goto err;

	/* If you want to use epoll FD in another epoll then top level
	 * epoll_wait() will drain all events from low-level FD if the
	 * low-level FD is not added with EPOLLIN. It means without EPOLLIN it
	 * it's impossible to detect which low-level FD has been active.
	 *
	 * Unfortunately, use EPOLLIN for mountinfo is tricky because in this
	 * case kernel returns events all time (we don't read from the FD).
	 * The solution is to use also edge-triggered (EPOLLET) flag, then
	 * kernel generate events on moutinfo changes only. The disadvantage is
	 * that we have to drain initial event generated by EPOLLIN after
	 * epoll_ctl(ADD). See monitor_modify_epoll().
	 */
	me->events = EPOLLPRI | EPOLLIN | EPOLLET;

	me->type = MNT_MONITOR_TYPE_KERNEL;
	me->opers = &kernel_opers;
	me->path = strdup(_PATH_PROC_MOUNTINFO);
	if (!me->path)
		goto err;

	return monitor_modify_epoll(mn, me, TRUE);
err:
	rc = -errno;
	free_monitor_entry(me);
	DBG(MONITOR, ul_debugobj(mn, "failed to allocate kernel monitor [rc=%d]", rc));
	return rc;
}

/*
 * Add/Remove monitor entry to/from monitor epoll.
 */
static int monitor_modify_epoll(struct libmnt_monitor *mn,
				struct monitor_entry *me, int enable)
{
	assert(mn);
	assert(me);

	me->enable = enable ? 1 : 0;
	me->changed = 0;

	if (mn->fd < 0)
		return 0;	/* no epoll, ignore request */

	if (enable) {
		struct epoll_event ev = { .events = me->events };
		int fd = me->opers->op_get_fd(mn, me);

		DBG(MONITOR, ul_debugobj(mn, " add fd=%d (for %s)", fd, me->path));

		ev.data.ptr = (void *) me;

		if (epoll_ctl(mn->fd, EPOLL_CTL_ADD, fd, &ev) < 0) {
			if (errno != EEXIST)
				goto err;
		}
		if (me->events & (EPOLLIN | EPOLLET)) {
			/* Drain initial events generated for /proc/self/mountinfo */
			struct epoll_event events[1];
			while (epoll_wait(mn->fd, events, 1, 0) > 0);
		}
	} else if (me->fd) {
		DBG(MONITOR, ul_debugobj(mn, " remove fd=%d (for %s)", me->fd, me->path));
		if (epoll_ctl(mn->fd, EPOLL_CTL_DEL, me->fd, NULL) < 0) {
			if (errno != ENOENT)
				goto err;
		}
	}

	return 0;
err:
	return -errno;
}

/**
 * mnt_monitor_close_fd:
 * @mn: monitor
 *
 * Close monitor file descriptor. This is usually unnecessary, because
 * mnt_unref_monitor() cleanups all.
 *
 * The function is necessary only if you want to reset monitor setting. The
 * next mnt_monitor_get_fd() or mnt_monitor_wait() will use newly initialized
 * monitor.  This restart is unnecessary for mnt_monitor_enable_*() functions.
 *
 * Returns: 0 on success, <0 on error.
 */
int mnt_monitor_close_fd(struct libmnt_monitor *mn)
{
	struct libmnt_iter itr;
	struct monitor_entry *me;

	if (!mn)
		return -EINVAL;

	mnt_reset_iter(&itr, MNT_ITER_FORWARD);

	/* disable all monitor entries */
	while (monitor_next_entry(mn, &itr, &me) == 0) {

		/* remove entry from epoll */
		if (mn->fd >= 0)
			monitor_modify_epoll(mn, me, FALSE);

		/* close entry FD */
		me->opers->op_close_fd(mn, me);
	}

	if (mn->fd >= 0) {
		DBG(MONITOR, ul_debugobj(mn, "closing top-level monitor fd"));
		close(mn->fd);
	}
	mn->fd = -1;
	return 0;
}

/**
 * mnt_monitor_get_fd:
 * @mn: monitor
 *
 * The file descriptor is associated with all monitored files and it's usable
 * for example for epoll. You have to call mnt_monitor_event_cleanup() or
 * mnt_monitor_next_changed() after each event.
 *
 * Returns: >=0 (fd) on success, <0 on error
 */
int mnt_monitor_get_fd(struct libmnt_monitor *mn)
{
	struct libmnt_iter itr;
	struct monitor_entry *me;
	int rc = 0;

	if (!mn)
		return -EINVAL;
	if (mn->fd >= 0)
		return mn->fd;

	DBG(MONITOR, ul_debugobj(mn, "create top-level monitor fd"));
	mn->fd = epoll_create1(EPOLL_CLOEXEC);
	if (mn->fd < 0)
		return -errno;

	mnt_reset_iter(&itr, MNT_ITER_FORWARD);

	DBG(MONITOR, ul_debugobj(mn, "adding monitor entries to epoll (fd=%d)", mn->fd));
	while (monitor_next_entry(mn, &itr, &me) == 0) {
		if (!me->enable)
			continue;
		rc = monitor_modify_epoll(mn, me, TRUE);
		if (rc)
			goto err;
	}

	DBG(MONITOR, ul_debugobj(mn, "successfully created monitor"));
	return mn->fd;
err:
	rc = errno ? -errno : -EINVAL;
	close(mn->fd);
	mn->fd = -1;
	DBG(MONITOR, ul_debugobj(mn, "failed to create monitor [rc=%d]", rc));
	return rc;
}

/**
 * mnt_monitor_wait:
 * @mn: monitor
 * @timeout: number of milliseconds, -1 block indefinitely, 0 return immediately
 *
 * Waits for the next change, after the event it's recommended to use
 * mnt_monitor_next_change() to get more details about the change or
 * at least mnt_monitor_event_cleanup().
 *
 * Returns: 1 success (something changed), 0 timeout, <0 error.
 */
int mnt_monitor_wait(struct libmnt_monitor *mn, int timeout)
{
	int rc;
	struct monitor_entry *me;
	struct epoll_event events[1];

	if (!mn)
		return -EINVAL;

	if (mn->fd < 0) {
		rc = mnt_monitor_get_fd(mn);
		if (rc < 0)
			return rc;
	}

	DBG(MONITOR, ul_debugobj(mn, "calling epoll_wait(), timeout=%d", timeout));
	rc = epoll_wait(mn->fd, events, 1, timeout);
	if (rc < 0)
		return -errno;		/* error */
	if (rc == 0)
		return 0;		/* timeout */

	me = (struct monitor_entry *) events[0].data.ptr;
	if (!me)
		return -EINVAL;
	me->changed = 1;

	if (me->opers->op_event_cleanup != NULL)
		me->opers->op_event_cleanup(mn, me);

	return 1;			/* success */
}


static struct monitor_entry *get_changed(struct libmnt_monitor *mn)
{
	struct libmnt_iter itr;
	struct monitor_entry *me;

	mnt_reset_iter(&itr, MNT_ITER_FORWARD);
	while (monitor_next_entry(mn, &itr, &me) == 0) {
		if (me->changed)
			return me;
	}
	return NULL;
}

/**
 * mnt_monitor_next_changed:
 * @mn: monitor
 * @filename: returns changed file (optional argument)
 * @type: returns MNT_MONITOR_TYPE_* (optional argument)
 *
 * The function does not wait and it's designed to provide details about changes.
 *
 * Returns: 0 on success, 1 no change, <0 on error
 */
int mnt_monitor_next_changed(struct libmnt_monitor *mn,
			     const char **filename,
			     int *type)
{
	int rc;
	struct monitor_entry *me;

	if (!mn || mn->fd < 0)
		return -EINVAL;

	/*
	 * if we previously called epoll_wait() (e.g. mnt_monitor_waith()) then
	 * info about unread change is already stored in monitor_entry.
	 *
	 * If we get nothing, then ask kernel.
	 */
	me = get_changed(mn);
	if (!me) {
		do {
			struct epoll_event events[1];

			DBG(MONITOR, ul_debugobj(mn, "asking for next changed"));

			rc = epoll_wait(mn->fd, events, 1, 0);	/* no timeout! */
			if (rc < 0) {
				DBG(MONITOR, ul_debugobj(mn, " *** error"));
				return -errno;
			}
			if (rc == 0) {
				DBG(MONITOR, ul_debugobj(mn, " *** nothing"));
				return 1;
			}

			me = (struct monitor_entry *) events[0].data.ptr;
			if (!me)
				return -EINVAL;
			if (me->opers->op_event_cleanup != NULL)
				me->opers->op_event_cleanup(mn, me);
			break;
		} while (1);
	}

	me->changed = 0;

	if (filename)
		*filename = me->path;
	if (type)
		*type = me->type;

	DBG(MONITOR, ul_debugobj(mn, " *** success"));
	return 0;				/* success */
}

/**
 * mnt_monitor_event_cleanup:
 * @mn: monitor
 *
 * This function cleanups (drain) internal buffers. It's necessary to call
 * this function after event if you do not call mnt_monitor_next_change().
 *
 * Returns: 0 on success, 1 no change, <0 on error
 */
int mnt_monitor_event_cleanup(struct libmnt_monitor *mn)
{
	struct monitor_entry *me;
	struct libmnt_iter itr;

	if (!mn || mn->fd < 0)
		return -EINVAL;

	mnt_reset_iter(&itr, MNT_ITER_FORWARD);
	while (monitor_next_entry(mn, &itr, &me) == 0) {
		if (me->opers->op_event_cleanup != NULL)
			me->opers->op_event_cleanup(mn, me);
	}
	return 0;
}

#ifdef TEST_PROGRAM

static struct libmnt_monitor *create_test_monitor(int argc, char *argv[])
{
	struct libmnt_monitor *mn;
	int i;

	mn = mnt_new_monitor();
	if (!mn) {
		warn("failed to allocate monitor");
		goto err;
	}

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "userspace") == 0) {
			if (mnt_monitor_enable_userspace(mn, TRUE, NULL)) {
				warn("failed to initialize userspace monitor");
				goto err;
			}

		} else if (strcmp(argv[i], "kernel") == 0) {
			if (mnt_monitor_enable_kernel(mn, TRUE)) {
				warn("failed to initialize kernel monitor");
				goto err;
			}
		}
	}
	if (i == 1) {
		warnx("No monitor type specified");
		goto err;
	}

	return mn;
err:
	mnt_unref_monitor(mn);
	return NULL;
}

/*
 * create a monitor and add the monitor fd to epoll
 */
int test_epoll(struct libmnt_test *ts, int argc, char *argv[])
{
	int fd, efd = -1, rc = -1;
	struct epoll_event ev;
	struct libmnt_monitor *mn = create_test_monitor(argc, argv);

	if (!mn)
		return -1;

	fd = mnt_monitor_get_fd(mn);
	if (fd < 0) {
		warn("failed to initialize monitor fd");
		goto done;
	}

	efd = epoll_create1(EPOLL_CLOEXEC);
	if (efd < 0) {
		warn("failed to create epoll");
		goto done;
	}

	ev.events = EPOLLPRI | EPOLLIN;
	ev.data.fd = fd;

	rc = epoll_ctl(efd, EPOLL_CTL_ADD, fd, &ev);
	if (rc < 0) {
		warn("failed to add fd to epoll");
		goto done;
	}

	printf("waiting for changes...\n");
	do {
		const char *filename = NULL;
		struct epoll_event events[1];
		int n = epoll_wait(efd, events, 1, -1);

		if (n < 0) {
			rc = -errno;
			warn("polling error");
			goto done;
		}
		if (n == 0 || events[0].data.fd != fd)
			continue;

		printf(" top-level FD active\n");
		while (mnt_monitor_next_changed(mn, &filename, NULL) == 0)
			printf("  %s: change detected\n", filename);
	} while (1);

	rc = 0;
done:
	if (efd >= 0)
		close(efd);
	mnt_unref_monitor(mn);
	return rc;
}

/*
 * create a monitor and wait for a change
 */
int test_wait(struct libmnt_test *ts, int argc, char *argv[])
{
	const char *filename;
	struct libmnt_monitor *mn = create_test_monitor(argc, argv);

	if (!mn)
		return -1;

	printf("waiting for changes...\n");
	while (mnt_monitor_wait(mn, -1) > 0) {
		printf("notification detected\n");

		while (mnt_monitor_next_changed(mn, &filename, NULL) == 0)
			printf(" %s: change detected\n", filename);

	}
	mnt_unref_monitor(mn);
	return 0;
}

int main(int argc, char *argv[])
{
	struct libmnt_test tss[] = {
		{ "--epoll", test_epoll, "<userspace kernel ...>  monitor in epoll" },
		{ "--wait",  test_wait,  "<userspace kernel ...>  monitor wait function" },
		{ NULL }
	};

	return mnt_run_test(tss, argc, argv);
}

#endif /* TEST_PROGRAM */
