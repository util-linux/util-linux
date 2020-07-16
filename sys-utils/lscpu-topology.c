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
}


/* Read topology for specified type */
static int cputype_read_topology(struct lscpu_cxt *cxt, struct lscpu_cputype *ct)
{
	size_t i, setsize, npos;
	struct path_cxt *sys;
	int nthreads = 0;

	sys = cxt->syscpu;				/* /sys/devices/system/cpu/ */
	setsize = CPU_ALLOC_SIZE(cxt->maxcpus);		/* CPU set size */
	npos = cxt->ncpuspos;				/* possible CPUs */

	DBG(TYPE, ul_debugobj(ct, "reading %s/%s/%s topology",
				ct->vendor ?: "", ct->model ?: "", ct->modelname ?:""));

	for (i = 0; i < cxt->ncpus; i++) {
		struct lscpu_cpu *cpu = cxt->cpus[i];
		cpu_set_t *thread_siblings = NULL, *core_siblings = NULL;
		cpu_set_t *book_siblings = NULL, *drawer_siblings = NULL;
		int num, n;

		if (cpu->type != ct)
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

	ct->nthreads =	(ct->ndrawers ?: 1) *
			(ct->nbooks   ?: 1) *
			(ct->nsockets ?: 1) *
			(ct->ncores   ?: 1) * nthreads;

	DBG(TYPE, ul_debugobj(ct, " nthreads: %d (per core: %d)", ct->nthreads, nthreads));
	DBG(TYPE, ul_debugobj(ct, "   ncores: %d", ct->ncores));
	DBG(TYPE, ul_debugobj(ct, " nsockets: %d", ct->nsockets));
	DBG(TYPE, ul_debugobj(ct, "   nbooks: %d", ct->nbooks));
	DBG(TYPE, ul_debugobj(ct, " ndrawers: %d", ct->ndrawers));

	return 0;
}

int lscpu_read_topology(struct lscpu_cxt *cxt)
{
	size_t i;
	int rc = 0;

	for (i = 0; i < cxt->ncputypes; i++)
		rc += cputype_read_topology(cxt, cxt->cputypes[i]);

	return rc;
}

int lscpu_read_topology_ids(struct lscpu_cxt *cxt)
{
	struct path_cxt *sys = cxt->syscpu;
	size_t i;

	for (i = 0; i < cxt->ncpus; i++) {
		struct lscpu_cpu *cpu = cxt->cpus[i];
		int num = cpu->logical_id;

		DBG(TYPE, ul_debugobj(cpu, "#%d reading IDs", num));

		if (ul_path_readf_s32(sys, &cpu->coreid, "cpu%d/topology/core_id", num) != 0)
			cpu->coreid = -1;
		if (ul_path_readf_s32(sys, &cpu->socketid, "cpu%d/topology/physical_package_id", num) != 0)
			cpu->socketid = -1;
		if (ul_path_readf_s32(sys, &cpu->bookid, "cpu%d/topology/book_id", num) != 0)
			cpu->bookid = -1;
		if (ul_path_readf_s32(sys, &cpu->drawerid, "cpu%d/topology/drawer_id", num) != 0)
			cpu->drawerid = -1;
	}

	return 0;
}

int lscpu_read_topology_polarization(struct lscpu_cxt *cxt)
{
	struct path_cxt *sys = cxt->syscpu;
	size_t i;

	for (i = 0; i < cxt->ncpus; i++) {
		struct lscpu_cpu *cpu = cxt->cpus[i];
		int num = cpu->logical_id;
		char mode[64];

		if (!cpu->type || cpu->type->dispatching < 0)
			continue;
		if (ul_path_accessf(sys, F_OK, "cpu%d/polarization", num) != 0)
			continue;

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
	}
	return 0;
}
