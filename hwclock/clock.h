#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>		/* for errno, EPERM, EINVAL, ENOENT */
#include <time.h>

struct clock_ops {
	char *interface_name;
        int (*get_permissions)(void);
        int (*read_hardware_clock)(struct tm *tm);
        int (*set_hardware_clock)(const struct tm *tm);
	int (*synchronize_to_clock_tick)(void);
};

extern struct clock_ops *probe_for_cmos_clock(void);
extern struct clock_ops *probe_for_rtc_clock(void);
extern struct clock_ops *probe_for_kd_clock(void);

typedef int bool;
#define TRUE 1
#define FALSE 0

/* hwclock.c */
extern char *progname;
extern int debug;
extern int epoch_option;
extern void outsyserr(char *msg, ...);

/* cmos.c */
extern void set_cmos_epoch(int ARCconsole, int SRM);
extern void set_cmos_access(int Jensen, int funky_toy);

/* rtc.c */
extern int get_epoch_rtc(unsigned long *epoch, int silent);
extern int set_epoch_rtc(unsigned long epoch);
