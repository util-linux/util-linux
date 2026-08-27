/*
 * test_mkfds_hungfs - user mode file system reproducing process hung
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

#include "c.h"
#include "xalloc.h"
#include "test_mkfds.h"

#ifdef HAVE_LIBFUSE

#define FUSE_USE_VERSION 26
#include <fuse.h>

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void *hungfs_init(struct fuse_conn_info *conn _U_)
{
	struct fuse_context *ctx = fuse_get_context();
	struct hungfs_args *args = ctx->private_data;
	char c = HUNGFS_READY;

	/* Signal parent that FUSE mount is ready */
	ignore_result(write(args->ctlfd, &c, 1));

	return args;
}

static int hungfs_getattr(const char *path, struct stat *stbuf)
{
	struct fuse_context *ctx = fuse_get_context();
	struct hungfs_args *args = ctx->private_data;
	char c;

	memset(stbuf, 0, sizeof(struct stat));
	if (strcmp(path, "/") == 0) {
		stbuf->st_mode = S_IFDIR | 0755;
		stbuf->st_nlink = 2;
		return 0;
	}
	if (args->file && strcmp(path + 1, args->file) == 0) {
		if (args->hung)
			ignore_result(read(args->ctlfd, &c, 1));
		stbuf->st_mode = S_IFREG | 0644;
		stbuf->st_nlink = 1;
		stbuf->st_size = 1024;
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
