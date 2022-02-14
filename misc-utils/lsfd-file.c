/*
 * lsfd(1) - list file descriptors
 *
 * Copyright (C) 2021 Red Hat, Inc. All rights reserved.
 * Written by Masatake YAMATO <yamato@redhat.com>
 *
 * Very generally based on lsof(8) by Victor A. Abell <abe@purdue.edu>
 * It supports multiple OSes. lsfd specializes to Linux.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <unistd.h>

#include "xalloc.h"
#include "nls.h"
#include "buffer.h"
#include "idcache.h"
#include "strutils.h"

#include "libsmartcols.h"

#include "lsfd.h"

static struct idcache *username_cache;

static const char *assocstr[N_ASSOCS] = {
	[ASSOC_CWD]       = "cwd",
	[ASSOC_EXE]       = "exe",
	/* "root" appears as user names, too.
	 * So we use "rtd" here instead of "root". */
	[ASSOC_ROOT]      = "rtd",
	[ASSOC_NS_CGROUP] = "cgroup",
	[ASSOC_NS_IPC]    = "ipc",
	[ASSOC_NS_MNT]    = "mnt",
	[ASSOC_NS_NET]    = "net",
	[ASSOC_NS_PID]    = "pid",
	[ASSOC_NS_PID4C]  = "pid4c",
	[ASSOC_NS_TIME]   = "time",
	[ASSOC_NS_TIME4C] = "time4c",
	[ASSOC_NS_USER]   = "user",
	[ASSOC_NS_UTS]    = "uts",
	[ASSOC_MEM]       = "mem",
	[ASSOC_SHM]       = "shm",
};

static const char *strftype(mode_t ftype)
{
	switch (ftype) {
	case S_IFBLK:
		return "BLK";
	case S_IFCHR:
		return "CHR";
	case S_IFDIR:
		return "DIR";
	case S_IFIFO:
		return "FIFO";
	case S_IFLNK:
		return "LINK";
	case S_IFREG:
		return "REG";
	case S_IFSOCK:
		return "SOCK";
	default:
		return "UNKN";
	}
}

