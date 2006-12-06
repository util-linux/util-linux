/**************************************************************************

  This is a component of the hwclock program.

  This file contains the code for accessing the hardware clock via
  direct I/O (kernel-style input and output operations) as opposed
  to via a device driver.


  MAINTENANCE NOTES

  Here is some information on how the Hardware Clock works, from
  unknown source and authority.  In theory, the specification for this
  stuff is the specification of Motorola's MC146818A clock chip, used
  in the early ISA machines.  Subsequent machines should have copied
  its function exactly.  In reality, though, the copies are inexact
  and the MC146818A itself may fail to implement its specifications,
  and we have just have to work with whatever is there (actually,
  anything that Windows works with, because that's what determines
  whether broken hardware has to be fixed!).

  i386 CMOS starts out with 14 bytes clock data
  alpha has something similar, but with details
  depending on the machine type.
 
  byte 0: seconds (0-59)
  byte 2: minutes (0-59)
  byte 4: hours (0-23 in 24hr mode,
                 1-12 in 12hr mode, with high bit unset/set if am/pm)
  byte 6: weekday (1-7, Sunday=1)
  byte 7: day of the month (1-31)
  byte 8: month (1-12)
  byte 9: year (0-99)


  Numbers are stored in BCD/binary if bit 2 of byte 11 is unset/set
  The clock is in 12hr/24hr mode if bit 1 of byte 11 is unset/set
  The clock is undefined (being updated) if bit 7 of byte 10 is set.
  The clock is frozen (to be updated) by setting bit 7 of byte 11
  Bit 7 of byte 14 indicates whether the CMOS clock is reliable:
  it is 1 if RTC power has been good since this bit was last read;
  it is 0 when the battery is dead and system power has been off.
 
  The century situation is messy:

  Usually byte 50 (0x32) gives the century (in BCD, so 0x19 or 0x20 in
  pure binary), but IBM PS/2 has (part of) a checksum there and uses
  byte 55 (0x37).  Sometimes byte 127 (0x7f) or Bank 1, byte 0x48
  gives the century.  The original RTC will not access any century
  byte; some modern versions will. If a modern RTC or BIOS increments
  the century byte it may go from 0x19 to 0x20, but in some buggy
  cases 0x1a is produced.

  CMOS byte 10 (clock status register A) has 3 bitfields:
  bit 7: 1 if data invalid, update in progress (read-only bit)
         (this is raised 224 us before the actual update starts)
  6-4    select base frequency
         010: 32768 Hz time base (default)
         111: reset
         all other combinations are manufacturer-dependent
         (e.g.: DS1287: 010 = start oscillator, anything else = stop)
  3-0    rate selection bits for interrupt
         0000 none
         0001, 0010 give same frequency as 1000, 1001
         0011 122 microseconds (minimum, 8192 Hz)
         .... each increase by 1 halves the frequency, doubles the period
         1111 500 milliseconds (maximum, 2 Hz)
         0110 976.562 microseconds (default 1024 Hz)

  Avoid setting the RTC clock within 2 seconds of the day rollover
  that starts a new month or enters daylight saving time.

****************************************************************************/

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#if defined(__i386__) || defined(__alpha__)
#include <asm/io.h>             /* for inb, outb */
#else
void outb(int a, int b){}
int inb(int c){ return 0; }
#endif

#include "hwclock.h"

#define BCD_TO_BIN(val) (((val)&0x0f) + ((val)>>4)*10)
#define BIN_TO_BCD(val) ((((val)/10)<<4) + (val)%10)


/*----------------------------------------------------------------------------
   ATOMIC_TOP and ATOMIC_BOTTOM are wierd macros that help us to do 
   atomic operations when we do ugly low level I/O.
   
   You put ATOMIC_TOP above some code and ATOMIC_BOTTOM below it and
   it makes sure all the enclosed code executes without interruption
   by some other process (and, in some cases, even the kernel).

   These work fundamentally differently depending on the machine
   architecture.  In the case of a x86, it simply turns interrupts off
   at the top and turns them back on at the bottom.

   For Alpha, we can't mess with interrupts (we shouldn't for x86
   either, but at least it tends to work!), so instead we start a loop
   at the top and close it at the bottom.  This loop repeats the
   enclosed code until the upper 32 bits of the cycle counter are the
   same before and after.  That means there was no context change
   while the enclosed code was executing.
   
   For other architectures, we do nothing, and the atomicity is only
   feigned.

-----------------------------------------------------------------------------*/

