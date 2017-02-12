/*
 * lsns(8) - list system namespaces
 *
 * Copyright (C) 2015 Karel Zak <kzak@redhat.com>
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
#include <string.h>
#include <getopt.h>
#include <stdlib.h>
#include <assert.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <wchar.h>
#include <libsmartcols.h>

#include "pathnames.h"
#include "nls.h"
#include "xalloc.h"
#include "c.h"
#include "list.h"
#include "closestream.h"
#include "optutils.h"
#include "procutils.h"
#include "strutils.h"
#include "namespace.h"
#include "path.h"
#include "idcache.h"

#include "debug.h"

static UL_DEBUG_DEFINE_MASK(lsns);
UL_DEBUG_DEFINE_MASKNAMES(lsns) = UL_DEBUG_EMPTY_MASKNAMES;

#define LSNS_DEBUG_INIT		(1 << 1)
#define LSNS_DEBUG_PROC		(1 << 2)
#define LSNS_DEBUG_NS		(1 << 3)
#define LSNS_DEBUG_ALL		0xFFFF

#define DBG(m, x)       __UL_DBG(lsns, LSNS_DEBUG_, m, x)
#define ON_DBG(m, x)    __UL_DBG_CALL(lsns, LSNS_DEBUG_, m, x)

static struct idcache *uid_cache = NULL;

/* column IDs */
enum {
	COL_NS = 0,
	COL_TYPE,
	COL_PATH,
	COL_NPROCS,
	COL_PID,
	COL_PPID,
	COL_COMMAND,
	COL_UID,
	COL_USER
};

/* column names */
struct colinfo {
	const char *name; /* header */
	double	   whint; /* width hint (N < 1 is in percent of termwidth) */
	int	   flags; /* SCOLS_FL_* */
	const char *help;
};

/* columns descriptions */
static const struct colinfo infos[] = {
	[COL_NS]      = { "NS",     10, SCOLS_FL_RIGHT, N_("namespace identifier (inode number)") },
	[COL_TYPE]    = { "TYPE",    5, 0, N_("kind of namespace") },
	[COL_PATH]    = { "PATH",    0, 0, N_("path to the namespace")},
	[COL_NPROCS]  = { "NPROCS",  5, SCOLS_FL_RIGHT, N_("number of processes in the namespace") },
	[COL_PID]     = { "PID",     5, SCOLS_FL_RIGHT, N_("lowest PID in the namespace") },
	[COL_PPID]    = { "PPID",    5, SCOLS_FL_RIGHT, N_("PPID of the PID") },
	[COL_COMMAND] = { "COMMAND", 0, SCOLS_FL_TRUNC, N_("command line of the PID")},
	[COL_UID]     = { "UID",     0, SCOLS_FL_RIGHT, N_("UID of the PID")},
	[COL_USER]    = { "USER",    0, 0, N_("username of the PID")}
};

static int columns[ARRAY_SIZE(infos) * 2];
static size_t ncolumns;

enum {
	LSNS_ID_MNT = 0,
	LSNS_ID_NET,
	LSNS_ID_PID,
	LSNS_ID_UTS,
	LSNS_ID_IPC,
	LSNS_ID_USER,
	LSNS_ID_CGROUP
};

static char *ns_names[] = {
	[LSNS_ID_MNT] = "mnt",
	[LSNS_ID_NET] = "net",
	[LSNS_ID_PID] = "pid",
	[LSNS_ID_UTS] = "uts",
	[LSNS_ID_IPC] = "ipc",
	[LSNS_ID_USER] = "user",
	[LSNS_ID_CGROUP] = "cgroup"
};

struct lsns_namespace {
	ino_t id;
	int type;			/* LSNS_* */
	int nprocs;

	struct lsns_process *proc;

	struct list_head namespaces;	/* lsns->processes member */
	struct list_head processes;	/* head of lsns_process *siblings */
};

struct lsns_process {
	pid_t pid;		/* process PID */
	pid_t ppid;		/* parent's PID */
	pid_t tpid;		/* thread group */
	char state;
	uid_t uid;

	ino_t            ns_ids[ARRAY_SIZE(ns_names)];
	struct list_head ns_siblings[ARRAY_SIZE(ns_names)];

	struct list_head processes;	/* list of processes */

	struct libscols_line *outline;
	struct lsns_process *parent;
};

struct lsns {
	struct list_head processes;
	struct list_head namespaces;

	pid_t	fltr_pid;	/* filter out by PID */
	ino_t	fltr_ns;	/* filter out by namespace */
	int	fltr_types[ARRAY_SIZE(ns_names)];
	int	fltr_ntypes;

