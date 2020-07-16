#include "lscpu-api.h"

struct lscpu_cpu *lscpu_new_cpu(void)
{
	struct lscpu_cpu *cpu;

	cpu = xcalloc(1, sizeof(struct lscpu_cpu));
	cpu->refcount = 1;
	cpu->logical_id = -1;

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

int lscpu_add_cpu(struct lscpu_cxt *cxt,
		  struct lscpu_cpu *cpu,
		  struct lscpu_cputype *ct)
{
	struct lscpu_cputype *type;

	/* make sure the type exists */
	if (ct)
		type = lscpu_add_cputype(cxt, ct);
	else
		type = lscpu_cputype_get_default(cxt);

	cxt->cpus = xrealloc(cxt->cpus, (cxt->ncpus + 1)
				* sizeof(struct lscpu_cpu *));

	cxt->cpus[cxt->ncpus] = cpu;
	cxt->ncpus++;
	lscpu_ref_cpu(cpu);

	if (type) {
		cpu->type = type;
		lscpu_ref_cputype(type);
		type->ncpus++;
	}

	return 0;
}

int lscpu_cpus_apply_type(struct lscpu_cxt *cxt, struct lscpu_cputype *type)
{
	size_t i;

	for (i = 0; i < cxt->ncpus; i++) {
		struct lscpu_cpu *cpu = cxt->cpus[i];

		if (!cpu->type) {
			cpu->type = type;
			lscpu_ref_cputype(type);
			type->ncpus++;
		}
	}
	return 0;
}
