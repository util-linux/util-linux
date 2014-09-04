#include <libfdisk.h>
#include <libsmartcols.h>
#include <assert.h>

#include "c.h"
#include "xalloc.h"
#include "nls.h"
#include "blkdev.h"
#include "mbsalign.h"
#include "pathnames.h"
#include "canonicalize.h"
#include "strutils.h"
#include "sysfs.h"
#include "colors.h"

#include "fdisk-list.h"

static int is_ide_cdrom_or_tape(char *device)
{
	int fd, ret;

	if ((fd = open(device, O_RDONLY)) < 0)
		return 0;
	ret = blkdev_is_cdrom(fd);

	close(fd);
	return ret;
}


void list_disk_geometry(struct fdisk_context *cxt)
{
	char *id = NULL;
	struct fdisk_label *lb = fdisk_get_label(cxt, NULL);
	uint64_t bytes = fdisk_get_nsectors(cxt) * fdisk_get_sector_size(cxt);
	char *strsz = size_to_human_string(SIZE_SUFFIX_SPACE
					   | SIZE_SUFFIX_3LETTER, bytes);

	color_scheme_enable("header", UL_COLOR_BOLD);
	fdisk_info(cxt,	_("Disk %s: %s, %ju bytes, %ju sectors"),
			fdisk_get_devname(cxt), strsz,
			bytes, (uintmax_t) fdisk_get_nsectors(cxt));
	color_disable();
	free(strsz);

	if (lb && (fdisk_label_require_geometry(lb) || fdisk_use_cylinders(cxt)))
		fdisk_info(cxt, _("Geometry: %d heads, %llu sectors/track, %llu cylinders"),
			       fdisk_get_geom_heads(cxt),
			       fdisk_get_geom_sectors(cxt),
			       fdisk_get_geom_cylinders(cxt));

	fdisk_info(cxt, _("Units: %s of %d * %ld = %ld bytes"),
	       fdisk_get_unit(cxt, FDISK_PLURAL),
	       fdisk_get_units_per_sector(cxt),
	       fdisk_get_sector_size(cxt),
	       fdisk_get_units_per_sector(cxt) * fdisk_get_sector_size(cxt));

	fdisk_info(cxt, _("Sector size (logical/physical): %lu bytes / %lu bytes"),
				fdisk_get_sector_size(cxt),
				fdisk_get_physector_size(cxt));
	fdisk_info(cxt, _("I/O size (minimum/optimal): %lu bytes / %lu bytes"),
				fdisk_get_minimal_iosize(cxt),
				fdisk_get_optimal_iosize(cxt));
	if (fdisk_get_alignment_offset(cxt))
		fdisk_info(cxt, _("Alignment offset: %lu bytes"),
				fdisk_get_alignment_offset(cxt));
	if (fdisk_has_label(cxt))
		fdisk_info(cxt, _("Disklabel type: %s"),
				fdisk_label_get_name(lb));

	if (fdisk_get_disklabel_id(cxt, &id) == 0 && id)
		fdisk_info(cxt, _("Disk identifier: %s"), id);
}