	unsigned int raw	: 1,
		     json	: 1,
		     tree	: 1,
		     list	: 1,
		     notrunc	: 1,
		     no_headings: 1;
};

static void lsns_init_debug(void)
{
	__UL_INIT_DEBUG(lsns, LSNS_DEBUG_, 0, LSNS_DEBUG);
}

static int ns_name2type(const char *name)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(ns_names); i++) {
		if (strcmp(ns_names[i], name) == 0)
			return i;
	}
	return -1;
}

static int column_name_to_id(const char *name, size_t namesz)
{
	size_t i;

	assert(name);

	for (i = 0; i < ARRAY_SIZE(infos); i++) {
		const char *cn = infos[i].name;

		if (!strncasecmp(name, cn, namesz) && !*(cn + namesz))
			return i;
	}
	warnx(_("unknown column: %s"), name);
	return -1;
}

static inline int get_column_id(int num)
{
	assert(num >= 0);
	assert((size_t) num < ncolumns);
	assert(columns[num] < (int) ARRAY_SIZE(infos));

	return columns[num];
}

static inline const struct colinfo *get_column_info(unsigned num)
{
	return &infos[ get_column_id(num) ];
}

static int get_ns_ino(int dir, const char *nsname, ino_t *ino)
{
	struct stat st;
	char path[16];

	snprintf(path, sizeof(path), "ns/%s", nsname);

	if (fstatat(dir, path, &st, 0) != 0)
		return -errno;
	*ino = st.st_ino;
	return 0;
}

static int parse_proc_stat(FILE *fp, pid_t *pid, char *state, pid_t *ppid)
{
	char *line = NULL, *p;
	size_t len = 0;
	int rc;

	if (getline(&line, &len, fp) < 0) {
		rc = -errno;
		goto error;
	}

	p = strrchr(line, ')');
	if (p == NULL ||
	    sscanf(line, "%d (", pid) != 1 ||
	    sscanf(p, ") %c %d*[^\n]", state, ppid) != 2) {
		rc = -EINVAL;
		goto error;
	}
	rc = 0;

error:
	free(line);
	return rc;
}

static int read_process(struct lsns *ls, pid_t pid)
{
	struct lsns_process *p = NULL;
	char buf[BUFSIZ];
	DIR *dir;
	int rc = 0, fd;
	FILE *f = NULL;
	size_t i;
	struct stat st;

	DBG(PROC, ul_debug("reading %d", (int) pid));

	snprintf(buf, sizeof(buf), "/proc/%d", pid);
	dir = opendir(buf);
	if (!dir)
		return -errno;

	p = xcalloc(1, sizeof(*p));
	if (!p) {
		rc = -ENOMEM;
		goto done;
	}

	if (fstat(dirfd(dir), &st) == 0) {
		p->uid = st.st_uid;
		add_uid(uid_cache, st.st_uid);
	}

	fd = openat(dirfd(dir), "stat", O_RDONLY);
	if (fd < 0) {
		rc = -errno;
		goto done;
	}
	if (!(f = fdopen(fd, "r"))) {
		rc = -errno;
		goto done;
	}
	rc = parse_proc_stat(f, &p->pid, &p->state, &p->ppid);
	if (rc < 0)
		goto done;
	rc = 0;

	for (i = 0; i < ARRAY_SIZE(p->ns_ids); i++) {
		INIT_LIST_HEAD(&p->ns_siblings[i]);

		if (!ls->fltr_types[i])
			continue;

		rc = get_ns_ino(dirfd(dir), ns_names[i], &p->ns_ids[i]);
		if (rc && rc != -EACCES && rc != -ENOENT)
			goto done;
		rc = 0;
	}

	INIT_LIST_HEAD(&p->processes);

	DBG(PROC, ul_debugobj(p, "new pid=%d", p->pid));
	list_add_tail(&p->processes, &ls->processes);
done:
	if (f)
		fclose(f);
	closedir(dir);
	if (rc)
		free(p);
	return rc;
}

static int read_processes(struct lsns *ls)
{
	struct proc_processes *proc = NULL;
	pid_t pid;
	int rc = 0;

	DBG(PROC, ul_debug("opening /proc"));

	if (!(proc = proc_open_processes())) {
		rc = -errno;
		goto done;
	}

	while (proc_next_pid(proc, &pid) == 0) {
		rc = read_process(ls, pid);
		if (rc && rc != -EACCES && rc != -ENOENT)
			break;
		rc = 0;
	}
done:
	DBG(PROC, ul_debug("closing /proc"));
	proc_close_processes(proc);
	return rc;
}

