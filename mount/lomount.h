extern int verbose;
extern int set_loop(const char *, const char *, unsigned long long,
		    const char *, int, int *);
extern int del_loop(const char *);
extern int is_loop_device(const char *);
extern char * find_unused_loop_device(void);
