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

#include <stdio.h>
#include <sys/types.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <unistd.h>
#include <getopt.h>

#include <sys/syscall.h>
#include <linux/kcmp.h>
static int kcmp(pid_t pid1, pid_t pid2, int type,
		unsigned long idx1, unsigned long idx2)
{
	return syscall(SYS_kcmp, pid1, pid2, type, idx1, idx2);
}

#include "c.h"
#include "nls.h"
#include "xalloc.h"
#include "list.h"
#include "closestream.h"
#include "strutils.h"
#include "procutils.h"
#include "fileutils.h"
#include "path.h"
#include "idcache.h"

#include "libsmartcols.h"

#include "lsfd.h"


static void fill_proc(struct proc *proc);
static void add_nodev(unsigned long minor, const char *filesystem);

/*
 * /proc/$pid/maps entries
 */
struct map {
	struct list_head maps;
	unsigned long mem_addr_start;
	unsigned long long file_offset;
	unsigned int read:1, write:1, exec:1, shared:1;
};

/*
 * /proc/$pid/mountinfo entries
 */
struct nodev {
	struct list_head nodevs;
	unsigned long minor;
	char *filesystem;
};

struct nodev_table {
#define NODEV_TABLE_SIZE 97
	struct list_head tables[NODEV_TABLE_SIZE];
} nodev_table;

struct name_manager {
	struct idcache *cache;
	unsigned long next_id;
};

/*
 * Column related stuffs
 */

/* column names */
struct colinfo {
	const char *name;
	double whint;
	int flags;
	int json_type;
	const char *help;
};

/* columns descriptions */
static struct colinfo infos[] = {
	[COL_ASSOC]   = { "ASSOC",    0, SCOLS_FL_RIGHT, SCOLS_JSON_STRING,
		N_("association between file and process") },
	[COL_CHRDRV]  = { "CHRDRV",   0, SCOLS_FL_RIGHT, SCOLS_JSON_STRING,
		N_("charcter device driver name resolved by /proc/devices") },
	[COL_COMMAND] = { "COMMAND",0.3, SCOLS_FL_TRUNC, SCOLS_JSON_STRING,
		N_("command of the process opening the file") },
	[COL_DELETED] = { "DELETED",  0, SCOLS_FL_RIGHT, SCOLS_JSON_BOOLEAN,
		N_("reachability from the file system") },
	[COL_DEV]     = { "DEV",      0, SCOLS_FL_RIGHT, SCOLS_JSON_STRING,
		N_("ID of device containing file") },
	[COL_DEVICE]  = { "DEVICE",   0, SCOLS_FL_RIGHT, SCOLS_JSON_STRING,
		N_("device ID for special, or ID of device containing file") },
	[COL_DEVNAME] = { "DEVNAME",  0, SCOLS_FL_RIGHT, SCOLS_JSON_STRING,
		N_("device name, decoded version of DEVICE") },
	[COL_FLAGS]   = { "FLAGS",    0, SCOLS_FL_RIGHT, SCOLS_JSON_STRING,
		N_("flags specified when opening the file") },
	[COL_FD]      = { "FD",       0, SCOLS_FL_RIGHT, SCOLS_JSON_NUMBER,
		N_("file descriptor for the file") },
	[COL_INODE]   = { "INODE",    0, SCOLS_FL_RIGHT, SCOLS_JSON_NUMBER,
		N_("inode number") },
	[COL_MAPLEN]  = { "MAPLEN",   0, SCOLS_FL_RIGHT, SCOLS_JSON_NUMBER,
		N_("length of file mapping (in page)") },
	[COL_MISCDEV] = { "MISCDEV",  0, SCOLS_FL_RIGHT, SCOLS_JSON_STRING,
		N_("misc character device name resolved by /procmisc") },
	[COL_MNT_ID]  = { "MNTID",    0, SCOLS_FL_RIGHT, SCOLS_JSON_NUMBER,
		N_("mount id") },
	[COL_MODE]    = { "MODE",     0, SCOLS_FL_RIGHT, SCOLS_JSON_STRING,
		N_("access mode (rwx)") },
	[COL_NAME]    = { "NAME",   0.4, SCOLS_FL_TRUNC, SCOLS_JSON_STRING,
		N_("name of the file") },
	[COL_NLINK]   = { "NLINK",    0, SCOLS_FL_RIGHT, SCOLS_JSON_NUMBER,
		N_("link count") },
	[COL_PID]     = { "PID",      5, SCOLS_FL_RIGHT, SCOLS_JSON_NUMBER,
		N_("PID of the process opening the file") },
	[COL_PARTITION]={ "PARTITION",0, SCOLS_FL_RIGHT, SCOLS_JSON_STRING,
		N_("block device name resolved by /proc/partition") },
	[COL_POS]     = { "POS",      5, SCOLS_FL_RIGHT, SCOLS_JSON_NUMBER,
		N_("file position") },
	[COL_PROTONAME]={ "PROTONAME",0, SCOLS_FL_RIGHT, SCOLS_JSON_STRING,
		N_("protocol name") },
	[COL_RDEV]    = { "RDEV",     0, SCOLS_FL_RIGHT, SCOLS_JSON_STRING,
		N_("device ID (if special file)") },
	[COL_SIZE]    = { "SIZE",     4, SCOLS_FL_RIGHT, SCOLS_JSON_NUMBER,
		N_("file size"), },
	[COL_TID]    = { "TID",       5, SCOLS_FL_RIGHT, SCOLS_JSON_NUMBER,
		N_("thread ID of the process opening the file") },
	[COL_TYPE]    = { "TYPE",     0, SCOLS_FL_RIGHT, SCOLS_JSON_STRING,
		N_("file type") },
	[COL_UID]     = { "UID",      0, SCOLS_FL_RIGHT, SCOLS_JSON_NUMBER,
		N_("user ID number") },
	[COL_USER]    = { "USER",     0, SCOLS_FL_RIGHT, SCOLS_JSON_STRING,
		N_("user of the process") },
};

