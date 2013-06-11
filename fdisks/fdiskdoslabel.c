/*
 * Many, many hands.
 * Specific DOS label file  - Davidlohr Bueso <dave@gnu.org>
 */

#include <unistd.h>
#include <ctype.h>

#include "nls.h"
#include "xalloc.h"
#include "randutils.h"
#include "common.h"
#include "fdisk.h"
#include "fdiskdoslabel.h"


/*
 * in-memory fdisk GPT stuff
 */
struct fdisk_dos_label {
	struct fdisk_label	head;		/* generic part */

	unsigned int	compatible : 1;		/* is DOS compatible? */
};

/*
 * Partition types
 */
static struct fdisk_parttype dos_parttypes[] = {
	#include "dos_part_types.h"
};

#define set_hsc(h,s,c,sector) { \
		s = sector % cxt->geom.sectors + 1;			\
		sector /= cxt->geom.sectors;				\
		h = sector % cxt->geom.heads;				\
		sector /= cxt->geom.heads;				\
		c = sector & 0xff;					\
		s |= (sector >> 2) & 0xc0;				\
	}


#define sector(s)	((s) & 0x3f)
#define cylinder(s, c)	((c) | (((s) & 0xc0) << 2))

#define alignment_required(_x)	((_x)->grain != (_x)->sector_size)

struct pte ptes[MAXIMUM_PARTS];
sector_t extended_offset;

static size_t ext_index;

static int MBRbuffer_changed;

#define cround(c, n)	(fdisk_context_use_cylinders(c) ? \
				((n) / fdisk_context_get_units_per_sector(c)) + 1 : (n))

static void warn_alignment(struct fdisk_context *cxt)
{
	if (nowarn)
		return;

	if (cxt->sector_size != cxt->phy_sector_size)
		fprintf(stderr, _("\n"
"The device presents a logical sector size that is smaller than\n"
"the physical sector size. Aligning to a physical sector (or optimal\n"
"I/O) size boundary is recommended, or performance may be impacted.\n"));

	if (is_dos_compatible(cxt))
		fprintf(stderr, _("\n"
"WARNING: DOS-compatible mode is deprecated. It's strongly recommended to\n"
"         switch off the mode (with command 'c')."));

	if (fdisk_context_use_cylinders(cxt))
		fprintf(stderr, _("\n"
"WARNING: cylinders as display units are deprecated. Use command 'u' to\n"
"         change units to sectors.\n"));

}

static int get_partition_unused_primary(struct fdisk_context *cxt)
{
	size_t orgmax = cxt->label->nparts_max;
	size_t n;
	int rc;

	cxt->label->nparts_max = 4;
	rc = fdisk_ask_partnum(cxt, &n, TRUE);
	cxt->label->nparts_max = orgmax;

	switch (rc) {
	case 1:
		fdisk_info(cxt, _("All primary partitions have been defined already"));
		return -1;
	case 0:
		return n;
	default:
		return rc;
	}
}


/* Allocate a buffer and read a partition table sector */
static void read_pte(struct fdisk_context *cxt, int pno, sector_t offset)
{
	struct pte *pe = &ptes[pno];

	pe->offset = offset;
	pe->sectorbuffer = xcalloc(1, cxt->sector_size);

	if (read_sector(cxt, offset, pe->sectorbuffer) != 0)
		fprintf(stderr, _("Failed to read extended partition table (offset=%jd)\n"),
					(uintmax_t) offset);
	pe->changed = 0;
	pe->part_table = pe->ext_pointer = NULL;
}

static void mbr_set_id(unsigned char *b, unsigned int id)
{
	store4_little_endian(&b[440], id);
}

static void mbr_set_magic(unsigned char *b)
{
	b[510] = 0x55;
	b[511] = 0xaa;
}

int mbr_is_valid_magic(unsigned char *b)
{
	return (b[510] == 0x55 && b[511] == 0xaa);
}

static unsigned int mbr_get_id(const unsigned char *b)
{
	return read4_little_endian(&b[440]);
}

static void clear_partition(struct partition *p)
{
	if (!p)
		return;
	p->boot_ind = 0;
	p->head = 0;
	p->sector = 0;
	p->cyl = 0;
	p->sys_ind = 0;
	p->end_head = 0;
	p->end_sector = 0;
	p->end_cyl = 0;
	set_start_sect(p,0);
	set_nr_sects(p,0);
}

void dos_init(struct fdisk_context *cxt)
{
	size_t i;

	cxt->label->nparts_max = 4;	/* default, unlimited number of logical */
	ext_index = 0;
	extended_offset = 0;

	for (i = 0; i < 4; i++) {
		struct pte *pe = &ptes[i];

		pe->part_table = pt_offset(cxt->firstsector, i);
		pe->ext_pointer = NULL;
		pe->offset = 0;
		pe->sectorbuffer = cxt->firstsector;
		pe->changed = 0;
	}

	warn_geometry(cxt);
	warn_limits(cxt);
	warn_alignment(cxt);
}

static int dos_delete_partition(struct fdisk_context *cxt, size_t partnum)
{
	struct pte *pe = &ptes[partnum];
	struct partition *p = pe->part_table;
	struct partition *q = pe->ext_pointer;

	/* Note that for the fifth partition (partnum == 4) we don't actually
	   decrement partitions. */

	if (partnum < 4) {
		if (IS_EXTENDED(p->sys_ind) && partnum == ext_index) {
			cxt->label->nparts_max = 4;
			ptes[ext_index].ext_pointer = NULL;
			extended_offset = 0;
		}
		ptes[partnum].changed = 1;
		clear_partition(p);
	} else if (!q->sys_ind && partnum > 4) {
		/* the last one in the chain - just delete */
		--cxt->label->nparts_max;
		--partnum;
		clear_partition(ptes[partnum].ext_pointer);
		ptes[partnum].changed = 1;
	} else {
		/* not the last one - further ones will be moved down */
		if (partnum > 4) {
			/* delete this link in the chain */
			p = ptes[partnum-1].ext_pointer;
			*p = *q;
			set_start_sect(p, get_start_sect(q));
			set_nr_sects(p, get_nr_sects(q));
			ptes[partnum-1].changed = 1;
		} else if (cxt->label->nparts_max > 5) {    /* 5 will be moved to 4 */
			/* the first logical in a longer chain */
			struct pte *pete = &ptes[5];

			if (pete->part_table) /* prevent SEGFAULT */
				set_start_sect(pete->part_table,
					       get_partition_start(pete) -
					       extended_offset);
			pete->offset = extended_offset;
			pete->changed = 1;
		}

		if (cxt->label->nparts_max > 5) {
			cxt->label->nparts_max--;
			while (partnum < cxt->label->nparts_max) {
				ptes[partnum] = ptes[partnum+1];
				partnum++;
			}
		} else
			/* the only logical: clear only */
			clear_partition(ptes[partnum].part_table);
	}

	fdisk_label_set_changed(cxt->label, 1);
	return 0;
}

