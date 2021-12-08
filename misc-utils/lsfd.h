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
#include <inttypes.h>

#include "list.h"

/*
 * column IDs
 */
enum {
	COL_ASSOC,
	COL_BLKDRV,
	COL_CHRDRV,
	COL_COMMAND,
	COL_DELETED,
	COL_DEV,
	COL_DEVTYPE,
	COL_FD,
	COL_FLAGS,
	COL_INODE,
	COL_KTHREAD,
	COL_MAJMIN,
	COL_MAPLEN,
	COL_MISCDEV,
	COL_MNT_ID,
	COL_MODE,
	COL_NAME,
	COL_NLINK,
	COL_PARTITION,
	COL_PID,
	COL_POS,
	COL_PROTONAME,
	COL_RDEV,
	COL_SIZE,
	COL_SOURCE,
	COL_TID,
	COL_TYPE,
	COL_UID,		/* process */
	COL_USER,		/* process */
	COL_FUID,		/* file */
	COL_OWNER,		/* file */
	LSFD_N_COLS		/* This must be at last. */
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
	ASSOC_MEM,		/* private file mapping */
	ASSOC_SHM,		/* shared file mapping */
	N_ASSOCS,
};

struct proc {
	pid_t pid;
	struct proc * leader;
	char *command;
	uid_t uid;
	ino_t ns_mnt;
	struct list_head procs;
	struct list_head files;
	unsigned int kthread: 1;
};

/*
 * File class
 */
struct file {
	struct list_head files;
	const struct file_class *class;
	int association;
	char *name;
	struct stat stat;
	mode_t mode;
	struct proc *proc;

	uint64_t pos;
	uint64_t map_start;
	uint64_t map_end;

	unsigned int sys_flags;
	unsigned int mnt_id;
};

#define is_association(_f, a)	((_f)->association < 0 && (_f)->association == -ASSOC_ ## a)

struct file_class {
	const struct file_class *super;
	size_t size;
	void (*initialize_class)(void);
	void (*finalize_class)(void);
	bool (*fill_column)(struct proc *proc,
			    struct file *file,
			    struct libscols_line *ln,
			    int column_id,
			    size_t column_index);
	int  (*handle_fdinfo)(struct file *file, const char *key, const char* value);
	void (*initialize_content)(struct file *file);
	void (*free_content)(struct file *file);
};

extern const struct file_class file_class, cdev_class, bdev_class, sock_class, unkn_class, fifo_class;

/*
 * Name managing
 */
struct name_manager;

struct name_manager *new_name_manager(void);
void free_name_manager(struct name_manager *nm);
const char *get_name(struct name_manager *nm, unsigned long id);
unsigned long add_name(struct name_manager *nm, const char *name);

const char *get_partition(dev_t dev);
const char *get_blkdrv(unsigned long major);
const char *get_chrdrv(unsigned long major);
const char *get_miscdev(unsigned long minor);
const char *get_nodev_filesystem(unsigned long minor);

#endif /* UTIL_LINUX_LSFD_H */