#if defined(__i386__)
#define ATOMIC_TOP \
  { \
    const bool interrupts_were_enabled = interrupts_enabled; \
    __asm__ volatile ("cli"); \
    interrupts_enabled = FALSE; 

#define ATOMIC_BOTTOM \
    if (interrupts_were_enabled) { \
      __asm__ volatile ("sti"); \
      interrupts_enabled = TRUE; \
    } \
  }
#elif defined(__alpha__)
#define ATOMIC_TOP \
  { \
    unsigned long ts1, ts2, n; \
    n = 0; \
    do { \
      asm volatile ("rpcc %0" : "r="(ts1));

#define ATOMIC_BOTTOM \
      asm volatile ("rpcc %0" : "r="(ts2)); \
      n++; \
    } while ((ts1 ^ ts2) >> 32 != 0); \
  }
#else
#define ATOMIC_BOTTOM
#define ATOMIC_TOP
#endif


#if defined(__i386__) || defined(__alpha__)
/* The following are just constants.  Oddly, this program will not
   compile if the inb() and outb() functions use something even
   slightly different from these variables.  This is probably at least
   partially related to the fact that __builtin_constant_p() doesn't
   work (is never true) in an inline function.  See comment to this 
   effect in asm/io.h. 
*/
static unsigned short clock_ctl_addr = 0x70;
static unsigned short clock_data_addr = 0x71;
#endif


static bool interrupts_enabled;
  /* Interrupts are enabled as normal.  We, unfortunately, turn interrupts
     on the machine off in some places where we do the direct ISA accesses
     to the Hardware Clock.  It is in extremely poor form for a user space
     program to do this, but that's the price we have to pay to run on an
     ISA machine without the rtc driver in the kernel.

     Code which turns interrupts off uses this value to determine if they
     need to be turned back on.
     */


void
assume_interrupts_enabled(void) {
  interrupts_enabled = TRUE;
}



static int
i386_iopl(const int level) {
/*----------------------------------------------------------------------------
   When compiled for an Intel target, this is just the iopl() kernel call.
   When compiled for any other target, this is a dummy function.

   We do it this way in order to keep the conditional compilation stuff
   out of the way so it doesn't mess up readability of the code.
-----------------------------------------------------------------------------*/
#ifdef __i386__
  extern int iopl(int level);
  return iopl(level);
#else
  return -1;
#endif
}



bool
uf_bit_needed(const bool user_wants_uf) {
/*----------------------------------------------------------------------------
   Return true iff the UIP bit doesn't work on this hardware clock, so
   we will need to use the UF bit to synchronize with the clock (if in
   fact we synchronize using direct I/O to the clock).

   To wit, we need to use the UF bit on a DEC Alpha PC164/LX164/SX164.
   Or, of course, if the user told us to.
-----------------------------------------------------------------------------*/
  bool retval;

  if (user_wants_uf) retval = TRUE;
  else {
    if (alpha_machine && (
                          is_in_cpuinfo("system variation", "PC164") ||
                          is_in_cpuinfo("system variation", "LX164") ||
                          is_in_cpuinfo("system variation", "SX164")))
      retval = TRUE;
    else retval = FALSE;
  }
  if (debug && retval)
    printf("We will be using the UF bit instead of the usual "
           "UIP bit to synchronize with the clock, as required on "
           "certain models of DEC Alpha.\n");

  return retval;
}