static void read_extended(struct fdisk_context *cxt, int ext)
{
	size_t i;
	struct pte *pex;
	struct partition *p, *q;

	ext_index = ext;
	pex = &ptes[ext];
	pex->ext_pointer = pex->part_table;

	p = pex->part_table;
	if (!get_start_sect(p)) {
		fprintf(stderr,
			_("Bad offset in primary extended partition\n"));
		return;
	}

	while (IS_EXTENDED (p->sys_ind)) {
		struct pte *pe = &ptes[cxt->label->nparts_max];

		if (cxt->label->nparts_max >= MAXIMUM_PARTS) {
			/* This is not a Linux restriction, but
			   this program uses arrays of size MAXIMUM_PARTS.
			   Do not try to `improve' this test. */
			struct pte *pre = &ptes[cxt->label->nparts_max - 1];

			fprintf(stderr,
				_("Warning: omitting partitions after #%zd.\n"
				  "They will be deleted "
				  "if you save this partition table.\n"),
				cxt->label->nparts_max);
			clear_partition(pre->ext_pointer);
			pre->changed = 1;
			return;
		}

		read_pte(cxt, cxt->label->nparts_max, extended_offset + get_start_sect(p));

		if (!extended_offset)
			extended_offset = get_start_sect(p);

		q = p = pt_offset(pe->sectorbuffer, 0);
		for (i = 0; i < 4; i++, p++) if (get_nr_sects(p)) {
			if (IS_EXTENDED (p->sys_ind)) {
				if (pe->ext_pointer)
					fprintf(stderr,
						_("Warning: extra link "
						  "pointer in partition table"
						  " %zd\n"), cxt->label->nparts_max + 1);
				else
					pe->ext_pointer = p;
			} else if (p->sys_ind) {
				if (pe->part_table)
					fprintf(stderr,
						_("Warning: ignoring extra "
						  "data in partition table"
						  " %zd\n"), cxt->label->nparts_max + 1);
				else
					pe->part_table = p;
			}
		}

		/* very strange code here... */
		if (!pe->part_table) {
			if (q != pe->ext_pointer)
				pe->part_table = q;
			else
				pe->part_table = q + 1;
		}
		if (!pe->ext_pointer) {
			if (q != pe->part_table)
				pe->ext_pointer = q;
			else
				pe->ext_pointer = q + 1;
		}

		p = pe->ext_pointer;
		cxt->label->nparts_cur = ++cxt->label->nparts_max;
	}

	/* remove empty links */
 remove:
	for (i = 4; i < cxt->label->nparts_max; i++) {
		struct pte *pe = &ptes[i];

		if (!get_nr_sects(pe->part_table) &&
		    (cxt->label->nparts_max > 5 || ptes[4].part_table->sys_ind)) {
			printf(_("omitting empty partition (%zd)\n"), i+1);
			dos_delete_partition(cxt, i);
			goto remove; 	/* numbering changed */
		}
	}
}

void dos_print_mbr_id(struct fdisk_context *cxt)
{
	printf(_("Disk identifier: 0x%08x\n"), mbr_get_id(cxt->firstsector));
}

static int dos_create_disklabel(struct fdisk_context *cxt)
{
	unsigned int id;

	assert(cxt);
	assert(cxt->label);
	assert(fdisk_is_disklabel(cxt, DOS));

	/* random disk signature */
	random_get_bytes(&id, sizeof(id));

	fprintf(stderr, _("Building a new DOS disklabel with disk identifier 0x%08x.\n"), id);

	dos_init(cxt);
	fdisk_zeroize_firstsector(cxt);
	fdisk_label_set_changed(cxt->label, 1);

	/* Generate an MBR ID for this disk */
	mbr_set_id(cxt->firstsector, id);

	/* Put MBR signature */
	mbr_set_magic(cxt->firstsector);
	return 0;
}

int dos_set_mbr_id(struct fdisk_context *cxt)
{
	char *end = NULL, *str = NULL;
	unsigned int id, old;
	int rc;

	old = mbr_get_id(cxt->firstsector);
	rc = fdisk_ask_string(cxt,
			_("Enter of the new disk identifier"), &str);
	if (rc)
		return rc;

	errno = 0;
	id = strtoul(str, &end, 0);
	if (errno || str == end || (end && *end)) {
		fdisk_warnx(cxt, _("Incorrect value."));
		return -EINVAL;
	}

	fdisk_info(cxt, _("Changing disk identifier from 0x%08x to 0x%08x."),
			old, id);

	mbr_set_id(cxt->firstsector, id);
	MBRbuffer_changed = 1;
	fdisk_label_set_changed(cxt->label, 1);
	return 0;
}

static void get_partition_table_geometry(struct fdisk_context *cxt,
			unsigned int *ph, unsigned int *ps)
{
	unsigned char *bufp = cxt->firstsector;
	struct partition *p;
	int i, h, s, hh, ss;
	int first = 1;
	int bad = 0;

	hh = ss = 0;
	for (i=0; i<4; i++) {
		p = pt_offset(bufp, i);
		if (p->sys_ind != 0) {
			h = p->end_head + 1;
			s = (p->end_sector & 077);
			if (first) {
				hh = h;
				ss = s;
				first = 0;
			} else if (hh != h || ss != s)
				bad = 1;
		}
	}

	if (!first && !bad) {
		*ph = hh;
		*ps = ss;
	}

	DBG(CONTEXT, dbgprint("DOS PT geometry: heads=%u, sectors=%u", *ph, *ps));
}

static int dos_reset_alignment(struct fdisk_context *cxt)
{
	assert(cxt);
	assert(cxt->label);
	assert(fdisk_is_disklabel(cxt, DOS));

	/* overwrite necessary stuff by DOS deprecated stuff */
	if (is_dos_compatible(cxt)) {
		if (cxt->geom.sectors)
			cxt->first_lba = cxt->geom.sectors;	/* usually 63 */

		cxt->grain = cxt->sector_size;			/* usually 512 */
	}

	return 0;
}

