/*
 * zramctl - control compressed block devices in RAM
 *
 * Copyright (c) 2014 Timofey Titovets <Nefelim4ag@gmail.com>
 * Copyright (C) 2014 Karel Zak <kzak@redhat.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
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

#include <getopt.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <assert.h>

#include <libsmartcols.h>

#include "c.h"
#include "nls.h"
#include "closestream.h"
#include "strutils.h"
#include "xalloc.h"
#include "sysfs.h"
#include "optutils.h"
#include "ismounted.h"

/*#define CONFIG_ZRAM_DEBUG*/

#ifdef CONFIG_ZRAM_DEBUG
# define DBG(x)	 do { fputs("zram: ", stderr); x; fputc('\n', stderr); } while(0)
#else
# define DBG(x)
#endif

/* status output columns */
struct colinfo {
	const char *name;
	double whint;
	int flags;
	const char *help;
};

enum {
	COL_NAME = 0,
	COL_DISKSIZE,
	COL_ORIG_SIZE,
	COL_COMP_SIZE,
	COL_ALGORITHM,
	COL_STREAMS,
	COL_ZEROPAGES,
	COL_MEMTOTAL,
	COL_MOUNTPOINT
};

static const struct colinfo infos[] = {
	[COL_NAME]      = { "NAME",      0.25, 0, N_("zram device name") },
	[COL_DISKSIZE]  = { "DISKSIZE",     5, SCOLS_FL_RIGHT, N_("limit on the uncompressed amount of data") },
	[COL_ORIG_SIZE] = { "DATA",         5, SCOLS_FL_RIGHT, N_("uncompressed size of stored data") },
	[COL_COMP_SIZE] = { "COMPR",        5, SCOLS_FL_RIGHT, N_("compressed size of stored data") },
	[COL_ALGORITHM] = { "ALGORITHM",    3, 0, N_("the selected compression algorithm") },
	[COL_STREAMS]   = { "STREAMS",      3, SCOLS_FL_RIGHT, N_("number of concurrent compress operations") },
	[COL_ZEROPAGES] = { "ZERO-PAGES",   3, SCOLS_FL_RIGHT, N_("empty pages with no allocated memory") },
	[COL_MEMTOTAL]  = { "TOTAL",        5, SCOLS_FL_RIGHT, N_("all memory including allocator fragmentation and metadata overhead") },
	[COL_MOUNTPOINT]= { "MOUNTPOINT",0.10, SCOLS_FL_TRUNC, N_("where the device is mounted") },
};

static int columns[ARRAY_SIZE(infos) * 2] = {-1};
static int ncolumns;

struct zram {
	char	devname[32];
	struct sysfs_cxt sysfs;
};

#define ZRAM_EMPTY	{ .devname = { '\0' }, .sysfs = UL_SYSFSCXT_EMPTY }

static unsigned int raw, no_headings, inbytes;


static int get_column_id(int num)
{
	assert(num < ncolumns);
	assert(columns[num] < (int) ARRAY_SIZE(infos));
	return columns[num];
}

static const struct colinfo *get_column_info(int num)
{
	return &infos[ get_column_id(num) ];
}

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

static void zram_set_devname(struct zram *z, const char *devname, size_t n)
{
	assert(z);

	if (!devname)
		snprintf(z->devname, sizeof(z->devname), "/dev/zram%zu", n);
	else {
		strncpy(z->devname, devname, sizeof(z->devname));
		z->devname[sizeof(z->devname) - 1] = '\0';
	}

	DBG(fprintf(stderr, "set devname: %s", z->devname));
	sysfs_deinit(&z->sysfs);
}

static struct zram *new_zram(const char *devname)
{
	struct zram *z = xcalloc(1, sizeof(struct zram));

	DBG(fprintf(stderr, "new: %p", z));
	if (devname)
		zram_set_devname(z, devname, 0);
	return z;
}

static void free_zram(struct zram *z)
{
	if (!z)
		return;
	DBG(fprintf(stderr, "free: %p", z));
	sysfs_deinit(&z->sysfs);
	free(z);
}

static struct sysfs_cxt *zram_get_sysfs(struct zram *z)
{
	assert(z);

	if (!z->sysfs.devno) {
		dev_t devno = sysfs_devname_to_devno(z->devname, NULL);
		if (!devno)
			return NULL;
		if (sysfs_init(&z->sysfs, devno, NULL))
			return NULL;
		if (*z->devname != '/') {
			/* cannonicalize the device name according to /sys */
			char name[PATH_MAX];
			if (sysfs_get_devname(&z->sysfs, name, sizeof(name)))
				snprintf(z->devname, sizeof(z->devname), "/dev/%s", name);
		}
	}

