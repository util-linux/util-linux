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
#include <pthread.h>
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
#include "idcache.h"

#include "libsmartcols.h"

#include "lsfd.h"

/*
 * Multi-threading related stuffs
 */
#define NUM_COLLECTORS 1
static pthread_cond_t  procs_ready = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t procs_ready_lock = PTHREAD_MUTEX_INITIALIZER;

static pthread_mutex_t procs_consumer_lock = PTHREAD_MUTEX_INITIALIZER;
static struct list_head *current_proc;

static void *fill_procs(void *arg);

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
 * idcaches
 */
struct idcache *username_cache;

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
	[COL_COMMAND] = { "COMMAND", 15, SCOLS_FL_TRUNC, SCOLS_JSON_STRING,
		N_("command of the process opening the file") },
	[COL_DELETED] = { "DELETED",  0, SCOLS_FL_RIGHT, SCOLS_JSON_BOOLEAN,
		N_("reachability from the file system") },
	[COL_DEV]     = { "DEV",      0, SCOLS_FL_RIGHT, SCOLS_JSON_STRING,
		N_("ID of device containing file") },
	[COL_DEVICE]  = { "DEVICE",   0, SCOLS_FL_RIGHT, SCOLS_JSON_STRING,
		N_("device ID for special, or ID of device containing file") },
	[COL_FLAGS]   = { "FLAGS",    0, SCOLS_FL_RIGHT, SCOLS_JSON_STRING,
		N_("flags specified when opening the file") },
	[COL_FD]      = { "FD",       0, SCOLS_FL_RIGHT, SCOLS_JSON_NUMBER,
		N_("file descriptor for the file") },
	[COL_INODE]   = { "INODE",    0, SCOLS_FL_RIGHT, SCOLS_JSON_NUMBER,
		N_("inode number") },
	[COL_MNT_ID]  = { "MNTID",    0, SCOLS_FL_RIGHT, SCOLS_JSON_NUMBER,
		N_("mount id") },
	[COL_MODE]    = { "MODE",     0, SCOLS_FL_RIGHT, SCOLS_JSON_STRING,
		N_("access mode (rwx)") },
	[COL_NAME]    = { "NAME",    45, 0,              SCOLS_JSON_STRING,
		N_("name of the file") },
	[COL_NLINK]   = { "NLINK",    0, SCOLS_FL_RIGHT, SCOLS_JSON_NUMBER,
		N_("link count") },
	[COL_PID]     = { "PID",      5, SCOLS_FL_RIGHT, SCOLS_JSON_NUMBER,
		N_("PID of the process opening the file") },
	[COL_POS]     = { "POS",      5, SCOLS_FL_RIGHT, SCOLS_JSON_NUMBER,
		N_("file position") },
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

static int default_columns[] = {
	COL_COMMAND,
	COL_PID,
	COL_USER,
	COL_ASSOC,
	COL_MODE,
	COL_TYPE,
	COL_DEVICE,
	COL_MNT_ID,
	COL_INODE,
	COL_NAME,
};

static int default_threads_columns[] = {
	COL_COMMAND,
	COL_PID,
	COL_TID,
	COL_USER,
	COL_ASSOC,
	COL_MODE,
	COL_TYPE,
	COL_DEVICE,
	COL_MNT_ID,
	COL_INODE,
	COL_NAME,
};

static int columns[ARRAY_SIZE(infos) * 2] = {-1};
static size_t ncolumns;

struct lsfd_control {
	struct libscols_table *tb;		/* output */

	unsigned int noheadings : 1,
		raw : 1,
		json : 1,
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

static struct proc *make_proc(pid_t pid, struct proc * leader)
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

		proc = make_proc((pid_t)num, leader);
		enqueue_proc(procs, proc);
	}
}

static void collect_procs(DIR *dirp, struct list_head *procs,
			  struct lsfd_control *ctl)
{
	struct dirent *dp;
	long num;