static const int default_columns[] = {
	COL_COMMAND,
	COL_PID,
	COL_USER,
	COL_ASSOC,
	COL_MODE,
	COL_TYPE,
	COL_DEVNAME,
	COL_MNT_ID,
	COL_INODE,
	COL_NAME,
};

static const int default_threads_columns[] = {
	COL_COMMAND,
	COL_PID,
	COL_TID,
	COL_USER,
	COL_ASSOC,
	COL_MODE,
	COL_TYPE,
	COL_DEVNAME,
	COL_MNT_ID,
	COL_INODE,
	COL_NAME,
};

static int columns[ARRAY_SIZE(infos) * 2] = {-1};
static size_t ncolumns;

struct lsfd_control {
	struct libscols_table *tb;		/* output */
	const char *sysroot;			/* default is NULL */
	struct path_cxt *procfs;

	unsigned int	noheadings : 1,
			raw : 1,
			json : 1,
			notrunc : 1,
			threads : 1;
};

static int column_name_to_id(const char *name, size_t namesz)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(infos); i++) {
		const char *cn = infos[i].name;

		if (!strncasecmp(name, cn, namesz) && !*(cn + namesz))
			return i;
	}
	warnx(_("unknown column: %s"), name);

	return -1;
}

static int get_column_id(int num)
{
	assert(num >= 0);
	assert((size_t) num < ncolumns);
	assert(columns[num] < (int) ARRAY_SIZE(infos));

	return columns[num];
}

static const struct colinfo *get_column_info(int num)
{
	return &infos[ get_column_id(num) ];
}


static struct file *new_file(const struct file_class *class,
		       struct proc *proc,
		       struct stat *sb, const char *name,
		       struct map_file_data *map_file_data,
		       int association)
{
	struct file *file;

	assert(class);

	file = xcalloc(1, class->size);
	file->class = class;
	file->association = association;
	file->name = xstrdup(name);
	file->stat = *sb;

	if (file->association == -ASSOC_SHM || file->association == -ASSOC_MEM) {
		static size_t pagesize = 0;

		assert(map_file_data);
		if (!pagesize)
			pagesize = getpagesize();

		file->assoc_data.map_length =
			(map_file_data->end - map_file_data->start) / pagesize;
	}

	if (file->class->initialize_content)
		file->class->initialize_content(file, proc, map_file_data);

	return file;
}


static struct proc *new_prococess(pid_t pid, struct proc * leader)
{
	struct proc *proc = xcalloc(1, sizeof(*proc));

	proc->pid  = pid;
	proc->leader = leader? leader: proc;
	proc->command = NULL;

	return proc;
}

static void free_file(struct file *file)
{
	const struct file_class *class = file->class;

	while (class) {
		if (class->free_content)
			class->free_content(file);
		class = class->super;
	}
	free(file);
}

