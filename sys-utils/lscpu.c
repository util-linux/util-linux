/*
 * lscpu - CPU architecture information helper
 *
 * Copyright (C) 2008 Cai Qian <qcai@redhat.com>
 * Copyright (C) 2008 Karel Zak <kzak@redhat.com>
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

#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>
#include <unistd.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/personality.h>

#include <libsmartcols.h>

#include "closestream.h"
#include "optutils.h"

#include "lscpu.h"
#include "lscpu-api.h"

static const char *virt_types[] = {
	[VIRT_TYPE_NONE]	= N_("none"),
	[VIRT_TYPE_PARA]	= N_("para"),
	[VIRT_TYPE_FULL]	= N_("full"),
	[VIRT_TYPE_CONTAINER]	= N_("container"),
};

static const char *hv_vendors[] = {
	[VIRT_VENDOR_NONE]	= NULL,
	[VIRT_VENDOR_XEN]	= "Xen",
	[VIRT_VENDOR_KVM]	= "KVM",
	[VIRT_VENDOR_MSHV]	= "Microsoft",
	[VIRT_VENDOR_VMWARE]  = "VMware",
	[VIRT_VENDOR_IBM]	= "IBM",
	[VIRT_VENDOR_VSERVER]	= "Linux-VServer",
	[VIRT_VENDOR_UML]	= "User-mode Linux",
	[VIRT_VENDOR_INNOTEK]	= "Innotek GmbH",
	[VIRT_VENDOR_HITACHI]	= "Hitachi",
	[VIRT_VENDOR_PARALLELS] = "Parallels",
	[VIRT_VENDOR_VBOX]	= "Oracle",
	[VIRT_VENDOR_OS400]	= "OS/400",
	[VIRT_VENDOR_PHYP]	= "pHyp",
	[VIRT_VENDOR_SPAR]	= "Unisys s-Par",
	[VIRT_VENDOR_WSL]	= "Windows Subsystem for Linux"
};

/* dispatching modes */
static const char *disp_modes[] = {
	[DISP_HORIZONTAL]	= N_("horizontal"),
	[DISP_VERTICAL]		= N_("vertical")
};

struct polarization_modes {
	char *parsable;
	char *readable;
};

static struct polarization_modes polar_modes[] = {
	[POLAR_UNKNOWN]	   = {"U",  "-"},
	[POLAR_VLOW]	   = {"VL", "vert-low"},
	[POLAR_VMEDIUM]	   = {"VM", "vert-medium"},
	[POLAR_VHIGH]	   = {"VH", "vert-high"},
	[POLAR_HORIZONTAL] = {"H",  "horizontal"},
};

/*
 * IDs
 */
enum {
	COL_CPU_CPU,
	COL_CPU_CORE,
	COL_CPU_SOCKET,
	COL_CPU_NODE,
	COL_CPU_BOOK,
	COL_CPU_DRAWER,
	COL_CPU_CACHE,
	COL_CPU_POLARIZATION,
	COL_CPU_ADDRESS,
	COL_CPU_CONFIGURED,
	COL_CPU_ONLINE,
	COL_CPU_MAXMHZ,
	COL_CPU_MINMHZ,
};

enum {
	COL_CACHE_ALLSIZE,
	COL_CACHE_LEVEL,
	COL_CACHE_NAME,
	COL_CACHE_ONESIZE,
	COL_CACHE_TYPE,
	COL_CACHE_WAYS,
	COL_CACHE_ALLOCPOL,
	COL_CACHE_WRITEPOL,
	COL_CACHE_PHYLINE,
	COL_CACHE_SETS,
	COL_CACHE_COHERENCYSIZE
};


/* column description
 */
struct lscpu_coldesc {
	const char *name;
	const char *help;

	int flags;
	unsigned int  is_abbr:1;	/* name is abbreviation */
};

static struct lscpu_coldesc coldescs_cpu[] =
{
	[COL_CPU_CPU]          = { "CPU", N_("logical CPU number"), SCOLS_FL_RIGHT, 1 },
	[COL_CPU_CORE]         = { "CORE", N_("logical core number"), SCOLS_FL_RIGHT },
	[COL_CPU_SOCKET]       = { "SOCKET", N_("logical socket number"), SCOLS_FL_RIGHT },
	[COL_CPU_NODE]         = { "NODE", N_("logical NUMA node number"), SCOLS_FL_RIGHT },
	[COL_CPU_BOOK]         = { "BOOK", N_("logical book number"), SCOLS_FL_RIGHT },
	[COL_CPU_DRAWER]       = { "DRAWER", N_("logical drawer number"), SCOLS_FL_RIGHT },
	[COL_CPU_CACHE]        = { "CACHE", N_("shows how caches are shared between CPUs") },
	[COL_CPU_POLARIZATION] = { "POLARIZATION", N_("CPU dispatching mode on virtual hardware") },
	[COL_CPU_ADDRESS]      = { "ADDRESS", N_("physical address of a CPU") },
	[COL_CPU_CONFIGURED]   = { "CONFIGURED", N_("shows if the hypervisor has allocated the CPU") },
	[COL_CPU_ONLINE]       = { "ONLINE", N_("shows if Linux currently makes use of the CPU"), SCOLS_FL_RIGHT },
	[COL_CPU_MAXMHZ]       = { "MAXMHZ", N_("shows the maximum MHz of the CPU"), SCOLS_FL_RIGHT },
	[COL_CPU_MINMHZ]       = { "MINMHZ", N_("shows the minimum MHz of the CPU"), SCOLS_FL_RIGHT }
};

