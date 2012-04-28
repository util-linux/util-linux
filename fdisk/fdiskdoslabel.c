/*
 * Many, many hands.
 * Specific DOS label file  - Davidlohr Bueso <dave@gnu.org>
 */

#include <unistd.h>

#include "nls.h"
#include "xalloc.h"
#include "randutils.h"
#include "common.h"
#include "fdisk.h"
#include "fdiskdoslabel.h"

/* Allocate a buffer and read a partition table sector */
static void read_pte(int fd, int pno, unsigned long long offset)
{
	struct pte *pe = &ptes[pno];

	pe->offset = offset;
	pe->sectorbuffer = xmalloc(sector_size);
	read_sector(fd, offset, pe->sectorbuffer);
	pe->changed = 0;
	pe->part_table = pe->ext_pointer = NULL;
}

static void dos_write_mbr_id(unsigned char *b, unsigned int id)
{
	store4_little_endian(&b[440], id);
}

static unsigned int dos_read_mbr_id(const unsigned char *b)
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

static void dos_init(void)
{
	int i;

	disklabel = DOS_LABEL;
	partitions = 4;
	ext_index = 0;
	extended_offset = 0;

	for (i = 0; i < 4; i++) {
		struct pte *pe = &ptes[i];

		pe->part_table = pt_offset(MBRbuffer, i);
		pe->ext_pointer = NULL;
		pe->offset = 0;
		pe->sectorbuffer = MBRbuffer;
		pe->changed = 0;
	}

	warn_geometry();
	warn_limits();
	warn_alignment();
}

static void read_extended(int ext)
{
	int i;
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
		struct pte *pe = &ptes[partitions];

		if (partitions >= MAXIMUM_PARTS) {
			/* This is not a Linux restriction, but
			   this program uses arrays of size MAXIMUM_PARTS.
			   Do not try to `improve' this test. */
			struct pte *pre = &ptes[partitions-1];

			fprintf(stderr,
				_("Warning: omitting partitions after #%d.\n"
				  "They will be deleted "
				  "if you save this partition table.\n"),
				partitions);
			clear_partition(pre->ext_pointer);
			pre->changed = 1;
			return;
		}

		read_pte(fd, partitions, extended_offset + get_start_sect(p));

		if (!extended_offset)
			extended_offset = get_start_sect(p);

		q = p = pt_offset(pe->sectorbuffer, 0);
		for (i = 0; i < 4; i++, p++) if (get_nr_sects(p)) {
			if (IS_EXTENDED (p->sys_ind)) {
				if (pe->ext_pointer)
					fprintf(stderr,
						_("Warning: extra link "
						  "pointer in partition table"
						  " %d\n"), partitions + 1);
				else
					pe->ext_pointer = p;
			} else if (p->sys_ind) {
				if (pe->part_table)
					fprintf(stderr,
						_("Warning: ignoring extra "
						  "data in partition table"
						  " %d\n"), partitions + 1);
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
		partitions++;
	}

	/* remove empty links */
 remove:
	for (i = 4; i < partitions; i++) {
		struct pte *pe = &ptes[i];

		if (!get_nr_sects(pe->part_table) &&
		    (partitions > 5 || ptes[4].part_table->sys_ind)) {
			printf(_("omitting empty partition (%d)\n"), i+1);
			dos_delete_partition(i);
			goto remove; 	/* numbering changed */
		}
	}
}

void dos_print_mbr_id(void)
{
	printf(_("Disk identifier: 0x%08x\n"), dos_read_mbr_id(MBRbuffer));
}

void create_doslabel(void)
{
	unsigned int id;

	/* random disk signature */
	random_get_bytes(&id, sizeof(id));

	fprintf(stderr, _("Building a new DOS disklabel with disk identifier 0x%08x.\n"), id);

	dos_init();
	zeroize_mbr_buffer();

	set_all_unchanged();
	set_changed(0);

	/* Generate an MBR ID for this disk */
	dos_write_mbr_id(MBRbuffer, id);

	/* Put MBR signature */
	write_part_table_flag(MBRbuffer);
}

void dos_set_mbr_id(void)
{
	unsigned long new_id;
	char *ep;
	char ps[64];

	snprintf(ps, sizeof ps, _("New disk identifier (current 0x%08x): "),
		 dos_read_mbr_id(MBRbuffer));

	if (read_chars(ps) == '\n')
		return;

	new_id = strtoul(line_ptr, &ep, 0);
	if (*ep != '\n')
		return;

	dos_write_mbr_id(MBRbuffer, new_id);
	MBRbuffer_changed = 1;
	dos_print_mbr_id();
}

void dos_delete_partition(int i)
{
	struct pte *pe = &ptes[i];
	struct partition *p = pe->part_table;
	struct partition *q = pe->ext_pointer;

	/* Note that for the fifth partition (i == 4) we don't actually
	   decrement partitions. */

	if (i < 4) {
		if (IS_EXTENDED (p->sys_ind) && i == ext_index) {
			partitions = 4;
			ptes[ext_index].ext_pointer = NULL;
			extended_offset = 0;
		}
		clear_partition(p);
	} else if (!q->sys_ind && i > 4) {
		/* the last one in the chain - just delete */
		--partitions;
		--i;
		clear_partition(ptes[i].ext_pointer);
		ptes[i].changed = 1;
	} else {
		/* not the last one - further ones will be moved down */
		if (i > 4) {
			/* delete this link in the chain */
			p = ptes[i-1].ext_pointer;
			*p = *q;
			set_start_sect(p, get_start_sect(q));
			set_nr_sects(p, get_nr_sects(q));
			ptes[i-1].changed = 1;
		} else if (partitions > 5) {    /* 5 will be moved to 4 */
			/* the first logical in a longer chain */
			struct pte *pe = &ptes[5];

			if (pe->part_table) /* prevent SEGFAULT */
				set_start_sect(pe->part_table,
					       get_partition_start(pe) -
					       extended_offset);
			pe->offset = extended_offset;
			pe->changed = 1;
		}

		if (partitions > 5) {
			partitions--;
			while (i < partitions) {
				ptes[i] = ptes[i+1];
				i++;
			}
		} else
			/* the only logical: clear only */
			clear_partition(ptes[i].part_table);
	}
}

int check_dos_label(void)
{
	int i;

	if (!valid_part_table_flag(MBRbuffer))
		return 0;

	dos_init();

	for (i = 0; i < 4; i++) {
		struct pte *pe = &ptes[i];

		if (IS_EXTENDED (pe->part_table->sys_ind)) {
			if (partitions != 4)
				fprintf(stderr, _("Ignoring extra extended "
					"partition %d\n"), i + 1);
			else
				read_extended(i);
		}
	}

	for (i = 3; i < partitions; i++) {
		struct pte *pe = &ptes[i];

		if (!valid_part_table_flag(pe->sectorbuffer)) {
			fprintf(stderr,
				_("Warning: invalid flag 0x%04x of partition "
				"table %d will be corrected by w(rite)\n"),
				part_table_flag(pe->sectorbuffer), i + 1);
			pe->changed = 1;
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
int is_dos_partition(int t)
{
	return (t == 1 || t == 4 || t == 6 ||
		t == 0x0b || t == 0x0c || t == 0x0e ||
		t == 0x11 || t == 0x12 || t == 0x14 || t == 0x16 ||
		t == 0x1b || t == 0x1c || t == 0x1e || t == 0x24 ||
		t == 0xc1 || t == 0xc4 || t == 0xc6);
}