int
zero_year(const bool arc_opt, const bool srm_opt) {
/*----------------------------------------------------------------------------
   Return the year of the century (e.g. 0) to which a zero value in
   the year register of the hardware clock applies (or at least what
   we are to assume -- nobody can say for sure!)

   'arc_opt' and 'srm_opt' are the true iff the user specified the
   corresponding invocation option to instruct us that the machine is an
   Alpha with ARC or SRM console time.

   A note about hardware clocks:

   ISA machines are simple: the year register is a year-of-century
   register, so the zero year is zero.  On Alphas, we may see 1980 or
   1952 (Digital Unix?) or 1958 (ALPHA_PRE_V1_2_SRM_CONSOLE) 
-----------------------------------------------------------------------------*/
  int retval;   /* our return value */

  if (arc_opt || srm_opt) {
    /* User is telling us what epoch his machine uses.  Believe it. */
    if (arc_opt) retval = 0;
    else retval = 0;
  } else {
    unsigned long kernel_epoch;
    char *reason;   /* malloc'ed */
    
    get_epoch(&kernel_epoch, &reason);
    if (reason == NULL) retval = kernel_epoch;
    else {
      /* OK, the user doesn't know and the kernel doesn't know; 
         figure it out from the machine model 
         */
      free(reason);             /* Don't care about kernel's excuses */
      /* See whether we are dealing with SRM or MILO, as they have
         different "epoch" ideas. */
      if (is_in_cpuinfo("system serial number", "MILO")) {
        if (debug) printf("booted from MILO\n");
        /* See whether we are dealing with a RUFFIAN aka UX, as they
           have REALLY different TOY (TimeOfYear) format: BCD, and not
           an ARC-style epoch.  BCD is detected dynamically, but we
           must NOT adjust like ARC. 
           */
        if (is_in_cpuinfo("system type", "Ruffian")) {
          if (debug) printf("Ruffian BCD clock\n");
          retval = 0;
        } else {
          if (debug) printf("Not Ruffian BCD clock\n");
          retval = 80;
        }
      } else {
        if (debug) printf("Not booted from MILO\n");
        retval = 0;
      }
    }
  }
  return retval;
}



static inline unsigned char 
hclock_read(const unsigned char reg, const int dev_port) {
/*---------------------------------------------------------------------------
  Relative byte 'reg' of the Hardware Clock value.

  Get this with direct CPU I/O instructions.  If 'dev_port' is not -1,
  use the /dev/port device driver (via the 'dev_port' file descriptor)
  to do this I/O.  Otherwise, use the kernel's inb()/outb() facility.

  On a system without the inb()/outb() facility, if 'dev_port' is -1,
  just return 0.

  Results undefined if 'reg' is out of range.
---------------------------------------------------------------------------*/
  unsigned char ret;

  ATOMIC_TOP
  if (dev_port >= 0) {
    const unsigned char v = reg | 0x80;
    lseek(dev_port, 0x170, 0);
    write(dev_port, &v, 1);
    lseek(dev_port, 0x171, 0);
    read(dev_port, &ret, 1);
 } else {
#if defined(__i386__) || defined(__alpha__)
    /* & 0x7f ensures that we are not disabling NMI while we read.
       Setting on Bit 7 here would disable NMI

       Various docs suggest that one should disable NMI while
       reading/writing CMOS data, and enable it again afterwards.
       This would yield the sequence
	  outb (reg | 0x80, 0x70);
	  val = inb(0x71);
	  outb (0x0d, 0x70);	// 0x0d: random read-only location
       Other docs state that "any write to 0x70 should be followed
       by an action to 0x71 or the RTC wil be left in an unknown state".
       Most docs say that it doesn't matter at all what one does.
       */
    outb(reg & 0x7f, clock_ctl_addr);
    ret = inb(clock_data_addr);
#else
    ret = 0;
#endif
  }
  ATOMIC_BOTTOM
  return ret;
}



static inline void 
hclock_write(unsigned char reg, unsigned char val, const int dev_port) {
/*----------------------------------------------------------------------------
  Set relative byte 'reg' of the Hardware Clock value to 'val'.
  Do this with the kernel's outb() function if 'dev_port' is -1, but
  if not, use the /dev/port device (via the 'dev_port' file descriptor), 
  which is almost the same thing.

  On a non-ISA, non-Alpha machine, if 'dev_port' is -1, do nothing.
----------------------------------------------------------------------------*/
  if (dev_port >= 0) {
    unsigned char v;
    v = reg | 0x80;
    lseek(dev_port, 0x170, 0);
    write(dev_port, &v, 1);
    v = (val & 0xff);
    lseek(dev_port, 0x171, 0);
    write(dev_port, &v, 1);
  } else {
#if defined(__i386__) || defined(__alpha__)
  /* & 0x7f ensures that we are not disabling NMI while we read.
     Setting on Bit 7 here would disable NMI
     */
  outb(reg & 0x7f, clock_ctl_addr);
  outb(val, clock_data_addr);
#endif
  }
}



static inline int
hardware_clock_busy(const int dev_port, const bool use_uf_bit) {
/*----------------------------------------------------------------------------
   Return whether the hardware clock is in the middle of an update
   (which happens once each second).

   Use the clock's UIP bit (bit 7 of Control Register A) to tell
   unless 'use_uf_bit' is true, in which case use the UF bit (bit 4 of
   Control Register C).
-----------------------------------------------------------------------------*/
  return
    use_uf_bit ? (hclock_read(12, dev_port) & 0x10) : 
      (hclock_read(10, dev_port) & 0x80);
}



