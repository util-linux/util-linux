/*
 * fdisksunlabel.c
 *
 * I think this is mostly, or entirely, due to
 * 	Jakub Jelinek (jj@sunsite.mff.cuni.cz), July 1996
 *
 * Merged with fdisk for other architectures, aeb, June 1998.
 *
 * Sat Mar 20 EST 1999 Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 *      Internationalization
 */

#include <stdio.h>		/* stderr */
#include <stdlib.h>		/* qsort */
#include <string.h>		/* strstr */
#include <unistd.h>		/* write */
#include <sys/ioctl.h>		/* ioctl */
#include <sys/stat.h>		/* stat */
#include <sys/sysmacros.h>	/* major */

#include "nls.h"

#include <endian.h>
#ifdef HAVE_SCSI_SCSI_H
#define u_char	unsigned char
#include <scsi/scsi.h>		/* SCSI_IOCTL_GET_IDLUN */
#undef u_char
#endif
#include <linux/major.h>	/* FLOPPY_MAJOR */

#include "common.h"
#include "fdisk.h"
#include "fdisksunlabel.h"

static int     other_endian = 0;
static int     scsi_disk = 0;
static int     floppy = 0;

#define LINUX_SWAP      0x82
#define LINUX_NATIVE    0x83

struct systypes sun_sys_types[] = {
	{0, N_("Empty")},
	{1, N_("Boot")},
	{2, N_("SunOS root")},
	{SUNOS_SWAP, N_("SunOS swap")},
	{4, N_("SunOS usr")},
	{WHOLE_DISK, N_("Whole disk")},
	{6, N_("SunOS stand")},
	{7, N_("SunOS var")},
	{8, N_("SunOS home")},
	{LINUX_SWAP, N_("Linux swap")},
	{LINUX_NATIVE, N_("Linux native")},
	{0x8e, N_("Linux LVM")},
	{0xfd, N_("Linux raid autodetect")},/* New (2.2.x) raid partition
					       with autodetect using
					       persistent superblock */
	{ 0, NULL }
};

static inline unsigned short __swap16(unsigned short x) {
        return (((__u16)(x) & 0xFF) << 8) | (((__u16)(x) & 0xFF00) >> 8);
}
static inline __u32 __swap32(__u32 x) {
        return (((__u32)(x) & 0xFF) << 24) | (((__u32)(x) & 0xFF00) << 8) | (((__u32)(x) & 0xFF0000) >> 8) | (((__u32)(x) & 0xFF000000) >> 24);
}

int
get_num_sectors(struct sun_partition p) {
	return SSWAP32(p.num_sectors);
}

#ifndef IDE0_MAJOR
#define IDE0_MAJOR 3
#endif
#ifndef IDE1_MAJOR
#define IDE1_MAJOR 22
#endif
void guess_device_type(int fd) {
	struct stat bootstat;

	if (fstat (fd, &bootstat) < 0) {
                scsi_disk = 0;
                floppy = 0;
	} else if (S_ISBLK(bootstat.st_mode)
		   && (major(bootstat.st_rdev) == IDE0_MAJOR ||
		       major(bootstat.st_rdev) == IDE1_MAJOR)) {
                scsi_disk = 0;
                floppy = 0;
	} else if (S_ISBLK(bootstat.st_mode)
		   && major(bootstat.st_rdev) == FLOPPY_MAJOR) {
                scsi_disk = 0;
                floppy = 1;
	} else {
                scsi_disk = 1;
                floppy = 0;
	}
}

static void
set_sun_partition(int i, unsigned int start, unsigned int stop, int sysid) {
	sunlabel->infos[i].id = sysid;
	sunlabel->partitions[i].start_cylinder =
		SSWAP32(start / (heads * sectors));
	sunlabel->partitions[i].num_sectors =
		SSWAP32(stop - start);
	set_changed(i);
}

void
sun_nolabel(void) {
	sun_label = 0;
	sunlabel->magic = 0;
	partitions = 4;
}