static struct lscpu_coldesc coldescs_cache[] =
{
	[COL_CACHE_ALLSIZE]    = { "ALL-SIZE", N_("size of all system caches"), SCOLS_FL_RIGHT },
	[COL_CACHE_LEVEL]      = { "LEVEL", N_("cache level"), SCOLS_FL_RIGHT },
	[COL_CACHE_NAME]       = { "NAME", N_("cache name") },
	[COL_CACHE_ONESIZE]    = { "ONE-SIZE", N_("size of one cache"), SCOLS_FL_RIGHT },
	[COL_CACHE_TYPE]       = { "TYPE", N_("cache type") },
	[COL_CACHE_WAYS]       = { "WAYS", N_("ways of associativity"), SCOLS_FL_RIGHT },
	[COL_CACHE_ALLOCPOL]   = { "ALLOC-POLICY", N_("allocation policy") },
	[COL_CACHE_WRITEPOL]   = { "WRITE-POLICY", N_("write policy") },
	[COL_CACHE_PHYLINE]    = { "PHY-LINE", N_("number of physical cache line per cache t"), SCOLS_FL_RIGHT },
	[COL_CACHE_SETS]       = { "SETS", N_("number of sets in the cache; set lines has the same cache index"), SCOLS_FL_RIGHT },
	[COL_CACHE_COHERENCYSIZE] = { "COHERENCY-SIZE", N_("minimum amount of data in bytes transferred from memory to cache"), SCOLS_FL_RIGHT }
};

static int
cpu_column_name_to_id(const char *name, size_t namesz)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(coldescs_cpu); i++) {
		const char *cn = coldescs_cpu[i].name;

		if (!strncasecmp(name, cn, namesz) && !*(cn + namesz))
			return i;
	}
	warnx(_("unknown column: %s"), name);
	return -1;
}

static int
cache_column_name_to_id(const char *name, size_t namesz)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(coldescs_cache); i++) {
		const char *cn = coldescs_cache[i].name;

		if (!strncasecmp(name, cn, namesz) && !*(cn + namesz))
			return i;
	}
	warnx(_("unknown column: %s"), name);
	return -1;
}

#ifdef LSCPU_OLD_OUTPUT_CODE	/* temporary disabled for revrite */

static char *
get_cell_data(struct lscpu_desc *desc, int idx, int col,
	      struct lscpu_modifier *mod,
	      char *buf, size_t bufsz)
{
	size_t setsize = CPU_ALLOC_SIZE(maxcpus);
	size_t i;
	int cpu = real_cpu_num(desc, idx);

	*buf = '\0';

	switch (col) {
	case COL_CPU_CPU:
		snprintf(buf, bufsz, "%d", cpu);
		break;
	case COL_CPU_CORE:
		if (mod->physical) {
			if (desc->coreids[idx] == -1)
				snprintf(buf, bufsz, "-");
			else
				snprintf(buf, bufsz, "%d", desc->coreids[idx]);
		} else {
			if (cpuset_ary_isset(cpu, desc->coremaps,
					     desc->ncores, setsize, &i) == 0)
				snprintf(buf, bufsz, "%zu", i);
		}
		break;
	case COL_CPU_SOCKET:
		if (mod->physical) {
			if (desc->socketids[idx] ==  -1)
				snprintf(buf, bufsz, "-");
			else
				snprintf(buf, bufsz, "%d", desc->socketids[idx]);
		} else {
			if (cpuset_ary_isset(cpu, desc->socketmaps,
					     desc->nsockets, setsize, &i) == 0)
				snprintf(buf, bufsz, "%zu", i);
		}
		break;
	case COL_CPU_NODE:
		if (cpuset_ary_isset(cpu, desc->nodemaps,
				     desc->nnodes, setsize, &i) == 0)
			snprintf(buf, bufsz, "%d", desc->idx2nodenum[i]);
		break;
	case COL_CPU_DRAWER:
		if (!desc->drawerids || !desc->drawermaps)
			break;
		if (mod->physical) {
			if (desc->drawerids[idx] == -1)
				snprintf(buf, bufsz, "-");
			else
				snprintf(buf, bufsz, "%d", desc->drawerids[idx]);
		} else {
			if (cpuset_ary_isset(cpu, desc->drawermaps,
					     desc->ndrawers, setsize, &i) == 0)
				snprintf(buf, bufsz, "%zu", i);
		}
		break;
	case COL_CPU_BOOK:
		if (!desc->bookids || !desc->bookmaps)
			break;
		if (mod->physical) {
			if (desc->bookids[idx] == -1)
				snprintf(buf, bufsz, "-");
			else
				snprintf(buf, bufsz, "%d", desc->bookids[idx]);
		} else {
			if (cpuset_ary_isset(cpu, desc->bookmaps,
					     desc->nbooks, setsize, &i) == 0)
				snprintf(buf, bufsz, "%zu", i);
		}
		break;
	case COL_CPU_CACHE:
	{
		char *p = buf;
		size_t sz = bufsz;
		int j;

		for (j = desc->ncaches - 1; j >= 0; j--) {
			struct cpu_cache *ca = &desc->caches[j];

			if (cpuset_ary_isset(cpu, ca->sharedmaps,
					     ca->nsharedmaps, setsize, &i) == 0) {
				int x = snprintf(p, sz, "%zu", i);
				if (x < 0 || (size_t) x >= sz)
					return NULL;
				p += x;
				sz -= x;
			}
			if (j != 0) {
				if (sz < 2)
					return NULL;
				*p++ = mod->compat ? ',' : ':';
				*p = '\0';
				sz--;
			}
		}
		break;
	}
	case COL_CPU_POLARIZATION:
		if (desc->polarization) {
			int x = desc->polarization[idx];

			snprintf(buf, bufsz, "%s",
				 mod->mode == OUTPUT_PARSABLE ?
						polar_modes[x].parsable :
						polar_modes[x].readable);
		}
		break;
	case COL_CPU_ADDRESS:
		if (desc->addresses)
			snprintf(buf, bufsz, "%d", desc->addresses[idx]);
		break;
	case COL_CPU_CONFIGURED:
		if (!desc->configured)
			break;
		if (mod->mode == OUTPUT_PARSABLE)
			snprintf(buf, bufsz, "%s",
				 desc->configured[idx] ? _("Y") : _("N"));
		else
			snprintf(buf, bufsz, "%s",
				 desc->configured[idx] ? _("yes") : _("no"));
		break;
	case COL_CPU_ONLINE:
		if (!desc->online)
			break;
		if (mod->mode == OUTPUT_PARSABLE)
			snprintf(buf, bufsz, "%s",
				 is_cpu_online(desc, cpu) ? _("Y") : _("N"));
		else
			snprintf(buf, bufsz, "%s",
				 is_cpu_online(desc, cpu) ? _("yes") : _("no"));
		break;
	case COL_CPU_MAXMHZ:
		if (desc->maxmhz && desc->maxmhz[idx])
			xstrncpy(buf, desc->maxmhz[idx], bufsz);
		break;
	case COL_CPU_MINMHZ:
		if (desc->minmhz && desc->minmhz[idx])
			xstrncpy(buf, desc->minmhz[idx], bufsz);
		break;
	}
	return buf;
}