void
synchronize_to_clock_tick_ISA(int *retcode_p, const int dev_port,
                              const bool use_uf_bit) {
/*----------------------------------------------------------------------------
  Same as synchronize_to_clock_tick(), but just for ISA.
-----------------------------------------------------------------------------*/
  int i;  /* local loop index */

  /* Wait for rise.  Should be within a second, but in case something
     weird happens, we have a limit on this loop to reduce the impact
     of this failure.
     */
  for (i = 0; 
       !hardware_clock_busy(dev_port, use_uf_bit) && (i < 10000000); 
       i++);
  if (i >= 10000000) *retcode_p = 1;
  else { 
    /* Wait for fall.  Should be within 2.228 ms. */
    for (i = 0; 
         hardware_clock_busy(dev_port, use_uf_bit) && (i < 1000000); 
         i++);
    if (i >= 10000000) *retcode_p = 1;
    else *retcode_p = 0;
  }
}



void
read_hardware_clock_isa(struct tm *tm, const int dev_port,
                        int hc_zero_year) {
/*----------------------------------------------------------------------------
  Read the hardware clock and return the current time via <tm> argument.
  Assume we have an ISA machine and read the clock directly with CPU I/O
  instructions.  If 'dev_port' isn't -1, use the /dev/port facility to 
  do this I/O.  Otherwise, use the kernel's inb()/outb() service.

  This function is not totally reliable.  It takes a finite and
  unpredictable amount of time to execute the code below.  During that
  time, the clock may change and we may even read an invalid value in
  the middle of an update.  We do a few checks to minimize this
  possibility, but only the kernel can actually read the clock
  properly, since it can execute code in a short and predictable
  amount of time (by turning off interrupts).

  In practice, the chance of this function returning the wrong time is
  extremely remote.

-----------------------------------------------------------------------------*/
  bool got_time;
    /* We've successfully read a time from the Hardware Clock */
  int attempts;
    /* Number of times we've tried to read the clock.  This only
       matters because we will give up (and proceed with garbage in
       variables) rather than hang if something is broken and we are
       never able to read the clock 
       */
  int hclock_sec = 0, hclock_min = 0, hclock_hour = 0, hclock_wday = 0, 
      hclock_mon = 0, hclock_mday = 0, hclock_year = 0;
    /* The values we got from the Hardware Clock's registers, assuming
       they are in pure binary. 
       */

  int status = 0;  /* Hardware Clock status register, as if pure binary */
  int adjusted_year;
  int ampmhour;
  int pmbit;

  got_time = FALSE;
  attempts = 0;    /* initial value */
  while (!got_time && attempts++ < 1000000) {
    /* Bit 7 of Byte 10 of the Hardware Clock value is the Update In Progress
       (UIP) bit, which is on while and 244 uS before the Hardware Clock 
       updates itself.  It updates the counters individually, so reading 
       them during an update would produce garbage.  The update takes 2mS,
       so we could be spinning here that long waiting for this bit to turn
       off.

       Furthermore, it is pathologically possible for us to be in this
       code so long that even if the UIP bit is not on at first, the
       clock has changed while we were running.  We check for that too,
       and if it happens, we start over.
       */

    if ((hclock_read(10, dev_port) & 0x80) == 0) {
      /* No clock update in progress, go ahead and read */

      status = hclock_read(11, dev_port);

      hclock_sec = hclock_read(0, dev_port);
      hclock_min = hclock_read(2, dev_port);
      hclock_hour = hclock_read(4, dev_port);
      hclock_wday = hclock_read(6, dev_port);
      hclock_mday = hclock_read(7, dev_port);
      hclock_mon = hclock_read(8, dev_port);
      hclock_year = hclock_read(9, dev_port);
      /* Unless the clock changed while we were reading, consider this 
         a good clock read .
         */
      if (hclock_sec == hclock_read(0, dev_port)) got_time = TRUE;
      /* Yes, in theory we could have been running for 60 seconds and
         the above test wouldn't work!
         */
    }
  }

  if (!(status & 0x04)) { 
    /* The hardware clock is in BCD mode.  This is normal. */
    tm->tm_sec = BCD_TO_BIN(hclock_sec);
    tm->tm_min = BCD_TO_BIN(hclock_min);
    ampmhour = BCD_TO_BIN(hclock_hour & 0x7f);
    pmbit = hclock_hour & 0x80;
    tm->tm_wday = BCD_TO_BIN(hclock_wday) - 1;  /* Used to be 3.  Why?? */
    tm->tm_mday = BCD_TO_BIN(hclock_mday);
    tm->tm_mon = BCD_TO_BIN(hclock_mon) - 1;
    adjusted_year = BCD_TO_BIN(hclock_year);
  } else {
    /* The hardware clock registers are in pure binary format.  */
    tm->tm_sec = hclock_sec;
    tm->tm_min = hclock_min;
    ampmhour = hclock_hour & 0x7f;
    pmbit = hclock_hour & 0x80;
    tm->tm_wday = hclock_wday - 1; /* Used to be 3.  Why?? */
    tm->tm_mday = hclock_mday;
    tm->tm_mon = hclock_mon - 1;
    adjusted_year = hclock_year;
  }

  if (!(status & 0x02)) {	
    /* Clock is in 12 hour (am/pm) mode.  This is unusual. */
    if (pmbit == 0x80) {
      if (ampmhour == 12) tm->tm_hour = 12;
      else tm->tm_hour = 12 + ampmhour;
    } else {
      if (ampmhour ==12) tm->tm_hour = 0;
      else tm->tm_hour = ampmhour;
    }
  } else {
    /* Clock is in 24 hour mode.  This is normal. */
    tm->tm_hour = ampmhour;
  }
  /* We don't use the century byte (Byte 50) of the Hardware Clock.
     Here's why:  It didn't exist in the original ISA specification,
     so old machines don't have it, and even some new ones don't.
     Some machines, including the IBM Valuepoint 6387-X93, use that
     byte for something else.  Some machines have the century in 
     Byte 55.
     
     Furthermore, the Linux standard time data structure doesn't
     allow for times beyond about 2037 and no Linux systems were 
     running before 1937.  Therefore, all the century byte could tell
     us is that the clock is wrong or this whole program is obsolete!
         
     So we just say if the year of century is less than 37, it's the
     2000's, otherwise it's the 1900's.
     
     Alpha machines (some, anyway) don't have this ambiguity
     because they do not have a year-of-century register.  We
     pretend they do anyway, for simplicity and to avoid
     recognizing times that can't be represented in Linux standard
     time.  So even though we already have enough information to
     know that the clock says 2050, we will render it as 1950.
     */
  {
    const int year_of_century = (adjusted_year + hc_zero_year) % 100;
    if (year_of_century >= 37) tm->tm_year = year_of_century;
    else tm->tm_year = year_of_century + 100;
  }
  tm->tm_isdst = -1;        /* don't know whether it's daylight */
}



