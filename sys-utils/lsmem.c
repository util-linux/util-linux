/*
 * lsmem - Show memory configuration
 *
 * Copyright IBM Corp. 2016
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
#include <strutils.h>
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

enum {
	OUTPUT_READABLE = 0,	/* default */
	OUTPUT_PARSABLE,	/* -p */
};

struct lsmem_desc {
	struct dirent		**dirs;
	int			ndirs;
	struct memory_block	*blocks;
	int			nblocks;
	unsigned int		have_nodes : 1;
	uint64_t		block_size;
	uint64_t		mem_online;
	uint64_t		mem_offline;
};

struct lsmem_modifier {
	int		mode; /* OUTPUT_* */
	unsigned int	compat : 1;
	unsigned int	list_all_blocks : 1;
};

enum {
	COL_RANGE,
	COL_SIZE,
	COL_STATE,
	COL_REMOVABLE,
	COL_BLOCK,
	COL_NODE,
};

struct lsmem_coldesc {
	const int flags;
	const char *name;
	const char *help;
};

static struct lsmem_coldesc coldescs[] = {
	[COL_RANGE]	= { 0,		    "RANGE",	 N_("adress range")},
	[COL_SIZE]	= { SCOLS_FL_RIGHT, "SIZE",	 N_("size of memory")},
	[COL_STATE]	= { 0,		    "STATE",	 N_("state of memory")},
	[COL_REMOVABLE]	= { 0,		    "REMOVABLE", N_("memory is removable")},
	[COL_BLOCK]	= { 0,		    "BLOCK",	 N_("memory block")},
	[COL_NODE]	= { 0,		    "NODE",	 N_("node information")},
};

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

static char *get_cell_header(int col, char *buf, size_t bufsz)
{
	snprintf(buf, bufsz, "%s", coldescs[col].name);
	return buf;
}

static char *get_cell_data(struct lsmem_desc *desc, int idx, int col,
			   struct lsmem_modifier *mod,
			   char *buf, size_t bufsz)
{
	struct memory_block *blk;
	uint64_t start, size;

	blk = &desc->blocks[idx];
	start = blk->index * desc->block_size;
	size = blk->count * desc->block_size;

	switch (col) {
	case COL_RANGE:
		snprintf(buf, bufsz, "0x%016"PRIx64"-0x%016"PRIx64, start, start + size - 1);
		break;
	case COL_SIZE:
		if (mod->mode == OUTPUT_PARSABLE)
			snprintf(buf, bufsz, "%"PRId64, size);
		else
			snprintf(buf, bufsz, "%s", size_to_human_string(SIZE_SUFFIX_1LETTER, size));
		break;
	case COL_STATE:
		if (blk->state == MEMORY_STATE_ONLINE)
			snprintf(buf, bufsz, _("online"));
		else if (blk->state == MEMORY_STATE_OFFLINE)
			snprintf(buf, bufsz, _("offline"));
		else if (blk->state == MEMORY_STATE_GOING_OFFLINE)
			snprintf(buf, bufsz, _("on->off"));
		else /* unknown */
			snprintf(buf, bufsz, "?");
		break;
	case COL_REMOVABLE:
		if (blk->state == MEMORY_STATE_ONLINE)
			snprintf(buf, bufsz, "%s", blk->removable ? _("yes") : _("no"));
		else
			snprintf(buf, bufsz, "-");
		break;
	case COL_BLOCK:
		if (blk->count == 1)
			snprintf(buf, bufsz, "%"PRId64, blk->index);
		else
			snprintf(buf, bufsz, "%"PRId64"-%"PRId64,
				 blk->index, blk->index + blk->count - 1);
		break;
	case COL_NODE:
		if (desc->have_nodes)
			snprintf(buf, bufsz, "%d", blk->node);
		else
			snprintf(buf, bufsz, "-");
		break;
	}
	return buf;
}

static void print_parsable(struct lsmem_desc *desc, int *cols, int ncols,
			   struct lsmem_modifier *mod)
{
	char buf[BUFSIZ], *data;
	int c, i;

	fputs("# ", stdout);
	for (i = 0; i < ncols; i++) {
		data = get_cell_header(cols[i], buf, sizeof(buf));
		if (i > 0)
			fputc(',', stdout);
		fputs(data && *data ? data : "", stdout);
	}
	fputc('\n', stdout);

