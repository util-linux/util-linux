#include <errno.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>

#include "lscpu-api.h"

/* add @set to the @ary, unnecessary set is deallocated. */
static int add_cpuset_to_array(cpu_set_t **ary, int *items, cpu_set_t *set, size_t setsize)
{
	int i;

	if (!ary)
		return -EINVAL;

	for (i = 0; i < *items; i++) {
		if (CPU_EQUAL_S(setsize, set, ary[i]))
			break;
	}
	if (i == *items) {
		ary[*items] = set;
		++*items;
		return 0;
	}
	CPU_FREE(set);
	return 1;
}

static void free_cpuset_array(cpu_set_t **ary, int items)
{
	int i;

	if (!ary)
		return;
	for (i = 0; i < items; i++)
		free(ary[i]);
	free(ary);
}

void lscpu_cputype_free_topology(struct lscpu_cputype *ct)
{
	if (!ct)
		return;
	free_cpuset_array(ct->coremaps, ct->ncores);
	free_cpuset_array(ct->socketmaps, ct->nsockets);
	free_cpuset_array(ct->bookmaps, ct->nbooks);
	free_cpuset_array(ct->drawermaps, ct->ndrawers);

	lscpu_free_caches(ct->caches, ct->ncaches);
}

void lscpu_free_caches(struct lscpu_cache *caches, size_t n)
{
	size_t i;

	if (!caches)
		return;

	for (i = 0; i < n; i++) {
		struct lscpu_cache *c = &caches[i];

		DBG(MISC, ul_debug(" freeing #%zu cache [%s]", i, c->name));

		free(c->name);
		free(c->type);
		free(c->allocation_policy);
		free(c->write_policy);

		free_cpuset_array(c->sharedmaps, c->nsharedmaps);
	}
	free(caches);
}

static int cmp_cache(const void *a0, const void *b0)
{
	const struct lscpu_cache
		*a = (const struct lscpu_cache *) a0,
		*b = (const struct lscpu_cache *) b0;
	return strcmp(a->name, b->name);
}

void lscpu_sort_caches(struct lscpu_cache *caches, size_t n)
{
	if (caches && n)
		qsort(caches, n, sizeof(struct lscpu_cache), cmp_cache);
}


