/*
 * lsmem - Show memory configuration
 *
 * Copyright IBM Corp. 2016
 * Copyright (C) 2016 Karel Zak <kzak@redhat.com>
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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
#include <c.h>
#include <nls.h>
#include <path.h>
#include <strutils.h>
#include <closestream.h>
#include <xalloc.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <fcntl.h>
#include <inttypes.h>
#include <assert.h>
#include <optutils.h>
#include <libsmartcols.h>

#define _PATH_SYS_MEMORY		"/sys/devices/system/memory"
#define _PATH_SYS_MEMORY_BLOCK_SIZE	_PATH_SYS_MEMORY "/block_size_bytes"

#define MEMORY_STATE_ONLINE		0
#define MEMORY_STATE_OFFLINE		1
#define MEMORY_STATE_GOING_OFFLINE	2
#define MEMORY_STATE_UNKNOWN		3

struct memory_block {
	uint64_t	index;
	uint64_t	count;
	int		state;
	int		node;
	unsigned int	removable:1;
};

struct lsmem {
	struct dirent		**dirs;
	int			ndirs;
	struct memory_block	*blocks;
	int			nblocks;
	uint64_t		block_size;
	uint64_t		mem_online;
	uint64_t		mem_offline;

	struct libscols_table	*table;
	unsigned int		have_nodes : 1,
				raw : 1,
				export : 1,
				json : 1,
				noheadings : 1,
				summary : 1,
				list_all : 1,
				bytes : 1,
				want_node : 1,
				want_state : 1,
				want_removable : 1,
				want_summary : 1,
				want_table : 1;
};

enum {
	COL_RANGE,
	COL_SIZE,
	COL_STATE,
	COL_REMOVABLE,
	COL_BLOCK,
	COL_NODE,
};

/* column names */
struct coldesc {
	const char	*name;		/* header */
	double		whint;		/* width hint (N < 1 is in percent of termwidth) */
	int		flags;		/* SCOLS_FL_* */
	const char      *help;

	int	sort_type;		/* SORT_* */
};

/* columns descriptions */
static struct coldesc coldescs[] = {
	[COL_RANGE]	= { "RANGE", 0, 0, N_("start and end address of the memory range")},
	[COL_SIZE]	= { "SIZE", 5, SCOLS_FL_RIGHT, N_("size of the memory range")},
	[COL_STATE]	= { "STATE", 0, SCOLS_FL_RIGHT, N_("online status of the memory range")},
	[COL_REMOVABLE]	= { "REMOVABLE", 0, SCOLS_FL_RIGHT, N_("memory is removable")},
	[COL_BLOCK]	= { "BLOCK", 0, SCOLS_FL_RIGHT, N_("memory block number or blocks range")},
	[COL_NODE]	= { "NODE", 0, SCOLS_FL_RIGHT, N_("numa node of memory")},
};

/* columns[] array specifies all currently wanted output column. The columns
 * are defined by coldescs[] array and you can specify (on command line) each
 * column twice. That's enough, dynamically allocated array of the columns is
 * unnecessary overkill and over-engineering in this case */
static int columns[ARRAY_SIZE(coldescs) * 2];
static size_t ncolumns;

static inline size_t err_columns_index(size_t arysz, size_t idx)
{
	if (idx >= arysz)
		errx(EXIT_FAILURE, _("too many columns specified, "
				     "the limit is %zu columns"),
				arysz - 1);
	return idx;
}

#define add_column(ary, n, id)	\
		((ary)[ err_columns_index(ARRAY_SIZE(ary), (n)) ] = (id))

static int column_name_to_id(const char *name, size_t namesz)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(coldescs); i++) {
		const char *cn = coldescs[i].name;

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
	assert(columns[num] < (int) ARRAY_SIZE(coldescs));

	return columns[num];
}

static inline struct coldesc *get_column_desc(int num)
{
	return &coldescs[ get_column_id(num) ];
}

static inline int has_column(int id)
{
	size_t i;

	for (i = 0; i < ncolumns; i++)
		if (columns[i] == id)
			return 1;
	return 0;
}