static char *
get_cell_header(struct lscpu_desc *desc, int col,
		struct lscpu_modifier *mod,
	        char *buf, size_t bufsz)
{
	*buf = '\0';

	if (col == COL_CPU_CACHE) {
		char *p = buf;
		size_t sz = bufsz;
		int i;

		for (i = desc->ncaches - 1; i >= 0; i--) {
			int x = snprintf(p, sz, "%s", desc->caches[i].name);
			if (x < 0 || (size_t) x >= sz)
				return NULL;
			sz -= x;
			p += x;
			if (i > 0) {
				if (sz < 2)
					return NULL;
				*p++ = mod->compat ? ',' : ':';
				*p = '\0';
				sz--;
			}
		}
		if (desc->ncaches)
			return buf;
	}
	snprintf(buf, bufsz, "%s", coldescs_cpu[col].name);
	return buf;
}

/*
 * [-C] backend
 */
static void
print_caches_readable(struct lscpu_desc *desc, int cols[], int ncols,
	       struct lscpu_modifier *mod)
{
	struct libscols_table *table;
	struct cpu_cache *cachesrc;
	int i, end, j, shared_allsize;

	scols_init_debug(0);

	table = scols_new_table();
	if (!table)
		 err(EXIT_FAILURE, _("failed to allocate output table"));
	if (mod->json) {
		scols_table_enable_json(table, 1);
		scols_table_set_name(table, "caches");
	}

	for (i = 0; i < ncols; i++) {
		struct lscpu_coldesc *cd = &coldescs_cache[cols[i]];
		if (!scols_table_new_column(table, cd->name, 0, cd->flags))
			err(EXIT_FAILURE, _("failed to allocate output column"));
	}

	for (j = 0; j < 2; j++) {
		/* First check the caches from /sys/devices */
		if (j == 0) {
			cachesrc = desc->caches;
			end = desc->ncaches - 1;
			shared_allsize = 0;
		} else {
			/* Check shared caches from /proc/cpuinfo s390 */
			cachesrc = desc->ecaches;
			end = desc->necaches - 1;
			/* Dont use get_cache_full_size */
			shared_allsize = 1;
		}

		for (i = end; i >= 0; i--) {
			struct libscols_line *line;
			struct cpu_cache *ca = &cachesrc[i];
			int c;

			line = scols_table_new_line(table, NULL);
			if (!line)
				err(EXIT_FAILURE, _("failed to allocate output line"));

			for (c = 0; c < ncols; c++) {
				char *data = NULL;

				switch (cols[c]) {
				case COL_CACHE_NAME:
					if (ca->name)
						data = xstrdup(ca->name);
					break;
				case COL_CACHE_ONESIZE:
					if (!ca->size)
						break;
					if (mod->bytes)
						xasprintf(&data, "%" PRIu64, ca->size);
					else
						data = size_to_human_string(SIZE_SUFFIX_1LETTER, ca->size);
					break;
				case COL_CACHE_ALLSIZE:
				{
					uint64_t sz = 0;
					if (shared_allsize)
						break;
					if (get_cache_full_size(desc, ca, &sz) != 0)
						break;
					if (mod->bytes)
						xasprintf(&data, "%" PRIu64, sz);
					else
						data = size_to_human_string(SIZE_SUFFIX_1LETTER, sz);
					break;
				}
				case COL_CACHE_WAYS:
					if (ca->ways_of_associativity)
						xasprintf(&data, "%u", ca->ways_of_associativity);
					break;
				case COL_CACHE_TYPE:
					if (ca->type)
						data = xstrdup(ca->type);
					break;
				case COL_CACHE_LEVEL:
					if (ca->level)
						xasprintf(&data, "%d", ca->level);
					break;
				case COL_CACHE_ALLOCPOL:
					if (ca->allocation_policy)
						data = xstrdup(ca->allocation_policy);
					break;
				case COL_CACHE_WRITEPOL:
					if (ca->write_policy)
						data = xstrdup(ca->write_policy);
					break;
				case COL_CACHE_PHYLINE:
					if (ca->physical_line_partition)
						xasprintf(&data, "%u", ca->physical_line_partition);
					break;
				case COL_CACHE_SETS:
					if (ca->number_of_sets)
						xasprintf(&data, "%u", ca->number_of_sets);
					break;
				case COL_CACHE_COHERENCYSIZE:
					if (ca->coherency_line_size)
						xasprintf(&data, "%u", ca->coherency_line_size);
					break;
				}

				if (data && scols_line_refer_data(line, c, data))
					err(EXIT_FAILURE, _("failed to add output data"));
			}
		}

	}

