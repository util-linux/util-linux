#ifndef UTIL_LINUX_CPUSET_H
#define UTIL_LINUX_CPUSET_H

struct bitmask {
	unsigned int size;
	unsigned long *maskp;
};


#define howmany(x,y) (((x)+((y)-1))/(y))
#define bitsperlong (8 * sizeof(unsigned long))
#define longsperbits(n) howmany(n, bitsperlong)
#define bytesperbits(x) ((x+7)/8)

extern unsigned int bitmask_nbytes(struct bitmask *bmp);
extern struct bitmask *bitmask_alloc(unsigned int n);

extern char *cpuset_to_cstr(struct bitmask *mask, char *str);
extern char *cpuset_to_str(struct bitmask *mask, char *str);
extern int str_to_cpuset(struct bitmask *mask, const char* str);
extern int cstr_to_cpuset(struct bitmask *mask, const char* str);

#endif /* UTIL_LINUX_CPUSET_H */