static struct nodev* new_nodev(unsigned long minor, const char *filesystem)
{
	struct nodev *nodev = xcalloc(1, sizeof(*nodev));

	INIT_LIST_HEAD(&nodev->nodevs);
	nodev->minor = minor;
	nodev->filesystem = xstrdup(filesystem);

	return nodev;
}

static void free_nodev(struct nodev *nodev)
{
	free(nodev->filesystem);
	free(nodev);
}

static void initialize_nodevs(void)
{
	for (int i = 0; i < NODEV_TABLE_SIZE; i++)
		INIT_LIST_HEAD(&nodev_table.tables[i]);
}

static void finalize_nodevs(void)
{
	for (int i = 0; i < NODEV_TABLE_SIZE; i++)
		list_free(&nodev_table.tables[i], struct nodev, nodevs, free_nodev);
}

static void free_proc(struct proc *proc)
{
	list_free(&proc->files, struct file, files, free_file);

	free(proc->command);
	free(proc);
}

static void enqueue_proc(struct list_head *procs, struct proc * proc)
{
	INIT_LIST_HEAD(&proc->procs);
	list_add_tail(&proc->procs, procs);
}

static void collect_tasks(struct proc *leader,
			  DIR *task_dirp, struct list_head *procs)
{
	struct dirent *dp;
	long num;

	while ((dp = xreaddir(task_dirp))) {
		struct proc *proc;

		/* care only for numerical entries.
		 * For a non-numerical entry, strtol returns 0.
		 * We can skip it because there is no task having 0 as pid. */
		if (!(num = strtol(dp->d_name, (char **) NULL, 10)))
			continue;

		if (leader->pid == (pid_t) num) {
			/* The leader is already queued. */
			continue;
		}

		proc = new_prococess((pid_t)num, leader);
		enqueue_proc(procs, proc);
	}
}

static void collect(struct list_head *procs, struct lsfd_control *ctl)
{
	DIR *dir;
	struct dirent *dp;
	struct list_head *p;

	/* open /proc */
	dir = ul_path_opendir(ctl->procfs, NULL);
	if (!dir)
		err(EXIT_FAILURE, _("failed to open /proc"));

	/* read /proc */
	while ((dp = readdir(dir))) {
		struct proc *proc;
		long num;

#ifdef _DIRENT_HAVE_D_TYPE
		if (dp->d_type != DT_DIR && dp->d_type != DT_UNKNOWN)
			continue;
#endif
		/* care only for numerical entries.
		 * For a non-numerical entry, strtol returns 0.
		 * We can skip it because there is no task having 0 as pid. */
		if (!(num = strtol(dp->d_name, NULL, 10)))
			continue;

		proc = new_prococess((pid_t)num, NULL);
		enqueue_proc(procs, proc);

		if (ctl->threads) {
			DIR *task_dirp = ul_path_opendirf(ctl->procfs, "%s/task", dp->d_name);
			if (task_dirp) {
				collect_tasks(proc, task_dirp, procs);
				closedir(task_dirp);
			}
		}

	}

	closedir(dir);

	list_for_each (p, procs) {
		struct proc *proc = list_entry(p, struct proc, procs);
		fill_proc(proc);
	}
}

static const struct file_class *stat2class(struct stat *sb)
{
	assert(sb);

	switch (sb->st_mode & S_IFMT) {
	case S_IFCHR:
		return &cdev_class;
	case S_IFBLK:
		return &bdev_class;
	case S_IFSOCK:
		return &sock_class;
	case S_IFIFO:
		return &fifo_class;
	case S_IFLNK:
	case S_IFREG:
	case S_IFDIR:
		return &file_class;
	default:
		break;
	}

	return &unkn_class;
}

static struct file *collect_file(struct proc *proc,
				 struct stat *sb, char *name,
				 struct map_file_data *map_file_data,
				 int assoc)
{
	return new_file(stat2class(sb), proc, sb, name, map_file_data, assoc);
}

static void read_fdinfo(struct file *file, FILE *fdinfo)
{
	const struct file_class *class;
	char buf[1024];
	char *val;

	while (fgets(buf, sizeof(buf), fdinfo)) {
		val = strchr(buf, ':');
		if (!val)
			continue;
		*val++ = '\0';
		while (*val == '\t' || *val == ' ')
			val++;

		class = file->class;
		while (class) {
			if (class->handle_fdinfo
			    && class->handle_fdinfo(file, buf, val))
				break;
			class = class->super;
		}
	}
}