/* Read topology for specified type */
static int cputype_read_topology(struct lscpu_cxt *cxt, struct lscpu_cputype *ct)
{
	size_t i, setsize, npos;
	struct path_cxt *sys;
	int nthreads = 0, sw_topo = 0;
	FILE *fd;

	sys = cxt->syscpu;				/* /sys/devices/system/cpu/ */
	setsize = CPU_ALLOC_SIZE(cxt->maxcpus);		/* CPU set size */
	npos = cxt->npossibles;				/* possible CPUs */

	DBG(TYPE, ul_debugobj(ct, "reading %s/%s/%s topology",
				ct->vendor ?: "", ct->model ?: "", ct->modelname ?:""));

	for (i = 0; i < npos; i++) {
		struct lscpu_cpu *cpu = cxt->cpus[i];
		cpu_set_t *thread_siblings = NULL, *core_siblings = NULL;
		cpu_set_t *book_siblings = NULL, *drawer_siblings = NULL;
		int num, n;

		if (!cpu || cpu->type != ct)
			continue;

		num = cpu->logical_id;
		if (ul_path_accessf(sys, F_OK,
					"cpu%d/topology/thread_siblings", num) != 0)
			continue;

		/*DBG(TYPE, ul_debugobj(ct, " #%d", num));*/

		/* read topology maps */
		ul_path_readf_cpuset(sys, &thread_siblings, cxt->maxcpus,
					"cpu%d/topology/thread_siblings", num);
		ul_path_readf_cpuset(sys, &core_siblings, cxt->maxcpus,
					"cpu%d/topology/core_siblings", num);
		ul_path_readf_cpuset(sys, &book_siblings, cxt->maxcpus,
					"cpu%d/topology/book_siblings", num);
		ul_path_readf_cpuset(sys, &drawer_siblings, cxt->maxcpus,
					"cpu%d/topology/drawer_siblings", num);

		n = CPU_COUNT_S(setsize, thread_siblings);
		if (!n)
			n = 1;
		if (n > nthreads)
			nthreads = n;

		/* Allocate arrays for topology maps.
		 *
		 * For each map we make sure that it can have up to ncpuspos
		 * entries. This is because we cannot reliably calculate the
		 * number of cores, sockets and books on all architectures.
		 * E.g. completely virtualized architectures like s390 may
		 * have multiple sockets of different sizes.
		 */
		if (!ct->coremaps)
			ct->coremaps = xcalloc(npos, sizeof(cpu_set_t *));
		if (!ct->socketmaps)
			ct->socketmaps = xcalloc(npos, sizeof(cpu_set_t *));
		if (!ct->bookmaps && book_siblings)
			ct->bookmaps = xcalloc(npos, sizeof(cpu_set_t *));
		if (!ct->drawermaps && drawer_siblings)
			ct->drawermaps = xcalloc(npos, sizeof(cpu_set_t *));

		/* add to topology maps */
		add_cpuset_to_array(ct->coremaps, &ct->ncores, thread_siblings, setsize);
		add_cpuset_to_array(ct->socketmaps, &ct->nsockets, core_siblings, setsize);

		if (book_siblings)
			add_cpuset_to_array(ct->bookmaps, &ct->nbooks, book_siblings, setsize);
		if (drawer_siblings)
			add_cpuset_to_array(ct->drawermaps, &ct->ndrawers, drawer_siblings, setsize);

	}

	/* s390 detects its cpu topology via /proc/sysinfo, if present.
	 * Using simply the cpu topology masks in sysfs will not give
	 * usable results since everything is virtualized. E.g.
	 * virtual core 0 may have only 1 cpu, but virtual core 2 may
	 * five cpus.
	 * If the cpu topology is not exported (e.g. 2nd level guest)
	 * fall back to old calculation scheme.
	 */
	if ((fd = ul_path_fopen(cxt->procfs, "r", "sysinfo"))) {
		int t0, t1;
		char buf[BUFSIZ];

		DBG(TYPE, ul_debugobj(ct, " reading sysinfo"));

		while (fgets(buf, sizeof(buf), fd) != NULL) {
			if (sscanf(buf, "CPU Topology SW: %d %d %d %d %d %d",
					&t0, &t1,
					&ct->ndrawers_per_system,
					&ct->nbooks_per_drawer,
					&ct->nsockets_per_book,
					&ct->ncores_per_socket) == 6) {
				sw_topo = 1;
				DBG(TYPE, ul_debugobj(ct, " using SW topology"));
				break;
			}
		}
		if (fd)
			fclose(fd);
	}

	if (ct->mtid)
		ct->nthreads_per_core = atoi(ct->mtid) + 1;
	else
		ct->nthreads_per_core = nthreads;

	if (!sw_topo) {
		ct->ndrawers_per_system = ct->nbooks_per_drawer =
			ct->nsockets_per_book = ct->ncores_per_socket = 0;
		if (!ct->ncores_per_socket && ct->nsockets)
			ct->ncores_per_socket = ct->ncores / ct->nsockets;
		if (!ct->nsockets_per_book && ct->nbooks)
			ct->nsockets_per_book = ct->nsockets / ct->nbooks;
		if (!ct->nbooks_per_drawer && ct->ndrawers)
			ct->nbooks_per_drawer = ct->nbooks / ct->ndrawers;
		if (ct->ndrawers_per_system)
			ct->ndrawers_per_system = ct->ndrawers;
	}

	DBG(TYPE, ul_debugobj(ct, " nthreads: %d (per core)", ct->nthreads_per_core));
	DBG(TYPE, ul_debugobj(ct, "   ncores: %d (%d per socket)", ct->ncores, ct->ncores_per_socket));
	DBG(TYPE, ul_debugobj(ct, " nsockets: %d (%d per books)", ct->nsockets, ct->nsockets_per_book));
	DBG(TYPE, ul_debugobj(ct, "   nbooks: %d (%d per drawer)", ct->nbooks, ct->nbooks_per_drawer));
	DBG(TYPE, ul_debugobj(ct, " ndrawers: %d (%d per system)", ct->ndrawers, ct->ndrawers_per_system));

	return 0;
}

