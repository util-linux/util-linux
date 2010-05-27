#ifndef UTIL_LINUX_CPUSET_H
#define UTIL_LINUX_CPUSET_H

#include <sched.h>


#define cpuset_nbits(setsize)	(8 * (setsize))

extern cpu_set_t *cpuset_alloc(int ncpus, size_t *setsize, size_t *nbits);
extern void cpuset_free(cpu_set_t *set);

extern char *cpulist_create(char *str, size_t len, cpu_set_t *set, size_t setsize);
extern int cpulist_parse(const char *str, cpu_set_t *set, size_t setsize);

extern char *cpumask_create(char *str, size_t len, cpu_set_t *set, size_t setsize);
extern int cpumask_parse(const char *str, cpu_set_t *set, size_t setsize);

#endif /* UTIL_LINUX_CPUSET_H */