/* TODO: move to include/pt-dos.h and share with libblkid */
#define AIX_MAGIC_STRING	"\xC9\xC2\xD4\xC1"
#define AIX_MAGIC_STRLEN	(sizeof(AIX_MAGIC_STRING) - 1)

static int dos_probe_label(struct fdisk_context *cxt)
{
	size_t i;
	unsigned int h = 0, s = 0;

	assert(cxt);
	assert(cxt->label);
	assert(fdisk_is_disklabel(cxt, DOS));

	/* ignore disks with AIX magic number */
	if (memcmp(cxt->firstsector, AIX_MAGIC_STRING, AIX_MAGIC_STRLEN) == 0)
		return 0;

	if (!mbr_is_valid_magic(cxt->firstsector))
		return 0;

	dos_init(cxt);

	get_partition_table_geometry(cxt, &h, &s);
	if (h && s) {
		cxt->geom.heads = h;
	        cxt->geom.sectors = s;
	}

	for (i = 0; i < 4; i++) {
		struct pte *pe = &ptes[i];

		if (!is_cleared_partition(pe->part_table))
			cxt->label->nparts_cur++;

		if (IS_EXTENDED (pe->part_table->sys_ind)) {
			if (cxt->label->nparts_max != 4)
				fprintf(stderr, _("Ignoring extra extended "
					"partition %zd\n"), i + 1);
			else
				read_extended(cxt, i);
		}
	}

	for (i = 3; i < cxt->label->nparts_max; i++) {
		struct pte *pe = &ptes[i];

		if (!mbr_is_valid_magic(pe->sectorbuffer)) {
			fprintf(stderr,
				_("Warning: invalid flag 0x%04x of partition "
				"table %zd will be corrected by w(rite)\n"),
				part_table_flag(pe->sectorbuffer), i + 1);
			pe->changed = 1;
			fdisk_label_set_changed(cxt->label, 1);
		}
	}

	return 1;
}

/*
 * Avoid warning about DOS partitions when no DOS partition was changed.
 * Here a heuristic "is probably dos partition".
 * We might also do the opposite and warn in all cases except
 * for "is probably nondos partition".
 */
static int is_dos_partition(int t)
{
	return (t == 1 || t == 4 || t == 6 ||
		t == 0x0b || t == 0x0c || t == 0x0e ||
		t == 0x11 || t == 0x12 || t == 0x14 || t == 0x16 ||
		t == 0x1b || t == 0x1c || t == 0x1e || t == 0x24 ||
		t == 0xc1 || t == 0xc4 || t == 0xc6);
}

static void set_partition(struct fdisk_context *cxt,
			  int i, int doext, sector_t start,
			  sector_t stop, int sysid)
{
	struct partition *p;
	sector_t offset;

	if (doext) {
		p = ptes[i].ext_pointer;
		offset = extended_offset;
	} else {
		p = ptes[i].part_table;
		offset = ptes[i].offset;
	}
	p->boot_ind = 0;
	p->sys_ind = sysid;
	set_start_sect(p, start - offset);
	set_nr_sects(p, stop - start + 1);

	if (!doext) {
		struct fdisk_parttype *t = fdisk_get_parttype_from_code(cxt, sysid);
		fdisk_info_new_partition(cxt, i + 1, start, stop, t);
	}
	if (is_dos_compatible(cxt) && (start/(cxt->geom.sectors*cxt->geom.heads) > 1023))
		start = cxt->geom.heads*cxt->geom.sectors*1024 - 1;
	set_hsc(p->head, p->sector, p->cyl, start);
	if (is_dos_compatible(cxt) && (stop/(cxt->geom.sectors*cxt->geom.heads) > 1023))
		stop = cxt->geom.heads*cxt->geom.sectors*1024 - 1;
	set_hsc(p->end_head, p->end_sector, p->end_cyl, stop);
	ptes[i].changed = 1;
}

static sector_t get_unused_start(struct fdisk_context *cxt,
				 int part_n, sector_t start,
				 sector_t first[], sector_t last[])
{
	size_t i;

	for (i = 0; i < cxt->label->nparts_max; i++) {
		sector_t lastplusoff;

		if (start == ptes[i].offset)
			start += cxt->first_lba;
		lastplusoff = last[i] + ((part_n < 4) ? 0 : cxt->first_lba);
		if (start >= first[i] && start <= lastplusoff)
			start = lastplusoff + 1;
	}

	return start;
}

static void fill_bounds(struct fdisk_context *cxt,
			sector_t *first, sector_t *last)
{
	size_t i;
	struct pte *pe = &ptes[0];
	struct partition *p;

	for (i = 0; i < cxt->label->nparts_max; pe++,i++) {
		p = pe->part_table;
		if (!p->sys_ind || IS_EXTENDED (p->sys_ind)) {
			first[i] = 0xffffffff;
			last[i] = 0;
		} else {
			first[i] = get_partition_start(pe);
			last[i] = first[i] + get_nr_sects(p) - 1;
		}
	}
}