int
check_sun_label(void) {
	unsigned short *ush;
	int csum;

	if (sunlabel->magic != SUN_LABEL_MAGIC &&
	    sunlabel->magic != SUN_LABEL_MAGIC_SWAPPED) {
		sun_label = 0;
		other_endian = 0;
		return 0;
	}
	other_endian = (sunlabel->magic == SUN_LABEL_MAGIC_SWAPPED);
	ush = ((unsigned short *) (sunlabel + 1)) - 1;
	for (csum = 0; ush >= (unsigned short *)sunlabel;) csum ^= *ush--;
	if (csum) {
		fprintf(stderr,_("Detected sun disklabel with wrong checksum.\n"
				"Probably you'll have to set all the values,\n"
				"e.g. heads, sectors, cylinders and partitions\n"
				"or force a fresh label (s command in main menu)\n"));
	} else {
		heads = SSWAP16(sunlabel->ntrks);
		cylinders = SSWAP16(sunlabel->ncyl);
		sectors = SSWAP16(sunlabel->nsect);
	}
	update_units();
	sun_label = 1;
	partitions = 8;
	return 1;
}

struct sun_predefined_drives {
	char *vendor;
	char *model;
	unsigned short sparecyl;
	unsigned short ncyl;
	unsigned short nacyl;
	unsigned short pcylcount;
	unsigned short ntrks;
	unsigned short nsect;
	unsigned short rspeed;
} sun_drives[] = {
{"Quantum","ProDrive 80S",1,832,2,834,6,34,3662},
{"Quantum","ProDrive 105S",1,974,2,1019,6,35,3662},
{"CDC","Wren IV 94171-344",3,1545,2,1549,9,46,3600},
{"IBM","DPES-31080",0,4901,2,4903,4,108,5400},
{"IBM","DORS-32160",0,1015,2,1017,67,62,5400},
{"IBM","DNES-318350",0,11199,2,11474,10,320,7200},
{"SEAGATE","ST34371",0,3880,2,3882,16,135,7228},
{"","SUN0104",1,974,2,1019,6,35,3662},
{"","SUN0207",4,1254,2,1272,9,36,3600},
{"","SUN0327",3,1545,2,1549,9,46,3600},
{"","SUN0340",0,1538,2,1544,6,72,4200},
{"","SUN0424",2,1151,2,2500,9,80,4400},
{"","SUN0535",0,1866,2,2500,7,80,5400},
{"","SUN0669",5,1614,2,1632,15,54,3600},
{"","SUN1.0G",5,1703,2,1931,15,80,3597},
{"","SUN1.05",0,2036,2,2038,14,72,5400},
{"","SUN1.3G",6,1965,2,3500,17,80,5400},
{"","SUN2.1G",0,2733,2,3500,19,80,5400},
{"IOMEGA","Jaz",0,1019,2,1021,64,32,5394},
};

static struct sun_predefined_drives *
sun_autoconfigure_scsi(void) {
    struct sun_predefined_drives *p = NULL;

#ifdef SCSI_IOCTL_GET_IDLUN
    unsigned int id[2];
    char buffer[2048];
    char buffer2[2048];
    FILE *pfd;
    char *vendor;
    char *model;
    char *q;
    int i;

    if (!ioctl(fd, SCSI_IOCTL_GET_IDLUN, &id)) {
        sprintf(buffer,
            "Host: scsi%d Channel: %02d Id: %02d Lun: %02d\n",
#if 0                    
            ((id[0]>>24)&0xff)-/*PROC_SCSI_SCSI+PROC_SCSI_FILE*/33,
#else
            /* This is very wrong (works only if you have one HBA),
               but I haven't found a way how to get hostno
               from the current kernel */
            0,
#endif                        
            (id[0]>>16)&0xff,
            id[0]&0xff,
            (id[0]>>8)&0xff);
        pfd = fopen("/proc/scsi/scsi","r");
        if (pfd) {
            while (fgets(buffer2,2048,pfd)) {
		if (!strcmp(buffer, buffer2)) {
                    if (fgets(buffer2,2048,pfd)) {
                        q = strstr(buffer2,"Vendor: ");
                        if (q) {
                            q += 8;
                            vendor = q;
			    q = strstr(q," ");
			    *q++ = 0;	/* truncate vendor name */
                            q = strstr(q,"Model: ");
                            if (q) {
                                *q = 0;
                                q += 7;
                                model = q;
                                q = strstr(q," Rev: ");
                                if (q) {
                                    *q = 0;
                                    for (i = 0; i < SIZE(sun_drives); i++) {
                                        if (*sun_drives[i].vendor && strcasecmp(sun_drives[i].vendor, vendor))
                                            continue;
                                        if (!strstr(model, sun_drives[i].model))
                                            continue;
                                        printf(_("Autoconfigure found a %s%s%s\n"),sun_drives[i].vendor,(*sun_drives[i].vendor) ? " " : "",sun_drives[i].model);
                                        p = sun_drives + i;
                                        break;
                                    }
                                }
                            }
                        }
                    }
                    break;
                }
            }
            fclose(pfd);
        }
    }
#endif
    return p;
}

