/*
   fdisk.h
*/

#define SECTOR_SIZE	512
#define NETBSD_PARTITION 0xa5
#define cround(n)	(((n) + display_factor * unit_flag) / display_factor)

#if defined(__GNUC__) || defined(HAS_LONG_LONG)
typedef long long ext2_loff_t;
#else
typedef long      ext2_loff_t;
#endif

extern ext2_loff_t ext2_llseek(unsigned int fd,
			       ext2_loff_t offset,
			       unsigned int origin);

enum failure {usage, unable_to_open, unable_to_read, unable_to_seek,
	unable_to_write, out_of_memory};

enum offset {ignore, lower, deflt, upper};

struct systypes {
  unsigned char index;
  char *name;
};

/* prototypes for fdisk.c */
extern char *disk_device,
            *line_ptr;
extern int fd,
           partitions;
extern uint unit_flag,
            display_factor;
extern struct partition *part_table[];
extern void fatal(enum failure why);
extern int  get_partition(int warn, int max);
extern void list_types(struct systypes *sys, int size);
extern int read_line (void);
extern char read_char(char *mesg);
extern int read_hex(struct systypes *sys, int size);
uint read_int(uint low, uint dflt, uint high, enum offset base, char *mesg);
extern char *const str_units(void);

/* prototypes for fdisklabel.c */
extern void bselect(void);
extern void btrydev (char * dev);
