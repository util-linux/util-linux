/*
 * Copyright (C) 2009 Karel Zak <kzak@redhat.com>
 *
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 */

/**
 * SECTION: lock
 * @title: Mtab locking
 * @short_description: locking methods for work with /etc/mtab
 *
 * The lock is backwardly compatible with the standard linux /etc/mtab locking.
 * Note, it's necessary to use the same locking schema in all application that
 * access the file.
 */
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <sys/time.h>
#include <time.h>
#include <signal.h>
#include <fcntl.h>
#include <limits.h>

#include "pathnames.h"
#include "nls.h"

#include "mountP.h"

/*
 * lock handler
 */
struct _mnt_lock {
	pid_t	id;		/* getpid() or so (see linkfile)... */
	char	*lockfile;	/* path to lock file (e.g. /etc/mtab~) */
	char	*linkfile;	/* path to link file (e.g. /etc/mtab~.<id>) */
	int	lockfile_fd;	/* lock file descriptor */
	int	locked;		/* do we own the lock? */
};


/**
 * mnt_new_lock:
 * @lockfile: path to lockfile or NULL (default is _PATH_MOUNTED_LOCK)
 * @id: unique linkfile identifier or 0 (default is getpid())
 *
 * Returns: newly allocated lock handler or NULL on case of error.
 */
mnt_lock *mnt_new_lock(const char *lockfile, pid_t id)
{
	mnt_lock *ml = calloc(1, sizeof(struct _mnt_lock));

	if (!ml)
		return NULL;

	ml->lockfile_fd = -1;
	ml->id = id;
	if (lockfile) {
		ml->lockfile = strdup(lockfile);
		if (!ml->lockfile) {
			free(ml);
			return NULL;
		}
	}
	return ml;
}

/**
 * mnt_free_lock:
 * @ml: mnt_lock handler
 *
 * Deallocates mnt_lock.
 */
void mnt_free_lock(mnt_lock *ml)
{
	if (!ml)
		return;
	free(ml->lockfile);
	free(ml->linkfile);
	free(ml);
}

/**
 * mnt_lock_get_lockfile:
 * @ml: mnt_lock handler
 *
 * Returns: path to lockfile.
 */
const char *mnt_lock_get_lockfile(mnt_lock *ml)
{
	if (!ml)
		return NULL;
	if (ml->lockfile)
		return ml->lockfile;
	return _PATH_MOUNTED_LOCK;
}

/**
 * mnt_lock_get_linkfile:
 * @ml: mnt_lock handler
 *
 * Returns: unique (per process) path to linkfile.
 */
const char *mnt_lock_get_linkfile(mnt_lock *ml)
{
	if (!ml)
		return NULL;

	if (!ml->linkfile) {
		const char *lf = mnt_lock_get_lockfile(ml);
		size_t sz;

		if (!lf)
			return NULL;
		sz = strlen(lf) + 32;

		ml->linkfile = malloc(sz);
		if (ml->linkfile)
			snprintf(ml->linkfile, sz, "%s.%d",
					lf, ml->id ? ml->id : getpid());
	}
	return ml->linkfile;
}

static void mnt_lockalrm_handler(int sig)
{
	/* do nothing, say nothing, be nothing */
}

/*
 * Waits for F_SETLKW, unfortunately we have to use SIGALRM here to interrupt
 * fcntl() to avoid never ending waiting.
 *
 * Returns: 0 on success, 1 on timeout, -errno on error.
 */