static int add_partition(struct fdisk_context *cxt, int n, struct fdisk_parttype *t)
{
	int sys, read = 0, rc;
	size_t i;
	struct partition *p = ptes[n].part_table;
	struct partition *q = ptes[ext_index].part_table;
	sector_t start, stop = 0, limit, temp,
		first[cxt->label->nparts_max],
		last[cxt->label->nparts_max];

	sys = t ? t->type : LINUX_NATIVE;

	if (p && p->sys_ind) {
		printf(_("Partition %d is already defined.  Delete "
			 "it before re-adding it.\n"), n + 1);
		return -EINVAL;
	}
	fill_bounds(cxt, first, last);
	if (n < 4) {
		start = cxt->first_lba;
		if (fdisk_context_use_cylinders(cxt) || !cxt->total_sectors)
			limit = cxt->geom.heads * cxt->geom.sectors * cxt->geom.cylinders - 1;
		else
			limit = cxt->total_sectors - 1;

		if (limit > UINT_MAX)
			limit = UINT_MAX;

		if (extended_offset) {
			first[ext_index] = extended_offset;
			last[ext_index] = get_start_sect(q) +
				get_nr_sects(q) - 1;
		}
	} else {
		start = extended_offset + cxt->first_lba;
		limit = get_start_sect(q) + get_nr_sects(q) - 1;
	}
	if (fdisk_context_use_cylinders(cxt))
		for (i = 0; i < cxt->label->nparts_max; i++)
			first[i] = (cround(cxt, first[i]) - 1) * fdisk_context_get_units_per_sector(cxt);

	/*
	 * Ask for first sector
	 */
	do {
		sector_t dflt, aligned;

		temp = start;
		dflt = start = get_unused_start(cxt, n, start, first, last);

		/* the default sector should be aligned and unused */
		do {
			aligned = fdisk_align_lba_in_range(cxt, dflt, dflt, limit);
			dflt = get_unused_start(cxt, n, aligned, first, last);
		} while (dflt != aligned && dflt > aligned && dflt < limit);

		if (dflt >= limit)
			dflt = start;
		if (start > limit)
			break;
		if (start >= temp+fdisk_context_get_units_per_sector(cxt) && read) {
			printf(_("Sector %llu is already allocated\n"), temp);
			temp = start;
			read = 0;
		}

		if (!read && start == temp) {
			sector_t j = start;
			struct fdisk_ask *ask = fdisk_new_ask();

			if (fdisk_context_use_cylinders(cxt))
				fdisk_ask_set_query(ask, _("First cylinder"));
			else
				fdisk_ask_set_query(ask, _("First sector"));

			fdisk_ask_set_type(ask, FDISK_ASKTYPE_NUMBER);
			fdisk_ask_number_set_low(ask, cround(cxt, j));
			fdisk_ask_number_set_default(ask, cround(cxt, dflt));
			fdisk_ask_number_set_high(ask, cround(cxt, limit));

			rc = fdisk_do_ask(cxt, ask);
			if (!rc)
				start = fdisk_ask_number_get_result(ask);
			fdisk_free_ask(ask);
			if (rc)
				return rc;

			if (fdisk_context_use_cylinders(cxt)) {
				start = (start - 1) * fdisk_context_get_units_per_sector(cxt);
				if (start < j)
					start = j;
			}
			read = 1;
		}
	} while (start != temp || !read);

	if (n > 4) {			/* NOT for fifth partition */
		struct pte *pe = &ptes[n];

		pe->offset = start - cxt->first_lba;
		if (pe->offset == extended_offset) { /* must be corrected */
			pe->offset++;
			if (cxt->first_lba == 1)
				start++;
		}
	}

	for (i = 0; i < cxt->label->nparts_max; i++) {
		struct pte *pe = &ptes[i];

		if (start < pe->offset && limit >= pe->offset)
			limit = pe->offset - 1;
		if (start < first[i] && limit >= first[i])
			limit = first[i] - 1;
	}
	if (start > limit) {
		printf(_("No free sectors available\n"));
		if (n > 4)
			cxt->label->nparts_max--;
		return -ENOSPC;
	}
	if (cround(cxt, start) == cround(cxt, limit)) {
		stop = limit;
	} else {
		/*
		 * Ask for last sector
		 */
		struct fdisk_ask *ask = fdisk_new_ask();

		fdisk_ask_set_type(ask, FDISK_ASKTYPE_OFFSET);

		if (fdisk_context_use_cylinders(cxt)) {
			fdisk_ask_set_query(ask, _("Last cylinder, +cylinders or +size{K,M,G,T,P}"));
			fdisk_ask_number_set_unit(ask,
				     cxt->sector_size *
				     fdisk_context_get_units_per_sector(cxt));
		} else {
			fdisk_ask_set_query(ask, _("Last sector, +sectors or +size{K,M,G,T,P}"));
			fdisk_ask_number_set_unit(ask,cxt->sector_size);
		}

		fdisk_ask_number_set_low(ask, cround(cxt, start));
		fdisk_ask_number_set_default(ask, cround(cxt, limit));
		fdisk_ask_number_set_high(ask, cround(cxt, limit));
		fdisk_ask_number_set_base(ask, cround(cxt, start));	/* base for relative input */

		rc = fdisk_do_ask(cxt, ask);
		if (rc) {
			fdisk_free_ask(ask);
			return rc;
		}

		stop = fdisk_ask_number_get_result(ask);

		if (fdisk_ask_number_is_relative(ask)
		    && alignment_required(cxt)) {
			/* the last sector has not been exactly requested (but
			 * defined by +size{K,M,G} convention), so be smart and
			 * align the end of the partition. The next partition
			 * will start at phy.block boundary.
			 */
			stop = fdisk_align_lba_in_range(cxt, stop, start, limit) - 1;
			if (stop > limit)
				stop = limit;
		}
		fdisk_free_ask(ask);
	}

	set_partition(cxt, n, 0, start, stop, sys);
	if (n > 4)
		set_partition(cxt, n - 1, 1, ptes[n].offset, stop, EXTENDED);

	if (IS_EXTENDED (sys)) {
		struct pte *pe4 = &ptes[4];
		struct pte *pen = &ptes[n];

		ext_index = n;
		pen->ext_pointer = p;
		pe4->offset = extended_offset = start;
		pe4->sectorbuffer = xcalloc(1, cxt->sector_size);
		pe4->part_table = pt_offset(pe4->sectorbuffer, 0);
		pe4->ext_pointer = pe4->part_table + 1;
		pe4->changed = 1;
		cxt->label->nparts_max = 5;
	}

	fdisk_label_set_changed(cxt->label, 1);
	return 0;
}

static int add_logical(struct fdisk_context *cxt)
{
	assert(cxt);
	assert(cxt->label);

	if (cxt->label->nparts_max > 5 || ptes[4].part_table->sys_ind) {
		struct pte *pe = &ptes[cxt->label->nparts_max];

		pe->sectorbuffer = xcalloc(1, cxt->sector_size);
		pe->part_table = pt_offset(pe->sectorbuffer, 0);
		pe->ext_pointer = pe->part_table + 1;
		pe->offset = 0;
		pe->changed = 1;
		cxt->label->nparts_max++;
	}
	printf(_("Adding logical partition %zd\n"), cxt->label->nparts_max);
	return add_partition(cxt, cxt->label->nparts_max - 1, NULL);
}

static void check(struct fdisk_context *cxt, size_t n,
	   unsigned int h, unsigned int s, unsigned int c,
	   unsigned int start)
{
	unsigned int total, real_s, real_c;

	real_s = sector(s) - 1;
	real_c = cylinder(s, c);
	total = (real_c * cxt->geom.sectors + real_s) * cxt->geom.heads + h;
	if (!total)
		fprintf(stderr, _("Warning: partition %zd contains sector 0\n"), n);
	if (h >= cxt->geom.heads)
		fprintf(stderr,
			_("Partition %zd: head %d greater than maximum %d\n"),
			n, h + 1, cxt->geom.heads);
	if (real_s >= cxt->geom.sectors)
		fprintf(stderr, _("Partition %zd: sector %d greater than "
			"maximum %llu\n"), n, s, cxt->geom.sectors);
	if (real_c >= cxt->geom.cylinders)
		fprintf(stderr, _("Partition %zd: cylinder %d greater than "
			"maximum %llu\n"), n, real_c + 1, cxt->geom.cylinders);
	if (cxt->geom.cylinders <= 1024 && start != total)
		fprintf(stderr,
			_("Partition %zd: previous sectors %d disagrees with "
			"total %d\n"), n, start, total);
}