static void add_scols_line(struct lsmem *lsmem, struct memory_block *blk)
{
	size_t i;
	struct libscols_line *line;

	line = scols_table_new_line(lsmem->table, NULL);
	if (!line)
		err_oom();

	for (i = 0; i < ncolumns; i++) {
		char *str = NULL;

		switch (get_column_id(i)) {
		case COL_RANGE:
		{
			uint64_t start = blk->index * lsmem->block_size;
			uint64_t size = blk->count * lsmem->block_size;
			xasprintf(&str, "0x%016"PRIx64"-0x%016"PRIx64, start, start + size - 1);
			break;
		}
		case COL_SIZE:
			if (lsmem->bytes)
				xasprintf(&str, "%"PRId64, (uint64_t) blk->count * lsmem->block_size);
			else
				str = size_to_human_string(SIZE_SUFFIX_1LETTER,
						(uint64_t) blk->count * lsmem->block_size);
			break;
		case COL_STATE:
			str = xstrdup(
				blk->state == MEMORY_STATE_ONLINE ? _("online") :
				blk->state == MEMORY_STATE_OFFLINE ? _("offline") :
				blk->state == MEMORY_STATE_GOING_OFFLINE ? _("on->off") :
				"?");
			break;
		case COL_REMOVABLE:
			if (blk->state == MEMORY_STATE_ONLINE)
				str = xstrdup(blk->removable ? _("yes") : _("no"));
			else
				str = xstrdup("-");
			break;
		case COL_BLOCK:
			if (blk->count == 1)
				xasprintf(&str, "%"PRId64, blk->index);
			else
				xasprintf(&str, "%"PRId64"-%"PRId64,
					 blk->index, blk->index + blk->count - 1);
			break;
		case COL_NODE:
			if (lsmem->have_nodes)
				xasprintf(&str, "%d", blk->node);
			else
				str = xstrdup("-");
			break;
		}

		if (str && scols_line_refer_data(line, i, str) != 0)
			err_oom();
	}
}

static void fill_scols_table(struct lsmem *lsmem)
{
	int i;

	for (i = 0; i < lsmem->nblocks; i++)
		add_scols_line(lsmem, &lsmem->blocks[i]);
}

static void print_summary(struct lsmem *lsmem)
{
	if (lsmem->bytes) {
		printf("%-23s %15"PRId64"\n",_("Memory block size:"), lsmem->block_size);
		printf("%-23s %15"PRId64"\n",_("Total online memory:"), lsmem->mem_online);
		printf("%-23s %15"PRId64"\n",_("Total offline memory:"), lsmem->mem_offline);
	} else {
		printf("%-23s %5s\n",_("Memory block size:"),
			size_to_human_string(SIZE_SUFFIX_1LETTER, lsmem->block_size));
		printf("%-23s %5s\n",_("Total online memory:"),
			size_to_human_string(SIZE_SUFFIX_1LETTER, lsmem->mem_online));
		printf("%-23s %5s\n",_("Total offline memory:"),
			size_to_human_string(SIZE_SUFFIX_1LETTER, lsmem->mem_offline));
	}
}

static int memory_block_get_node(char *name)
{
	struct dirent *de;
	char *path;
	DIR *dir;
	int node;

	path = path_strdup(_PATH_SYS_MEMORY"/%s", name);
	dir = opendir(path);
	free(path);
	if (!dir)
		err(EXIT_FAILURE, _("Failed to open %s"), path);
	node = -1;
	while ((de = readdir(dir)) != NULL) {
		if (strncmp("node", de->d_name, 4))
			continue;
		if (!isdigit_string(de->d_name + 4))
			continue;
		node = strtol(de->d_name + 4, NULL, 10);
		break;
	}
	closedir(dir);
	return node;
}

static void memory_block_read_attrs(struct lsmem *lsmem, char *name,
				    struct memory_block *blk)
{
	char line[BUFSIZ];

	blk->count = 1;
	blk->index = strtoumax(name + 6, NULL, 10); /* get <num> of "memory<num>" */
	blk->removable = path_read_u64(_PATH_SYS_MEMORY"/%s/%s", name, "removable");
	blk->state = MEMORY_STATE_UNKNOWN;
	path_read_str(line, sizeof(line), _PATH_SYS_MEMORY"/%s/%s", name, "state");
	if (strcmp(line, "offline") == 0)
		blk->state = MEMORY_STATE_OFFLINE;
	else if (strcmp(line, "online") == 0)
		blk->state = MEMORY_STATE_ONLINE;
	else if (strcmp(line, "going-offline") == 0)
		blk->state = MEMORY_STATE_GOING_OFFLINE;
	if (lsmem->have_nodes)
		blk->node = memory_block_get_node(name);
}

static int is_mergeable(struct lsmem *lsmem, struct memory_block *blk)
{
	struct memory_block *curr;

	if (!lsmem->nblocks)
		return 0;
	curr = &lsmem->blocks[lsmem->nblocks - 1];
	if (lsmem->list_all)
		return 0;
	if (curr->index + curr->count != blk->index)
		return 0;
	if (lsmem->want_state && curr->state != blk->state)
		return 0;
	if (lsmem->want_removable && curr->removable != blk->removable)
		return 0;
	if (lsmem->want_node && lsmem->have_nodes) {
		if (curr->node != blk->node)
			return 0;
	}
	return 1;
}