static int mnt_wait_lock(mnt_lock *ml, struct flock *fl, time_t maxtime)
{
	struct timeval now;
	struct sigaction sa, osa;
	int ret = 0;

	gettimeofday(&now, NULL);

	if (now.tv_sec >= maxtime)
		return 1;		/* timeout */

	/* setup ALARM handler -- we don't want to wait forever */
	sa.sa_flags = 0;
	sa.sa_handler = mnt_lockalrm_handler;
	sigfillset (&sa.sa_mask);

	sigaction(SIGALRM, &sa, &osa);

	DBG(DEBUG_LOCKS, fprintf(stderr,
		"LOCK: (%d) waiting for F_SETLKW.\n", getpid()));

	alarm(maxtime - now.tv_sec);
	if (fcntl(ml->lockfile_fd, F_SETLKW, fl) == -1)
		ret = errno == EINTR ? 1 : -errno;
	alarm(0);

	/* restore old sigaction */
	sigaction(SIGALRM, &osa, NULL);

	DBG(DEBUG_LOCKS, fprintf(stderr,
		"LOCK: (%d) leaving mnt_wait_setlkw(), rc=%d.\n", getpid(), ret));
	return ret;
}

/*
 * Create the lock file.
 *
 * The old code here used flock on a lock file /etc/mtab~ and deleted
 * this lock file afterwards. However, as rgooch remarks, that has a
 * race: a second mount may be waiting on the lock and proceed as
 * soon as the lock file is deleted by the first mount, and immediately
 * afterwards a third mount comes, creates a new /etc/mtab~, applies
 * flock to that, and also proceeds, so that the second and third mount
 * now both are scribbling in /etc/mtab.
 *
 * The new code uses a link() instead of a creat(), where we proceed
 * only if it was us that created the lock, and hence we always have
 * to delete the lock afterwards. Now the use of flock() is in principle
 * superfluous, but avoids an arbitrary sleep().
 *
 * Where does the link point to? Obvious choices are mtab and mtab~~.
 * HJLu points out that the latter leads to races. Right now we use
 * mtab~.<pid> instead.
 *
 *
 * The original mount locking code has used sleep(1) between attempts and
 * maximal number of attempts has been 5.
 *
 * There was very small number of attempts and extremely long waiting (1s)
 * that is useless on machines with large number of mount processes.
 *
 * Now we wait few thousand microseconds between attempts and we have a global
 * time limit (30s) rather than limit for number of attempts. The advantage
 * is that this method also counts time which we spend in fcntl(F_SETLKW) and
 * number of attempts is not restricted.
 * -- kzak@redhat.com [Mar-2007]
 *
 *
 * This mtab locking code has been refactored and moved to libmount. The mtab
 * locking is really not perfect (e.g. SIGALRM), but it's stable, reliable and
 * backwardly compatible code. Don't forget that this code has to be compatible
 * with 3rd party mounts (/sbin/mount.<foo>) and has to work with NFS.
 * -- kzak@redhat.com [May-2009]
 */

/* maximum seconds between first and last attempt */
#define MOUNTLOCK_MAXTIME		30

/* sleep time (in microseconds, max=999999) between attempts */
#define MOUNTLOCK_WAITTIME		5000

/* Remove lock file.  */
void mnt_unlock_file(mnt_lock *ml)
{
	if (!ml)
		return;

	DBG(DEBUG_LOCKS, fprintf(stderr, "LOCK: (%d) unlocking/cleaning.\n", getpid()));

	if (ml->locked == 0 && ml->lockfile && ml->linkfile)
	{
		/* We have (probably) all files, but we don't own the lock,
		 * Really? Check it! Maybe ml->locked wasn't set properly
		 * because code was interrupted by signal. Paranoia? Yes.
		 *
		 * We own the lock when linkfile == lockfile.
		 */
		struct stat lo, li;

		if (!stat(ml->lockfile, &lo) && !stat(ml->linkfile, &li) &&
		    lo.st_dev == li.st_dev && lo.st_ino == li.st_ino)
			ml->locked = 1;
	}
	if (ml->linkfile)
		unlink(ml->linkfile);
	if (ml->lockfile_fd >= 0)
		close(ml->lockfile_fd);
	if (ml->locked == 1 && ml->lockfile)
		unlink(ml->lockfile);

	ml->locked = 0;
	ml->lockfile_fd = -1;
}