void
set_hardware_clock_isa(const struct tm new_tm, 
                       const int hc_zero_year,
                       const int dev_port,
                       const bool testing) {
/*----------------------------------------------------------------------------
  Set the Hardware Clock to the time (in broken down format)
  new_tm.  Use direct I/O instructions to what we assume is
  an ISA Hardware Clock.

  Iff 'dev_port' is -1, use the kernel inb()/outb() service, otherwise
  use the /dev/port device (via file descriptor 'dev_port')
  to do those I/O instructions.
----------------------------------------------------------------------------*/
  unsigned char save_control, save_freq_select;

  if (testing) 
    printf("Not setting Hardware Clock because running in test mode.\n");
  else {
    int ampmhour;
      /* The hour number that goes into the hardware clock, taking into
         consideration whether the clock is in 12 or 24 hour mode
         */
    int pmbit;
      /* Value to OR into the hour register as the am/pm bit */
    const int adjusted_year = 
        (new_tm.tm_year - hc_zero_year)%100;
      /* The number that goes in the hardware clock's year register */

    int hclock_sec, hclock_min, hclock_hour, hclock_wday, hclock_mon,
        hclock_mday, hclock_year;
      /* The values we will put, in pure binary, in the Hardware Clock's
         registers.
         */

    ATOMIC_TOP

    save_control = hclock_read(11, dev_port); 
      /* tell the clock it's being set */
    hclock_write(11, (save_control | 0x80), dev_port);
    save_freq_select = hclock_read(10, dev_port); 
      /* stop and reset prescaler */
    hclock_write (10, (save_freq_select | 0x70), dev_port);


    if (!(save_control & 0x02)) {	
      /* Clock is in 12 hour (am/pm) mode.  This is unusual. */
      if (new_tm.tm_hour == 0) {
        ampmhour = 12;
        pmbit = 0x00;
      } else if (new_tm.tm_hour < 12) {
        ampmhour = new_tm.tm_hour;
        pmbit = 0x00;
      } else if (new_tm.tm_hour == 12) {
        ampmhour = 12;
        pmbit = 0x80;
      } else {
        ampmhour = new_tm.tm_hour - 12;
        pmbit = 0x80;
      }
    } else {
      /* Clock is in 24 hour mode.  This is normal. */
      ampmhour = new_tm.tm_hour;
      pmbit = 0x00;
    }


    if (!(save_control & 0x04)) { 
      /* Clock's registers are in BCD.  This is normal. */
      hclock_sec = BIN_TO_BCD(new_tm.tm_sec);
      hclock_min = BIN_TO_BCD(new_tm.tm_min);
      hclock_hour = pmbit | BIN_TO_BCD(ampmhour);
      hclock_wday = BIN_TO_BCD(new_tm.tm_wday + 1); /* Used to be 3.  Why??*/
      hclock_mday = BIN_TO_BCD(new_tm.tm_mday);
      hclock_mon = BIN_TO_BCD(new_tm.tm_mon + 1);
      hclock_year = BIN_TO_BCD(adjusted_year);
    } else {
      /* Clock's registers are in pure binary.  This is unusual. */
      hclock_sec = new_tm.tm_sec;
      hclock_min = new_tm.tm_min;
      hclock_hour = pmbit | ampmhour;
      hclock_wday = new_tm.tm_wday + 1; /* Used to be 3.  Why?? */
      hclock_mday = new_tm.tm_mday;
      hclock_mon = new_tm.tm_mon + 1;
      hclock_year = adjusted_year;
    }
      
    hclock_write(0, hclock_sec, dev_port);
    hclock_write(2, hclock_min, dev_port);
    hclock_write(4, hclock_hour, dev_port);
    hclock_write(6, hclock_wday, dev_port);
    hclock_write(7, hclock_mday, dev_port);
    hclock_write(8, hclock_mon, dev_port);
    hclock_write(9, hclock_year, dev_port);

    /* We don't set the century byte (usually Byte 50) because it isn't
       always there.  (see further comments in read_hardware_clock_isa).
       In previous releases, we did.
       */
    
    /* The kernel sources, linux/arch/i386/kernel/time.c, have the
       following comment:
    
       The following flags have to be released exactly in this order,
       otherwise the DS12887 (popular MC146818A clone with integrated
       battery and quartz) will not reset the oscillator and will not
       update precisely 500 ms later.  You won't find this mentioned
       in the Dallas Semiconductor data sheets, but who believes data
       sheets anyway ...  -- Markus Kuhn
    
    Hence, they will also be done in this order here.
    faith@cs.unc.edu, Thu Nov  9 08:26:37 1995 
    */

    hclock_write (11, save_control, dev_port);
    hclock_write (10, save_freq_select, dev_port);

    ATOMIC_BOTTOM
  }
}



void
get_inb_outb_privilege(const enum clock_access_method clock_access, 
                       bool * const no_auth_p) {

  if (clock_access == ISA) {
    const int rc = i386_iopl(3);
    if (rc != 0) {
      fprintf(stderr, MYNAME " is unable to get I/O port access.  "
              "I.e. iopl(3) returned nonzero return code %d.\n"
              "This is often because the program isn't running "
              "with superuser privilege, which it needs.\n", 
              rc);
      *no_auth_p = TRUE;
    } else *no_auth_p = FALSE;
  } else *no_auth_p = FALSE;
}



void
get_dev_port_access(const enum clock_access_method clock_access,
                    int * dev_port_p) {

  if (clock_access == DEV_PORT) {
    /* Get the /dev/port file open */
    *dev_port_p = open("/dev/port", O_RDWR);
    if (*dev_port_p < 0) {
      fprintf(stderr, MYNAME "is unable to open the /dev/port file.  "
              "I.e. open() of the file failed with errno = %s (%d).\n"
              "Run with the --debug option and check documentation "
              "to find out why we are trying "
              "to use /dev/port instead of some other means to access "
              "the Hardware Clock.",
              strerror(errno), errno);
    } 
  } else *dev_port_p = 0;
}