/* check_consistency() and long2chs() added Sat Mar 6 12:28:16 1993,
 * faith@cs.unc.edu, based on code fragments from pfdisk by Gordon W. Ross,
 * Jan.  1990 (version 1.2.1 by Gordon W. Ross Aug. 1990; Modified by S.
 * Lubkin Oct.  1991). */

static void
long2chs(struct fdisk_context *cxt, unsigned long ls,
	 unsigned int *c, unsigned int *h, unsigned int *s) {
	int spc = cxt->geom.heads * cxt->geom.sectors;

	*c = ls / spc;
	ls = ls % spc;
	*h = ls / cxt->geom.sectors;
	*s = ls % cxt->geom.sectors + 1;	/* sectors count from 1 */
}

static void check_consistency(struct fdisk_context *cxt, struct partition *p,
			      size_t partition)
{
	unsigned int pbc, pbh, pbs;	/* physical beginning c, h, s */
	unsigned int pec, peh, pes;	/* physical ending c, h, s */
	unsigned int lbc, lbh, lbs;	/* logical beginning c, h, s */
	unsigned int lec, leh, les;	/* logical ending c, h, s */

	if (!is_dos_compatible(cxt))
		return;

	if (!cxt->geom.heads || !cxt->geom.sectors || (partition >= 4))
		return;		/* do not check extended partitions */

/* physical beginning c, h, s */
	pbc = (p->cyl & 0xff) | ((p->sector << 2) & 0x300);
	pbh = p->head;
	pbs = p->sector & 0x3f;

/* physical ending c, h, s */
	pec = (p->end_cyl & 0xff) | ((p->end_sector << 2) & 0x300);
	peh = p->end_head;
	pes = p->end_sector & 0x3f;

/* compute logical beginning (c, h, s) */
	long2chs(cxt, get_start_sect(p), &lbc, &lbh, &lbs);

/* compute logical ending (c, h, s) */
	long2chs(cxt, get_start_sect(p) + get_nr_sects(p) - 1, &lec, &leh, &les);

/* Same physical / logical beginning? */
	if (cxt->geom.cylinders <= 1024 && (pbc != lbc || pbh != lbh || pbs != lbs)) {
		printf(_("Partition %zd has different physical/logical "
			"beginnings (non-Linux?):\n"), partition + 1);
		printf(_("     phys=(%d, %d, %d) "), pbc, pbh, pbs);
		printf(_("logical=(%d, %d, %d)\n"),lbc, lbh, lbs);
	}

/* Same physical / logical ending? */
	if (cxt->geom.cylinders <= 1024 && (pec != lec || peh != leh || pes != les)) {
		printf(_("Partition %zd has different physical/logical "
			"endings:\n"), partition + 1);
		printf(_("     phys=(%d, %d, %d) "), pec, peh, pes);
		printf(_("logical=(%d, %d, %d)\n"),lec, leh, les);
	}

/* Ending on cylinder boundary? */
	if (peh != (cxt->geom.heads - 1) || pes != cxt->geom.sectors) {
		printf(_("Partition %zd does not end on cylinder boundary.\n"),
			partition + 1);
	}
}

static int dos_verify_disklabel(struct fdisk_context *cxt)
{
	size_t i, j;
	sector_t total = 1, n_sectors = cxt->total_sectors;
	unsigned long long first[cxt->label->nparts_max],
			   last[cxt->label->nparts_max];
	struct partition *p;

	assert(cxt);
	assert(cxt->label);
	assert(fdisk_is_disklabel(cxt, DOS));

	fill_bounds(cxt, first, last);
	for (i = 0; i < cxt->label->nparts_max; i++) {
		struct pte *pe = &ptes[i];

		p = pe->part_table;
		if (p->sys_ind && !IS_EXTENDED (p->sys_ind)) {
			check_consistency(cxt, p, i);
			fdisk_warn_alignment(cxt, get_partition_start(pe), i);
			if (get_partition_start(pe) < first[i])
				printf(_("Warning: bad start-of-data in "
					 "partition %zd\n"), i + 1);
			check(cxt, i + 1, p->end_head, p->end_sector, p->end_cyl,
			      last[i]);
			total += last[i] + 1 - first[i];
			for (j = 0; j < i; j++)
				if ((first[i] >= first[j] && first[i] <= last[j])
				    || ((last[i] <= last[j] && last[i] >= first[j]))) {
					printf(_("Warning: partition %zd overlaps "
						 "partition %zd.\n"), j + 1, i + 1);
					total += first[i] >= first[j] ?
						first[i] : first[j];
					total -= last[i] <= last[j] ?
						last[i] : last[j];
				}
		}
	}

	if (extended_offset) {
		struct pte *pex = &ptes[ext_index];
		sector_t e_last = get_start_sect(pex->part_table) +
			get_nr_sects(pex->part_table) - 1;

		for (i = 4; i < cxt->label->nparts_max; i++) {
			total++;
			p = ptes[i].part_table;
			if (!p->sys_ind) {
				if (i != 4 || i + 1 < cxt->label->nparts_max)
					printf(_("Warning: partition %zd "
						 "is empty\n"), i + 1);
			}
			else if (first[i] < extended_offset ||
					last[i] > e_last)
				printf(_("Logical partition %zd not entirely in "
					"partition %zd\n"), i + 1, ext_index + 1);
		}
	}

	if (total > n_sectors)
		printf(_("Total allocated sectors %llu greater than the maximum"
			 " %llu\n"), total, n_sectors);
	else if (total < n_sectors)
		printf(_("Remaining %lld unallocated %ld-byte sectors\n"),
		       n_sectors - total, cxt->sector_size);

	return 0;
}

/*
 * Ask the user for new partition type information (logical, extended).
 * This function calls the actual partition adding logic - add_partition.
 *
 * API callback.
 */