static struct file *collect_fd_file(struct proc *proc, int dd, struct dirent *dp, void *data)
{
	long num;
	char *endptr = NULL;
	struct stat sb, lsb;
	ssize_t len;
	char sym[PATH_MAX];
	struct file *f;
	int *fdinfo_dd = data;
	FILE *fdinfo_fp;

	/* care only for numerical descriptors */
	num = strtol(dp->d_name, &endptr, 10);
	if (num == 0 && endptr == dp->d_name)
		return NULL;

	if (fstatat(dd, dp->d_name, &sb, 0) < 0)
		return NULL;

	memset(sym, 0, sizeof(sym));
	if ((len = readlinkat(dd, dp->d_name, sym, sizeof(sym) - 1)) < 0)
		return NULL;

	f = collect_file(proc, &sb, sym, NULL, (int)num);
	if (!f)
		return NULL;

	if (fstatat(dd, dp->d_name, &lsb, AT_SYMLINK_NOFOLLOW) == 0)
		f->mode = lsb.st_mode;

	if (*fdinfo_dd < 0)
		return f;

	fdinfo_fp = fopen_at(*fdinfo_dd, dp->d_name, O_RDONLY, "r");
	if (fdinfo_fp) {
		read_fdinfo(f, fdinfo_fp);
		fclose(fdinfo_fp);
	}

	return f;
}

static struct map *find_map(struct list_head *maps, unsigned long start_addr)
{
	struct list_head *m;

	list_for_each(m, maps) {
		struct map *map = list_entry(m, struct map, maps);
		if (map->mem_addr_start == start_addr)
			return map;
	}
	return NULL;
}

static struct file *collect_mem_file(struct proc *proc, int dd, struct dirent *dp,
				     void *data)
{
	struct list_head *maps = data;
	struct stat sb;
	ssize_t len;
	char sym[PATH_MAX];
	struct file *f;
	struct map_file_data map_file_data;
	struct map *map;
	enum association assoc;

	if (fstatat(dd, dp->d_name, &sb, 0) < 0)
		return NULL;

	memset(sym, 0, sizeof(sym));
	if ((len = readlinkat(dd, dp->d_name, sym, sizeof(sym) - 1)) < 0)
		return NULL;


	map = NULL;
	if (sscanf(dp->d_name, "%lx-%lx", &map_file_data.start, &map_file_data.end) == 2)
		map = find_map(maps, map_file_data.start);

	assoc = (map && map->shared)? ASSOC_SHM: ASSOC_MEM;
	f = collect_file(proc, &sb, sym, &map_file_data, -assoc);
	if (!f)
		return NULL;

	if (map) {
		f->mode = (map->read? S_IRUSR: 0) | (map->write? S_IWUSR: 0) | (map->exec? S_IXUSR: 0);
		f->pos = map->file_offset;
	}

	return f;
}

static void enqueue_file(struct proc *proc, struct file * file)
{
	INIT_LIST_HEAD(&file->files);
	list_add_tail(&file->files, &proc->files);
}


static void collect_fd_files_generic(struct proc *proc, const char *proc_template,
				     struct file *(*collector)(struct proc *,int, struct dirent *, void *),
				     void *data)
{
	DIR *dirp;
	int dd;
	struct dirent *dp;

	dirp = opendirf(proc_template, proc->pid);
	if (!dirp)
		return;

	if ((dd = dirfd(dirp)) < 0 )
		return;

	while ((dp = xreaddir(dirp))) {
		struct file *file;

		if ((file = (* collector)(proc, dd, dp, data)) == NULL)
			continue;

		enqueue_file(proc, file);
	}
	closedir(dirp);
}

static void collect_fd_files(struct proc *proc)
{
	DIR *dirp;
	int dd = -1;

	dirp = opendirf("/proc/%d/fdinfo/", proc->pid);
	if (dirp)
		dd = dirfd(dirp);

	collect_fd_files_generic(proc, "/proc/%d/fd/", collect_fd_file, &dd);

	if (dirp)
		closedir(dirp);
}