/**
 * mnt_lock_file
 * @ml: pointer to mnt_lock instance
 *
 * Creates lock file (e.g. /etc/mtab~). Note that this function uses
 * alarm().
 *
 * Your application has to always call mnt_unlock_file() before exit.
 *
 * Locking scheme:
 *
 *   1. create linkfile (e.g. /etc/mtab~.$PID)
 *   2. link linkfile --> lockfile (e.g. /etc/mtab~.$PID --> /etc/mtab~)
 *
 *   3. a) link() success: setups F_SETLK lock (see fcnlt(2))
 *      b) link() failed:  wait (max 30s) on F_SETLKW lock, goto 2.
 *
 * Example:
 *
 * <informalexample>
 *   <programlisting>
 *	mnt_lock *ml;
 *
 *	void unlock_fallback(void)
 *	{
 *		if (!ml)
 *			return;
 *		mnt_unlock_file(ml);
 *		mnt_free_lock(ml);
 *	}
 *
 *	int update_mtab()
 *	{
 *		int sig = 0;
 *
 *		atexit(unlock_fallback);
 *
 *		ml = mnt_new_lock(NULL, 0);
 *
 *		if (mnt_lock_file(ml) != 0) {
 *			printf(stderr, "cannot create %s lockfile\n",
 *					mnt_lock_get_lockfile(ml));
 *			return -1;
 *		}
 *
 *		... modify mtab ...
 *
 *		mnt_unlock_file(ml);
 *		mnt_free_lock(ml);
 *		ml = NULL;
 *		return 0;
 *	}
 *   </programlisting>
 * </informalexample>
 *
 * Returns: 0 on success or -1 in case of error.
 */
int mnt_lock_file(mnt_lock *ml)
{
	int i;
	struct timespec waittime;
	struct timeval maxtime;
	const char *lockfile, *linkfile;

	if (!ml)
		return -1;
	if (ml->locked)
		return 0;

	lockfile = mnt_lock_get_lockfile(ml);
	if (!lockfile)
		return -1;
	linkfile = mnt_lock_get_linkfile(ml);
	if (!linkfile)
		return -1;

	i = open(linkfile, O_WRONLY|O_CREAT, S_IRUSR|S_IWUSR);
	if (i < 0)
		/* linkfile does not exist (as a file)
		   and we cannot create it. Read-only filesystem?
		   Too many files open in the system?
		   Filesystem full? */
		goto failed;

	close(i);

	gettimeofday(&maxtime, NULL);
	maxtime.tv_sec += MOUNTLOCK_MAXTIME;

	waittime.tv_sec = 0;
	waittime.tv_nsec = (1000 * MOUNTLOCK_WAITTIME);

	/* Repeat until it was us who made the link */
	while (ml->locked == 0) {
		struct timeval now;
		struct flock flock;
		int j;

		j = link(linkfile, lockfile);
		if (j == 0)
			ml->locked = 1;

		if (j < 0 && errno != EEXIST)
			goto failed;

		ml->lockfile_fd = open(lockfile, O_WRONLY);

		if (ml->lockfile_fd < 0) {
			/* Strange... Maybe the file was just deleted? */
			int errsv = errno;
			gettimeofday(&now, NULL);
			if (errsv == ENOENT && now.tv_sec < maxtime.tv_sec) {
				ml->locked = 0;
				continue;
			}
			goto failed;
		}

		flock.l_type = F_WRLCK;
		flock.l_whence = SEEK_SET;
		flock.l_start = 0;
		flock.l_len = 0;

		if (ml->locked) {
			/* We made the link. Now claim the lock. */
			if (fcntl (ml->lockfile_fd, F_SETLK, &flock) == -1) {
				DBG(DEBUG_LOCKS, fprintf(stderr,
					"%s: can't F_SETLK lockfile, errno=%d\n",
					lockfile, errno));
				/* proceed, since it was us who created the lockfile anyway */
			}
			break;
		} else {
			/* Someone else made the link. Wait. */
			int err = mnt_wait_lock(ml, &flock, maxtime.tv_sec);

			if (err == 1) {
				DBG(DEBUG_LOCKS, fprintf(stderr,
					"%s: can't create link: time out (perhaps "
					"there is a stale lock file?)", lockfile));
				goto failed;

			} else if (err < 0)
				goto failed;

			nanosleep(&waittime, NULL);
			close(ml->lockfile_fd);
			ml->lockfile_fd = -1;
		}
	}
	DBG(DEBUG_LOCKS, fprintf(stderr,
			"LOCK: %s: (%d) successfully locked\n",
			ml->lockfile, getpid()));
	unlink(linkfile);
	return 0;

failed:
	mnt_unlock_file(ml);
	return -1;
}