	while ((dp = readdir(dirp))) {
		struct proc *proc;

		/* care only for numerical entries.
		 * For a non-numerical entry, strtol returns 0.
		 * We can skip it because there is no task having 0 as pid. */
		if (!(num = strtol(dp->d_name, (char **) NULL, 10)))
			continue;

		proc = make_proc((pid_t)num, NULL);
		enqueue_proc(procs, proc);

		if (ctl->threads) {
			DIR *task_dirp = opendirf("/proc/%s/task", dp->d_name);
			if (task_dirp) {
				collect_tasks(proc, task_dirp, procs);
				closedir(task_dirp);
			}
		}
	}
}

static void run_collectors(struct list_head *procs)
{
	pthread_t collectors[NUM_COLLECTORS];

	for (int i = 0; i < NUM_COLLECTORS; i++) {
		errno = pthread_create(collectors + i, NULL, fill_procs, procs);
		if (errno)
			err(EXIT_FAILURE, _("failed to create a thread"));
	}

	errno = pthread_mutex_lock(&procs_ready_lock);
	if (errno != 0)
		err(EXIT_FAILURE, _("failed to lock a mutex"));

	current_proc = procs->next;

	errno = pthread_mutex_unlock(&procs_ready_lock);
	if (errno != 0)
		err(EXIT_FAILURE, _("failed to unlock a mutex"));

	errno = pthread_cond_broadcast(&procs_ready);
	if (errno != 0)
		err(EXIT_FAILURE, _("failed to broadcast a condvar"));

	for (int i = 0; i < NUM_COLLECTORS; i++)
		pthread_join(collectors[i], NULL);
}

static void collect(struct list_head *procs, struct lsfd_control *ctl)
{
	DIR *dirp;

	dirp = opendir("/proc");
	if (!dirp)
		err(EXIT_FAILURE, _("failed to open /proc"));
	collect_procs(dirp, procs, ctl);
	closedir(dirp);

	run_collectors(procs);
}

static struct file *collect_file(struct stat *sb, char *name, int assoc)
{
	switch (sb->st_mode & S_IFMT) {
	case S_IFCHR:
		return make_cdev(NULL, sb, name, assoc);
	case S_IFBLK:
		return make_bdev(NULL, sb, name, assoc);
	case S_IFSOCK:
		return make_sock(NULL, sb, name, assoc);
	case S_IFLNK:
	case S_IFREG:
	case S_IFIFO:
	case S_IFDIR:
		return make_file(NULL, sb, name, assoc);
	default:
		return make_unkn(NULL, sb, name, assoc);
	}
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

static struct file *collect_fd_file(int dd, struct dirent *dp, void *data)
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