void list_disklabel(struct fdisk_context *cxt)
{
	struct fdisk_table *tb = NULL;
	struct fdisk_partition *pa = NULL;
	struct fdisk_iter *itr = NULL;
	struct fdisk_label *lb;
	struct libscols_table *out = NULL;
	const char *bold = NULL;
	int *ids = NULL;		/* IDs of fdisk_fields */
	size_t	nids = 0, i;

	/* print label specific stuff by libfdisk FDISK_ASK_INFO API */
	fdisk_list_disklabel(cxt);

	/* get partitions and generate output */
	if (fdisk_get_partitions(cxt, &tb) || fdisk_table_get_nents(tb) <= 0)
		goto done;

	if (fdisk_label_get_fields_ids(NULL, cxt, &ids, &nids))
		goto done;

	itr = fdisk_new_iter(FDISK_ITER_FORWARD);
	if (!itr) {
		fdisk_warn(cxt, _("faild to allocate iterator"));
		goto done;
	}

	out = scols_new_table();
	if (!out) {
		fdisk_warn(cxt, _("faild to allocate output table"));
		goto done;
	}

	if (colors_wanted()) {
		scols_table_enable_colors(out, 1);
		bold = color_scheme_get_sequence("header", UL_COLOR_BOLD);
	}

	lb = fdisk_get_label(cxt, NULL);
	assert(lb);

	/* define output table columns */
	for (i = 0; i < nids; i++) {
		int fl = 0;
		struct libscols_column *co;
		const struct fdisk_field *field =
				fdisk_label_get_field(lb, ids[i]);
		if (!field)
			goto done;
		if (fdisk_field_is_number(field))
			fl |= SCOLS_FL_RIGHT;
		if (fdisk_field_get_id(field) == FDISK_FIELD_TYPE)
			fl |= SCOLS_FL_TRUNC;

		co = scols_table_new_column(out,
				fdisk_field_get_name(field),
				fdisk_field_get_width(field), fl);
		if (!co)
			goto done;

		/* set colum header color */
		if (bold)
			scols_cell_set_color(scols_column_get_header(co), bold);
	}

	/* fill-in output table */
	while (fdisk_table_next_partition(tb, itr, &pa) == 0) {
		struct libscols_line *ln = scols_table_new_line(out, NULL);

		if (!ln) {
			fdisk_warn(cxt, _("faild to allocate output line"));
			goto done;
		}

		for (i = 0; i < nids; i++) {
			char *data = NULL;

			if (fdisk_partition_to_string(pa, cxt, ids[i], &data))
				continue;
			scols_line_refer_data(ln, i, data);
		}
	}

	/* print */
	if (!scols_table_is_empty(out)) {
		fputc('\n', stdout);
		scols_print_table(out);
	}

	/* print warnings */
	while (itr && fdisk_table_next_partition(tb, itr, &pa) == 0) {
		if (!fdisk_lba_is_phy_aligned(cxt, fdisk_partition_get_start(pa)))
			fdisk_warnx(cxt, _("Partition %zu does not start on physical sector boundary."),
					  fdisk_partition_get_partno(pa) + 1);
	}

	if (fdisk_table_wrong_order(tb))
		fdisk_info(cxt, _("Partition table entries are not in disk order."));
done:
	free(ids);
	scols_unref_table(out);
	fdisk_unref_table(tb);
	fdisk_free_iter(itr);
}

int print_device_pt(struct fdisk_context *cxt, char *device, int warnme)
{
	if (fdisk_assign_device(cxt, device, 1) != 0) {	/* read-only */
		if (warnme || errno == EACCES)
			warn(_("cannot open %s"), device);
		return -1;
	}

	list_disk_geometry(cxt);

	if (fdisk_has_label(cxt))
		list_disklabel(cxt);

	fdisk_deassign_device(cxt, 1);
	return 0;
}

void print_all_devices_pt(struct fdisk_context *cxt)
{
	FILE *f;
	char line[128 + 1];
	int ct = 0;

	f = fopen(_PATH_PROC_PARTITIONS, "r");
	if (!f) {
		warn(_("cannot open %s"), _PATH_PROC_PARTITIONS);
		return;
	}

	while (fgets(line, sizeof(line), f)) {
		char buf[PATH_MAX], *cn;
		dev_t devno;

		if (sscanf(line, " %*d %*d %*d %128[^\n ]", buf) != 1)
			continue;

		devno = sysfs_devname_to_devno(buf, NULL);
		if (devno <= 0)
			continue;

		if (sysfs_devno_is_lvm_private(devno) ||
		    sysfs_devno_is_wholedisk(devno) <= 0)
			continue;

		if (!sysfs_devno_to_devpath(devno, buf, sizeof(buf)))
			continue;

		cn = canonicalize_path(buf);
		if (!cn)
			continue;

		if (!is_ide_cdrom_or_tape(cn)) {
			if (ct)
				fputs("\n\n", stdout);
			if (print_device_pt(cxt, cn, 0) == 0)
				ct++;
		}
		free(cn);
	}
	fclose(f);
}