	scols_print_table(table);
	scols_unref_table(table);
}

/*
 * [-p] backend, we support two parsable formats:
 *
 * 1) "compatible" -- this format is compatible with the original lscpu(1)
 * output and it contains fixed set of the columns. The CACHE columns are at
 * the end of the line and the CACHE is not printed if the number of the caches
 * is zero. The CACHE columns are separated by two commas, for example:
 *
 *    $ lscpu --parse
 *    # CPU,Core,Socket,Node,,L1d,L1i,L2
 *    0,0,0,0,,0,0,0
 *    1,1,0,0,,1,1,0
 *
 * 2) "user defined output" -- this format prints always all columns without
 * special prefix for CACHE column. If there are not CACHEs then the column is
 * empty and the header "Cache" is printed rather than a real name of the cache.
 * The CACHE columns are separated by ':'.
 *
 *	$ lscpu --parse=CPU,CORE,SOCKET,NODE,CACHE
 *	# CPU,Core,Socket,Node,L1d:L1i:L2
 *	0,0,0,0,0:0:0
 *	1,1,0,0,1:1:0
 */
static void
print_cpus_parsable(struct lscpu_desc *desc, int cols[], int ncols,
	       struct lscpu_modifier *mod)
{
	char buf[BUFSIZ], *data;
	int i;

	/*
	 * Header
	 */
	printf(_(
	"# The following is the parsable format, which can be fed to other\n"
	"# programs. Each different item in every column has an unique ID\n"
	"# starting from zero.\n"));

	fputs("# ", stdout);
	for (i = 0; i < ncols; i++) {
		int col = cols[i];

		if (col == COL_CPU_CACHE) {
			if (mod->compat && !desc->ncaches)
				continue;
			if (mod->compat && i != 0)
				putchar(',');
		}
		if (i > 0)
			putchar(',');

		data = get_cell_header(desc, col, mod, buf, sizeof(buf));

		if (data && * data && col != COL_CPU_CACHE &&
		    !coldescs_cpu[col].is_abbr) {
			/*
			 * For normal column names use mixed case (e.g. "Socket")
			 */
			char *p = data + 1;

			while (p && *p != '\0') {
				*p = tolower((unsigned int) *p);
				p++;
			}
		}
		fputs(data && *data ? data : "", stdout);
	}
	putchar('\n');

	/*
	 * Data
	 */
	for (i = 0; i < desc->ncpuspos; i++) {
		int c;
		int cpu = real_cpu_num(desc, i);

		if (desc->online) {
			if (!mod->offline && !is_cpu_online(desc, cpu))
				continue;
			if (!mod->online && is_cpu_online(desc, cpu))
				continue;
		}
		if (desc->present && !is_cpu_present(desc, cpu))
			continue;
		for (c = 0; c < ncols; c++) {
			if (mod->compat && cols[c] == COL_CPU_CACHE) {
				if (!desc->ncaches)
					continue;
				if (c > 0)
					putchar(',');
			}
			if (c > 0)
				putchar(',');

			data = get_cell_data(desc, i, cols[c], mod,
					     buf, sizeof(buf));
			fputs(data && *data ? data : "", stdout);
			*buf = '\0';
		}
		putchar('\n');
	}
}

/*
 * [-e] backend
 */
static void
print_cpus_readable(struct lscpu_desc *desc, int cols[], int ncols,
	       struct lscpu_modifier *mod)
{
	int i;
	char buf[BUFSIZ];
	const char *data;
	struct libscols_table *table;

	scols_init_debug(0);

	table = scols_new_table();
	if (!table)
		 err(EXIT_FAILURE, _("failed to allocate output table"));
	if (mod->json) {
		scols_table_enable_json(table, 1);
		scols_table_set_name(table, "cpus");
	}

	for (i = 0; i < ncols; i++) {
		data = get_cell_header(desc, cols[i], mod, buf, sizeof(buf));
		if (!scols_table_new_column(table, data, 0, coldescs_cpu[cols[i]].flags))
			err(EXIT_FAILURE, _("failed to allocate output column"));
	}

	for (i = 0; i < desc->ncpuspos; i++) {
		int c;
		struct libscols_line *line;
		int cpu = real_cpu_num(desc, i);

		if (desc->online) {
			if (!mod->offline && !is_cpu_online(desc, cpu))
				continue;
			if (!mod->online && is_cpu_online(desc, cpu))
				continue;
		}
		if (desc->present && !is_cpu_present(desc, cpu))
			continue;

		line = scols_table_new_line(table, NULL);
		if (!line)
			err(EXIT_FAILURE, _("failed to allocate output line"));

		for (c = 0; c < ncols; c++) {
			data = get_cell_data(desc, i, cols[c], mod,
					     buf, sizeof(buf));
			if (!data || !*data)
				data = "-";
			if (scols_line_set_data(line, c, data))
				err(EXIT_FAILURE, _("failed to add output data"));
		}
	}

	scols_print_table(table);
	scols_unref_table(table);
}


static void __attribute__ ((__format__(printf, 3, 4)))
	add_summary_sprint(struct libscols_table *tb,
			const char *txt,
			const char *fmt,
			...)
{
	struct libscols_line *ln = scols_table_new_line(tb, NULL);
	char *data;
	va_list args;

	if (!ln)
		err(EXIT_FAILURE, _("failed to allocate output line"));

	/* description column */
	if (txt && scols_line_set_data(ln, 0, txt))
		err(EXIT_FAILURE, _("failed to add output data"));

	/* data column */
	va_start(args, fmt);
	xvasprintf(&data, fmt, args);
	va_end(args);

	if (data && scols_line_refer_data(ln, 1, data))
		 err(EXIT_FAILURE, _("failed to add output data"));
}