void create_sunlabel(void)
{
	struct hd_geometry geometry;
	unsigned int ndiv;
	int i;
	unsigned char c;
	struct sun_predefined_drives *p = NULL;

	fprintf(stderr,
	_("Building a new sun disklabel. Changes will remain in memory only,\n"
       	"until you decide to write them. After that, of course, the previous\n"
	"content won't be recoverable.\n\n"));
#if BYTE_ORDER == LITTLE_ENDIAN
	other_endian = 1;
#else
	other_endian = 0;
#endif
	memset(MBRbuffer, 0, sizeof(MBRbuffer));
	sunlabel->magic = SSWAP16(SUN_LABEL_MAGIC);
	if (!floppy) {
	    puts(_("Drive type\n"
	         "   ?   auto configure\n"
	         "   0   custom (with hardware detected defaults)"));
	    for (i = 0; i < SIZE(sun_drives); i++) {
	        printf("   %c   %s%s%s\n",
		       i + 'a', sun_drives[i].vendor,
		       (*sun_drives[i].vendor) ? " " : "",
		       sun_drives[i].model);
	    }
	    for (;;) {
	        c = read_char(_("Select type (? for auto, 0 for custom): "));
	        if (c >= 'a' && c < 'a' + SIZE(sun_drives)) {
	     	    p = sun_drives + c - 'a';
	     	    break;
	        } else if (c >= 'A' && c < 'A' + SIZE(sun_drives)) {
	            p = sun_drives + c - 'A';
	            break;
	        } else if (c == '0') {
	            break;
	        } else if (c == '?' && scsi_disk) {
		    p = sun_autoconfigure_scsi();
	            if (!p)
	                printf(_("Autoconfigure failed.\n"));
	            else
	                break;
	        }
	    }
	}
	if (!p || floppy) {
	    if (!ioctl(fd, HDIO_GETGEO, &geometry)) {
	        heads = geometry.heads;
	        sectors = geometry.sectors;
	        cylinders = geometry.cylinders;
	    } else {
	        heads = 0;
	        sectors = 0;
	        cylinders = 0;
	    }
	    if (floppy) {
	        sunlabel->nacyl = 0;
	        sunlabel->pcylcount = SSWAP16(cylinders);
	        sunlabel->rspeed = SSWAP16(300);
	        sunlabel->ilfact = SSWAP16(1);
	        sunlabel->sparecyl = 0;
	    } else {
	        heads = read_int(1,heads,1024,0,_("Heads"));
		sectors = read_int(1,sectors,1024,0,_("Sectors/track"));
	        if (cylinders)
	            cylinders = read_int(1,cylinders-2,65535,0,_("Cylinders"));
	        else
	            cylinders = read_int(1,0,65535,0,_("Cylinders"));
	        sunlabel->nacyl =
			SSWAP16(read_int(0,2,65535,0,
					 _("Alternate cylinders")));
	        sunlabel->pcylcount =
			SSWAP16(read_int(0,cylinders+SSWAP16(sunlabel->nacyl),
					 65535,0,_("Physical cylinders")));
	        sunlabel->rspeed =
			SSWAP16(read_int(1,5400,100000,0,
					 _("Rotation speed (rpm)")));
	        sunlabel->ilfact =
			SSWAP16(read_int(1,1,32,0,_("Interleave factor")));
	        sunlabel->sparecyl =
			SSWAP16(read_int(0,0,sectors,0,
					 _("Extra sectors per cylinder")));
	    }
	} else {
	    sunlabel->sparecyl = SSWAP16(p->sparecyl);
	    sunlabel->ncyl = SSWAP16(p->ncyl);
	    sunlabel->nacyl = SSWAP16(p->nacyl);
	    sunlabel->pcylcount = SSWAP16(p->pcylcount);
	    sunlabel->ntrks = SSWAP16(p->ntrks);
	    sunlabel->nsect = SSWAP16(p->nsect);
	    sunlabel->rspeed = SSWAP16(p->rspeed);
	    sunlabel->ilfact = SSWAP16(1);
	    cylinders = p->ncyl;
	    heads = p->ntrks;
	    sectors = p->nsect;
	    puts(_("You may change all the disk params from the x menu"));
	}

	snprintf(sunlabel->info, sizeof(sunlabel->info),
		 "%s%s%s cyl %d alt %d hd %d sec %d", 
		 p ? p->vendor : "", (p && *p->vendor) ? " " : "",
		 p ? p->model
		   : (floppy ? _("3,5\" floppy") : _("Linux custom")),
		cylinders, SSWAP16(sunlabel->nacyl), heads, sectors);

	sunlabel->ntrks = SSWAP16(heads);
	sunlabel->nsect = SSWAP16(sectors);
	sunlabel->ncyl = SSWAP16(cylinders);
	if (floppy)
	    set_sun_partition(0, 0, cylinders * heads * sectors, LINUX_NATIVE);
	else {
	    if (cylinders * heads * sectors >= 150 * 2048) {
	        ndiv = cylinders - (50 * 2048 / (heads * sectors)); /* 50M swap */
	    } else
	        ndiv = cylinders * 2 / 3;
	    set_sun_partition(0, 0, ndiv * heads * sectors, LINUX_NATIVE);
	    set_sun_partition(1, ndiv * heads * sectors, cylinders * heads * sectors, LINUX_SWAP);
	    sunlabel->infos[1].flags |= 0x01; /* Not mountable */
	}
	set_sun_partition(2, 0, cylinders * heads * sectors, WHOLE_DISK);
	{
		unsigned short *ush = (unsigned short *)sunlabel;
		unsigned short csum = 0;
		while(ush < (unsigned short *)(&sunlabel->csum))
			csum ^= *ush++;
		sunlabel->csum = csum;
	}

	set_all_unchanged();
	get_boot(create_empty_sun);
	set_changed(0);
}