static void read_info(struct lsmem *lsmem)
{
	struct memory_block blk;
	char line[BUFSIZ];
	int i;

	path_read_str(line, sizeof(line), _PATH_SYS_MEMORY_BLOCK_SIZE);
	lsmem->block_size = strtoumax(line, NULL, 16);

	for (i = 0; i < lsmem->ndirs; i++) {
		memory_block_read_attrs(lsmem, lsmem->dirs[i]->d_name, &blk);
		if (is_mergeable(lsmem, &blk)) {
			lsmem->blocks[lsmem->nblocks - 1].count++;
			continue;
		}
		lsmem->nblocks++;
		lsmem->blocks = xrealloc(lsmem->blocks, lsmem->nblocks * sizeof(blk));
		*&lsmem->blocks[lsmem->nblocks - 1] = blk;
	}
	for (i = 0; i < lsmem->nblocks; i++) {
		if (lsmem->blocks[i].state == MEMORY_STATE_ONLINE)
			lsmem->mem_online += lsmem->block_size * lsmem->blocks[i].count;
		else
			lsmem->mem_offline += lsmem->block_size * lsmem->blocks[i].count;
	}
}

static int memory_block_filter(const struct dirent *de)
{
	if (strncmp("memory", de->d_name, 6))
		return 0;
	return isdigit_string(de->d_name + 6);
}

static void read_basic_info(struct lsmem *lsmem)
{
	char *dir;

	if (!path_exist(_PATH_SYS_MEMORY_BLOCK_SIZE))
		errx(EXIT_FAILURE, _("This system does not support memory blocks"));

	dir = path_strdup(_PATH_SYS_MEMORY);
	lsmem->ndirs = scandir(dir, &lsmem->dirs, memory_block_filter, versionsort);
	free(dir);
	if (lsmem->ndirs <= 0)
		err(EXIT_FAILURE, _("Failed to read %s"), _PATH_SYS_MEMORY);

	if (memory_block_get_node(lsmem->dirs[0]->d_name) != -1)
		lsmem->have_nodes = 1;
}

static void __attribute__((__noreturn__)) lsmem_usage(FILE *out)
{
	unsigned int i;

	fputs(USAGE_HEADER, out);
	fprintf(out, _(" %s [options]\n"), program_invocation_short_name);

	fputs(USAGE_SEPARATOR, out);
	fputs(_("List the ranges of available memory with their online status.\n"), out);

	fputs(USAGE_OPTIONS, out);
	fputs(_(" -J, --json           use JSON output format\n"), out);
	fputs(_(" -P, --pairs          use key=\"value\" output format\n"), out);
	fputs(_(" -a, --all            list each individual memory block\n"), out);
	fputs(_(" -b, --bytes          print SIZE in bytes rather than in human readable format\n"), out);
	fputs(_(" -n, --noheadings     don't print headings\n"), out);
	fputs(_(" -o, --output <list>  output columns\n"), out);
	fputs(_(" -r, --raw            use raw output format\n"), out);
	fputs(_(" -s, --sysroot <dir>  use the specified directory as system root\n"), out);
	fputs(_("     --summary[=when] print summary information (never,always or only)\n"), out);

	fputs(USAGE_SEPARATOR, out);
	fputs(USAGE_HELP, out);
	fputs(USAGE_VERSION, out);

	fputs(_("\nAvailable columns:\n"), out);

	for (i = 0; i < ARRAY_SIZE(coldescs); i++)
		fprintf(out, " %10s  %s\n", coldescs[i].name, coldescs[i].help);

	fprintf(out, USAGE_MAN_TAIL("lsmem(1)"));

	exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}