static struct map* new_map(unsigned long mem_addr_start, unsigned long long file_offset,
			    int r, int w, int x, int s)
{
	struct map *map = xcalloc(1, sizeof(*map));

	INIT_LIST_HEAD(&map->maps);

	map->mem_addr_start = mem_addr_start;
	map->file_offset = file_offset;

	map->read   = r;
	map->write  = w;
	map->exec   = x;
	map->shared = s;

	return map;
}

static void free_map(struct map *map)
{
	free(map);
}

static void read_maps(struct list_head *maps_list, FILE *maps_fp)
{
	/* Taken from proc(5):
	 * address           perms offset  dev   inode       pathname
	 * 00400000-00452000 r-xp 00000000 08:02 173521      /usr/bin/dbus-daemon
	 * ... */

	char line[PATH_MAX + 128];
	unsigned long start, end;
	char r, w, x, s;
	unsigned long long file_offset;
	unsigned long major;
	unsigned long minor;
	long unsigned inode;

	while (fgets(line, sizeof(line), maps_fp)) {
		struct map *map;

		if (sscanf(line, "%lx-%lx %c%c%c%c %llx %lx:%lx %lu %*[^\n]",
			   &start, &end, &r, &w, &x, &s, &file_offset,
			   &major, &minor, &inode) != 10)
			continue;
		map = new_map(start, file_offset, r == 'r', w == 'w', x == 'x', s == 's');
		list_add_tail(&map->maps, maps_list);
	}
}

static FILE* open_maps(pid_t pid)
{
	return fopenf("r", "/proc/%d/maps", pid);
}

static void free_maps(struct list_head *maps)
{
	list_free(maps, struct map, maps, free_map);
}

static void collect_mem_files(struct proc *proc)
{

	struct list_head maps;
	FILE *fp;

	INIT_LIST_HEAD(&maps);
	fp = open_maps(proc->pid);
	if (fp) {
		read_maps(&maps, fp);
		fclose(fp);
	}

	collect_fd_files_generic(proc, "/proc/%d/map_files/", collect_mem_file, &maps);

	free_maps(&maps);
}

static struct file *collect_outofbox_file(struct proc *proc,
					  int dd, const char *name, int association)
{
	struct stat sb;
	ssize_t len;
	char sym[PATH_MAX];

	if (fstatat(dd, name, &sb, 0) < 0)
		return NULL;

	memset(sym, 0, sizeof(sym));
	if ((len = readlinkat(dd, name, sym, sizeof(sym) - 1)) < 0)
		return NULL;

	return collect_file(proc, &sb, sym, NULL, association);
}

static void collect_proc_uid(struct proc *proc, int dd)
{
	struct stat sb;
	if (fstat(dd, &sb) == 0)
		proc->uid = sb.st_uid;
}

static void collect_outofbox_files(struct proc *proc,
				   const char *proc_template,
				   enum association assocs[],
				   const char* assoc_names[],
				   unsigned int count)
{
	DIR *dirp;
	int dd;

	dirp = opendirf(proc_template, proc->pid);
	if (!dirp)
		return;

	if ((dd = dirfd(dirp)) < 0 )
		return;

	for (unsigned int i = 0; i < count; i++) {
		struct file *file;

		if (assocs[i] == ASSOC_EXE)
			collect_proc_uid(proc, dd);

		if ((file = collect_outofbox_file(proc, dd,
						  assoc_names[assocs[i]],
						  assocs[i] * -1)) == NULL)
			continue;

		enqueue_file(proc, file);
	}
	closedir(dirp);
}

static void collect_execve_file(struct proc *proc)
{
	const char *execve_template = "/proc/%d";
	enum association execve_assocs[] = { ASSOC_EXE };
	const char* execve_assoc_names[] = {
		[ASSOC_EXE]  = "exe",
	};
	collect_outofbox_files(proc, execve_template,
			       execve_assocs, execve_assoc_names,
			       ARRAY_SIZE(execve_assocs));
}

static void collect_execve_and_fs_files(struct proc *proc)
{
	const char *execve_template = "/proc/%d";
	enum association execve_assocs[] = { ASSOC_EXE, ASSOC_CWD, ASSOC_ROOT };
	const char* execve_assoc_names[] = {
		[ASSOC_EXE]  = "exe",
		[ASSOC_CWD]  = "cwd",
		[ASSOC_ROOT] = "root",
	};
	collect_outofbox_files(proc, execve_template,
			       execve_assocs, execve_assoc_names,
			       ARRAY_SIZE(execve_assocs));
}


