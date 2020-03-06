#ifndef UTIL_LINUX_H_IRQ_COMMON
#define UTIL_LINUX_H_IRQ_COMMON

#include "c.h"
#include "nls.h"

/* supported columns */
enum {
	COL_IRQ = 0,
	COL_TOTAL,
	COL_DELTA,
	COL_NAME,

	__COL_COUNT
};

struct irq_info {
	char *irq;			/* short name of this irq */
	char *name;			/* descriptive name of this irq */
	unsigned long total;		/* total count since system start up */
	unsigned long delta;		/* delta count since previous update */
};


struct irq_stat {
	unsigned int nr_irq;		/* number of irq vector */
	unsigned int nr_irq_info;	/* number of irq info */
	struct irq_info *irq_info;	/* array of irq_info */
	long nr_active_cpu;		/* number of active cpu */
	unsigned long total_irq;	/* total irqs */
	unsigned long delta_irq;	/* delta irqs */
};

typedef int (sort_fp)(const struct irq_info *, const struct irq_info *);

#define DEF_SORT_FUNC	((sort_fp *)sort_total)

/* output definition */
struct irq_output {
	int columns[__COL_COUNT * 2];
	size_t ncolumns;

	sort_fp *sort_func;

	unsigned int
		json:1,
		no_headings:1;
};

int irq_column_name_to_id(char const *const name, size_t const namesz);
void free_irqstat(struct irq_stat *stat);

void irq_print_columns(FILE *f);

static inline int sort_name(const struct irq_info *a,
		     const struct irq_info *b)
{
	return (strcmp(a->name, b->name) > 0) ? 1 : 0;
}

static inline int sort_total(const struct irq_info *a,
		      const struct irq_info *b)
{
	return a->total < b->total;
}

static inline int sort_delta(const struct irq_info *a,
		      const struct irq_info *b)
{
	return a->delta < b->delta;
}

static inline int sort_interrupts(const struct irq_info *a,
			   const struct irq_info *b)
{
	return (strcmp(a->irq, b->irq) > 0) ? 1 : 0;
}

sort_fp *set_sort_func(char key);

struct libscols_table *get_scols_table(struct irq_output *out,
                                              struct irq_stat *prev,
                                              struct irq_stat **xstat);

#endif /* UTIL_LINUX_H_IRQ_COMMON */