void
toggle_sunflags(int i, unsigned char mask) {
	if (sunlabel->infos[i].flags & mask)
		sunlabel->infos[i].flags &= ~mask;
	else sunlabel->infos[i].flags |= mask;
	set_changed(i);
}

static void
fetch_sun(unsigned int *starts, unsigned int *lens, unsigned int *start, unsigned int *stop) {
	int i, continuous = 1;
	*start = 0; *stop = cylinders * heads * sectors;
	for (i = 0; i < partitions; i++) {
		if (sunlabel->partitions[i].num_sectors
		    && sunlabel->infos[i].id
		    && sunlabel->infos[i].id != WHOLE_DISK) {
			starts[i] = SSWAP32(sunlabel->partitions[i].start_cylinder) * heads * sectors;
			lens[i] = SSWAP32(sunlabel->partitions[i].num_sectors);
			if (continuous) {
				if (starts[i] == *start)
					*start += lens[i];
				else if (starts[i] + lens[i] >= *stop)
					*stop = starts[i];
				else
					continuous = 0;
				        /* There will be probably more gaps
					  than one, so lets check afterwards */
			}
		} else {
			starts[i] = 0;
			lens[i] = 0;
		}
	}
}

static unsigned int *verify_sun_starts;

static int
verify_sun_cmp(int *a, int *b) {
    if (*a == -1) return 1;
    if (*b == -1) return -1;
    if (verify_sun_starts[*a] > verify_sun_starts[*b]) return 1;
    return -1;
}