static void collect_namespace_files(struct proc *proc)
{
	const char *namespace_template = "/proc/%d/ns";
	enum association namespace_assocs[] = {
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
	};
	const char* namespace_assoc_names[] = {
		[ASSOC_NS_CGROUP] = "cgroup",
		[ASSOC_NS_IPC]    = "ipc",
		[ASSOC_NS_MNT]    = "mnt",
		[ASSOC_NS_NET]    = "net",
		[ASSOC_NS_PID]    = "pid",
		[ASSOC_NS_PID4C]  = "pid_for_children",
		[ASSOC_NS_TIME]   = "time",
		[ASSOC_NS_TIME4C] = "time_for_children",
		[ASSOC_NS_USER]   = "user",
		[ASSOC_NS_UTS]    = "uts",
	};
	collect_outofbox_files(proc, namespace_template,
			       namespace_assocs, namespace_assoc_names,
			       ARRAY_SIZE(namespace_assocs));
}

static FILE *open_mountinfo(pid_t pid, pid_t tid)
{
	return fopenf("r", "/proc/%d/task/%d/mountinfo", pid, tid);
}

static void add_nodevs(FILE *mountinfo_fp)
{
	/* This can be very long.
	   A line in mountinfo can have more than 3 paths. */
	char line[PATH_MAX * 3 + 256];
	while (fgets(line, sizeof(line), mountinfo_fp)) {
		unsigned long major, minor;
		char filesystem[256];

		/* 23 61 0:22 / /sys rw,nosuid,nodev,noexec,relatime shared:2 - sysfs sysfs rw,seclabel */
		if(sscanf(line, "%*d %*d %lu:%lu %*s %*s %*s %*[^-] - %s %*[^\n]",
			  &major, &minor, filesystem) != 3)
			/* 1600 1458 0:55 / / rw,nodev,relatime - overlay overlay rw,context="s... */
			if (sscanf(line, "%*d %*d %lu:%lu %*s %*s %*s - %s %*[^\n]",
				   &major, &minor, filesystem) != 3)
				continue;

		if (major != 0)
			continue;

		add_nodev(minor, filesystem);
	}
}

static void fill_proc(struct proc *proc)
{
	FILE *mountinfo_fp;

	INIT_LIST_HEAD(&proc->files);

	proc->command = proc_get_command_name(proc->pid);
	if (!proc->command)
		proc->command = xstrdup(_("(unknown)"));


	if (proc->pid == proc->leader->pid
	    || kcmp(proc->leader->pid, proc->pid,
		    KCMP_FS, 0, 0) != 0)
		collect_execve_and_fs_files(proc);
	else
		collect_execve_file(proc);

	collect_namespace_files(proc);

	mountinfo_fp = open_mountinfo(proc->leader->pid,
				      proc->pid);
	if (mountinfo_fp) {
		add_nodevs(mountinfo_fp);
		fclose(mountinfo_fp);
	}

	/* If kcmp is not available,
	 * there is no way to no whether threads share resources.
	 * In such cases, we must pay the costs: call collect_mem_files()
	 * and collect_fd_files().
	 */
	if (proc->pid == proc->leader->pid
	    || kcmp(proc->leader->pid, proc->pid,
		    KCMP_VM, 0, 0) != 0)
		collect_mem_files(proc);

	if (proc->pid == proc->leader->pid
	    || kcmp(proc->leader->pid, proc->pid,
		    KCMP_FILES, 0, 0) != 0)
		collect_fd_files(proc);
}

static void fill_column(struct proc *proc,
			struct file *file,
			struct libscols_line *ln,
			int column_id,
			size_t column_index)
{
	const struct file_class *class = file->class;

	while (class) {
		if (class->fill_column
		    && class->fill_column(proc, file, ln,
					  column_id, column_index))
			break;
		class = class->super;
	}
}

static void convert1(struct proc *proc,
		     struct file *file,
		     struct libscols_line *ln)

{
	for (size_t i = 0; i < ncolumns; i++)
		fill_column(proc, file, ln, get_column_id(i), i);
}

static void convert(struct list_head *procs, struct lsfd_control *ctl)
{
	struct list_head *p;

	list_for_each (p, procs) {
		struct proc *proc = list_entry(p, struct proc, procs);
		struct list_head *f;

		list_for_each (f, &proc->files) {
			struct file *file = list_entry(f, struct file, files);
			struct libscols_line *ln = scols_table_new_line(ctl->tb, NULL);
			if (!ln)
				err(EXIT_FAILURE, _("failed to allocate output line"));
			convert1(proc, file, ln);
		}
	}
}

