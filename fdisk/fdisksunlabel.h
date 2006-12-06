#include <linux/types.h>   /* for __u16, __u32 */

typedef struct {
	unsigned char info[128];   /* Informative text string */
	unsigned char spare0[14];
	struct sun_info {
		unsigned char spare1;
		unsigned char id;
		unsigned char spare2;
		unsigned char flags;
	} infos[8];
	unsigned char spare1[246]; /* Boot information etc. */
	unsigned short rspeed;     /* Disk rotational speed */
	unsigned short pcylcount;  /* Physical cylinder count */
	unsigned short sparecyl;   /* extra sects per cylinder */
	unsigned char spare2[4];   /* More magic... */
	unsigned short ilfact;     /* Interleave factor */
	unsigned short ncyl;       /* Data cylinder count */
	unsigned short nacyl;      /* Alt. cylinder count */
	unsigned short ntrks;      /* Tracks per cylinder */
	unsigned short nsect;      /* Sectors per track */
	unsigned char spare3[4];   /* Even more magic... */
	struct sun_partition {
		__u32 start_cylinder;
		__u32 num_sectors;
	} partitions[8];
	unsigned short magic;      /* Magic number */
	unsigned short csum;       /* Label xor'd checksum */
} sun_partition;

#define SUN_LABEL_MAGIC          0xDABE
#define SUN_LABEL_MAGIC_SWAPPED  0xBEDA
#define sunlabel ((sun_partition *)MBRbuffer)
#define SSWAP16(x) (other_endian ? __swap16(x) \
				 : (__u16)(x))
#define SSWAP32(x) (other_endian ? __swap32(x) \
				 : (__u32)(x))
				 
/* fdisk.c */
extern char MBRbuffer[MAX_SECTOR_SIZE];
extern unsigned int heads, sectors, cylinders;
extern int show_begin;
extern int sun_label;
extern char *partition_type(unsigned char type);
extern void update_units(void);
extern char read_chars(char *mesg);
extern void set_all_unchanged(void);
extern void set_changed(int);

/* fdisksunlabel.c */
#define SUNOS_SWAP 3
#define WHOLE_DISK 5

extern struct systypes sun_sys_types[];
extern int get_num_sectors(struct sun_partition p);
extern void guess_device_type(int fd);
extern int check_sun_label(void);
extern void sun_nolabel(void);
extern void create_sunlabel(void);
extern void sun_delete_partition(int i);
extern void sun_change_sysid(int i, int sys);
extern void sun_list_table(int xtra);
extern void verify_sun(void);
extern void add_sun_partition(int n, int sys);
extern void sun_write_table(void);
extern void sun_set_alt_cyl(void);
extern void sun_set_ncyl(int cyl);
extern void sun_set_xcyl(void);
extern void sun_set_ilfact(void);
extern void sun_set_rspeed(void);
extern void sun_set_pcylcount(void);
extern void toggle_sunflags(int i, unsigned char mask);