static int dos_add_partition(
			struct fdisk_context *cxt,
			size_t partnum __attribute__ ((__unused__)),
			struct fdisk_parttype *t)
{
	size_t i, free_primary = 0;
	int rc = 0;

	assert(cxt);
	assert(cxt->label);
	assert(fdisk_is_disklabel(cxt, DOS));

	for (i = 0; i < 4; i++)
		free_primary += !ptes[i].part_table->sys_ind;

	if (!free_primary && cxt->label->nparts_max >= MAXIMUM_PARTS) {
		printf(_("The maximum number of partitions has been created\n"));
		return -EINVAL;
	}
	rc = 1;
	if (!free_primary) {
		if (extended_offset) {
			printf(_("All primary partitions are in use\n"));
			rc = add_logical(cxt);
		} else
			printf(_("If you want to create more than four partitions, you must replace a\n"
				 "primary partition with an extended partition first.\n"));
	} else if (cxt->label->nparts_max >= MAXIMUM_PARTS) {
		int j;

		printf(_("All logical partitions are in use\n"));
		printf(_("Adding a primary partition\n"));

		j = get_partition_unused_primary(cxt);
		if (j >= 0)
			rc = add_partition(cxt, j, t);
	} else {
		char buf[16];
		char c, prompt[LINE_LENGTH];
		int dflt;

		dflt = (free_primary == 1 && !extended_offset) ? 'e' : 'p';
		snprintf(prompt, sizeof(prompt),
			 _("Partition type:\n"
			   "   p   primary (%zd primary, %d extended, %zd free)\n"
			   "%s\n"
			   "Select (default %c): "),
			 4 - (extended_offset ? 1 : 0) - free_primary,
			 extended_offset ? 1 : 0, free_primary,
			 extended_offset ? _("   l   logical (numbered from 5)") : _("   e   extended"),
			 dflt);

		rc = get_user_reply(cxt, prompt, buf, sizeof(buf));
		if (rc)
			return rc;
		if (!buf[0]) {
			c = dflt;
			printf(_("Using default response %c\n"), c);
		} else
			c = tolower(buf[0]);

		if (c == 'p') {
			int j = get_partition_unused_primary(cxt);
			if (j >= 0)
				rc = add_partition(cxt, j, t);
			goto done;
		} else if (c == 'l' && extended_offset) {
			rc = add_logical(cxt);
			goto done;
		} else if (c == 'e' && !extended_offset) {
			int j = get_partition_unused_primary(cxt);
			if (j >= 0) {
				t = fdisk_get_parttype_from_code(cxt, EXTENDED);
				rc = add_partition(cxt, j, t);
			}
			goto done;
		} else
			printf(_("Invalid partition type `%c'\n"), c);
	}
done:
	if (rc == 0)
		cxt->label->nparts_cur++;
	return rc;
}

static int write_sector(struct fdisk_context *cxt, sector_t secno,
			       unsigned char *buf)
{
	int rc;

	rc = seek_sector(cxt, secno);
	if (rc != 0) {
		fprintf(stderr, _("write sector %jd failed: seek failed"),
				(uintmax_t) secno);
		return rc;
	}
	if (write(cxt->dev_fd, buf, cxt->sector_size) != (ssize_t) cxt->sector_size)
		return -errno;
	return 0;
}

static int dos_write_disklabel(struct fdisk_context *cxt)
{
	size_t i;
	int rc = 0;

	assert(cxt);
	assert(cxt->label);
	assert(fdisk_is_disklabel(cxt, DOS));

	/* MBR (primary partitions) */
	if (!MBRbuffer_changed) {
		for (i = 0; i < 4; i++)
			if (ptes[i].changed)
				MBRbuffer_changed = 1;
	}
	if (MBRbuffer_changed) {
		mbr_set_magic(cxt->firstsector);
		rc = write_sector(cxt, 0, cxt->firstsector);
		if (rc)
			goto done;
	}
	/* EBR (logical partitions) */
	for (i = 4; i < cxt->label->nparts_max; i++) {
		struct pte *pe = &ptes[i];

		if (pe->changed) {
			mbr_set_magic(pe->sectorbuffer);
			rc = write_sector(cxt, pe->offset, pe->sectorbuffer);
			if (rc)
				goto done;
		}
	}

done:
	return rc;
}

static struct fdisk_parttype *dos_get_parttype(
		struct fdisk_context *cxt,
		size_t partnum)
{
	struct fdisk_parttype *t;
	struct partition *p;

	assert(cxt);
	assert(cxt->label);
	assert(fdisk_is_disklabel(cxt, DOS));

	if (partnum >= cxt->label->nparts_max)
		return NULL;

	p = ptes[partnum].part_table;
	t = fdisk_get_parttype_from_code(cxt, p->sys_ind);
	if (!t)
		t = fdisk_new_unknown_parttype(p->sys_ind, NULL);
	return t;
}

static int dos_set_parttype(
		struct fdisk_context *cxt,
		size_t partnum,
		struct fdisk_parttype *t)
{
	struct partition *p;

	assert(cxt);
	assert(cxt->label);
	assert(fdisk_is_disklabel(cxt, DOS));

	if (partnum >= cxt->label->nparts_max || !t || t->type > UINT8_MAX)
		return -EINVAL;

	p = ptes[partnum].part_table;
	if (t->type == p->sys_ind)
		return 0;

	if (IS_EXTENDED(p->sys_ind) || IS_EXTENDED(t->type)) {
		printf(_("\nYou cannot change a partition into an extended one "
			 "or vice versa.\nDelete it first.\n\n"));
		return -EINVAL;
	}

	if (is_dos_partition(t->type) || is_dos_partition(p->sys_ind))
	    printf(
		_("\nWARNING: If you have created or modified any DOS 6.x "
		"partitions, please see the fdisk manual page for additional "
		"information.\n\n"));

	ptes[partnum].changed = 1;
	p->sys_ind = t->type;
	fdisk_label_set_changed(cxt->label, 1);
	return 0;
}

/*
 * Check whether partition entries are ordered by their starting positions.
 * Return 0 if OK. Return i if partition i should have been earlier.
 * Two separate checks: primary and logical partitions.
 */
static int wrong_p_order(struct fdisk_context *cxt, size_t *prev)
{
	struct pte *pe;
	struct partition *p;
	size_t last_p_start_pos = 0, p_start_pos;
	size_t i, last_i = 0;

	for (i = 0 ; i < cxt->label->nparts_max; i++) {
		if (i == 4) {
			last_i = 4;
			last_p_start_pos = 0;
		}
		pe = &ptes[i];
		if ((p = pe->part_table)->sys_ind) {
			p_start_pos = get_partition_start(pe);

			if (last_p_start_pos > p_start_pos) {
				if (prev)
					*prev = last_i;
				return i;
			}

			last_p_start_pos = p_start_pos;
			last_i = i;
		}
	}
	return 0;
}