	return &z->sysfs;
}

static inline int zram_exist(struct zram *z)
{
	assert(z);

	errno = 0;
	if (zram_get_sysfs(z) == NULL) {
		errno = ENODEV;
		return 0;
	}

	DBG(fprintf(stderr, "%s exists", z->devname));
	return 1;
}

static int zram_set_u64parm(struct zram *z, const char *attr, uint64_t num)
{
	struct sysfs_cxt *sysfs = zram_get_sysfs(z);
	if (!sysfs)
		return -EINVAL;
	DBG(fprintf(stderr, "%s writing %ju to %s", z->devname, num, attr));
	return sysfs_write_u64(sysfs, attr, num);
}

static int zram_set_strparm(struct zram *z, const char *attr, const char *str)
{
	struct sysfs_cxt *sysfs = zram_get_sysfs(z);
	if (!sysfs)
		return -EINVAL;
	DBG(fprintf(stderr, "%s writing %s to %s", z->devname, str, attr));
	return sysfs_write_string(sysfs, attr, str);
}


static int zram_used(struct zram *z)
{
	uint64_t size;
	struct sysfs_cxt *sysfs = zram_get_sysfs(z);

	if (sysfs &&
	    sysfs_read_u64(sysfs, "disksize", &size) == 0 &&
	    size > 0) {

		DBG(fprintf(stderr, "%s used", z->devname));
		return 1;
	}
	DBG(fprintf(stderr, "%s unused", z->devname));
	return 0;
}

static struct zram *find_free_zram(void)
{
	struct zram *z = new_zram(NULL);
	size_t i;
	int isfree = 0;

	for (i = 0; isfree == 0; i++) {
		DBG(fprintf(stderr, "find free: checking zram%zu", i));
		zram_set_devname(z, NULL, i);
		if (!zram_exist(z))
			break;
		isfree = !zram_used(z);
	}
	if (!isfree) {
		free_zram(z);
		z = NULL;
	}
	return z;
}

static void fill_table_row(struct libscols_table *tb, struct zram *z)
{
	static struct libscols_line *ln;
	struct sysfs_cxt *sysfs;
	size_t i;
	uint64_t num;

	assert(tb);
	assert(z);

	DBG(fprintf(stderr, "%s: filling status table", z->devname));

	sysfs = zram_get_sysfs(z);
	if (!sysfs)
		return;

	ln = scols_table_new_line(tb, NULL);
	if (!ln)
		err(EXIT_FAILURE, _("failed to initialize output line"));

	for (i = 0; i < (size_t) ncolumns; i++) {
		char *str = NULL;

		switch (get_column_id(i)) {
		case COL_NAME:
			str = xstrdup(z->devname);
			break;
		case COL_DISKSIZE:
			if (inbytes)
				str = sysfs_strdup(sysfs, "disksize");
			else if (sysfs_read_u64(sysfs, "disksize", &num) == 0)
				str = size_to_human_string(SIZE_SUFFIX_1LETTER, num);
			break;
		case COL_ORIG_SIZE:
			if (inbytes)
				str = sysfs_strdup(sysfs, "orig_data_size");
			else if (sysfs_read_u64(sysfs, "orig_data_size", &num) == 0)
				str = size_to_human_string(SIZE_SUFFIX_1LETTER, num);
			break;
		case COL_COMP_SIZE:
			if (inbytes)
				str = sysfs_strdup(sysfs, "compr_data_size");
			else if (sysfs_read_u64(sysfs, "compr_data_size", &num) == 0)
				str = size_to_human_string(SIZE_SUFFIX_1LETTER, num);
			break;
		case COL_ALGORITHM:
		{
			char *alg = sysfs_strdup(sysfs, "comp_algorithm");
			if (!alg)
				break;
			if (strstr(alg, "[lzo]") == NULL) {
				if (strstr(alg, "[lz4]") == NULL)
					;
				else
					str = xstrdup("lz4");
			} else
				str = xstrdup("lzo");
			free(alg);
			break;
		}
		case COL_MOUNTPOINT:
		{
			char path[PATH_MAX] = { '\0' };
			int fl;

			check_mount_point(z->devname, &fl, path, sizeof(path));
			if (*path)
				str = xstrdup(path);
			break;
		}
		case COL_STREAMS:
			str = sysfs_strdup(sysfs, "max_comp_streams");
			break;
		case COL_ZEROPAGES:
			str = sysfs_strdup(sysfs, "zero_pages");
			break;
		case COL_MEMTOTAL:
			if (inbytes)
				str = sysfs_strdup(sysfs, "mem_used_total");
			else if (sysfs_read_u64(sysfs, "mem_used_total", &num) == 0)
				str = size_to_human_string(SIZE_SUFFIX_1LETTER, num);
			break;
		}

		if (str)
			scols_line_refer_data(ln, i, str);
	}
}