static void delete(struct list_head *procs, struct lsfd_control *ctl)
{
	list_free(procs, struct proc, procs, free_proc);

	scols_unref_table(ctl->tb);
}

static void emit(struct lsfd_control *ctl)
{
	scols_print_table(ctl->tb);
}


static void initialize_class(const struct file_class *class)
{
	if (class->initialize_class)
		class->initialize_class();
}

static void initialize_classes(void)
{
	initialize_class(&file_class);
	initialize_class(&cdev_class);
	initialize_class(&bdev_class);
	initialize_class(&sock_class);
	initialize_class(&unkn_class);
}

static void finalize_class(const struct file_class *class)
{
	if (class->finalize_class)
		class->finalize_class();
}

static void finalize_classes(void)
{
	finalize_class(&file_class);
	finalize_class(&cdev_class);
	finalize_class(&bdev_class);
	finalize_class(&sock_class);
	finalize_class(&unkn_class);
}



struct name_manager *new_name_manager(void)
{
	struct name_manager *nm = xcalloc(1, sizeof(struct name_manager));

	nm->cache = new_idcache();
	if (!nm->cache)
		err(EXIT_FAILURE, _("failed to allocate an idcache"));

	nm->next_id = 1;	/* 0 is never issued as id. */
	return nm;
}

void free_name_manager(struct name_manager *nm)
{
	free_idcache(nm->cache);
	free(nm);
}

const char *get_name(struct name_manager *nm, unsigned long id)
{
	struct identry *e;

	e = get_id(nm->cache, id);

	return e? e->name: NULL;
}

unsigned long add_name(struct name_manager *nm, const char *name)
{
	struct identry *e = NULL, *tmp;

	for (tmp = nm->cache->ent; tmp; tmp = tmp->next) {
		if (strcmp(tmp->name, name) == 0) {
			e = tmp;
			break;
		}
	}

	if (e)
		return e->id;

	e = xmalloc(sizeof(struct identry));
	e->name = xstrdup(name);
	e->id = nm->next_id++;
	e->next = nm->cache->ent;
	nm->cache->ent = e;

	return e->id;
}

DIR *opendirf(const char *format, ...)
{
	va_list ap;
	char path[PATH_MAX];

	memset(path, 0, sizeof(path));

	va_start(ap, format);
	vsprintf(path, format, ap);
	va_end(ap);

	return opendir(path);
}

FILE *fopenf(const char *mode, const char *format, ...)
{
	va_list ap;
	char path[PATH_MAX];

	memset(path, 0, sizeof(path));

	va_start(ap, format);
	vsprintf(path, format, ap);
	va_end(ap);

	return fopen(path, mode);
}

static void add_nodev(unsigned long minor, const char *filesystem)
{
	struct nodev *nodev;
	int slot;

	if (get_nodev_filesystem(minor))
		return;

	nodev = new_nodev(minor, filesystem);
	slot = minor % NODEV_TABLE_SIZE;

	list_add_tail(&nodev->nodevs, &nodev_table.tables[slot]);
}

const char *get_nodev_filesystem(unsigned long minor)
{
	struct list_head *n;
	int slot = minor % NODEV_TABLE_SIZE;

	list_for_each (n, &nodev_table.tables[slot]) {
		struct nodev *nodev = list_entry(n, struct nodev, nodevs);
		if (nodev->minor == minor)
			return nodev->filesystem;
	}
	return NULL;
}

static void __attribute__((__noreturn__)) usage(void)
{
	FILE *out = stdout;
	size_t i;

	fputs(USAGE_HEADER, out);
	fprintf(out, _(" %s [options]\n"), program_invocation_short_name);

	fputs(USAGE_OPTIONS, out);
	fputs(_(" -l, --threads         list in threads level\n"), out);
	fputs(_(" -J, --json            use JSON output format\n"), out);
	fputs(_(" -n, --noheadings      don't print headings\n"), out);
	fputs(_(" -o, --output <list>   output columns\n"), out);
	fputs(_(" -r, --raw             use raw output format\n"), out);
	fputs(_("     --sysroot <dir>   use specified directory as system root\n"), out);
	fputs(_(" -u, --notruncate      don't truncate text in columns\n"), out);

	fputs(USAGE_SEPARATOR, out);
	printf(USAGE_HELP_OPTIONS(23));

	fprintf(out, USAGE_COLUMNS);

	for (i = 0; i < ARRAY_SIZE(infos); i++)
		fprintf(out, " %11s  %s\n", infos[i].name, _(infos[i].help));

	printf(USAGE_MAN_TAIL("lsfd(1)"));

	exit(EXIT_SUCCESS);
}

