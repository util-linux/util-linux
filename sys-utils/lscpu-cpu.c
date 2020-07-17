#include "lscpu-api.h"

struct lscpu_cpu *lscpu_new_cpu(int id)
{
	struct lscpu_cpu *cpu;

	cpu = xcalloc(1, sizeof(struct lscpu_cpu));
	cpu->refcount = 1;
	cpu->logical_id = id;

	DBG(CPU, ul_debugobj(cpu, "alloc"));
	return cpu;
}

void lscpu_ref_cpu(struct lscpu_cpu *cpu)
{
	if (cpu)
		cpu->refcount++;
}

void lscpu_unref_cpu(struct lscpu_cpu *cpu)
{
	if (!cpu)
		return;

	if (--cpu->refcount <= 0) {
		DBG(CPU, ul_debugobj(cpu, "  freeing"));
		lscpu_unref_cputype(cpu->type);
		free(cpu->dynamic_mhz);
		free(cpu->static_mhz);
		free(cpu->mhz);
		free(cpu);
	}
}

/*
 * Create and initialize array with CPU structs according to @cpuset.
 */
int lscpu_create_cpus(struct lscpu_cxt *cxt, cpu_set_t *cpuset, size_t setsize)
{
	size_t n;

	assert(!cxt->cpus);

	cxt->npossibles = CPU_COUNT_S(setsize, cpuset);
	cxt->cpus = xcalloc(1, cxt->npossibles * sizeof(struct lscpu_cpu *));

	for (n = 0; n < cxt->npossibles; n++) {
		if (CPU_ISSET_S(n, setsize, cpuset))
			cxt->cpus[n] = lscpu_new_cpu(n);
	}

	return 0;
}

int lscpu_cpu_set_type(struct lscpu_cpu *cpu, struct lscpu_cputype *type)
{
	struct lscpu_cputype *old = cpu->type;

	cpu->type = type;

	if (type) {
		lscpu_ref_cputype(type);
		type->ncpus++;
	}
	if (old) {
		lscpu_unref_cputype(old);
		old->ncpus--;
	}
	return 0;
}

int lscpu_cpus_apply_type(struct lscpu_cxt *cxt, struct lscpu_cputype *type)
{
	size_t i;

	for (i = 0; i < cxt->npossibles; i++) {
		struct lscpu_cpu *cpu = cxt->cpus[i];

		if (cpu && !cpu->type)
			lscpu_cpu_set_type(cpu, type);
	}
	return 0;
}

struct lscpu_cpu *lscpu_get_cpu(struct lscpu_cxt *cxt, int logical_id)
{
	size_t i;

	for (i = 0; i < cxt->npossibles; i++) {
		struct lscpu_cpu *cpu = cxt->cpus[i];

		if (cpu && cpu->logical_id == logical_id)
			return cpu;
	}

	return NULL;
}