#define add_summary_n(tb, txt, num)	add_summary_sprint(tb, txt, "%d", num)
#define add_summary_s(tb, txt, str)	add_summary_sprint(tb, txt, "%s", str)

static void
print_cpuset(struct libscols_table *tb,
	     const char *key, cpu_set_t *set, int hex)
{
	size_t setsize = CPU_ALLOC_SIZE(maxcpus);
	size_t setbuflen = 7 * maxcpus;
	char setbuf[setbuflen], *p;

	if (hex) {
		p = cpumask_create(setbuf, setbuflen, set, setsize);
		add_summary_s(tb, key, p);
	} else {
		p = cpulist_create(setbuf, setbuflen, set, setsize);
		add_summary_s(tb, key, p);
	}
}

static int get_cache_full_size(struct lscpu_desc *desc,
		struct cpu_cache *ca, uint64_t *res)
{
	size_t setsize = CPU_ALLOC_SIZE(maxcpus);
	int i, nshares = 0;

	/* Count number of CPUs which shares the cache */
	for (i = 0; i < desc->ncpuspos; i++) {
		int cpu = real_cpu_num(desc, i);

		if (desc->present && !is_cpu_present(desc, cpu))
			continue;
		if (CPU_ISSET_S(cpu, setsize, ca->sharedmaps[0]))
			nshares++;
	}

	/* Correction for CPU threads */
	if (desc->nthreads > desc->ncores)
		nshares /= (desc->nthreads / desc->ncores);
	if (nshares < 1)
		nshares = 1;

	*res = (desc->ncores / nshares) * ca->size;
	return 0;
}

/*
 * default output
 */