static struct lsns_namespace *get_namespace(struct lsns *ls, ino_t ino)
{
	struct list_head *p;

	list_for_each(p, &ls->namespaces) {
		struct lsns_namespace *ns = list_entry(p, struct lsns_namespace, namespaces);

		if (ns->id == ino)
			return ns;
	}
	return NULL;
}

static int namespace_has_process(struct lsns_namespace *ns, pid_t pid)
{
	struct list_head *p;

	list_for_each(p, &ns->processes) {
		struct lsns_process *proc = list_entry(p, struct lsns_process, ns_siblings[ns->type]);

		if (proc->pid == pid)
			return 1;
	}
	return 0;
}

static struct lsns_namespace *add_namespace(struct lsns *ls, int type, ino_t ino)
{
	struct lsns_namespace *ns = xcalloc(1, sizeof(*ns));

	if (!ns)
		return NULL;

	DBG(NS, ul_debugobj(ns, "new %s[%ju]", ns_names[type], (uintmax_t)ino));

	INIT_LIST_HEAD(&ns->processes);
	INIT_LIST_HEAD(&ns->namespaces);

	ns->type = type;
	ns->id = ino;

	list_add_tail(&ns->namespaces, &ls->namespaces);
	return ns;
}

static int add_process_to_namespace(struct lsns *ls, struct lsns_namespace *ns, struct lsns_process *proc)
{
	struct list_head *p;

	DBG(NS, ul_debugobj(ns, "add process [%p] pid=%d to %s[%ju]",
		proc, proc->pid, ns_names[ns->type], (uintmax_t)ns->id));

	list_for_each(p, &ls->processes) {
		struct lsns_process *xproc = list_entry(p, struct lsns_process, processes);

		if (xproc->pid == proc->ppid)		/* my parent */
			proc->parent = xproc;
		else if (xproc->ppid == proc->pid)	/* my child */
			xproc->parent = proc;
	}

	list_add_tail(&proc->ns_siblings[ns->type], &ns->processes);
	ns->nprocs++;

	if (!ns->proc || ns->proc->pid > proc->pid)
		ns->proc = proc;

	return 0;
}

static int cmp_namespaces(struct list_head *a, struct list_head *b,
			  __attribute__((__unused__)) void *data)
{
	struct lsns_namespace *xa = list_entry(a, struct lsns_namespace, namespaces),
			      *xb = list_entry(b, struct lsns_namespace, namespaces);

	return cmp_numbers(xa->id, xb->id);
}

static int read_namespaces(struct lsns *ls)
{
	struct list_head *p;

	DBG(NS, ul_debug("reading namespace"));

	list_for_each(p, &ls->processes) {
		size_t i;
		struct lsns_namespace *ns;
		struct lsns_process *proc = list_entry(p, struct lsns_process, processes);

		for (i = 0; i < ARRAY_SIZE(proc->ns_ids); i++) {
			if (proc->ns_ids[i] == 0)
				continue;
			if (!(ns = get_namespace(ls, proc->ns_ids[i]))) {
				ns = add_namespace(ls, i, proc->ns_ids[i]);
				if (!ns)
					return -ENOMEM;
			}
			add_process_to_namespace(ls, ns, proc);
		}
	}

	list_sort(&ls->namespaces, cmp_namespaces, NULL);

	return 0;
}

static void add_scols_line(struct lsns *ls, struct libscols_table *table,
			   struct lsns_namespace *ns, struct lsns_process *proc)
{
	size_t i;
	struct libscols_line *line;

	assert(ns);
	assert(table);

	line = scols_table_new_line(table,
			ls->tree && proc->parent ? proc->parent->outline : NULL);
	if (!line) {
		warn(_("failed to add line to output"));
		return;
	}

	for (i = 0; i < ncolumns; i++) {
		char *str = NULL;

		switch (get_column_id(i)) {
		case COL_NS:
			xasprintf(&str, "%ju", (uintmax_t)ns->id);
			break;
		case COL_PID:
			xasprintf(&str, "%d", (int) proc->pid);
			break;
		case COL_PPID:
			xasprintf(&str, "%d", (int) proc->ppid);
			break;
		case COL_TYPE:
			xasprintf(&str, "%s", ns_names[ns->type]);
			break;
		case COL_NPROCS:
			xasprintf(&str, "%d", ns->nprocs);
			break;
		case COL_COMMAND:
			str = proc_get_command(proc->pid);
			if (!str)
				str = proc_get_command_name(proc->pid);
			break;
		case COL_PATH:
			xasprintf(&str, "/proc/%d/ns/%s", (int) proc->pid, ns_names[ns->type]);
			break;
		case COL_UID:
			xasprintf(&str, "%d", (int) proc->uid);
			break;
		case COL_USER:
			xasprintf(&str, "%s", get_id(uid_cache, proc->uid)->name);
			break;
		default:
			break;
		}

		if (str && scols_line_refer_data(line, i, str) != 0)
			err_oom();
	}