static int is_garbage_table(void)
{
	size_t i;

	for (i = 0; i < 4; i++) {
		struct pte *pe = &ptes[i];
		struct partition *p = pe->part_table;

		if (p->boot_ind != 0 && p->boot_ind != 0x80)
			return 1;
	}
	return 0;
}

int dos_list_table(struct fdisk_context *cxt,
		    int xtra  __attribute__ ((__unused__)))
{
	struct partition *p;
	size_t i, w;

	assert(cxt);
	assert(cxt->label);
	assert(fdisk_is_disklabel(cxt, DOS));

	if (is_garbage_table()) {
		printf(_("This doesn't look like a partition table\n"
			 "Probably you selected the wrong device.\n\n"));
	}

	/* Heuristic: we list partition 3 of /dev/foo as /dev/foo3,
	   but if the device name ends in a digit, say /dev/foo1,
	   then the partition is called /dev/foo1p3. */
	w = strlen(cxt->dev_path);
	if (w && isdigit(cxt->dev_path[w-1]))
		w++;
	if (w < 5)
		w = 5;

	printf(_("%*s Boot      Start         End      Blocks   Id  System\n"),
	       (int) w + 1, _("Device"));

	for (i = 0; i < cxt->label->nparts_max; i++) {
		struct pte *pe = &ptes[i];

		p = pe->part_table;
		if (p && !is_cleared_partition(p)) {
			unsigned int psects = get_nr_sects(p);
			unsigned int pblocks = psects;
			unsigned int podd = 0;
			struct fdisk_parttype *type =
					fdisk_get_parttype_from_code(cxt, p->sys_ind);

			if (cxt->sector_size < 1024) {
				pblocks /= (1024 / cxt->sector_size);
				podd = psects % (1024 / cxt->sector_size);
			}
			if (cxt->sector_size > 1024)
				pblocks *= (cxt->sector_size / 1024);
                        printf(
			    "%s  %c %11lu %11lu %11lu%c  %2x  %s\n",
			partname(cxt->dev_path, i+1, w+2),
/* boot flag */		!p->boot_ind ? ' ' : p->boot_ind == ACTIVE_FLAG
			? '*' : '?',
/* start */		(unsigned long) cround(cxt, get_partition_start(pe)),
/* end */		(unsigned long) cround(cxt, get_partition_start(pe) + psects
				- (psects ? 1 : 0)),
/* odd flag on end */	(unsigned long) pblocks, podd ? '+' : ' ',
/* type id */		p->sys_ind,
/* type name */		type ? type->name : _("Unknown"));
			check_consistency(cxt, p, i);
			fdisk_warn_alignment(cxt, get_partition_start(pe), i);
		}
	}

	/* Is partition table in disk order? It need not be, but... */
	/* partition table entries are not checked for correct order if this
	   is a sgi, sun labeled disk... */
	if (wrong_p_order(cxt, NULL))
		printf(_("\nPartition table entries are not in disk order\n"));

	return 0;
}

/*
 * TODO: merge into dos_list_table
 */
void dos_list_table_expert(struct fdisk_context *cxt, int extend)
{
	struct pte *pe;
	struct partition *p;
	size_t i;

	printf(_("\nDisk %s: %d heads, %llu sectors, %llu cylinders\n\n"),
		cxt->dev_path, cxt->geom.heads, cxt->geom.sectors, cxt->geom.cylinders);
        printf(_("Nr AF  Hd Sec  Cyl  Hd Sec  Cyl     Start      Size ID\n"));
	for (i = 0 ; i < cxt->label->nparts_max; i++) {
		pe = &ptes[i];
		p = (extend ? pe->ext_pointer : pe->part_table);
		if (p != NULL) {
                        printf("%2zd %02x%4d%4d%5d%4d%4d%5d%11lu%11lu %02x\n",
				i + 1, p->boot_ind, p->head,
				sector(p->sector),
				cylinder(p->sector, p->cyl), p->end_head,
				sector(p->end_sector),
				cylinder(p->end_sector, p->end_cyl),
				(unsigned long) get_start_sect(p),
				(unsigned long) get_nr_sects(p), p->sys_ind);
			if (p->sys_ind) {
				check_consistency(cxt, p, i);
				fdisk_warn_alignment(cxt, get_partition_start(pe), i);
			}
		}
	}
}

/*
 * Fix the chain of logicals.
 * extended_offset is unchanged, the set of sectors used is unchanged
 * The chain is sorted so that sectors increase, and so that
 * starting sectors increase.
 *
 * After this it may still be that cfdisk doesn't like the table.
 * (This is because cfdisk considers expanded parts, from link to
 * end of partition, and these may still overlap.)
 * Now
 *   sfdisk /dev/hda > ohda; sfdisk /dev/hda < ohda
 * may help.
 */
static void fix_chain_of_logicals(struct fdisk_context *cxt)
{
	size_t j, oj, ojj, sj, sjj;
	struct partition *pj,*pjj,tmp;

	/* Stage 1: sort sectors but leave sector of part 4 */
	/* (Its sector is the global extended_offset.) */
 stage1:
	for (j = 5; j < cxt->label->nparts_max - 1; j++) {
		oj = ptes[j].offset;
		ojj = ptes[j+1].offset;
		if (oj > ojj) {
			ptes[j].offset = ojj;
			ptes[j+1].offset = oj;
			pj = ptes[j].part_table;
			set_start_sect(pj, get_start_sect(pj)+oj-ojj);
			pjj = ptes[j+1].part_table;
			set_start_sect(pjj, get_start_sect(pjj)+ojj-oj);
			set_start_sect(ptes[j-1].ext_pointer,
				       ojj-extended_offset);
			set_start_sect(ptes[j].ext_pointer,
				       oj-extended_offset);
			goto stage1;
		}
	}

	/* Stage 2: sort starting sectors */
 stage2:
	for (j = 4; j < cxt->label->nparts_max - 1; j++) {
		pj = ptes[j].part_table;
		pjj = ptes[j+1].part_table;
		sj = get_start_sect(pj);
		sjj = get_start_sect(pjj);
		oj = ptes[j].offset;
		ojj = ptes[j+1].offset;
		if (oj+sj > ojj+sjj) {
			tmp = *pj;
			*pj = *pjj;
			*pjj = tmp;
			set_start_sect(pj, ojj+sjj-oj);
			set_start_sect(pjj, oj+sj-ojj);
			goto stage2;
		}
	}

	/* Probably something was changed */
	for (j = 4; j < cxt->label->nparts_max; j++)
		ptes[j].changed = 1;
}