static int read_caches(struct lscpu_cxt *cxt, struct lscpu_cputype *ct, struct lscpu_cpu *cpu)
{
	char buf[256];
	struct path_cxt *sys = cxt->syscpu;
	int num = cpu->logical_id;
	size_t i, setsize;

	if (!ct->ncaches) {
		while (ul_path_accessf(sys, F_OK,
					"cpu%d/cache/index%zu",
					num, ct->ncaches) == 0)
			ct->ncaches++;

		if (!ct->ncaches)
			return 0;
		ct->caches = xcalloc(ct->ncaches, sizeof(*ct->caches));
	}

	setsize = CPU_ALLOC_SIZE(cxt->maxcpus);

	for (i = 0; i < ct->ncaches; i++) {
		struct lscpu_cache *ca = &ct->caches[i];
		cpu_set_t *map;

		if (ul_path_accessf(sys, F_OK, "cpu%d/cache/index%zu", num, i) != 0)
			continue;

		if (!ca->name) {
			int type = 0;

			/* cache type */
			if (ul_path_readf_string(sys, &ca->type,
					"cpu%d/cache/index%zu/type", num, i) > 0) {
				if (!strcmp(ca->type, "Data"))
					type = 'd';
				else if (!strcmp(ca->type, "Instruction"))
					type = 'i';
			}

			/* cache level */
			ul_path_readf_s32(sys, &ca->level,
					"cpu%d/cache/index%zu/level", num, i);
			if (type)
				snprintf(buf, sizeof(buf), "L%d%c", ca->level, type);
			else
				snprintf(buf, sizeof(buf), "L%d", ca->level);

			ca->name = xstrdup(buf);

			ul_path_readf_u32(sys, &ca->ways_of_associativity,
					"cpu%d/cache/index%zu/ways_of_associativity", num, i);
			ul_path_readf_u32(sys, &ca->physical_line_partition,
					"cpu%d/cache/index%zu/physical_line_partition", num, i);
			ul_path_readf_u32(sys, &ca->number_of_sets,
					"cpu%d/cache/index%zu/number_of_sets", num, i);
			ul_path_readf_u32(sys, &ca->coherency_line_size,
					"cpu%d/cache/index%zu/coherency_line_size", num, i);

			ul_path_readf_string(sys, &ca->allocation_policy,
					"cpu%d/cache/index%zu/allocation_policy", num, i);
			ul_path_readf_string(sys, &ca->write_policy,
					"cpu%d/cache/index%zu/write_policy", num, i);

			/* cache size */
			if (ul_path_readf_buffer(sys, buf, sizeof(buf),
					"cpu%d/cache/index%zu/size", num, i) > 0)
				parse_size(buf, &ca->size, NULL);
			else
				ca->size = 0;
		}

		/* information about how CPUs share different caches */
		ul_path_readf_cpuset(sys, &map, cxt->maxcpus,
				  "cpu%d/cache/index%zu/shared_cpu_map", num, i);

		if (!ca->sharedmaps)
			ca->sharedmaps = xcalloc(cxt->npossibles, sizeof(cpu_set_t *));

		add_cpuset_to_array(ca->sharedmaps, &ca->nsharedmaps, map, setsize);
	}

	return 0;
}

/* Read cache for specified type */
static int cputype_read_caches(struct lscpu_cxt *cxt, struct lscpu_cputype *ct)
{
	size_t i;
	int rc = 0;

	DBG(TYPE, ul_debugobj(ct, "reading %s/%s/%s topology",
				ct->vendor ?: "", ct->model ?: "", ct->modelname ?:""));

	for (i = 0; i < cxt->npossibles; i++) {
		struct lscpu_cpu *cpu = cxt->cpus[i];

		if (!cpu || cpu->type != ct)
			continue;
		rc = read_caches(cxt, ct, cpu);
		if (rc)
			break;
	}

	lscpu_sort_caches(ct->caches, ct->ncaches);
	return rc;
}