static void
print_summary(struct lscpu_desc *desc, struct lscpu_modifier *mod)
{
	char buf[BUFSIZ];
	int i = 0;
	size_t setsize = CPU_ALLOC_SIZE(maxcpus);
	struct libscols_table *tb;

	scols_init_debug(0);

	tb = scols_new_table();
	if (!tb)
		err(EXIT_FAILURE, _("failed to allocate output table"));

	scols_table_enable_noheadings(tb, 1);
	if (mod->json) {
		scols_table_enable_json(tb, 1);
		scols_table_set_name(tb, "lscpu");
	}

	if (scols_table_new_column(tb, "field", 0, 0) == NULL ||
	    scols_table_new_column(tb, "data", 0, SCOLS_FL_NOEXTREMES | SCOLS_FL_WRAP) == NULL)
		err(EXIT_FAILURE, _("failed to initialize output column"));

	add_summary_s(tb, _("Architecture:"), desc->arch);
	if (desc->mode) {
		char *p = buf;

		if (desc->mode & MODE_32BIT) {
			strcpy(p, "32-bit, ");
			p += 8;
		}
		if (desc->mode & MODE_64BIT) {
			strcpy(p, "64-bit, ");
			p += 8;
		}
		*(p - 2) = '\0';
		add_summary_s(tb, _("CPU op-mode(s):"), buf);
	}
#if !defined(WORDS_BIGENDIAN)
	add_summary_s(tb, _("Byte Order:"), "Little Endian");
#else
	add_summary_s(tb, _("Byte Order:"), "Big Endian");
#endif

	if (desc->addrsz)
		add_summary_s(tb, _("Address sizes:"), desc->addrsz);

	add_summary_n(tb, _("CPU(s):"), desc->ncpus);

	if (desc->online)
		print_cpuset(tb, mod->hex ? _("On-line CPU(s) mask:") :
					    _("On-line CPU(s) list:"),
				desc->online, mod->hex);

	if (desc->online && CPU_COUNT_S(setsize, desc->online) != desc->ncpus) {
		cpu_set_t *set;

		/* Linux kernel provides cpuset of off-line CPUs that contains
		 * all configured CPUs (see /sys/devices/system/cpu/offline),
		 * but want to print real (present in system) off-line CPUs only.
		 */
		set = cpuset_alloc(maxcpus, NULL, NULL);
		if (!set)
			err(EXIT_FAILURE, _("failed to callocate cpu set"));
		CPU_ZERO_S(setsize, set);
		for (i = 0; i < desc->ncpuspos; i++) {
			int cpu = real_cpu_num(desc, i);
			if (!is_cpu_online(desc, cpu) && is_cpu_present(desc, cpu))
				CPU_SET_S(cpu, setsize, set);
		}
		print_cpuset(tb, mod->hex ? _("Off-line CPU(s) mask:") :
					    _("Off-line CPU(s) list:"),
			     set, mod->hex);
		cpuset_free(set);
	}

	if (desc->nsockets) {
		int threads_per_core, cores_per_socket, sockets_per_book;
		int books_per_drawer, drawers;
		FILE *fd;

		threads_per_core = cores_per_socket = sockets_per_book = 0;
		books_per_drawer = drawers = 0;
		/* s390 detects its cpu topology via /proc/sysinfo, if present.
		 * Using simply the cpu topology masks in sysfs will not give
		 * usable results since everything is virtualized. E.g.
		 * virtual core 0 may have only 1 cpu, but virtual core 2 may
		 * five cpus.
		 * If the cpu topology is not exported (e.g. 2nd level guest)
		 * fall back to old calculation scheme.
		 */
		if ((fd = ul_path_fopen(desc->procfs, "r", "sysinfo"))) {
			int t0, t1;

			while (fd && fgets(buf, sizeof(buf), fd) != NULL) {
				if (sscanf(buf, "CPU Topology SW:%d%d%d%d%d%d",
					   &t0, &t1, &drawers, &books_per_drawer,
					   &sockets_per_book,
					   &cores_per_socket) == 6)
					break;
			}
			if (fd)
				fclose(fd);
		}
		if (desc->mtid)
			threads_per_core = atoi(desc->mtid) + 1;
		add_summary_n(tb, _("Thread(s) per core:"),
			threads_per_core ?: desc->nthreads / desc->ncores);
		add_summary_n(tb, _("Core(s) per socket:"),
			cores_per_socket ?: desc->ncores / desc->nsockets);
		if (desc->nbooks) {
			add_summary_n(tb, _("Socket(s) per book:"),
				sockets_per_book ?: desc->nsockets / desc->nbooks);
			if (desc->ndrawers) {
				add_summary_n(tb, _("Book(s) per drawer:"),
					books_per_drawer ?: desc->nbooks / desc->ndrawers);
				add_summary_n(tb, _("Drawer(s):"), drawers ?: desc->ndrawers);
			} else {
				add_summary_n(tb, _("Book(s):"), books_per_drawer ?: desc->nbooks);
			}
		} else {
			add_summary_n(tb, _("Socket(s):"), sockets_per_book ?: desc->nsockets);
		}
	}
	if (desc->nnodes)
		add_summary_n(tb, _("NUMA node(s):"), desc->nnodes);
	if (desc->vendor)
		add_summary_s(tb, _("Vendor ID:"), desc->vendor);
	if (desc->machinetype)
		add_summary_s(tb, _("Machine type:"), desc->machinetype);
	if (desc->family)
		add_summary_s(tb, _("CPU family:"), desc->family);
	if (desc->model || desc->revision)
		add_summary_s(tb, _("Model:"), desc->revision ? desc->revision : desc->model);
	if (desc->modelname || desc->cpu)
		add_summary_s(tb, _("Model name:"), desc->cpu ? desc->cpu : desc->modelname);
	if (desc->stepping)
		add_summary_s(tb, _("Stepping:"), desc->stepping);
	if (desc->freqboost >= 0)
		add_summary_s(tb, _("Frequency boost:"), desc->freqboost ?
				_("enabled") : _("disabled"));
	if (desc->mhz)
		add_summary_s(tb, _("CPU MHz:"), desc->mhz);
	if (desc->dynamic_mhz)
		add_summary_s(tb, _("CPU dynamic MHz:"), desc->dynamic_mhz);
	if (desc->static_mhz)
		add_summary_s(tb, _("CPU static MHz:"), desc->static_mhz);
	if (desc->maxmhz)
		add_summary_s(tb, _("CPU max MHz:"), cpu_max_mhz(desc, buf, sizeof(buf)));
	if (desc->minmhz)
		add_summary_s(tb, _("CPU min MHz:"), cpu_min_mhz(desc, buf, sizeof(buf)));
	if (desc->bogomips)
		add_summary_s(tb, _("BogoMIPS:"), desc->bogomips);
	if (desc->virtflag) {
		if (!strcmp(desc->virtflag, "svm"))
			add_summary_s(tb, _("Virtualization:"), "AMD-V");
		else if (!strcmp(desc->virtflag, "vmx"))
			add_summary_s(tb, _("Virtualization:"), "VT-x");
	}
	if (desc->hypervisor)
		add_summary_s(tb, _("Hypervisor:"), desc->hypervisor);
	if (desc->hyper) {
		add_summary_s(tb, _("Hypervisor vendor:"), hv_vendors[desc->hyper]);
		add_summary_s(tb, _("Virtualization type:"), _(virt_types[desc->virtype]));
	}
	if (desc->dispatching >= 0)
		add_summary_s(tb, _("Dispatching mode:"), _(disp_modes[desc->dispatching]));
	if (desc->ncaches) {
		for (i = desc->ncaches - 1; i >= 0; i--) {
			uint64_t sz = 0;
			char *tmp;
			struct cpu_cache *ca = &desc->caches[i];

			if (ca->size == 0)
				continue;
			if (get_cache_full_size(desc, ca, &sz) != 0 || sz == 0)
				continue;
			if (mod->bytes)
				xasprintf(&tmp, "%" PRIu64, sz);
			else
				tmp = size_to_human_string(
					SIZE_SUFFIX_3LETTER | SIZE_SUFFIX_SPACE,
					sz);
			snprintf(buf, sizeof(buf), _("%s cache:"), ca->name);
			add_summary_s(tb, buf, tmp);
			free(tmp);
		}
	}
	if (desc->necaches) {
		for (i = desc->necaches - 1; i >= 0; i--) {
			char *tmp;
			struct cpu_cache *ca = &desc->ecaches[i];

			if (ca->size == 0)
				continue;
			if (mod->bytes)
				xasprintf(&tmp, "%" PRIu64, ca->size);
			else
				tmp = size_to_human_string(
					SIZE_SUFFIX_3LETTER | SIZE_SUFFIX_SPACE,
					ca->size);
			snprintf(buf, sizeof(buf), _("%s cache:"), ca->name);
			add_summary_s(tb, buf, tmp);
			free(tmp);
		}
	}

	for (i = 0; i < desc->nnodes; i++) {
		snprintf(buf, sizeof(buf), _("NUMA node%d CPU(s):"), desc->idx2nodenum[i]);
		print_cpuset(tb, buf, desc->nodemaps[i], mod->hex);
	}

	if (desc->physsockets) {
		add_summary_n(tb, _("Physical sockets:"), desc->physsockets);
		add_summary_n(tb, _("Physical chips:"), desc->physchips);
		add_summary_n(tb, _("Physical cores/chip:"), desc->physcoresperchip);
	}

	if (desc->vuls) {
		for (i = 0; i < desc->nvuls; i++) {
			snprintf(buf, sizeof(buf), ("Vulnerability %s:"), desc->vuls[i].name);
			add_summary_s(tb, buf, desc->vuls[i].text);
		}
	}

	if (desc->flags)
		add_summary_s(tb, _("Flags:"), desc->flags);

	scols_print_table(tb);
	scols_unref_table(tb);
}