static void status(struct zram *z)
{
	struct libscols_table *tb;
	size_t i;

	scols_init_debug(0);

	tb = scols_new_table();
	if (!tb)
		err(EXIT_FAILURE, _("failed to initialize output table"));

	scols_table_enable_raw(tb, raw);
	scols_table_enable_noheadings(tb, no_headings);

	for (i = 0; i < (size_t) ncolumns; i++) {
		const struct colinfo *col = get_column_info(i);

		if (!scols_table_new_column(tb, col->name, col->whint, col->flags))
			err(EXIT_FAILURE, _("failed to initialize output column"));
	}

	if (z)
		fill_table_row(tb, z);		/* just one device specified */
	else {
		/* list all used devices */
		z = new_zram(NULL);

		for (i = 0; ; i++) {
			zram_set_devname(z, NULL, i);
			if (!zram_exist(z))
				break;
			if (zram_used(z))
				fill_table_row(tb, z);
		}
		free_zram(z);
	}

	scols_print_table(tb);
	scols_unref_table(tb);
}

static void __attribute__ ((__noreturn__)) usage(FILE * out)
{
	size_t i;

	fputs(USAGE_HEADER, out);
	fprintf(out, _(	" %1$s [options] <device>\n"
			" %1$s -r <device> [...]\n"
			" %1$s [options] -f | <device> -s <size>\n"),
			program_invocation_short_name);

	fputs(USAGE_SEPARATOR, out);
	fputs(_("Set up and control zram devices.\n"), out);

	fputs(USAGE_OPTIONS, out);
	fputs(_(" -a, --algorithm lzo|lz4   compression algorithm to use\n"), out);
	fputs(_(" -b, --bytes               print sizes in bytes rather than in human readable format\n"), out);
	fputs(_(" -f, --find                find a free device\n"), out);
	fputs(_(" -n, --noheadings          don't print headings\n"), out);
	fputs(_(" -o, --output <list>       columns to use for status output\n"), out);
	fputs(_("     --raw                 use raw status output format\n"), out);
	fputs(_(" -r, --reset               reset all specified devices\n"), out);
	fputs(_(" -s, --size <size>         device size\n"), out);
	fputs(_(" -t, --streams <number>    number of compression streams\n"), out);

	fputs(USAGE_SEPARATOR, out);
	fputs(USAGE_HELP, out);
	fputs(USAGE_VERSION, out);

	fputs(_("\nAvailable columns (for --output):\n"), out);
	for (i = 0; i < ARRAY_SIZE(infos); i++)
		fprintf(out, " %11s  %s\n", infos[i].name, _(infos[i].help));

	fprintf(out, USAGE_MAN_TAIL("zramctl(8)"));
	exit(out == stderr ? 1 : EXIT_SUCCESS);
}

/* actions */
enum {
	A_NONE = 0,
	A_STATUS,
	A_CREATE,
	A_FINDONLY,
	A_RESET
};