	for (i = 0; i < desc->nblocks; i++) {
		for (c = 0; c < ncols; c++) {
			if (c > 0)
				putchar(',');
			data = get_cell_data(desc, i, cols[c], mod, buf, sizeof(buf));
			fputs(data && *data ? data : "", stdout);
		}
		fputc('\n', stdout);
	}
}

static void print_readable_table(struct lsmem_desc *desc, int *cols, int ncols,
				 struct lsmem_modifier *mod)
{
	struct libscols_table *table;
	char buf[BUFSIZ], *data;
	int c, i;

	scols_init_debug(0);
	table = scols_new_table();
	if (!table)
		err(EXIT_FAILURE, _("Failed to initialize output table"));
	for (i = 0; i < ncols; i++) {
		data = get_cell_header(cols[i], buf, sizeof(buf));
		if (!scols_table_new_column(table, xstrdup(data), 0, coldescs[i].flags))
			err(EXIT_FAILURE, _("Failed to initialize output column"));
	}
	for (i = 0; i < desc->nblocks; i++) {
		struct libscols_line *line;

		line = scols_table_new_line(table, NULL);
		if (!line)
			err(EXIT_FAILURE, _("Failed to initialize output line"));

		for (c = 0; c < ncols; c++) {
			data = get_cell_data(desc, i, cols[c], mod, buf, sizeof(buf));
			if (!data || !*data)
				data = "-";
			scols_line_set_data(line, c, data);
		}
	}
	scols_print_table(table);
	scols_unref_table(table);
}

static void print_readable(struct lsmem_desc *desc, int *cols, int ncols,
			   struct lsmem_modifier *mod)
{
	print_readable_table(desc, cols, ncols, mod);
	fputc('\n', stdout);
	fprintf(stdout, _("Memory block size   : %8s\n"),
		size_to_human_string(SIZE_SUFFIX_1LETTER, desc->block_size));
	fprintf(stdout, _("Total online memory : %8s\n"),
		size_to_human_string(SIZE_SUFFIX_1LETTER, desc->mem_online));
	fprintf(stdout, _("Total offline memory: %8s\n"),
		size_to_human_string(SIZE_SUFFIX_1LETTER, desc->mem_offline));
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
	}
	closedir(dir);
	return node;
}

static void memory_block_read_attrs(struct lsmem_desc *desc, char *name,
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
	if (desc->have_nodes)
		blk->node = memory_block_get_node(name);
}

static int is_mergeable(struct lsmem_desc *desc, char *enabled,
			struct memory_block *blk,
			struct lsmem_modifier *mod)
{
	struct memory_block *curr;

	if (!desc->nblocks)
		return 0;
	curr = &desc->blocks[desc->nblocks - 1];
	if (mod->list_all_blocks)
		return 0;
	if (curr->index + curr->count != blk->index)
		return 0;
	if (enabled[COL_STATE] && (curr->state != blk->state))
		return 0;
	if (enabled[COL_REMOVABLE] && (curr->removable != blk->removable))
		return 0;
	if (enabled[COL_NODE] && desc->have_nodes) {
		if (curr->node != blk->node)
			return 0;
	}
	return 1;
}

static void read_info(struct lsmem_desc *desc, int *cols, int ncols,
		      struct lsmem_modifier *mod)
{
	char enabled[ARRAY_SIZE(coldescs)];
	struct memory_block blk;
	char line[BUFSIZ];
	int i;

	path_read_str(line, sizeof(line), _PATH_SYS_MEMORY_BLOCK_SIZE);
	desc->block_size = strtoumax(line, NULL, 16);

	memset(enabled, 0, sizeof(enabled));
	for (i = 0; i < ncols; i++)
		enabled[cols[i]] = 1;
	for (i = 0; i < desc->ndirs; i++) {
		memory_block_read_attrs(desc, desc->dirs[i]->d_name, &blk);
		if (is_mergeable(desc, enabled, &blk, mod)) {
			desc->blocks[desc->nblocks - 1].count++;
			continue;
		}
		desc->nblocks++;
		desc->blocks = xrealloc(desc->blocks, desc->nblocks * sizeof(blk));
		*&desc->blocks[desc->nblocks - 1] = blk;
	}
	for (i = 0; i < desc->nblocks; i++) {
		if (desc->blocks[i].state == MEMORY_STATE_ONLINE)
			desc->mem_online += desc->block_size * desc->blocks[i].count;
		else
			desc->mem_offline += desc->block_size * desc->blocks[i].count;
	}
}

