/*
 * lslocks(8) - list local system locks
 *
 * Copyright (C) 2012 Davidlohr Bueso <dave@gnu.org>
 *
 * Very generally based on lslk(8) by Victor A. Abell <abe@purdue.edu>
 * Since it stopped being maintained over a decade ago, this
 * program should be considered its replacement.
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

#include <libmount.h>
#include <libsmartcols.h>

#include "pathnames.h"
#include "canonicalize.h"
#include "nls.h"
#include "xalloc.h"
#include "strutils.h"
#include "c.h"
#include "list.h"
#include "closestream.h"
#include "optutils.h"
#include "procutils.h"

/* column IDs */
enum {
	COL_SRC = 0,
	COL_PID,
	COL_TYPE,
	COL_SIZE,
	COL_MODE,
	COL_M,
	COL_START,
	COL_END,
	COL_PATH,
	COL_BLOCKER
};

/* column names */
struct colinfo {
	const char *name; /* header */
	double	   whint; /* width hint (N < 1 is in percent of termwidth) */
	int	   flags; /* SCOLS_FL_* */
	const char *help;
};

/* columns descriptions */
static struct colinfo infos[] = {
	[COL_SRC]  = { "COMMAND",15, 0, N_("command of the process holding the lock") },
	[COL_PID]  = { "PID",     5, SCOLS_FL_RIGHT, N_("PID of the process holding the lock") },
	[COL_TYPE] = { "TYPE",    5, SCOLS_FL_RIGHT, N_("kind of lock") },
	[COL_SIZE] = { "SIZE",    4, SCOLS_FL_RIGHT, N_("size of the lock") },
	[COL_MODE] = { "MODE",    5, 0, N_("lock access mode") },
	[COL_M]    = { "M",       1, 0, N_("mandatory state of the lock: 0 (none), 1 (set)")},
	[COL_START] = { "START", 10, SCOLS_FL_RIGHT, N_("relative byte offset of the lock")},
	[COL_END]  = { "END",    10, SCOLS_FL_RIGHT, N_("ending offset of the lock")},
	[COL_PATH] = { "PATH",    0, SCOLS_FL_TRUNC, N_("path of the locked file")},
	[COL_BLOCKER] = { "BLOCKER", 0, SCOLS_FL_RIGHT, N_("PID of the process blocking the lock") }
};

static int columns[ARRAY_SIZE(infos) * 2];
static size_t ncolumns;

static pid_t pid = 0;

static struct libmnt_table *tab;		/* /proc/self/mountinfo */

/* basic output flags */
static int no_headings;
static int no_inaccessible;
static int raw;
static int json;
static int bytes;

struct lock {
	struct list_head locks;

	char *cmdname;
	pid_t pid;
	char *path;
	char *type;
	char *mode;
	off_t start;
	off_t end;
	unsigned int mandatory :1,
		     blocked   :1;
	uint64_t size;
	int id;
};

static void rem_lock(struct lock *lock)
{
	if (!lock)
		return;

	free(lock->path);
	free(lock->mode);
	free(lock->cmdname);
	free(lock->type);
	list_del(&lock->locks);
	free(lock);
}

static void disable_columns_truncate(void)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(infos); i++)
		infos[i].flags &= ~SCOLS_FL_TRUNC;
}

/*
 * Associate the device's mountpoint for a filename
 */
static char *get_fallback_filename(dev_t dev)
{
	struct libmnt_fs *fs;
	char *res = NULL;

	if (!tab) {
		tab = mnt_new_table_from_file(_PATH_PROC_MOUNTINFO);
		if (!tab)
			return NULL;
	}

	fs = mnt_table_find_devno(tab, dev, MNT_ITER_BACKWARD);
	if (!fs)
		return NULL;

	xasprintf(&res, "%s...", mnt_fs_get_target(fs));
	return res;
}

/*
 * Return the absolute path of a file from
 * a given inode number (and its size)
 */
