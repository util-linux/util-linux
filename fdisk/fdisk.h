/*
   fdisk.h
*/

#define DEFAULT_SECTOR_SIZE	512
#define MAX_SECTOR_SIZE	2048
#define SECTOR_SIZE	512	/* still used in BSD code */
#define MAXIMUM_PARTS	60

#define ACTIVE_FLAG     0x80

#define EXTENDED        0x05
#define WIN98_EXTENDED  0x0f
#define LINUX_PARTITION 0x81
#define LINUX_SWAP      0x82
#define LINUX_NATIVE    0x83
#define LINUX_EXTENDED  0x85

#define IS_EXTENDED(i) \
	((i) == EXTENDED || (i) == WIN98_EXTENDED || (i) == LINUX_EXTENDED)

#define SIZE(a)	(sizeof(a)/sizeof((a)[0]))

#define cround(n)	(display_in_cyl_units ? ((n)/units_per_sector)+1 : (n))
#define scround(x)	(((x)+units_per_sector-1)/units_per_sector)

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

enum failure {usage, usage2, ioctl_error,
	unable_to_open, unable_to_read, unable_to_seek,
	unable_to_write, out_of_memory};

enum action {fdisk, require, try_only, create_empty};

struct geom {
	unsigned int heads;
	unsigned int sectors;
	unsigned int cylinders;
};

/* prototypes for fdisk.c */
extern char *disk_device,
            *line_ptr;
extern int fd,
           partitions;
extern uint display_in_cyl_units,
            units_per_sector;
extern void change_units(void);
extern struct partition *part_table[];
extern void fatal(enum failure why);
extern void get_geometry(int fd, struct geom *);
extern int get_boot(enum action what);
extern int  get_partition(int warn, int max);
extern void list_types(struct systypes *sys);
extern int read_line (void);
extern char read_char(char *mesg);
extern int read_hex(struct systypes *sys);
extern void reread_partition_table(int leave);
extern struct partition *get_part_table(int);
extern int valid_part_table_flag(unsigned char *b);
extern uint read_int(uint low, uint dflt, uint high, uint base, char *mesg);


#define PLURAL	0
#define SINGULAR 1
extern char *const str_units(int);

extern unsigned int get_start_sect(struct partition *p);
extern unsigned int get_nr_sects(struct partition *p);

extern int osf_label;

/* prototypes for fdiskbsdlabel.c */
extern void bselect(void);
extern int check_osf_label(void);
extern int btrydev(char * dev);
extern void xbsd_print_disklabel(int);

/* prototypes for fdisksgilabel.c */
extern int valid_part_table_flag(unsigned char *b);

#define PROC_PARTITIONS "/proc/partitions"