void
verify_sun(void) {
    unsigned int starts[8], lens[8], start, stop;
    int i,j,k,starto,endo;
    int array[8];

    verify_sun_starts = starts;
    fetch_sun(starts,lens,&start,&stop);
    for (k = 0; k < 7; k++) {
	for (i = 0; i < 8; i++) {
	    if (k && (lens[i] % (heads * sectors))) {
	        printf(_("Partition %d doesn't end on cylinder boundary\n"), i+1);
	    }
	    if (lens[i]) {
	        for (j = 0; j < i; j++)
	            if (lens[j]) {
	                if (starts[j] == starts[i]+lens[i]) {
	                    starts[j] = starts[i]; lens[j] += lens[i];
	                    lens[i] = 0;
	                } else if (starts[i] == starts[j]+lens[j]){
	                    lens[j] += lens[i];
	                    lens[i] = 0;
	                } else if (!k) {
	                    if (starts[i] < starts[j]+lens[j] &&
				starts[j] < starts[i]+lens[i]) {
	                        starto = starts[i];
	                        if (starts[j] > starto)
					starto = starts[j];
	                        endo = starts[i]+lens[i];
	                        if (starts[j]+lens[j] < endo)
					endo = starts[j]+lens[j];
	                        printf(_("Partition %d overlaps with others in "
				       "sectors %d-%d\n"), i+1, starto, endo);
	                    }
	                }
	            }
	    }
	}
    }
    for (i = 0; i < 8; i++) {
        if (lens[i])
            array[i] = i;
        else
            array[i] = -1;
    }
    qsort(array,SIZE(array),sizeof(array[0]),
	  (int (*)(const void *,const void *)) verify_sun_cmp);
    if (array[0] == -1) {
    	printf(_("No partitions defined\n"));
    	return;
    }
    stop = cylinders * heads * sectors;
    if (starts[array[0]])
        printf(_("Unused gap - sectors 0-%d\n"),starts[array[0]]);
    for (i = 0; i < 7 && array[i+1] != -1; i++) {
        printf(_("Unused gap - sectors %d-%d\n"),starts[array[i]]+lens[array[i]],starts[array[i+1]]);
    }
    start = starts[array[i]]+lens[array[i]];
    if (start < stop)
        printf(_("Unused gap - sectors %d-%d\n"),start,stop);
}