#endif /* LSCPU_OLD_OUTPUT_CODE */

static void __attribute__((__noreturn__)) usage(void)
{
	FILE *out = stdout;
	size_t i;

	fputs(USAGE_HEADER, out);
	fprintf(out, _(" %s [options]\n"), program_invocation_short_name);

	fputs(USAGE_SEPARATOR, out);
	fputs(_("Display information about the CPU architecture.\n"), out);

	fputs(USAGE_OPTIONS, out);
	fputs(_(" -a, --all               print both online and offline CPUs (default for -e)\n"), out);
	fputs(_(" -b, --online            print online CPUs only (default for -p)\n"), out);
	fputs(_(" -B, --bytes             print sizes in bytes rather than in human readable format\n"), out);
	fputs(_(" -C, --caches[=<list>]   info about caches in extended readable format\n"), out);
	fputs(_(" -c, --offline           print offline CPUs only\n"), out);
	fputs(_(" -J, --json              use JSON for default or extended format\n"), out);
	fputs(_(" -e, --extended[=<list>] print out an extended readable format\n"), out);
	fputs(_(" -p, --parse[=<list>]    print out a parsable format\n"), out);
	fputs(_(" -s, --sysroot <dir>     use specified directory as system root\n"), out);
	fputs(_(" -x, --hex               print hexadecimal masks rather than lists of CPUs\n"), out);
	fputs(_(" -y, --physical          print physical instead of logical IDs\n"), out);
	fputs(_("     --output-all        print all available columns for -e, -p or -C\n"), out);
	fputs(USAGE_SEPARATOR, out);
	printf(USAGE_HELP_OPTIONS(25));

	fputs(_("\nAvailable output columns for -e or -p:\n"), out);
	for (i = 0; i < ARRAY_SIZE(coldescs_cpu); i++)
		fprintf(out, " %13s  %s\n", coldescs_cpu[i].name, _(coldescs_cpu[i].help));

	fputs(_("\nAvailable output columns for -C:\n"), out);
	for (i = 0; i < ARRAY_SIZE(coldescs_cache); i++)
		fprintf(out, " %13s  %s\n", coldescs_cache[i].name, _(coldescs_cache[i].help));

	printf(USAGE_MAN_TAIL("lscpu(1)"));

	exit(EXIT_SUCCESS);
}