static char *get_filename_sz(ino_t inode, pid_t lock_pid, size_t *size)
{
	struct stat sb;
	struct dirent *dp;
	DIR *dirp;
	size_t len;
	int fd;
	char path[PATH_MAX], sym[PATH_MAX], *ret = NULL;

	*size = 0;
	memset(path, 0, sizeof(path));
	memset(sym, 0, sizeof(sym));

	/*
	 * We know the pid so we don't have to
	 * iterate the *entire* filesystem searching
	 * for the damn file.
	 */
	sprintf(path, "/proc/%d/fd/", lock_pid);
	if (!(dirp = opendir(path)))
		return NULL;

	if ((len = strlen(path)) >= (sizeof(path) - 2))
		goto out;

	if ((fd = dirfd(dirp)) < 0 )
		goto out;

	while ((dp = readdir(dirp))) {
		if (!strcmp(dp->d_name, ".") ||
		    !strcmp(dp->d_name, ".."))
			continue;

		/* care only for numerical descriptors */
		if (!strtol(dp->d_name, (char **) NULL, 10))
			continue;

		if (!fstatat(fd, dp->d_name, &sb, 0)
		    && inode != sb.st_ino)
			continue;

		if ((len = readlinkat(fd, dp->d_name, sym, sizeof(sym) - 1)) < 1)
			goto out;

		*size = sb.st_size;
		sym[len] = '\0';

		ret = xstrdup(sym);
		break;
	}
out:
	closedir(dirp);
	return ret;
}

/*
 * Return the inode number from a string
 */
static ino_t get_dev_inode(char *str, dev_t *dev)
{
	unsigned int maj = 0, min = 0;
	ino_t inum = 0;

	sscanf(str, "%02x:%02x:%ju", &maj, &min, &inum);

	*dev = (dev_t) makedev(maj, min);
	return inum;
}