void
add_sun_partition(int n, int sys) {
	unsigned int start, stop, stop2;
	unsigned int starts[8], lens[8];
	int whole_disk = 0;
		
	char mesg[256];
	int i, first, last;

	if (sunlabel->partitions[n].num_sectors && sunlabel->infos[n].id) {
		printf(_("Partition %d is already defined.  Delete "
			"it before re-adding it.\n"), n + 1);
		return;
	}
	
	fetch_sun(starts,lens,&start,&stop);
	if (stop <= start) {
		if (n == 2)
			whole_disk = 1;
		else {
			printf(_("Other partitions already cover the whole disk.\nDelete "
			       "some/shrink them before retry.\n"));
			return;
		}
	}
	snprintf(mesg, sizeof(mesg), _("First %s"), str_units(SINGULAR));
	for (;;) {
		if (whole_disk)
			first = read_int(0, 0, 0, 0, mesg);
		else
			first = read_int(scround(start), scround(stop)+1,
					 scround(stop), 0, mesg);
		if (display_in_cyl_units)
			first *= units_per_sector;
		else {
			/* Starting sector has to be properly aligned */
			int cs = heads * sectors;
			int x = first % cs;

			if (x)
				first += cs - x;
		}
		if (n == 2 && first != 0)
			printf ("\
It is highly recommended that the third partition covers the whole disk\n\
and is of type `Whole disk'\n");
		/* ewt asks to add: "don't start a partition at cyl 0"
		   However, edmundo@rano.demon.co.uk writes:
		   "In addition to having a Sun partition table, to be able to
		   boot from the disc, the first partition, /dev/sdX1, must
		   start at cylinder 0. This means that /dev/sdX1 contains
		   the partition table and the boot block, as these are the
		   first two sectors of the disc. Therefore you must be
		   careful what you use /dev/sdX1 for. In particular, you must
		   not use a partition starting at cylinder 0 for Linux swap,
		   as that would overwrite the partition table and the boot
		   block. You may, however, use such a partition for a UFS
		   or EXT2 file system, as these file systems leave the first
		   1024 bytes undisturbed. */
		/* On the other hand, one should not use partitions
		   starting at block 0 in an md, or the label will
		   be trashed. */
		for (i = 0; i < partitions; i++)
			if (lens[i] && starts[i] <= first
			            && starts[i] + lens[i] > first)
				break;
		if (i < partitions && !whole_disk) {
			if (n == 2 && !first) {
			    whole_disk = 1;
			    break;
			}
			printf(_("Sector %d is already allocated\n"), first);
		} else
			break;
	}
	stop = cylinders * heads * sectors;	/* ancient */
	stop2 = stop;
	for (i = 0; i < partitions; i++) {
		if (starts[i] > first && starts[i] < stop)
			stop = starts[i];
	}
	snprintf(mesg, sizeof(mesg),
		 _("Last %s or +size or +sizeM or +sizeK"),
		 str_units(SINGULAR));
	if (whole_disk)
		last = read_int(scround(stop2), scround(stop2), scround(stop2),
				0, mesg);
	else if (n == 2 && !first)
		last = read_int(scround(first), scround(stop2), scround(stop2),
				scround(first), mesg);
	else
		last = read_int(scround(first), scround(stop), scround(stop),
				scround(first), mesg);
	if (display_in_cyl_units)
		last *= units_per_sector;
	if (n == 2 && !first) {
		if (last >= stop2) {
		    whole_disk = 1;
		    last = stop2;
		} else if (last > stop) {
		    printf (
   _("You haven't covered the whole disk with the 3rd partition, but your value\n"
     "%d %s covers some other partition. Your entry has been changed\n"
     "to %d %s\n"),
			scround(last), str_units(SINGULAR),
			scround(stop), str_units(SINGULAR));
		    last = stop;
		}
	} else if (!whole_disk && last > stop)
		last = stop;

	if (whole_disk) sys = WHOLE_DISK;
	set_sun_partition(n, first, last, sys);
}

void
sun_delete_partition(int i) {
	unsigned int nsec;

	if (i == 2 && sunlabel->infos[i].id == WHOLE_DISK && 
	    !sunlabel->partitions[i].start_cylinder && 
	    (nsec = SSWAP32(sunlabel->partitions[i].num_sectors))
	      == heads * sectors * cylinders)
		printf(_("If you want to maintain SunOS/Solaris compatibility, "
		       "consider leaving this\n"
		       "partition as Whole disk (5), starting at 0, with %u "
		       "sectors\n"), nsec);
	sunlabel->infos[i].id = 0;
	sunlabel->partitions[i].num_sectors = 0;
}

void
sun_change_sysid(int i, int sys) {
	if (sys == LINUX_SWAP && !sunlabel->partitions[i].start_cylinder) {
	    read_chars(
	      _("It is highly recommended that the partition at offset 0\n"
	      "is UFS, EXT2FS filesystem or SunOS swap. Putting Linux swap\n"
	      "there may destroy your partition table and bootblock.\n"
	      "Type YES if you're very sure you would like that partition\n"
	      "tagged with 82 (Linux swap): "));
	    if (strcmp (line_ptr, _("YES\n")))
		    return;
	}
	switch (sys) {
	case SUNOS_SWAP:
	case LINUX_SWAP:
		/* swaps are not mountable by default */
		sunlabel->infos[i].flags |= 0x01;
		break;
	default:
		/* assume other types are mountable;
		   user can change it anyway */
		sunlabel->infos[i].flags &= ~0x01;
		break;
	}
	sunlabel->infos[i].id = sys;
}