int main(int argc, char *argv[])
{
#ifdef LSCPU_OLD_OUTPUT_CODE
	struct lscpu_modifier _mod = { .mode = OUTPUT_SUMMARY }, *mod = &_mod;
	struct lscpu_desc _desc = { .flags = NULL }, *desc = &_desc;
	int c, i, all = 0;
	int columns[ARRAY_SIZE(coldescs_cpu)], ncolumns = 0;
	int cpu_modifier_specified = 0;
	size_t setsize;

	enum {
		OPT_OUTPUT_ALL = CHAR_MAX + 1,
	};
	static const struct option longopts[] = {
		{ "all",        no_argument,       NULL, 'a' },
		{ "online",     no_argument,       NULL, 'b' },
		{ "bytes",      no_argument,       NULL, 'B' },
		{ "caches",     optional_argument, NULL, 'C' },
		{ "offline",    no_argument,       NULL, 'c' },
		{ "help",	no_argument,       NULL, 'h' },
		{ "extended",	optional_argument, NULL, 'e' },
		{ "json",       no_argument,       NULL, 'J' },
		{ "parse",	optional_argument, NULL, 'p' },
		{ "sysroot",	required_argument, NULL, 's' },
		{ "physical",	no_argument,	   NULL, 'y' },
		{ "hex",	no_argument,	   NULL, 'x' },
		{ "version",	no_argument,	   NULL, 'V' },
		{ "output-all",	no_argument,	   NULL, OPT_OUTPUT_ALL },
		{ NULL,		0, NULL, 0 }
	};

	static const ul_excl_t excl[] = {	/* rows and cols in ASCII order */
		{ 'C','e','p' },
		{ 'a','b','c' },
		{ 0 }
	};
	int excl_st[ARRAY_SIZE(excl)] = UL_EXCL_STATUS_INIT;

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	close_stdout_atexit();

	while ((c = getopt_long(argc, argv, "aBbC::ce::hJp::s:xyV", longopts, NULL)) != -1) {

		err_exclusive_options(c, longopts, excl, excl_st);

		switch (c) {
		case 'a':
			mod->online = mod->offline = 1;
			cpu_modifier_specified = 1;
			break;
		case 'B':
			mod->bytes = 1;
			break;
		case 'b':
			mod->online = 1;
			cpu_modifier_specified = 1;
			break;
		case 'c':
			mod->offline = 1;
			cpu_modifier_specified = 1;
			break;
		case 'C':
			if (optarg) {
				if (*optarg == '=')
					optarg++;
				ncolumns = string_to_idarray(optarg,
						columns, ARRAY_SIZE(columns),
						cache_column_name_to_id);
				if (ncolumns < 0)
					return EXIT_FAILURE;
			}
			mod->mode = OUTPUT_CACHES;
			break;
		case 'J':
			mod->json = 1;
			break;
		case 'p':
		case 'e':
			if (optarg) {
				if (*optarg == '=')
					optarg++;
				ncolumns = string_to_idarray(optarg,
						columns, ARRAY_SIZE(columns),
						cpu_column_name_to_id);
				if (ncolumns < 0)
					return EXIT_FAILURE;
			}
			mod->mode = c == 'p' ? OUTPUT_PARSABLE : OUTPUT_READABLE;
			break;
		case 's':
			desc->prefix = optarg;
			mod->system = SYSTEM_SNAPSHOT;
			break;
		case 'x':
			mod->hex = 1;
			break;
		case 'y':
			mod->physical = 1;
			break;
		case OPT_OUTPUT_ALL:
			all = 1;
			break;

		case 'h':
			usage();
		case 'V':
			print_version(EXIT_SUCCESS);
		default:
			errtryhelp(EXIT_FAILURE);
		}
	}

	if (all && ncolumns == 0) {
		size_t sz, maxsz = mod->mode == OUTPUT_CACHES ?
				ARRAY_SIZE(coldescs_cache) :
				ARRAY_SIZE(coldescs_cpu);

		for (sz = 0; sz < maxsz; sz++)
			columns[ncolumns++] = sz;
	}

	if (cpu_modifier_specified && mod->mode == OUTPUT_SUMMARY) {
		fprintf(stderr,
			_("%s: options --all, --online and --offline may only "
			  "be used with options --extended or --parse.\n"),
			program_invocation_short_name);
		return EXIT_FAILURE;
	}

	if (argc != optind) {
		warnx(_("bad usage"));
		errtryhelp(EXIT_FAILURE);
	}

	/* set default cpu display mode if none was specified */
	if (!mod->online && !mod->offline) {
		mod->online = 1;
		mod->offline = mod->mode == OUTPUT_READABLE ? 1 : 0;
	}

	ul_path_init_debug();

	/* /sys/devices/system/cpu */
	desc->syscpu = ul_new_path(_PATH_SYS_CPU);
	if (!desc->syscpu)
		err(EXIT_FAILURE, _("failed to initialize CPUs sysfs handler"));
	if (desc->prefix)
		ul_path_set_prefix(desc->syscpu, desc->prefix);

	/* /proc */
	desc->procfs = ul_new_path("/proc");
	if (!desc->procfs)
		err(EXIT_FAILURE, _("failed to initialize procfs handler"));
	if (desc->prefix)
		ul_path_set_prefix(desc->procfs, desc->prefix);

	read_basicinfo(desc, mod);

	setsize = CPU_ALLOC_SIZE(maxcpus);

	for (i = 0; i < desc->ncpuspos; i++) {
		/* only consider present CPUs */
		if (desc->present &&
		    !CPU_ISSET_S(real_cpu_num(desc, i), setsize, desc->present))
			continue;
		read_topology(desc, i);
		read_cache(desc, i);
		read_polarization(desc, i);
		read_address(desc, i);
		read_configured(desc, i);
		read_max_mhz(desc, i);
		read_min_mhz(desc, i);
	}

	if (desc->caches)
		qsort(desc->caches, desc->ncaches,
				sizeof(struct cpu_cache), cachecmp);

	if (desc->ecaches)
		qsort(desc->ecaches, desc->necaches,
				sizeof(struct cpu_cache), cachecmp);

	read_nodes(desc);
	read_hypervisor(desc, mod);
	arm_cpu_decode(desc, mod);

	switch(mod->mode) {
	case OUTPUT_SUMMARY:
		print_summary(desc, mod);
		break;
	case OUTPUT_CACHES:
		if (!ncolumns) {
			columns[ncolumns++] = COL_CACHE_NAME;
			columns[ncolumns++] = COL_CACHE_ONESIZE;
			columns[ncolumns++] = COL_CACHE_ALLSIZE;
			columns[ncolumns++] = COL_CACHE_WAYS;
			columns[ncolumns++] = COL_CACHE_TYPE;
			columns[ncolumns++] = COL_CACHE_LEVEL;
			columns[ncolumns++] = COL_CACHE_SETS;
			columns[ncolumns++] = COL_CACHE_PHYLINE;
			columns[ncolumns++] = COL_CACHE_COHERENCYSIZE;
		}
		print_caches_readable(desc, columns, ncolumns, mod);
		break;
	case OUTPUT_PARSABLE:
		if (!ncolumns) {
			columns[ncolumns++] = COL_CPU_CPU;
			columns[ncolumns++] = COL_CPU_CORE;
			columns[ncolumns++] = COL_CPU_SOCKET;
			columns[ncolumns++] = COL_CPU_NODE;
			columns[ncolumns++] = COL_CPU_CACHE;
			mod->compat = 1;
		}
		print_cpus_parsable(desc, columns, ncolumns, mod);
		break;
	case OUTPUT_READABLE:
		if (!ncolumns) {
			/* No list was given. Just print whatever is there. */
			columns[ncolumns++] = COL_CPU_CPU;
			if (desc->nodemaps)
				columns[ncolumns++] = COL_CPU_NODE;
			if (desc->drawermaps)
				columns[ncolumns++] = COL_CPU_DRAWER;
			if (desc->bookmaps)
				columns[ncolumns++] = COL_CPU_BOOK;
			if (desc->socketmaps)
				columns[ncolumns++] = COL_CPU_SOCKET;
			if (desc->coremaps)
				columns[ncolumns++] = COL_CPU_CORE;
			if (desc->caches)
				columns[ncolumns++] = COL_CPU_CACHE;
			if (desc->online)
				columns[ncolumns++] = COL_CPU_ONLINE;
			if (desc->configured)
				columns[ncolumns++] = COL_CPU_CONFIGURED;
			if (desc->polarization)
				columns[ncolumns++] = COL_CPU_POLARIZATION;
			if (desc->addresses)
				columns[ncolumns++] = COL_CPU_ADDRESS;
			if (desc->maxmhz)
				columns[ncolumns++] = COL_CPU_MAXMHZ;
			if (desc->minmhz)
				columns[ncolumns++] = COL_CPU_MINMHZ;
		}
		print_cpus_readable(desc, columns, ncolumns, mod);
		break;
	}

	ul_unref_path(desc->syscpu);
	ul_unref_path(desc->procfs);
#endif /* LSCPU_OLD_OUTPUT_CODE */
	return EXIT_SUCCESS;
}