	f = collect_file(&sb, sym, (int)num);
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

static struct file *collect_mem_file(int dd, struct dirent *dp,
				     void *data)
{
	struct list_head *maps = data;
	struct stat sb;
	ssize_t len;
	char sym[PATH_MAX];
	struct file *f;
	unsigned long start, end;
	struct map *map;
	enum association assoc;

	if (fstatat(dd, dp->d_name, &sb, 0) < 0)
		return NULL;

	memset(sym, 0, sizeof(sym));
	if ((len = readlinkat(dd, dp->d_name, sym, sizeof(sym) - 1)) < 0)
		return NULL;


	map = NULL;
	if (sscanf(dp->d_name, "%lx-%lx", &start, &end) == 2)
		map = find_map(maps, start);

	assoc = (map && map->shared)? ASSOC_SHM: ASSOC_MEM;
	f = collect_file(&sb, sym, -assoc);
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
				     struct file *(*collector)(int, struct dirent *, void *),
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

		if ((file = (* collector)(dd, dp, data)) == NULL)
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

static struct map* make_map(unsigned long mem_addr_start, unsigned long long file_offset,
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
		map = make_map(start, file_offset, r == 'r', w == 'w', x == 'x', s == 's');
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

static struct file *collect_outofbox_file(int dd, const char *name, int association)
{
	struct stat sb;
	ssize_t len;
	char sym[PATH_MAX];

	if (fstatat(dd, name, &sb, 0) < 0)
		return NULL;

	memset(sym, 0, sizeof(sym));
	if ((len = readlinkat(dd, name, sym, sizeof(sym) - 1)) < 0)
		return NULL;

	return collect_file(&sb, sym, association);
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

		if ((file = collect_outofbox_file(dd,
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

static void fill_proc(struct proc *proc)
{
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

static void *fill_procs(void *arg)
{
	struct list_head *procs = arg;
	struct list_head *target_proc;

	errno = pthread_mutex_lock(&procs_ready_lock);
	if (errno != 0)
		err(EXIT_FAILURE, _("failed to lock a mutex"));

	if (current_proc == NULL) {
		errno = pthread_cond_wait(&procs_ready, &procs_ready_lock);
		if (errno != 0)
			err(EXIT_FAILURE, _("failed to wait a condvar"));
	}

	errno = pthread_mutex_unlock(&procs_ready_lock);
	if (errno != 0)
		err(EXIT_FAILURE, _("failed to unlock a mutex"));

	while (1) {
		errno = pthread_mutex_lock(&procs_consumer_lock);
		if (errno != 0)
			err(EXIT_FAILURE, _("failed to lock a mutex"));

		target_proc = current_proc;

		if (current_proc != procs)
			current_proc = current_proc->next;

		errno = pthread_mutex_unlock(&procs_consumer_lock);
		if (errno != 0)
			err(EXIT_FAILURE, _("failed to lock a mutex"));

		if (target_proc == procs) {
			/* All pids are processed. */
			break;
		}

		fill_proc(list_entry(target_proc, struct proc, procs));
	}

	return NULL;
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

	fputs(USAGE_SEPARATOR, out);
	printf(USAGE_HELP_OPTIONS(23));

	fprintf(out, USAGE_COLUMNS);

	for (i = 0; i < ARRAY_SIZE(infos); i++)
		fprintf(out, " %11s  %s\n", infos[i].name, _(infos[i].help));

	printf(USAGE_MAN_TAIL("lsfd(1)"));

	exit(EXIT_SUCCESS);
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

int main(int argc, char *argv[])
{
	int c;
	char *outarg = NULL;

	struct list_head procs;

	struct lsfd_control ctl = {};

	static const struct option longopts[] = {
		{ "noheadings", no_argument, NULL, 'n' },
		{ "output",     required_argument, NULL, 'o' },
		{ "version",    no_argument, NULL, 'V' },
		{ "help",	no_argument, NULL, 'h' },
		{ "json",       no_argument, NULL, 'J' },
		{ "raw",        no_argument, NULL, 'r' },
		{ "threads",    no_argument, NULL, 'l' },
		{ NULL, 0, NULL, 0 },
	};

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	close_stdout_atexit();

	while ((c = getopt_long(argc, argv, "no:JrVhl", longopts, NULL)) != -1) {
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
		case 'V':
			print_version(EXIT_SUCCESS);
		case 'h':
			usage();
		default:
			errtryhelp(EXIT_FAILURE);
		}
	}

#define INITIALIZE_COLUMNS(COLUMN_SPEC)				\
	for (size_t i = 0; i < ARRAY_SIZE(COLUMN_SPEC); i++)	\
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

	username_cache = new_idcache();
	if (!username_cache)
		err(EXIT_FAILURE, _("failed to allocate UID cache"));

	scols_init_debug(0);
	ctl.tb = scols_new_table();

	if (!ctl.tb)
		err(EXIT_FAILURE, _("failed to allocate output table"));

	scols_table_enable_noheadings(ctl.tb, ctl.noheadings);
	scols_table_enable_raw(ctl.tb, ctl.raw);
	scols_table_enable_json(ctl.tb, ctl.json);
	if (ctl.json)
		scols_table_set_name(ctl.tb, "lsfd");

	for (size_t i = 0; i < ncolumns; i++) {
		const struct colinfo *col = get_column_info(i);
		struct libscols_column *cl;

		cl = scols_table_new_column(ctl.tb, col->name, col->whint, col->flags);
		if (!cl)
			err(EXIT_FAILURE, _("failed to allocate output column"));

		if (ctl.json)
			scols_column_set_json_type(cl, col->json_type);
	}

	initialize_classes();

	INIT_LIST_HEAD(&procs);
	collect(&procs, &ctl);

	convert(&procs, &ctl);
	emit(&ctl);
	delete(&procs, &ctl);

	free_idcache(username_cache);

	finalize_classes();

	return 0;
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