int main(int argc, char *argv[])
{
	int c;
	size_t i;
	char *outarg = NULL;

	struct list_head procs;
	struct lsfd_control ctl = {};

	enum {
		OPT_SYSROOT = CHAR_MAX + 1
	};
	static const struct option longopts[] = {
		{ "noheadings", no_argument, NULL, 'n' },
		{ "output",     required_argument, NULL, 'o' },
		{ "version",    no_argument, NULL, 'V' },
		{ "help",	no_argument, NULL, 'h' },
		{ "json",       no_argument, NULL, 'J' },
		{ "raw",        no_argument, NULL, 'r' },
		{ "threads",    no_argument, NULL, 'l' },
		{ "notruncate", no_argument, NULL, 'u' },
		{ "sysroot",    required_argument, NULL, OPT_SYSROOT },
		{ NULL, 0, NULL, 0 },
	};

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	close_stdout_atexit();

	while ((c = getopt_long(argc, argv, "no:JrVhlu", longopts, NULL)) != -1) {
		switch (c) {
		case 'n':
			ctl.noheadings = 1;
			break;
		case 'o':
			outarg = optarg;
			break;
		case 'J':
			ctl.json = 1;
			break;
		case 'r':
			ctl.raw = 1;
			break;
		case 'l':
			ctl.threads = 1;
			break;
		case 'u':
			ctl.notrunc = 1;
			break;
		case OPT_SYSROOT:
			ctl.sysroot = optarg;
			break;

		case 'V':
			print_version(EXIT_SUCCESS);
		case 'h':
			usage();
		default:
			errtryhelp(EXIT_FAILURE);
		}
	}

#define INITIALIZE_COLUMNS(COLUMN_SPEC)				\
	for (i = 0; i < ARRAY_SIZE(COLUMN_SPEC); i++)	\
		columns[ncolumns++] = COLUMN_SPEC[i]
	if (!ncolumns) {
		if (ctl.threads)
			INITIALIZE_COLUMNS(default_threads_columns);
		else
			INITIALIZE_COLUMNS(default_columns);
	}

	if (outarg && string_add_to_idarray(outarg, columns, ARRAY_SIZE(columns),
					    &ncolumns, column_name_to_id) < 0)
		return EXIT_FAILURE;

	scols_init_debug(0);
	ul_path_init_debug();

	/* initialize paths */
	ctl.procfs = ul_new_path("/proc");
	if (!ctl.procfs)
		err(EXIT_FAILURE, _("failed to allocate /proc handler"));
	ul_path_set_prefix(ctl.procfs, ctl.sysroot);

	/* inilialize scols table */
	ctl.tb = scols_new_table();
	if (!ctl.tb)
		err(EXIT_FAILURE, _("failed to allocate output table"));

	scols_table_enable_noheadings(ctl.tb, ctl.noheadings);
	scols_table_enable_raw(ctl.tb, ctl.raw);
	scols_table_enable_json(ctl.tb, ctl.json);
	if (ctl.json)
		scols_table_set_name(ctl.tb, "lsfd");

	/* create output columns */
	for (i = 0; i < ncolumns; i++) {
		const struct colinfo *col = get_column_info(i);
		struct libscols_column *cl;
		int flags = col->flags;

		if (ctl.notrunc)
			flags &= ~SCOLS_FL_TRUNC;
		cl = scols_table_new_column(ctl.tb, col->name, col->whint, flags);
		if (!cl)
			err(EXIT_FAILURE, _("failed to allocate output column"));

		if (ctl.json)
			scols_column_set_json_type(cl, col->json_type);
	}

	/* collect data */
	initialize_nodevs();
	initialize_classes();

	INIT_LIST_HEAD(&procs);
	collect(&procs, &ctl);

	convert(&procs, &ctl);
	emit(&ctl);

	/* cleabup */
	delete(&procs, &ctl);

	finalize_classes();
	finalize_nodevs();

	ul_unref_path(ctl.procfs);

	return 0;
}