int main(int argc, char **argv)
{
	uintmax_t size = 0, nstreams = 0;
	char *algorithm = NULL;
	int rc = 0, c, find = 0, act = A_NONE;
	struct zram *zram = NULL;

	enum { OPT_RAW = CHAR_MAX + 1 };

	static const struct option longopts[] = {
		{ "algorithm", required_argument, NULL, 'a' },
		{ "bytes",     no_argument, NULL, 'b' },
		{ "find",      no_argument, NULL, 'f' },
		{ "help",      no_argument, NULL, 'h' },
		{ "output",    required_argument, NULL, 'o' },
		{ "noheadings",no_argument, NULL, 'n' },
		{ "reset",     no_argument, NULL, 'r' },
		{ "raw",       no_argument, NULL, OPT_RAW },
		{ "size",      required_argument, NULL, 's' },
		{ "streams",   required_argument, NULL, 't' },
		{ "version",   no_argument, NULL, 'V' },
		{ NULL, 0, NULL, 0 }
	};

	static const ul_excl_t excl[] = {
		{ 'f', 'o', 'r' },
		{ 'o', 'r', 's' },
		{ 0 }
	};
	int excl_st[ARRAY_SIZE(excl)] = UL_EXCL_STATUS_INIT;

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	while ((c = getopt_long(argc, argv, "a:bfho:nrs:t:V", longopts, NULL)) != -1) {

		err_exclusive_options(c, longopts, excl, excl_st);

		switch (c) {
		case 'a':
			if (strcmp(optarg,"lzo") && strcmp(optarg,"lz4"))
				errx(EXIT_FAILURE, _("unsupported algorithm: %s"),
					     optarg);
			algorithm = optarg;
			break;
		case 'b':
			inbytes = 1;
			break;
		case 'f':
			find = 1;
			break;
		case 'o':
			ncolumns = string_to_idarray(optarg,
						     columns, ARRAY_SIZE(columns),
						     column_name_to_id);
			if (ncolumns < 0)
				return EXIT_FAILURE;
			break;
		case 's':
			size = strtosize_or_err(optarg, _("failed to parse size"));
			act = A_CREATE;
			break;
		case 't':
			nstreams = strtou64_or_err(optarg, _("failed to parse streams"));
			break;
		case 'r':
			act = A_RESET;
			break;
		case OPT_RAW:
			raw = 1;
			break;
		case 'n':
			no_headings = 1;
			break;
		case 'V':
			printf(UTIL_LINUX_VERSION);
			return EXIT_SUCCESS;
		case 'h':
			usage(stdout);
		default:
			usage(stderr);
		}
	}

	if (find && optind < argc)
		errx(EXIT_FAILURE, _("option --find is mutually exclusive "
				     "with <device>"));
	if (act == A_NONE)
		act = find ? A_FINDONLY : A_STATUS;

	if (act != A_RESET && optind + 1 < argc)
		errx(EXIT_FAILURE, _("only one <device> at a time is allowed"));

	if ((act == A_STATUS || act == A_FINDONLY) && (algorithm || nstreams))
		errx(EXIT_FAILURE, _("options --algorithm and --streams "
				     "must be combined with --size"));

	switch (act) {
	case A_STATUS:
		if (!ncolumns) {		/* default columns */
			columns[ncolumns++] = COL_NAME;
			columns[ncolumns++] = COL_ALGORITHM;
			columns[ncolumns++] = COL_DISKSIZE;
			columns[ncolumns++] = COL_ORIG_SIZE;
			columns[ncolumns++] = COL_COMP_SIZE;
			columns[ncolumns++] = COL_MEMTOTAL;
			columns[ncolumns++] = COL_STREAMS;
			columns[ncolumns++] = COL_MOUNTPOINT;
		}
		if (optind < argc) {
			zram = new_zram(argv[optind++]);
			if (!zram_exist(zram))
				err(EXIT_FAILURE, "%s", zram->devname);
		}
		status(zram);
		free_zram(zram);
		break;
	case A_RESET:
		if (optind == argc)
			errx(EXIT_FAILURE, _("no device specified"));
		while (optind < argc) {
			zram = new_zram(argv[optind]);
			if (!zram_exist(zram)
			    || zram_set_u64parm(zram, "reset", 1)) {
				warn(_("%s: failed to reset"), zram->devname);
				rc = 1;
			}
			free_zram(zram);
			optind++;
		}
		break;
	case A_FINDONLY:
		zram = find_free_zram();
		if (!zram)
			errx(EXIT_FAILURE, _("no free zram device found"));
		printf("%s\n", zram->devname);
		free_zram(zram);
		break;
	case A_CREATE:
		if (find) {
			zram = find_free_zram();
			if (!zram)
				errx(EXIT_FAILURE, _("no free zram device found"));
		} else if (optind == argc)
			errx(EXIT_FAILURE, _("no device specified"));
		else {
			zram = new_zram(argv[optind]);
			if (!zram_exist(zram))
				err(EXIT_FAILURE, "%s", zram->devname);
		}

		if (zram_set_u64parm(zram, "reset", 1))
			err(EXIT_FAILURE, _("%s: failed to reset"), zram->devname);

		if (nstreams &&
		    zram_set_u64parm(zram, "max_comp_streams", nstreams))
			err(EXIT_FAILURE, _("%s: failed to set number of streams"), zram->devname);

		if (algorithm &&
		    zram_set_strparm(zram, "comp_algorithm", algorithm))
			err(EXIT_FAILURE, _("%s: failed to set algorithm"), zram->devname);

		if (zram_set_u64parm(zram, "disksize", size))
			err(EXIT_FAILURE, _("%s: failed to set disksize (%ju bytes)"),
				zram->devname, size);
		if (find)
			printf("%s\n", zram->devname);
		free_zram(zram);
		break;
	}

	return rc ? EXIT_FAILURE : EXIT_SUCCESS;
}