void
sun_list_table(int xtra) {
	int i, w;
	char *type;

	w = strlen(disk_device);
	if (xtra)
		printf(
		_("\nDisk %s (Sun disk label): %d heads, %d sectors, %d rpm\n"
		"%d cylinders, %d alternate cylinders, %d physical cylinders\n"
		"%d extra sects/cyl, interleave %d:1\n"
		"%s\n"
		"Units = %s of %d * 512 bytes\n\n"),
		       disk_device, heads, sectors, SSWAP16(sunlabel->rspeed),
		       cylinders, SSWAP16(sunlabel->nacyl),
		       SSWAP16(sunlabel->pcylcount),
		       SSWAP16(sunlabel->sparecyl),
		       SSWAP16(sunlabel->ilfact),
		       (char *)sunlabel,
		       str_units(PLURAL), units_per_sector);
	else
		printf(
	_("\nDisk %s (Sun disk label): %d heads, %d sectors, %d cylinders\n"
	"Units = %s of %d * 512 bytes\n\n"),
		       disk_device, heads, sectors, cylinders,
		       str_units(PLURAL), units_per_sector);

	printf(_("%*s Flag    Start       End    Blocks   Id  System\n"),
	       w + 1, _("Device"));
	for (i = 0 ; i < partitions; i++) {
		if (sunlabel->partitions[i].num_sectors) {
			__u32 start = SSWAP32(sunlabel->partitions[i].start_cylinder) * heads * sectors;
			__u32 len = SSWAP32(sunlabel->partitions[i].num_sectors);
			printf(
			    "%s %c%c %9ld %9ld %9ld%c  %2x  %s\n",
/* device */		  partname(disk_device, i+1, w),
/* flags */		  (sunlabel->infos[i].flags & 0x01) ? 'u' : ' ',
			  (sunlabel->infos[i].flags & 0x10) ? 'r' : ' ',
/* start */		  (long) scround(start),
/* end */		  (long) scround(start+len),
/* odd flag on end */	  (long) len / 2, len & 1 ? '+' : ' ',
/* type id */		  sunlabel->infos[i].id,
/* type name */		  (type = partition_type(sunlabel->infos[i].id))
			        ? type : _("Unknown"));
		}
	}
}

void
sun_set_alt_cyl(void) {
	sunlabel->nacyl =
		SSWAP16(read_int(0,SSWAP16(sunlabel->nacyl), 65535, 0,
				 _("Number of alternate cylinders")));
}

void
sun_set_ncyl(int cyl) {
	sunlabel->ncyl = SSWAP16(cyl);
}

void
sun_set_xcyl(void) {
	sunlabel->sparecyl =
		SSWAP16(read_int(0, SSWAP16(sunlabel->sparecyl), sectors, 0,
				 _("Extra sectors per cylinder")));
}

void
sun_set_ilfact(void) {
	sunlabel->ilfact =
		SSWAP16(read_int(1, SSWAP16(sunlabel->ilfact), 32, 0,
				 _("Interleave factor")));
}

void
sun_set_rspeed(void) {
	sunlabel->rspeed =
		SSWAP16(read_int(1, SSWAP16(sunlabel->rspeed), 100000, 0,
				 _("Rotation speed (rpm)")));
}

void
sun_set_pcylcount(void) {
	sunlabel->pcylcount =
		SSWAP16(read_int(0, SSWAP16(sunlabel->pcylcount), 65535, 0,
				 _("Number of physical cylinders")));
}

void
sun_write_table(void) {
	unsigned short *ush = (unsigned short *)sunlabel;
	unsigned short csum = 0;

	while(ush < (unsigned short *)(&sunlabel->csum))
		csum ^= *ush++;
	sunlabel->csum = csum;
	if (lseek(fd, 0, SEEK_SET) < 0)
		fatal(unable_to_seek);
	if (write(fd, sunlabel, SECTOR_SIZE) != SECTOR_SIZE)
		fatal(unable_to_write);
}
