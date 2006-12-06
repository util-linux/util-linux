/*
   fdisk.h
*/

#define SECTOR_SIZE	512
#define MAXIMUM_PARTS	60

#define SIZE(a)	(sizeof(a)/sizeof((a)[0]))

#define cround(n)	(((n) + display_factor * unit_flag) / display_factor)

#if defined(__GNUC__) || defined(HAS_LONG_LONG)
typedef long long ext2_loff_t;
#else
typedef long      ext2_loff_t;
#endif

extern ext2_loff_t ext2_llseek(unsigned int fd,
			       ext2_loff_t offset,
			       unsigned int origin);

struct partition {
	unsigned char boot_ind;         /* 0x80 - active */
	unsigned char head;             /* starting head */
	unsigned char sector;           /* starting sector */
	unsigned char cyl;              /* starting cylinder */
	unsigned char sys_ind;          /* What partition type */
	unsigned char end_head;         /* end head */
	unsigned char end_sector;       /* end sector */
	unsigned char end_cyl;          /* end cylinder */
	unsigned char start4[4];        /* starting sector counting from 0 */
	unsigned char size4[4];         /* nr of sectors in partition */
};

enum failure {usage, unable_to_open, unable_to_read, unable_to_seek,
	unable_to_write, out_of_memory};

enum offset {ignore, lower, deflt, upper};

enum action {fdisk, require, try_only, create_empty};

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
extern int get_boot(enum action what);
extern int  get_partition(int warn, int max);
extern void list_types(struct systypes *sys);
extern int read_line (void);
extern char read_char(char *mesg);
extern int read_hex(struct systypes *sys);
uint read_int(uint low, uint dflt, uint high, enum offset base, char *mesg);
extern char *const str_units(void);

extern unsigned int get_start_sect(struct partition *p);
extern unsigned int get_nr_sects(struct partition *p);

/* prototypes for fdisklabel.c */
extern void bselect(void);
extern int btrydev (char * dev);

/* prototypes for fdisksunlabel.c */