void dos_fix_partition_table_order(struct fdisk_context *cxt)
{
	struct pte *pei, *pek;
	size_t i,k;

	if (!wrong_p_order(cxt, NULL)) {
		printf(_("Nothing to do. Ordering is correct already.\n\n"));
		return;
	}

	while ((i = wrong_p_order(cxt, &k)) != 0 && i < 4) {
		/* partition i should have come earlier, move it */
		/* We have to move data in the MBR */
		struct partition *pi, *pk, *pe, pbuf;
		pei = &ptes[i];
		pek = &ptes[k];

		pe = pei->ext_pointer;
		pei->ext_pointer = pek->ext_pointer;
		pek->ext_pointer = pe;

		pi = pei->part_table;
		pk = pek->part_table;

		memmove(&pbuf, pi, sizeof(struct partition));
		memmove(pi, pk, sizeof(struct partition));
		memmove(pk, &pbuf, sizeof(struct partition));

		pei->changed = pek->changed = 1;
	}

	if (i)
		fix_chain_of_logicals(cxt);

	printf(_("Done.\n"));

}

void dos_move_begin(struct fdisk_context *cxt, int i)
{
	struct pte *pe = &ptes[i];
	struct partition *p = pe->part_table;
	unsigned int new, free_start, curr_start, last;
	uintmax_t res = 0;
	size_t x;

	assert(cxt);
	assert(fdisk_is_disklabel(cxt, DOS));

	if (warn_geometry(cxt))
		return;
	if (!p->sys_ind || !get_nr_sects(p) || IS_EXTENDED (p->sys_ind)) {
		printf(_("Partition %d has no data area\n"), i + 1);
		return;
	}

	/* the default start is at the second sector of the disk or at the
	 * second sector of the extended partition
	 */
	free_start = pe->offset ? pe->offset + 1 : 1;

	curr_start = get_partition_start(pe);

	/* look for a free space before the current start of the partition */
	for (x = 0; x < cxt->label->nparts_max; x++) {
		unsigned int end;
		struct pte *prev_pe = &ptes[x];
		struct partition *prev_p = prev_pe->part_table;

		if (!prev_p)
			continue;
		end = get_partition_start(prev_pe) + get_nr_sects(prev_p);

		if (!is_cleared_partition(prev_p) &&
		    end > free_start && end <= curr_start)
			free_start = end;
	}

	last = get_partition_start(pe) + get_nr_sects(p) - 1;

	if (fdisk_ask_number(cxt, free_start, curr_start, last,
			_("New beginning of data"), &res))
		return;
	new = res - pe->offset;

	if (new != get_nr_sects(p)) {
		unsigned int sects = get_nr_sects(p) + get_start_sect(p) - new;
		set_nr_sects(p, sects);
		set_start_sect(p, new);
		pe->changed = 1;
	}
}

static int dos_get_partition_status(
		struct fdisk_context *cxt,
		size_t i,
		int *status)
{
	struct pte *pe;
	struct partition *p;

	assert(cxt);
	assert(cxt->label);
	assert(fdisk_is_disklabel(cxt, DOS));

	if (!status || i >= cxt->label->nparts_max)
		return -EINVAL;

	*status = FDISK_PARTSTAT_NONE;
	pe = &ptes[i];
	p = pe->part_table;

	if (p && !is_cleared_partition(p))
		*status = FDISK_PARTSTAT_USED;

	return 0;
}

static int dos_toggle_partition_flag(
		struct fdisk_context *cxt,
		size_t i,
		unsigned long flag)
{
	struct pte *pe;
	struct partition *p;

	assert(cxt);
	assert(cxt->label);
	assert(fdisk_is_disklabel(cxt, DOS));

	if (i >= cxt->label->nparts_max)
		return -EINVAL;

	pe = &ptes[i];
	p = pe->part_table;

	switch (flag) {
	case DOS_FLAG_ACTIVE:
		if (IS_EXTENDED(p->sys_ind) && !p->boot_ind)
			fdisk_warnx(cxt, _("WARNING: Partition %d is an extended partition"), i + 1);

		p->boot_ind = (p->boot_ind ? 0 : ACTIVE_FLAG);
		pe->changed = 1;
		fdisk_label_set_changed(cxt->label, 1);
		break;
	default:
		return 1;
	}

	return 0;
}

static const struct fdisk_label_operations dos_operations =
{
	.probe		= dos_probe_label,
	.write		= dos_write_disklabel,
	.verify		= dos_verify_disklabel,
	.create		= dos_create_disklabel,
	.part_add	= dos_add_partition,
	.part_delete	= dos_delete_partition,
	.part_get_type	= dos_get_parttype,
	.part_set_type	= dos_set_parttype,

	.part_toggle_flag = dos_toggle_partition_flag,
	.part_get_status = dos_get_partition_status,

	.reset_alignment = dos_reset_alignment,
};

/*
 * allocates DOS in-memory stuff
 */
struct fdisk_label *fdisk_new_dos_label(struct fdisk_context *cxt)
{
	struct fdisk_label *lb;
	struct fdisk_dos_label *dos;

	assert(cxt);

	dos = calloc(1, sizeof(*dos));
	if (!dos)
		return NULL;

	/* initialize generic part of the driver */
	lb = (struct fdisk_label *) dos;
	lb->name = "dos";
	lb->id = FDISK_DISKLABEL_DOS;
	lb->op = &dos_operations;
	lb->parttypes = dos_parttypes;
	lb->nparttypes = ARRAY_SIZE(dos_parttypes);

	/* don't ask for partition number for op->part_add() */
	lb->flags = FDISK_LABEL_FL_ADDPART_NOPARTNO;

	return lb;
}

/*
 * Public label specific functions
 */

int fdisk_dos_enable_compatible(struct fdisk_label *lb, int enable)
{
	struct fdisk_dos_label *dos = (struct fdisk_dos_label *) lb;

	if (!lb)
		return -EINVAL;

	dos->compatible = enable;
	return 0;
}

int fdisk_dos_is_compatible(struct fdisk_label *lb)
{
	return ((struct fdisk_dos_label *) lb)->compatible;
}
