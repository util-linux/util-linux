extern int set_loop(const char *, const char *, unsigned long long, unsigned long long,
		    const char *, int, int *);
extern int del_loop(const char *);
extern int is_loop_device(const char *);
extern int is_loop_autoclear(const char *device);
extern char * find_unused_loop_device(void);

extern int loopfile_used_with(char *devname, const char *filename, unsigned long long offset);
extern char *loopfile_used (const char *filename, unsigned long long offset);

#define SETLOOP_RDONLY     (1<<0)  /* Open loop read-only */
#define SETLOOP_AUTOCLEAR  (1<<1)  /* Automatically detach loop on close (2.6.25?) */