int main(int argc, char **argv)
{
	struct lsmem _lsmem = {
			.want_table = 1,
			.want_summary = 1
		}, *lsmem = &_lsmem;

	const char *outarg = NULL;
	int c;
	size_t i;

	enum {
		LSMEM_OPT_SUMARRY = CHAR_MAX + 1
	};

	static const struct option longopts[] = {
		{"all",		no_argument,		NULL, 'a'},
		{"bytes",	no_argument,		NULL, 'b'},
		{"help",	no_argument,		NULL, 'h'},
		{"json",	no_argument,		NULL, 'J'},
		{"noheadings",	no_argument,		NULL, 'n'},
		{"output",	required_argument,	NULL, 'o'},
		{"pairs",	no_argument,		NULL, 'P'},
		{"raw",		no_argument,		NULL, 'r'},
		{"sysroot",	required_argument,	NULL, 's'},
		{"version",	no_argument,		NULL, 'V'},
		{"summary",     optional_argument,	NULL, LSMEM_OPT_SUMARRY },
		{NULL,		0,			NULL, 0}
	};
	static const ul_excl_t excl[] = {	/* rows and cols in ASCII order */
		{ 'J', 'P', 'r' },
		{ 0 }
	};
	int excl_st[ARRAY_SIZE(excl)] = UL_EXCL_STATUS_INIT;

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	while ((c = getopt_long(argc, argv, "abhJno:Prs:V", longopts, NULL)) != -1) {

		err_exclusive_options(c, longopts, excl, excl_st);

		switch (c) {
		case 'a':
			lsmem->list_all = 1;
			break;
		case 'b':
			lsmem->bytes = 1;
			break;
		case 'h':
			lsmem_usage(stdout);
			break;
		case 'J':
			lsmem->json = 1;
			lsmem->want_summary = 0;
			break;
		case 'n':
			lsmem->noheadings = 1;
			break;
		case 'o':
			outarg = optarg;
			break;
		case 'P':
			lsmem->export = 1;
			lsmem->want_summary = 0;
			break;
		case 'r':
			lsmem->raw = 1;
			lsmem->want_summary = 0;
			break;
		case 's':
			path_set_prefix(optarg);
			break;
		case 'V':
			printf(UTIL_LINUX_VERSION);
			return 0;
		case LSMEM_OPT_SUMARRY:
			if (optarg) {
				if (strcmp(optarg, "never") == 0)
					lsmem->want_summary = 0;
				else if (strcmp(optarg, "only") == 0)
					lsmem->want_table = 0;
				else if (strcmp(optarg, "always") == 0)
					lsmem->want_summary = 1;
				else
					errx(EXIT_FAILURE, _("unsupported --summary argument"));
			} else
				lsmem->want_table = 0;
			break;
		default:
			errtryhelp(EXIT_FAILURE);
		}
	}

	if (argc != optind)
		lsmem_usage(stderr);

	if (lsmem->want_table + lsmem->want_summary == 0)
		errx(EXIT_FAILURE, _("options --{raw,json,pairs} and --summary=only are mutually exclusive"));

	/* Shortcut to avoid scols machinery on --summary=only */
	if (lsmem->want_table == 0 && lsmem->want_summary) {
		read_basic_info(lsmem);
		read_info(lsmem);
		print_summary(lsmem);
		return EXIT_SUCCESS;
	}

	/*
	 * Default columns
	 */
	if (!ncolumns) {
		add_column(columns, ncolumns++, COL_RANGE);
		add_column(columns, ncolumns++, COL_SIZE);
		add_column(columns, ncolumns++, COL_STATE);
		add_column(columns, ncolumns++, COL_REMOVABLE);
		add_column(columns, ncolumns++, COL_BLOCK);
	}

	if (outarg && string_add_to_idarray(outarg, columns, ARRAY_SIZE(columns),
					 &ncolumns, column_name_to_id) < 0)
		return EXIT_FAILURE;

	/*
	 * Initialize output
	 */
	scols_init_debug(0);

	if (!(lsmem->table = scols_new_table()))
		errx(EXIT_FAILURE, _("failed to initialize output table"));
	scols_table_enable_raw(lsmem->table, lsmem->raw);
	scols_table_enable_export(lsmem->table, lsmem->export);
	scols_table_enable_json(lsmem->table, lsmem->json);
	scols_table_enable_noheadings(lsmem->table, lsmem->noheadings);

	if (lsmem->json)
		scols_table_set_name(lsmem->table, "memory");

	for (i = 0; i < ncolumns; i++) {
		struct coldesc *ci = get_column_desc(i);
		if (!scols_table_new_column(lsmem->table, ci->name, ci->whint, ci->flags))
			err(EXIT_FAILURE, _("Failed to initialize output column"));
	}

	if (has_column(COL_STATE))
		lsmem->want_state = 1;
	if (has_column(COL_NODE))
		lsmem->want_node = 1;
	if (has_column(COL_REMOVABLE))
		lsmem->want_removable = 1;

	/*
	 * Read data and print output
	 */
	read_basic_info(lsmem);
	read_info(lsmem);

	if (lsmem->want_table) {
		fill_scols_table(lsmem);
		scols_print_table(lsmem->table);

		if (lsmem->want_summary)
			fputc('\n', stdout);
	}

	if (lsmem->want_summary)
		print_summary(lsmem);

	scols_unref_table(lsmem->table);
	return 0;
}
