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
#ifndef UTIL_LINUX_LSFD_H
#define UTIL_LINUX_LSFD_H

#include <stdbool.h>
#include <sys/stat.h>
#include <dirent.h>

#include "idcache.h"
#include "list.h"

/*
 * Utilities
 */
#define list_free(LIST,TYPE,MEMBER,FREEFN)				\
	do {								\
		struct list_head *__p, *__pnext;			\
									\
		list_for_each_safe (__p, __pnext, (LIST)) {		\
			TYPE *__elt = list_entry(__p, TYPE, MEMBER);	\
			list_del(__p);					\
			FREEFN(__elt);					\
		}							\
	} while (0)

DIR *opendirf(const char *format, ...) __attribute__((format (printf, 1, 2)));

/*
 * column IDs
 */
enum {
	COL_ASSOC,
	COL_COMMAND,
	COL_DELETED,
	COL_DEVICE,
	COL_DEV,
	COL_FD,
	COL_FLAGS,
	COL_INODE,
	COL_MNT_ID,
	COL_MODE,
	COL_NAME,
	COL_NLINK,
	COL_PID,
	COL_POS,
	COL_RDEV,
	COL_SIZE,
	COL_TID,
	COL_TYPE,
	COL_UID,
	COL_USER,
};

/*
 * Process structure
 */
enum association {
	ASSOC_EXE = 1,
	ASSOC_CWD,
	ASSOC_ROOT,
	ASSOC_NS_CGROUP,
	ASSOC_NS_IPC,
	ASSOC_NS_MNT,
	ASSOC_NS_NET,
	ASSOC_NS_PID,
	ASSOC_NS_PID4C,
	ASSOC_NS_TIME,
	ASSOC_NS_TIME4C,
	ASSOC_NS_USER,
	ASSOC_NS_UTS,
	ASSOC_MEM,
	N_ASSOCS,
};

struct proc {
	pid_t pid;
	struct proc * leader;
	char *command;
	uid_t uid;
	struct list_head procs;
	struct list_head files;
};

/*
 * File classes
 */
struct file {
	struct list_head files;
	const struct file_class *class;
	int association;
	char *name;
	struct stat stat;
	mode_t mode;
	unsigned long long pos;
	int flags;
	int mnt_id;
};

struct file_class {
	const struct file_class *super;
	size_t size;
	bool (*fill_column)(struct proc *proc,
			    struct file *file,
			    struct libscols_line *ln,
			    int column_id,
			    size_t column_index);
	int  (*handle_fdinfo)(struct file *file, const char *key, const char* value);
	void (*free_content)(struct file *file);
};

extern const struct file_class file_class, cdev_class, bdev_class, sock_class, unkn_class;

struct file *make_file(const struct file_class *class,
		       struct stat *sb, const char *name, int association);
struct file *make_cdev(const struct file_class *class,
		       struct stat *sb, const char *name, int fd);
struct file *make_bdev(const struct file_class *class,
		       struct stat *sb, const char *name, int fd);
struct file *make_sock(const struct file_class *class,
		       struct stat *sb, const char *name, int fd);
struct file *make_unkn(const struct file_class *class,
		       struct stat *sb, const char *name, int fd);

extern struct idcache *username_cache;

#endif /* UTIL_LINUX_LSFD_H */