	proc->outline = line;
}

static struct libscols_table *init_scols_table(struct lsns *ls)
{
	struct libscols_table *tab;
	size_t i;

	tab = scols_new_table();
	if (!tab) {
		warn(_("failed to initialize output table"));
		return NULL;
	}

	scols_table_enable_raw(tab, ls->raw);
	scols_table_enable_json(tab, ls->json);
	scols_table_enable_noheadings(tab, ls->no_headings);

	if (ls->json)
		scols_table_set_name(tab, "namespaces");

	for (i = 0; i < ncolumns; i++) {
		const struct colinfo *col = get_column_info(i);
		int flags = col->flags;

		if (ls->notrunc)
		       flags &= ~SCOLS_FL_TRUNC;
		if (ls->tree && get_column_id(i) == COL_COMMAND)
			flags |= SCOLS_FL_TREE;

		if (!scols_table_new_column(tab, col->name, col->whint, flags)) {
			warnx(_("failed to initialize output column"));
			goto err;
		}
	}

	return tab;
err:
	scols_unref_table(tab);
	return NULL;
}

static int show_namespaces(struct lsns *ls)
{
	struct libscols_table *tab;
	struct list_head *p;
	int rc = 0;

	tab = init_scols_table(ls);
	if (!tab)
		return -ENOMEM;

	list_for_each(p, &ls->namespaces) {
		struct lsns_namespace *ns = list_entry(p, struct lsns_namespace, namespaces);

		if (ls->fltr_pid != 0 && !namespace_has_process(ns, ls->fltr_pid))
			continue;

		add_scols_line(ls, tab, ns, ns->proc);
	}

	scols_print_table(tab);
	scols_unref_table(tab);
	return rc;
}

static void show_process(struct lsns *ls, struct libscols_table *tab,
			 struct lsns_process *proc, struct lsns_namespace *ns)
{
	/*
	 * create a tree from parent->child relation, but only if the parent is
	 * within the same namespace
	 */
	if (ls->tree
	    && proc->parent
	    && !proc->parent->outline
	    && proc->parent->ns_ids[ns->type] == proc->ns_ids[ns->type])
		show_process(ls, tab, proc->parent, ns);

	add_scols_line(ls, tab, ns, proc);
}


static int show_namespace_processes(struct lsns *ls, struct lsns_namespace *ns)
{
	struct libscols_table *tab;
	struct list_head *p;

	tab = init_scols_table(ls);
	if (!tab)
		return -ENOMEM;

	list_for_each(p, &ns->processes) {
		struct lsns_process *proc = list_entry(p, struct lsns_process, ns_siblings[ns->type]);

		if (!proc->outline)
			show_process(ls, tab, proc, ns);
	}


	scols_print_table(tab);
	scols_unref_table(tab);
	return 0;
}

static void __attribute__ ((__noreturn__)) usage(FILE * out)
{
	size_t i;

	fputs(USAGE_HEADER, out);

	fprintf(out,
		_(" %s [options] [<namespace>]\n"), program_invocation_short_name);

	fputs(USAGE_SEPARATOR, out);
	fputs(_("List system namespaces.\n"), out);

	fputs(USAGE_OPTIONS, out);
	fputs(_(" -J, --json             use JSON output format\n"), out);
	fputs(_(" -l, --list             use list format output\n"), out);
	fputs(_(" -n, --noheadings       don't print headings\n"), out);
	fputs(_(" -o, --output <list>    define which output columns to use\n"), out);
	fputs(_(" -p, --task <pid>       print process namespaces\n"), out);
	fputs(_(" -r, --raw              use the raw output format\n"), out);
	fputs(_(" -u, --notruncate       don't truncate text in columns\n"), out);
	fputs(_(" -t, --type <name>      namespace type (mnt, net, ipc, user, pid, uts, cgroup)\n"), out);

	fputs(USAGE_SEPARATOR, out);
	fputs(USAGE_HELP, out);
	fputs(USAGE_VERSION, out);

	fputs(_("\nAvailable columns (for --output):\n"), out);

	for (i = 0; i < ARRAY_SIZE(infos); i++)
		fprintf(out, " %11s  %s\n", infos[i].name, _(infos[i].help));

	fprintf(out, USAGE_MAN_TAIL("lsns(8)"));

	exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}


