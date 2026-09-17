/*
 * test_mkfds_hungfs - user-mode file system reproducing process hangs
 *
 * Written by Masatake YAMATO <yamato@redhat.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://gnu.org/licenses/>.
 */

/*
 * Protocol for hungfs state and hang control:
 *
 * Communication between the test_mkfds runner (parent) and the FUSE server
 * (child) is conducted via an AF_UNIX socketpair (ctlfd):
 *
 * 1. Startup synchronization:
 *    - Child sends HUNGFS_READY ('R') in hungfs_init() once the FUSE mount
 *      initialization is complete.
 *    - Parent waits for HUNGFS_READY (monitored along with the child's pidfd)
 *      before opening files on the mount point.
 *
 * 2. Arming hang:
 *    - Parent sends HUNGFS_HANG ('H') to transition the file system to
 *      hung state (args->is_hung = true).
 *    - Subsequent getattr requests for matching targets (HUNGFS_TARGET_FILE,
 *      HUNGFS_TARGET_ROOT, or HUNGFS_TARGET_ALL) block in
 *      hungfs_wait_unhung().
 *
 * 3. Disarming hang:
 *    - Parent sends HUNGFS_UNHUNG ('G') to transition back to normal state
 *      (args->is_hung = false), immediately unblocking any in-flight or
 *      subsequent getattr operations.
 *
 * 4. Teardown:
 *    - When the control socket is closed (EOF) by the parent during cleanup
 *      or termination, blocked getattr operations unblock immediately to allow
 *      clean unmounting and exit.
 */

#include "c.h"

#ifdef HAVE_LIBFUSE

#define FUSE_USE_VERSION 26
#include <fuse.h>

#include <errno.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "test_mkfds.h"
#include "xalloc.h"

static void *hungfs_init(struct fuse_conn_info *conn _U_)
{
	struct fuse_context *ctx = fuse_get_context();
	struct hungfs_args *args = ctx->private_data;
	char c = HUNGFS_READY;
	ssize_t r;

	/* Signal parent that FUSE mount is ready */
	while ((r = write(args->ctlfd, &c, 1)) < 0 && errno == EINTR)
		;
	if (r != 1)
		_exit(EXIT_FAILURE);

	return args;
}

static void hungfs_update_state(struct hungfs_args *args)
{
	struct pollfd pfd = { .fd = args->ctlfd, .events = POLLIN };
	ssize_t r;
	char c;
	int pr;

	while (1) {
		do {
			pr = poll(&pfd, 1, 0);
		} while (pr < 0 && errno == EINTR);

		if (pr <= 0 || !(pfd.revents & POLLIN))
			break;

		while ((r = read(args->ctlfd, &c, 1)) < 0 && errno == EINTR)
			;
		if (r != 1)
			break;
		if (c == HUNGFS_HANG)
			args->is_hung = true;
		else if (c == HUNGFS_UNHUNG)
			args->is_hung = false;
	}
}

static void hungfs_wait_unhung(struct hungfs_args *args)
{
	ssize_t r;
	char c;

	while (1) {
		while ((r = read(args->ctlfd, &c, 1)) < 0 && errno == EINTR)
			;
		if (r <= 0 || c == HUNGFS_UNHUNG) {
			args->is_hung = false;
			break;
		}
		/*
		 * If we read HUNGFS_HANG ('H'), the parent sent a hang command
		 * that arrived while or just after we started waiting. Keep the
		 * hung state and continue blocking until an UNHUNG command ('G')
		 * or EOF is received.
		 */
		if (c == HUNGFS_HANG)
			args->is_hung = true;
	}
}

static int hungfs_getattr(const char *path, struct stat *stbuf)
{
	struct fuse_context *ctx = fuse_get_context();
	struct hungfs_args *args = ctx->private_data;

	hungfs_update_state(args);

	memset(stbuf, 0, sizeof(struct stat));
	if (strcmp(path, "/") == 0) {
		if (args->hung && args->is_hung
		    && (args->hung_target & HUNGFS_TARGET_ROOT))
			hungfs_wait_unhung(args);
		stbuf->st_mode = S_IFDIR | 0755;
		stbuf->st_nlink = 2;
		return 0;
	}
	if (args->file && strcmp(path + 1, args->file) == 0) {
		if (args->hung && args->is_hung
		    && (args->hung_target & HUNGFS_TARGET_FILE))
			hungfs_wait_unhung(args);
		stbuf->st_mode = S_IFREG | 0644;
		stbuf->st_nlink = 1;
		stbuf->st_size = HUNGFS_FILE_SIZE;
		return 0;
	}
	return -ENOENT;
}

static int hungfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
			  off_t offset _U_, struct fuse_file_info *fi _U_)
{
	struct fuse_context *ctx = fuse_get_context();
	struct hungfs_args *args = ctx->private_data;

	if (strcmp(path, "/") != 0)
		return -ENOENT;

	filler(buf, ".", NULL, 0);
	filler(buf, "..", NULL, 0);
	if (args && args->file)
		filler(buf, args->file, NULL, 0);
	return 0;
}

static int hungfs_open(const char *path, struct fuse_file_info *fi _U_)
{
	struct fuse_context *ctx = fuse_get_context();
	struct hungfs_args *args = ctx->private_data;

	if (strcmp(path + 1, args->file) == 0)
		return 0;
	return -ENOENT;
}

static struct fuse_operations hungfs_ops = {
	.init    = hungfs_init,
	.getattr = hungfs_getattr,
	.readdir = hungfs_readdir,
	.open    = hungfs_open,
};

void run_hungfs(struct hungfs_args *args)
{
	int r;
	char *mountpoint = xstrdup(args->mountpoint);
	/*
	 * Disable kernel attribute and dentry caching (attr_timeout=0,
	 * entry_timeout=0) so that pathname lookups and getattr operations
	 * are deterministically forwarded to the FUSE server without
	 * relying on cached VFS entries.
	 */
	char *fuse_argv[] = {
		"hungfs",
		"-f",          /* foreground */
		"-s",          /* single-threaded */
		"-o", "attr_timeout=0,entry_timeout=0",
		mountpoint,
		NULL
	};

	r = fuse_main(ARRAY_SIZE(fuse_argv) - 1, fuse_argv, &hungfs_ops, args);
	free(mountpoint);
	exit(r);
}

#endif /* HAVE_LIBFUSE */
