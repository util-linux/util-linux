#ifndef UTIL_LINUX_PROCUTILS
#define UTIL_LINUX_PROCUTILS

#include <dirent.h>

struct proc_tasks {
	DIR *dir;
};

extern struct proc_tasks *proc_open_tasks(pid_t pid);
extern void proc_close_tasks(struct proc_tasks *tasks);
extern int proc_next_tid(struct proc_tasks *tasks, pid_t *tid);

struct proc_processes {
	DIR *dir;

	const char *fltr_name;
	uid_t fltr_uid;

	unsigned int has_fltr_name : 1,
		     has_fltr_uid : 1;
};

extern struct proc_processes *proc_open_processes(void);
extern void proc_close_processes(struct proc_processes *ps);

extern void proc_processes_filter_by_name(struct proc_processes *ps, const char *name);
extern void proc_processes_filter_by_uid(struct proc_processes *ps, uid_t uid);
extern int proc_next_pid(struct proc_processes *ps, pid_t *pid);

extern char *proc_get_command(pid_t pid);
extern char *proc_get_command_name(pid_t pid);

#endif /* UTIL_LINUX_PROCUTILS */