/* See /usr/include/asm-generic/fcntl.h */
static void file_fill_flags_buf(struct ul_buffer *buf, int flags)
{
#define SET_FLAG_FULL(L,s)						\
	do {								\
		if (flags & (L)) {					\
			if (!ul_buffer_is_empty(buf))			\
				ul_buffer_append_data(buf, ",", 1);	\
			ul_buffer_append_string(buf, #s);		\
		}							\
	} while (0)

#define SET_FLAG(L,s) SET_FLAG_FULL(O_##L,s)

#ifdef O_WRONLY
	SET_FLAG(WRONLY,wronly);
#endif

#ifdef O_RDWR
	SET_FLAG(RDWR,rdwr);
#endif

#ifdef O_CREAT
	SET_FLAG(CREAT,creat);
#endif

#ifdef O_EXCL
	SET_FLAG(EXCL,excl);
#endif

#ifdef O_NOCTTY
	SET_FLAG(NOCTTY,noctty);
#endif

#ifdef O_APPEND
	SET_FLAG(APPEND,append);
#endif

#ifdef O_NONBLOCK
	SET_FLAG(NONBLOCK,nonblock);
#endif

#ifdef O_DSYNC
	SET_FLAG(DSYNC,dsync);
#endif

#ifdef O_FASYNC
	SET_FLAG(FASYNC,fasync);
#endif

#ifdef O_DIRECT
	SET_FLAG(DIRECT,direct);
#endif

#ifdef O_LARGEFILE
	SET_FLAG(LARGEFILE,largefile);
#endif

#ifdef O_DIRECTORY
	SET_FLAG(DIRECTORY,directory);
#endif

#ifdef O_FOLLOW
	SET_FLAG(FOLLOW,follow);
#endif

#ifdef O_NOATIME
	SET_FLAG(NOATIME,noatime);
#endif

#ifdef O_CLOEXEC
	SET_FLAG(CLOEXEC,cloexec);
#endif

#ifdef __O_SYNC
	SET_FLAG_FULL(__O_SYNC,_sync);
#endif

#ifdef O_PATH
	SET_FLAG(PATH,path);
#endif

#ifdef __O_TMPFILE
	SET_FLAG_FULL(__O_TMPFILE,_tmpfile);
#endif

}

#define does_file_has_fdinfo_alike(file)	\
	((file)->association >= 0		\
	 || (file)->association == -ASSOC_SHM	\
	 || (file)->association == -ASSOC_MEM)

static uint64_t get_map_length(struct file *file)
{
	uint64_t res = 0;

	if (is_association(file, SHM) || is_association(file, MEM)) {
		static size_t pagesize = 0;

		if (!pagesize)
			pagesize = getpagesize();

		res = (file->map_end - file->map_start) / pagesize;
	}

	return res;
}

static bool file_fill_column(struct proc *proc,
			     struct file *file,
			     struct libscols_line *ln,
			     int column_id,
			     size_t column_index)
{
	char *str = NULL;
	mode_t ftype;
	const char *partition;

	switch(column_id) {
	case COL_COMMAND:
		if (proc->command
		    && scols_line_set_data(ln, column_index, proc->command))
			err(EXIT_FAILURE, _("failed to add output data"));
		return true;
	case COL_NAME:
		if (file->name
		    && scols_line_set_data(ln, column_index, file->name))
			err(EXIT_FAILURE, _("failed to add output data"));
		return true;
	case COL_TYPE:
		ftype = file->stat.st_mode & S_IFMT;
		if (scols_line_set_data(ln, column_index, strftype(ftype)))
			err(EXIT_FAILURE, _("failed to add output data"));
		return true;
	case COL_USER:
		add_uid(username_cache, (int)proc->uid);
		if (scols_line_set_data(ln, column_index,
					get_id(username_cache,
					       (int)proc->uid)->name))
			err(EXIT_FAILURE, _("failed to add output data"));
		return true;
	case COL_OWNER:
		add_uid(username_cache, (int)file->stat.st_uid);
		if (scols_line_set_data(ln, column_index,
					get_id(username_cache,
					       (int)file->stat.st_uid)->name))
			err(EXIT_FAILURE, _("failed to add output data"));
		return true;
	case COL_DEVTYPE:
		if (scols_line_set_data(ln, column_index,
					"nodev"))
			err(EXIT_FAILURE, _("failed to add output data"));
		return true;
	case COL_FD:
		if (file->association < 0)
			return false;
		/* FALL THROUGH */
	case COL_ASSOC:
		if (file->association >= 0)
			xasprintf(&str, "%d", file->association);
		else {
			int assoc = file->association * -1;
			if (assoc >= N_ASSOCS)
				return false; /* INTERNAL ERROR */
			xasprintf(&str, "%s", assocstr[assoc]);
		}
		break;
	case COL_INODE:
		xasprintf(&str, "%llu", (unsigned long long)file->stat.st_ino);
		break;
	case COL_SOURCE:
		if (major(file->stat.st_dev) == 0) {
			const char *filesystem = get_nodev_filesystem(minor(file->stat.st_dev));
			if (filesystem) {
				xasprintf(&str, "%s", filesystem);
				break;
			}
		}
		/* FALL THROUGH */
	case COL_PARTITION:
		partition = get_partition(file->stat.st_dev);
		if (partition) {
			str = strdup(partition);
			break;
		}
		/* FALL THROUGH */
	case COL_DEV:
	case COL_MAJMIN:
		xasprintf(&str, "%u:%u",
			  major(file->stat.st_dev),
			  minor(file->stat.st_dev));
		break;
	case COL_RDEV:
		xasprintf(&str, "%u:%u",
			  major(file->stat.st_rdev),
			  minor(file->stat.st_rdev));
		break;
	case COL_PID:
		xasprintf(&str, "%d", (int)proc->leader->pid);
		break;
	case COL_TID:
		xasprintf(&str, "%d", (int)proc->pid);
		break;
	case COL_UID:
		xasprintf(&str, "%d", (int)proc->uid);
		break;
	case COL_FUID:
		xasprintf(&str, "%d", (int)file->stat.st_uid);
		break;
	case COL_SIZE:
		xasprintf(&str, "%jd", (intmax_t)file->stat.st_size);
		break;
	case COL_NLINK:
		xasprintf(&str, "%ju", (uintmax_t)file->stat.st_nlink);
		break;
	case COL_DELETED:
		xasprintf(&str, "%d", file->stat.st_nlink == 0);
		break;
	case COL_KTHREAD:
		xasprintf(&str, "%u", proc->kthread);
		break;
	case COL_MNT_ID:
		xasprintf(&str, "%d", file->association < 0? 0: file->mnt_id);
		break;
	case COL_MODE:
		if (does_file_has_fdinfo_alike(file))
			xasprintf(&str, "%c%c%c",
				  file->mode & S_IRUSR? 'r': '-',
				  file->mode & S_IWUSR? 'w': '-',
				  ((file->association == -ASSOC_SHM
				   || file->association == -ASSOC_MEM)
				   && file->mode & S_IXUSR)? 'x': '-');
		else
			xasprintf(&str, "---");
		break;
	case COL_POS:
		xasprintf(&str, "%" PRIu64,
			  (does_file_has_fdinfo_alike(file))? file->pos: 0);
		break;
	case COL_FLAGS: {
		struct ul_buffer buf = UL_INIT_BUFFER;

		if (file->association < 0)
			return true;

		if (file->sys_flags == 0)
			return true;

		file_fill_flags_buf(&buf, file->sys_flags);
		if (ul_buffer_is_empty(&buf))
			return true;
		str = ul_buffer_get_data(&buf, NULL, NULL);
		break;
	}
	case COL_MAPLEN:
		if (file->association != -ASSOC_SHM
		    && file->association != -ASSOC_MEM)
			return true;
		xasprintf(&str, "%ju", (uintmax_t)get_map_length(file));
		break;
	default:
		return false;
	};

	if (!str)
		err(EXIT_FAILURE, _("failed to add output data"));
	if (scols_line_refer_data(ln, column_index, str))
		err(EXIT_FAILURE, _("failed to add output data"));
	return true;
}

static int file_handle_fdinfo(struct file *file, const char *key, const char* value)
{
	int rc;

	if (strcmp(key, "pos") == 0) {
		rc = ul_strtou64(value, &file->pos, 10);

	} else if (strcmp(key, "flags") == 0) {
		rc = ul_strtou32(value, &file->sys_flags, 8);

	} else if (strcmp(key, "mnt_id") == 0) {
		rc = ul_strtou32(value, &file->mnt_id, 10);

	} else
		return 0;	/* ignore -- unknown item */

	if (rc < 0)
		return 0;	/* ignore -- parse failed */

	return 1;		/* success */
}

static void file_free_content(struct file *file)
{
	free(file->name);
}

static void file_class_initialize(void)
{
	username_cache = new_idcache();
	if (!username_cache)
		err(EXIT_FAILURE, _("failed to allocate UID cache"));
}

static void file_class_finalize(void)
{
	free_idcache(username_cache);
}

const struct file_class file_class = {
	.super = NULL,
	.size = sizeof(struct file),
	.initialize_class = file_class_initialize,
	.finalize_class = file_class_finalize,
	.fill_column = file_fill_column,
	.handle_fdinfo = file_handle_fdinfo,
	.free_content = file_free_content,
};