static int memory_block_filter(const struct dirent *de)
{
	if (strncmp("memory", de->d_name, 6))
		return 0;
	return isdigit_string(de->d_name + 6);
}

static void read_basic_info(struct lsmem_desc *desc)
{
	char *dir;

	if (!path_exist(_PATH_SYS_MEMORY_BLOCK_SIZE))
		errx(EXIT_FAILURE, _("This system does not support memory blocks"));

	dir = path_strdup(_PATH_SYS_MEMORY);
	desc->ndirs = scandir(dir, &desc->dirs, memory_block_filter, versionsort);
	free(dir);
	if (desc->ndirs <= 0)
		err(EXIT_FAILURE, _("Failed to read %s"), _PATH_SYS_MEMORY);

	if (memory_block_get_node(desc->dirs[0]->d_name) != -1)
		desc->have_nodes = 1;
}

static void __attribute__((__noreturn__)) lsmem_usage(FILE *out)
{
	unsigned int i;

	fputs(USAGE_HEADER, out);
	fprintf(out, _(" %s [options]\n"), program_invocation_short_name);

	fputs(USAGE_SEPARATOR, out);
	fputs(_("List the ranges of available memory with their online status.\n"), out);

	fputs(USAGE_OPTIONS, out);
	fputs(_(" -a, --all               list each individiual memory block\n"), out);
	fputs(_(" -e, --extended[=<list>] print customized output in a readable format\n"), out);
	fputs(_(" -p, --parse[=<list>]    print customized output in a parsable format\n"), out);
	fputs(_(" -s, --sysroot <dir>     use the specified directory as system root\n"), out);
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
	struct lsmem_modifier _mod = { .mode = OUTPUT_READABLE, .compat = 1 }, *mod = &_mod;
	struct lsmem_desc _desc = { }, *desc = &_desc;
	int columns[ARRAY_SIZE(coldescs)], ncolumns = 0;
	int c;

	static const struct option longopts[] = {
		{"all",		no_argument,		NULL, 'a'},
		{"extended",	optional_argument,	NULL, 'e'},
		{"help",	no_argument,		NULL, 'h'},
		{"parse",	optional_argument,	NULL, 'p'},
		{"sysroot",	required_argument,	NULL, 's'},
		{"version",	no_argument,		NULL, 'V'},
		{NULL,		0,			NULL, 0}
	};
	static const ul_excl_t excl[] = {	/* rows and cols in ASCII order */
		{ 'e','p' },
		{ 0 }
	};
	int excl_st[ARRAY_SIZE(excl)] = UL_EXCL_STATUS_INIT;

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	while ((c = getopt_long(argc, argv, "ae::hp::s:V", longopts, NULL)) != -1) {

		err_exclusive_options(c, longopts, excl, excl_st);

		switch (c) {
		case 'a':
			mod->list_all_blocks = 1;
			break;
		case 'h':
			lsmem_usage(stdout);
			break;
		case 'p':
		case 'e':
			if (optarg) {
				if (*optarg == '=')
					optarg++;
				ncolumns = string_to_idarray(optarg,
							     columns, ARRAY_SIZE(columns),
							     column_name_to_id);
				if (ncolumns < 0)
					return EXIT_FAILURE;
			}
			mod->mode = c == 'p' ? OUTPUT_PARSABLE : OUTPUT_READABLE;
			mod->compat = 0;
			break;
		case 's':
			path_set_prefix(optarg);
			break;
		case 'V':
			printf(UTIL_LINUX_VERSION);
			return 0;
		default:
			lsmem_usage(stderr);
		}
	}

	if (argc != optind)
		lsmem_usage(stderr);

	read_basic_info(desc);

	if (!ncolumns) {
		/* No list was given. Print the following lines by default */
		columns[ncolumns++] = COL_RANGE;
		columns[ncolumns++] = COL_SIZE;
		columns[ncolumns++] = COL_STATE;
		columns[ncolumns++] = COL_REMOVABLE;
		columns[ncolumns++] = COL_BLOCK;
		if (!mod->compat) {
			/* Print everything else what is there if the
			 * extended or parsable mode has been specified */
			if (desc->have_nodes)
				columns[ncolumns++] = COL_NODE;
		}
	}

	read_info(desc, columns, ncolumns, mod);

	switch (mod->mode) {
	case OUTPUT_READABLE:
		print_readable(desc, columns, ncolumns, mod);
		break;
	case OUTPUT_PARSABLE:
		print_parsable(desc, columns, ncolumns, mod);
		break;
	}
	return 0;
}
