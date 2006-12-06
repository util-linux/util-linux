#include <time.h>
#include <sys/time.h>
#include <unistd.h>

typedef int bool;
#define TRUE 1
#define FALSE 0

#define MYNAME "hwclock"
#define VERSION "2.5"


enum clock_access_method {ISA, RTC_IOCTL, KD, DEV_PORT, NOCLOCK};
  /* A method for accessing (reading, writing) the hardware clock:
     
     ISA: 
       via direct CPU I/O instructions that work on an ISA family
       machine (IBM PC compatible) or most DEC Alphas, which implement
       enough of ISA to get by.

     RTC_IOCTL: 
       via the rtc device driver, using device special file /dev/rtc.

     KD:
       via the console driver, using device special file /dev/tty1.
       This is the m68k ioctl interface, known as KDGHWCLK.

     DEV_PORT:
       via the /dev/port device, which is almost the same thing as 
       direct I/O instructions via the ISA method, but works on a Jensen 
       model of Alpha, whereas ISA does not.  Also, use I/O addresses
       0x170 and 0x171 instead of the ISA 0x70 and 0x71.

     NO_CLOCK:
       Unable to determine a usable access method for the system clock.
   */
       



/* hwclock.c */
extern int debug;
extern const bool alpha_machine;
extern const bool isa_machine;

/* directio.c */

extern void
assume_interrupts_enabled(void);

extern void
synchronize_to_clock_tick_ISA(int *retcode_p, const int dev_port,
                              const bool use_uf_bit);

extern void
read_hardware_clock_isa(struct tm *tm, const int dev_port,
                        int hc_zero_year);

extern void
set_hardware_clock_isa(const struct tm new_tm, 
                       const int hc_zero_year,
                       const int dev_port,
                       const bool testing);

extern void
get_inb_outb_privilege(const enum clock_access_method clock_access, 
                       bool * const no_auth_p);

extern void
get_dev_port_access(const enum clock_access_method clock_access,
                    int * dev_port_p);

extern bool
uf_bit_needed(const bool user_wants_uf);

extern int
zero_year(const bool arc_opt, const bool srm_opt);


/* rtc.c */
extern void
synchronize_to_clock_tick_RTC(int *retcode_p);

extern void
read_hardware_clock_rtc_ioctl(struct tm *tm);

extern void
set_hardware_clock_rtc_ioctl(const struct tm new_broken_time, 
                             const bool testing);
extern void
see_if_rtc_works(bool * const rtc_works_p);

extern void
get_epoch(unsigned long *epoch_p, char **reason_p);

extern void
set_epoch(unsigned long epoch, const bool testing, int *retcode_p);


/* kd.c */

extern void
synchronize_to_clock_tick_KD(int *retcode_p);

extern void
read_hardware_clock_kd(struct tm *tm);

extern void
set_hardware_clock_kd(const struct tm new_broken_time, 
                      const bool testing);

extern void
see_if_kdghwclk_works(bool * const kdghwclk_works_p);

extern const bool got_kdghwclk;

/* util.c */
extern bool
is_in_cpuinfo(const char * const fmt, const char * const str);

extern char *
ctime2(const time_t time);

extern struct timeval
t2tv(time_t argument);

extern struct timeval
t2tv(time_t argument);

extern float 
time_diff(struct timeval subtrahend, struct timeval subtractor);

extern struct timeval
time_inc(struct timeval addend, float increment);