static int get_local_locks(struct list_head *locks)
{
	int i;
	ino_t inode = 0;
	FILE *fp;
	char buf[PATH_MAX], *tok = NULL;
	size_t sz;
	struct lock *l;
	dev_t dev = 0;

	if (!(fp = fopen(_PATH_PROC_LOCKS, "r")))
		return -1;

	while (fgets(buf, sizeof(buf), fp)) {

		l = xcalloc(1, sizeof(*l));
		INIT_LIST_HEAD(&l->locks);

		for (tok = strtok(buf, " "), i = 0; tok;
		     tok = strtok(NULL, " "), i++) {

			/*
			 * /proc/locks has *exactly* 8 "blocks" of text
			 * separated by ' ' - check <kernel>/fs/locks.c
			 */
			switch (i) {
			case 0: /* ID: */
				tok[strlen(tok) - 1] = '\0';
				l->id = strtos32_or_err(tok, _("failed to parse ID"));
				break;
			case 1: /* posix, flock, etc */
				if (strcmp(tok, "->") == 0) {	/* optional field */
					l->blocked = 1;
					i--;
				} else
					l->type = xstrdup(tok);
				break;

			case 2: /* is this a mandatory lock? other values are advisory or noinode */
				l->mandatory = *tok == 'M' ? 1 : 0;
				break;
			case 3: /* lock mode */
				l->mode = xstrdup(tok);
				break;

			case 4: /* PID */
				/*
				 * If user passed a pid we filter it later when adding
				 * to the list, no need to worry now. OFD locks use -1 PID.
				 */
				l->pid = strtos32_or_err(tok, _("failed to parse pid"));
				if (l->pid > 0) {
					l->cmdname = proc_get_command_name(l->pid);
					if (!l->cmdname)
						l->cmdname = xstrdup(_("(unknown)"));
				} else
					l->cmdname = xstrdup(_("(undefined)"));
				break;

			case 5: /* device major:minor and inode number */
				inode = get_dev_inode(tok, &dev);
				break;

			case 6: /* start */
				l->start = !strcmp(tok, "EOF") ? 0 :
					   strtou64_or_err(tok, _("failed to parse start"));
				break;

			case 7: /* end */
				/* replace '\n' character */
				tok[strlen(tok)-1] = '\0';
				l->end = !strcmp(tok, "EOF") ? 0 :
					 strtou64_or_err(tok, _("failed to parse end"));
				break;
			default:
				break;
			}
		}

		l->path = get_filename_sz(inode, l->pid, &sz);

		/* no permissions -- ignore */
		if (!l->path && no_inaccessible) {
			rem_lock(l);
			continue;
		}

		if (!l->path) {
			/* probably no permission to peek into l->pid's path */
			l->path = get_fallback_filename(dev);
			l->size = 0;
		} else
			l->size = sz;

		list_add(&l->locks, locks);
	}

	fclose(fp);
	return 0;
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


static inline struct colinfo *get_column_info(unsigned num)
{
	return &infos[ get_column_id(num) ];
}

static pid_t get_blocker(int id, struct list_head *locks)
{
	struct list_head *p;

	list_for_each(p, locks) {
		struct lock *l = list_entry(p, struct lock, locks);

		if (l->id == id && !l->blocked)
			return l->pid;
	}

	return 0;
}

static void add_scols_line(struct libscols_table *table, struct lock *l, struct list_head *locks)
{
	size_t i;
	struct libscols_line *line;
	/*
	 * Whenever cmdname or filename is NULL it is most
	 * likely  because there's no read permissions
	 * for the specified process.
	 */
	const char *notfnd = "";

	assert(l);
	assert(table);

	line = scols_table_new_line(table, NULL);
	if (!line)
		err(EXIT_FAILURE, _("failed to allocate output line"));

	for (i = 0; i < ncolumns; i++) {
		char *str = NULL;

		switch (get_column_id(i)) {
		case COL_SRC:
			xasprintf(&str, "%s", l->cmdname ? l->cmdname : notfnd);
			break;
		case COL_PID:
			xasprintf(&str, "%d", l->pid);
			break;
		case COL_TYPE:
			xasprintf(&str, "%s", l->type);
			break;
		case COL_SIZE:
			if (!l->size)
				break;
			if (bytes)
				xasprintf(&str, "%ju", l->size);
			else
				str = size_to_human_string(SIZE_SUFFIX_1LETTER, l->size);
			break;
		case COL_MODE:
			xasprintf(&str, "%s%s", l->mode, l->blocked ? "*" : "");
			break;
		case COL_M:
			xasprintf(&str, "%d", l->mandatory ? 1 : 0);
			break;
		case COL_START:
			xasprintf(&str, "%jd", l->start);
			break;
		case COL_END:
			xasprintf(&str, "%jd", l->end);
			break;
		case COL_PATH:
			xasprintf(&str, "%s", l->path ? l->path : notfnd);
			break;
		case COL_BLOCKER:
		{
			pid_t bl = l->blocked && l->id ?
						get_blocker(l->id, locks) : 0;
			if (bl)
				xasprintf(&str, "%d", (int) bl);
		}
		default:
			break;
		}

		if (str && scols_line_refer_data(line, i, str))
			err(EXIT_FAILURE, _("failed to add output data"));
	}
}

static int show_locks(struct list_head *locks)
{
	int rc = 0;
	size_t i;
	struct list_head *p, *pnext;
	struct libscols_table *table;

	table = scols_new_table();
	if (!table)
		err(EXIT_FAILURE, _("failed to allocate output table"));

	scols_table_enable_raw(table, raw);
	scols_table_enable_json(table, json);
	scols_table_enable_noheadings(table, no_headings);

	if (json)
		scols_table_set_name(table, "locks");

	for (i = 0; i < ncolumns; i++) {
		struct libscols_column *cl;
		struct colinfo *col = get_column_info(i);

		cl = scols_table_new_column(table, col->name, col->whint, col->flags);
		if (!cl)
			err(EXIT_FAILURE, _("failed to allocate output column"));

		if (json) {
			int id = get_column_id(i);

			switch (id) {
			case COL_SIZE:
				if (!bytes)
					break;
				/* fallthrough */
			case COL_PID:
			case COL_START:
			case COL_END:
			case COL_BLOCKER:
				scols_column_set_json_type(cl, SCOLS_JSON_NUMBER);
				break;
			case COL_M:
				scols_column_set_json_type(cl, SCOLS_JSON_BOOLEAN);
				break;
			default:
				scols_column_set_json_type(cl, SCOLS_JSON_STRING);
				break;
			}
		}

	}

	/* prepare data for output */
	list_for_each(p, locks) {
		struct lock *l = list_entry(p, struct lock, locks);

		if (pid && pid != l->pid)
			continue;

		add_scols_line(table, l, locks);
	}

	/* destroy the list */
	list_for_each_safe(p, pnext, locks) {
		struct lock *l = list_entry(p, struct lock, locks);
		rem_lock(l);
	}

	scols_print_table(table);
	scols_unref_table(table);
	return rc;
}


static void __attribute__((__noreturn__)) usage(void)
{
	FILE *out = stdout;
	size_t i;

	fputs(USAGE_HEADER, out);

	fprintf(out,
		_(" %s [options]\n"), program_invocation_short_name);

	fputs(USAGE_SEPARATOR, out);
	fputs(_("List local system locks.\n"), out);

	fputs(USAGE_OPTIONS, out);
	fputs(_(" -b, --bytes            print SIZE in bytes rather than in human readable format\n"), out);
	fputs(_(" -J, --json             use JSON output format\n"), out);
	fputs(_(" -i, --noinaccessible   ignore locks without read permissions\n"), out);
	fputs(_(" -n, --noheadings       don't print headings\n"), out);
	fputs(_(" -o, --output <list>    define which output columns to use\n"), out);
	fputs(_("     --output-all       output all columns\n"), out);
	fputs(_(" -p, --pid <pid>        display only locks held by this process\n"), out);
	fputs(_(" -r, --raw              use the raw output format\n"), out);
	fputs(_(" -u, --notruncate       don't truncate text in columns\n"), out);

	fputs(USAGE_SEPARATOR, out);
	printf(USAGE_HELP_OPTIONS(24));

	fputs(USAGE_COLUMNS, out);

	for (i = 0; i < ARRAY_SIZE(infos); i++)
		fprintf(out, " %11s  %s\n", infos[i].name, _(infos[i].help));

	printf(USAGE_MAN_TAIL("lslocks(8)"));

	exit(EXIT_SUCCESS);
}

int main(int argc, char *argv[])
{
	int c, rc = 0;
	struct list_head locks;
	char *outarg = NULL;
	enum {
		OPT_OUTPUT_ALL = CHAR_MAX + 1
	};
	static const struct option long_opts[] = {
		{ "bytes",      no_argument,       NULL, 'b' },
		{ "json",       no_argument,       NULL, 'J' },
		{ "pid",	required_argument, NULL, 'p' },
		{ "help",	no_argument,       NULL, 'h' },
		{ "output",     required_argument, NULL, 'o' },
		{ "output-all",	no_argument,       NULL, OPT_OUTPUT_ALL },
		{ "notruncate", no_argument,       NULL, 'u' },
		{ "version",    no_argument,       NULL, 'V' },
		{ "noheadings", no_argument,       NULL, 'n' },
		{ "raw",        no_argument,       NULL, 'r' },
		{ "noinaccessible", no_argument, NULL, 'i' },
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
	close_stdout_atexit();

	while ((c = getopt_long(argc, argv,
				"biJp:o:nruhV", long_opts, NULL)) != -1) {

		err_exclusive_options(c, long_opts, excl, excl_st);

		switch(c) {
		case 'b':
			bytes = 1;
			break;
		case 'i':
			no_inaccessible = 1;
			break;
		case 'J':
			json = 1;
			break;
		case 'p':
			pid = strtos32_or_err(optarg, _("invalid PID argument"));
			break;
		case 'o':
			outarg = optarg;
			break;
		case OPT_OUTPUT_ALL:
			for (ncolumns = 0; ncolumns < ARRAY_SIZE(infos); ncolumns++)
				columns[ncolumns] = ncolumns;
			break;
		case 'n':
			no_headings = 1;
			break;
		case 'r':
			raw = 1;
			break;
		case 'u':
			disable_columns_truncate();
			break;

		case 'V':
			print_version(EXIT_SUCCESS);
		case 'h':
			usage();
		default:
			errtryhelp(EXIT_FAILURE);
		}
	}

	INIT_LIST_HEAD(&locks);

	if (!ncolumns) {
		/* default columns */
		columns[ncolumns++] = COL_SRC;
		columns[ncolumns++] = COL_PID;
		columns[ncolumns++] = COL_TYPE;
		columns[ncolumns++] = COL_SIZE;
		columns[ncolumns++] = COL_MODE;
		columns[ncolumns++] = COL_M;
		columns[ncolumns++] = COL_START;
		columns[ncolumns++] = COL_END;
		columns[ncolumns++] = COL_PATH;
	}

	if (outarg && string_add_to_idarray(outarg, columns, ARRAY_SIZE(columns),
					 &ncolumns, column_name_to_id) < 0)
		return EXIT_FAILURE;

	scols_init_debug(0);

	rc = get_local_locks(&locks);

	if (!rc && !list_empty(&locks))
		rc = show_locks(&locks);

	mnt_unref_table(tab);
	return rc;
}
