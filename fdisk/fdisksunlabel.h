#ifndef FDISK_SUN_LABEL_H
#define FDISK_SUN_LABEL_H

#include <linux/types.h>   /* for __u16, __u32 */

struct sun_partition {
	__u32	start_cylinder;
	__u32	num_sectors;
};

struct sun_tag_flag {
	__u16	tag;
#define SUN_TAG_UNASSIGNED	0x00	/* Unassigned partition */
#define SUN_TAG_BOOT		0x01	/* Boot partition	*/
#define SUN_TAG_ROOT		0x02	/* Root filesystem	*/
#define SUN_TAG_SWAP		0x03	/* Swap partition	*/
#define SUN_TAG_USR		0x04	/* /usr filesystem	*/
#define SUN_TAG_BACKUP		0x05	/* Full-disk slice	*/
#define SUN_TAG_STAND		0x06	/* Stand partition	*/
#define SUN_TAG_VAR		0x07	/* /var filesystem	*/
#define SUN_TAG_HOME		0x08	/* /home filesystem	*/
#define SUN_TAG_ALTSCTR		0x09	/* Alt sector partition	*/
#define SUN_TAG_CACHE		0x0a	/* Cachefs partition	*/
#define SUN_TAG_RESERVED	0x0b	/* SMI reserved data	*/
#define SUN_TAG_LINUX_SWAP	0x82	/* Linux SWAP		*/
#define SUN_TAG_LINUX_NATIVE	0x83	/* Linux filesystem	*/
#define SUN_TAG_LINUX_LVM	0x8e	/* Linux LVM		*/
#define SUN_TAG_LINUX_RAID	0xfd	/* LInux RAID		*/

	__u16	flag;
#define SUN_FLAG_UNMNT		0x01	/* Unmountable partition*/
#define SUN_FLAG_RONLY		0x10	/* Read only		*/
};

#define SUN_LABEL_SIZE		512

#define SUN_LABEL_ID_SIZE	128
#define SUN_VOLUME_ID_SIZE	8

#define SUN_LABEL_VERSION	0x00000001
#define SUN_LABEL_SANE		0x600ddeee
#define SUN_NUM_PARTITIONS	8

struct sun_disk_label {
	char			label_id[SUN_LABEL_ID_SIZE];
	__u32			version;
	char			volume_id[SUN_VOLUME_ID_SIZE];
	__u16			num_partitions;
	struct sun_tag_flag	part_tags[SUN_NUM_PARTITIONS];
	__u32			bootinfo[3];
	__u32			sanity;
	__u32			resv[10];
	__u32			part_timestamps[SUN_NUM_PARTITIONS];
	__u32			write_reinstruct;
	__u32			read_reinstruct;
	__u8			pad[148];
	__u16			rpm;
	__u16			pcyl;
	__u16			apc;
	__u16			resv1;
	__u16			resv2;
	__u16			intrlv;
	__u16			ncyl;
	__u16			acyl;
	__u16			nhead;
	__u16			nsect;
	__u16			resv3;
	__u16			resv4;
	struct sun_partition	partitions[SUN_NUM_PARTITIONS];
	__u16			magic;
	__u16			cksum;
};

#define SUN_LABEL_MAGIC		0xDABE
#define SUN_LABEL_MAGIC_SWAPPED	0xBEDA
#define sunlabel ((struct sun_disk_label *)MBRbuffer)

/* fdisksunlabel.c */
extern struct systypes sun_sys_types[];
extern void guess_device_type(int fd);
extern int check_sun_label(void);
extern void sun_nolabel(void);
extern void create_sunlabel(void);
extern void sun_delete_partition(int i);
extern int sun_change_sysid(int i, __u16 sys);
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
extern void toggle_sunflags(int i, __u16 mask);
extern int sun_get_sysid(int i);

#endif /* FDISK_SUN_LABEL_H */