static int read_ids(struct lscpu_cxt *cxt, struct lscpu_cpu *cpu)
{
	struct path_cxt *sys = cxt->syscpu;
	int num = cpu->logical_id;

	if (ul_path_accessf(sys, F_OK, "cpu%d/topology", num) != 0)
		return 0;

	DBG(CPU, ul_debugobj(cpu, "#%d reading IDs", num));

	if (ul_path_readf_s32(sys, &cpu->coreid, "cpu%d/topology/core_id", num) != 0)
		cpu->coreid = -1;
	if (ul_path_readf_s32(sys, &cpu->socketid, "cpu%d/topology/physical_package_id", num) != 0)
		cpu->socketid = -1;
	if (ul_path_readf_s32(sys, &cpu->bookid, "cpu%d/topology/book_id", num) != 0)
		cpu->bookid = -1;
	if (ul_path_readf_s32(sys, &cpu->drawerid, "cpu%d/topology/drawer_id", num) != 0)
		cpu->drawerid = -1;

	return 0;
}

static int read_polarization(struct lscpu_cxt *cxt, struct lscpu_cpu *cpu)
{
	struct path_cxt *sys = cxt->syscpu;
	int num = cpu->logical_id;
	char mode[64];

	if (ul_path_accessf(sys, F_OK, "cpu%d/polarization", num) != 0)
		return 0;

	ul_path_readf_buffer(sys, mode, sizeof(mode), "cpu%d/polarization", num);

	DBG(CPU, ul_debugobj(cpu, "#%d polar=%s", num, mode));

	if (strncmp(mode, "vertical:low", sizeof(mode)) == 0)
		cpu->polarization = POLAR_VLOW;
	else if (strncmp(mode, "vertical:medium", sizeof(mode)) == 0)
		cpu->polarization = POLAR_VMEDIUM;
	else if (strncmp(mode, "vertical:high", sizeof(mode)) == 0)
		cpu->polarization = POLAR_VHIGH;
	else if (strncmp(mode, "horizontal", sizeof(mode)) == 0)
		cpu->polarization = POLAR_HORIZONTAL;
	else
		cpu->polarization = POLAR_UNKNOWN;

	return 0;
}

static int read_address(struct lscpu_cxt *cxt, struct lscpu_cpu *cpu)
{
	struct path_cxt *sys = cxt->syscpu;
	int num = cpu->logical_id;

	if (ul_path_accessf(sys, F_OK, "cpu%d/address", num) != 0)
		return 0;

	ul_path_readf_s32(sys, &cpu->address, "cpu%d/address", num);
	return 0;
}

static int read_configure(struct lscpu_cxt *cxt, struct lscpu_cpu *cpu)
{
	struct path_cxt *sys = cxt->syscpu;
	int num = cpu->logical_id;

	if (ul_path_accessf(sys, F_OK, "cpu%d/configure", num) != 0)
		return 0;

	ul_path_readf_s32(sys, &cpu->configured, "cpu%d/configure", num);
	return 0;
}

static int read_mhz(struct lscpu_cxt *cxt, struct lscpu_cpu *cpu)
{
	struct path_cxt *sys = cxt->syscpu;
	int num = cpu->logical_id;
	int mhz;

	if (ul_path_readf_s32(sys, &mhz, "cpu%d/cpufreq/cpuinfo_max_freq", num) == 0)
		cpu->mhz_max_freq = (float) mhz / 1000;
	if (ul_path_readf_s32(sys, &mhz, "cpu%d/cpufreq/cpuinfo_min_freq", num) == 0)
		cpu->mhz_min_freq = (float) mhz / 1000;
	return 0;
}

int lscpu_read_topology(struct lscpu_cxt *cxt)
{
	size_t i;
	int rc = 0;

	for (i = 0; i < cxt->ncputypes; i++) {
		rc += cputype_read_topology(cxt, cxt->cputypes[i]);
		rc += cputype_read_caches(cxt, cxt->cputypes[i]);
	}

	for (i = 0; rc == 0 && i < cxt->npossibles; i++) {
		struct lscpu_cpu *cpu = cxt->cpus[i];

		if (!cpu || !cpu->type)
			continue;

		rc = read_ids(cxt, cpu);
		if (!rc)
			rc = read_polarization(cxt, cpu);
		if (!rc)
			rc = read_address(cxt, cpu);
		if (!rc)
			rc = read_configure(cxt, cpu);
		if (!rc)
			rc = read_mhz(cxt, cpu);
	}

	return rc;
}