int main(int argc, char *argv[])
{
	struct lsns ls;
	int c;
	int r = 0;
	char *outarg = NULL;
	static const struct option long_opts[] = {
		{ "json",       no_argument,       NULL, 'J' },
		{ "task",       required_argument, NULL, 'p' },
		{ "help",	no_argument,       NULL, 'h' },
		{ "output",     required_argument, NULL, 'o' },
		{ "notruncate", no_argument,       NULL, 'u' },
		{ "version",    no_argument,       NULL, 'V' },
		{ "noheadings", no_argument,       NULL, 'n' },
		{ "list",       no_argument,       NULL, 'l' },
		{ "raw",        no_argument,       NULL, 'r' },
		{ "type",       required_argument, NULL, 't' },
		{ NULL, 0, NULL, 0 }
	};

	static const ul_excl_t excl[] = {	/* rows and cols in ASCII order */
		{ 'J','r' },
		{ 0 }
	};
	int excl_st[ARRAY_SIZE(excl)] = UL_EXCL_STATUS_INIT;

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	lsns_init_debug();
	memset(&ls, 0, sizeof(ls));

	INIT_LIST_HEAD(&ls.processes);
	INIT_LIST_HEAD(&ls.namespaces);

	while ((c = getopt_long(argc, argv,
				"Jlp:o:nruhVt:", long_opts, NULL)) != -1) {

		err_exclusive_options(c, long_opts, excl, excl_st);

		switch(c) {
		case 'J':
			ls.json = 1;
			break;
		case 'l':
			ls.list = 1;
			break;
		case 'o':
			outarg = optarg;
			break;
		case 'V':
			printf(UTIL_LINUX_VERSION);
			return EXIT_SUCCESS;
		case 'p':
			ls.fltr_pid = strtos32_or_err(optarg, _("invalid PID argument"));
			break;
		case 'h':
			usage(stdout);
		case 'n':
			ls.no_headings = 1;
			break;
		case 'r':
			ls.raw = 1;
			break;
		case 'u':
			ls.notrunc = 1;
			break;
		case 't':
		{
			int type = ns_name2type(optarg);
			if (type < 0)
				errx(EXIT_FAILURE, _("unknown namespace type: %s"), optarg);
			ls.fltr_types[type] = 1;
			ls.fltr_ntypes++;
			break;
		}
		default:
			errtryhelp(EXIT_FAILURE);
		}
	}

	if (!ls.fltr_ntypes) {
		size_t i;
		for (i = 0; i < ARRAY_SIZE(ns_names); i++)
			ls.fltr_types[i] = 1;
	}

	if (optind < argc) {
		if (ls.fltr_pid)
			errx(EXIT_FAILURE, _("--task is mutually exclusive with <namespace>"));
		ls.fltr_ns = strtou64_or_err(argv[optind], _("invalid namespace argument"));
		ls.tree = ls.list ? 0 : 1;

		if (!ncolumns) {
			columns[ncolumns++] = COL_PID;
			columns[ncolumns++] = COL_PPID;
			columns[ncolumns++] = COL_USER;
			columns[ncolumns++] = COL_COMMAND;
		}
	}

	if (!ncolumns) {
		columns[ncolumns++] = COL_NS;
		columns[ncolumns++] = COL_TYPE;
		columns[ncolumns++] = COL_NPROCS;
		columns[ncolumns++] = COL_PID;
		columns[ncolumns++] = COL_USER;
		columns[ncolumns++] = COL_COMMAND;
	}

	if (outarg && string_add_to_idarray(outarg, columns, ARRAY_SIZE(columns),
					 &ncolumns, column_name_to_id) < 0)
		return EXIT_FAILURE;

	scols_init_debug(0);

	uid_cache = new_idcache();
	if (!uid_cache)
		err(EXIT_FAILURE, _("failed to allocate UID cache"));

	r = read_processes(&ls);
	if (!r)
		r = read_namespaces(&ls);
	if (!r) {
		if (ls.fltr_ns) {
			struct lsns_namespace *ns = get_namespace(&ls, ls.fltr_ns);

			if (!ns)
				errx(EXIT_FAILURE, _("not found namespace: %ju"), (uintmax_t) ls.fltr_ns);
			r = show_namespace_processes(&ls, ns);
		} else
			r = show_namespaces(&ls);
	}

	free_idcache(uid_cache);
	return r == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