#ifdef TEST_PROGRAM
#include <err.h>

mnt_lock *lock;

/*
 * read number from @filename, increment the number and
 * write the number back to the file
 */
void increment_data(const char *filename, int verbose, int loopno)
{
	long num;
	FILE *f;
	char buf[256];

	if (!(f = fopen(filename, "r")))
		err(EXIT_FAILURE, "%d: failed to open: %s", getpid(), filename);

	if (!fgets(buf, sizeof(buf), f))
		err(EXIT_FAILURE, "%d failed read: %s", getpid(), filename);

	fclose(f);
	num = atol(buf) + 1;

	if (!(f = fopen(filename, "w")))
		err(EXIT_FAILURE, "%d: failed to open: %s", getpid(), filename);

	fprintf(f, "%ld", num);
	fclose(f);

	if (verbose)
		fprintf(stderr, "%d: %s: %ld --> %ld (loop=%d)\n", getpid(),
				filename, num - 1, num, loopno);
}

void clean_lock(void)
{
	fprintf(stderr, "%d: cleaning\n", getpid());
	if (!lock)
		return;
	mnt_unlock_file(lock);
	mnt_free_lock(lock);
}

void sig_handler(int sig)
{
	errx(EXIT_FAILURE, "\n%d: catch signal: %s\n", getpid(), strsignal(sig));
}

int test_lock(struct mtest *ts, int argc, char *argv[])
{
	const char *lockfile, *datafile;
	int verbose = 0, loops, l;

	if (argc < 4)
		return -1;

	lockfile = argv[1];
	datafile = argv[2];
	loops = atoi(argv[3]);

	if (argc == 5 && strcmp(argv[4], "--verbose") == 0)
		verbose = 1;

	atexit(clean_lock);

	/* be paranoid and call exit() (=clean_lock()) for all signals */
	{
		int sig = 0;
		struct sigaction sa;

		sa.sa_handler = sig_handler;
		sa.sa_flags = 0;
		sigfillset(&sa.sa_mask);

		while (sigismember(&sa.sa_mask, ++sig) != -1 && sig != SIGCHLD)
			sigaction (sig, &sa, (struct sigaction *) 0);
	}

	for (l = 0; l < loops; l++) {
		lock = mnt_new_lock(lockfile, 0);

		if (mnt_lock_file(lock) == -1) {
			fprintf(stderr, "%d: failed to create lock file: %s\n",
					getpid(), lockfile);
			return -1;
		}

		increment_data(datafile, verbose, l);

		mnt_unlock_file(lock);
		mnt_free_lock(lock);
		lock = NULL;
	}

	return 0;
}

/*
 * Note that this test should be executed from a script that creates many
 * parallel processes, otherwise this test does not make sense.
 */
int main(int argc, char *argv[])
{
	struct mtest tss[] = {
	{ "--lock", test_lock,  "  <lockfile> <datafile> <loops> [--verbose]   increment number in datafile" },
	{ NULL }
	};

	return mnt_run_test(tss, argc, argv);
}

#endif /* TEST_PROGRAM */
