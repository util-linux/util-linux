#ifdef HAVE_LIBBLKID
# include <blkid.h>
#endif

#include "fdiskP.h"

/**
 * fdisk_new_context:
 *
 * Returns: newly allocated libfdisk handler
 */
struct fdisk_context *fdisk_new_context(void)
{
	struct fdisk_context *cxt;

	cxt = calloc(1, sizeof(*cxt));
	if (!cxt)
		return NULL;

	DBG(CXT, ul_debugobj(cxt, "alloc"));
	cxt->dev_fd = -1;

	/*
	 * Allocate label specific structs.
	 *
	 * This is necessary (for example) to store label specific
	 * context setting.
	 */
	cxt->labels[ cxt->nlabels++ ] = fdisk_new_gpt_label(cxt);
	cxt->labels[ cxt->nlabels++ ] = fdisk_new_dos_label(cxt);
	cxt->labels[ cxt->nlabels++ ] = fdisk_new_bsd_label(cxt);
	cxt->labels[ cxt->nlabels++ ] = fdisk_new_sgi_label(cxt);
	cxt->labels[ cxt->nlabels++ ] = fdisk_new_sun_label(cxt);

	return cxt;
}

/**
 * fdisk_new_nested_context:
 * @parent: parental context
 * @name: optional label name (e.g. "bsd")
 *
 * This is supported for MBR+BSD and GPT+pMBR only.
 *
 * Returns: new context for nested partiton table.
 */
struct fdisk_context *fdisk_new_nested_context(struct fdisk_context *parent,
				const char *name)
{
	struct fdisk_context *cxt;
	struct fdisk_label *lb = NULL;

	assert(parent);

	cxt = calloc(1, sizeof(*cxt));
	if (!cxt)
		return NULL;

	DBG(CXT, ul_debugobj(parent, "alloc nested [%p]", cxt));
	cxt->dev_fd = parent->dev_fd;
	cxt->parent = parent;

	cxt->io_size =          parent->io_size;
	cxt->optimal_io_size =  parent->optimal_io_size;
	cxt->min_io_size =      parent->min_io_size;
	cxt->phy_sector_size =  parent->phy_sector_size;
	cxt->sector_size =      parent->sector_size;
	cxt->alignment_offset = parent->alignment_offset;
	cxt->grain =            parent->grain;
	cxt->first_lba =        parent->first_lba;
	cxt->total_sectors =    parent->total_sectors;
	cxt->firstsector =	parent->firstsector;

	cxt->ask_cb =		parent->ask_cb;
	cxt->ask_data =		parent->ask_data;

	cxt->geom = parent->geom;

	if (name) {
		if (strcmp(name, "bsd") == 0)
			lb = cxt->labels[ cxt->nlabels++ ] = fdisk_new_bsd_label(cxt);
		else if (strcmp(name, "dos") == 0)
			lb = cxt->labels[ cxt->nlabels++ ] = fdisk_new_dos_label(cxt);
	}

	if (lb) {
		DBG(CXT, ul_debugobj(cxt, "probing for nested %s", lb->name));

		cxt->label = lb;

		if (lb->op->probe(cxt) == 1)
			__fdisk_switch_label(cxt, lb);
		else {
			DBG(CXT, ul_debugobj(cxt, "not found %s label", lb->name));
			if (lb->op->deinit)
				lb->op->deinit(lb);
			cxt->label = NULL;
		}
	}

	return cxt;
}


/**
 * fdisk_get_label:
 * @cxt: context instance
 * @name: label name (e.g. "gpt")
 *
 * If no @name specified then returns the current context label.
 *
 * Returns: label struct or NULL in case of error.
 */
struct fdisk_label *fdisk_get_label(struct fdisk_context *cxt, const char *name)
{
	size_t i;

	assert(cxt);

	if (!name)
		return cxt->label;

	for (i = 0; i < cxt->nlabels; i++)
		if (cxt->labels[i]
		    && strcmp(cxt->labels[i]->name, name) == 0)
			return cxt->labels[i];

	DBG(CXT, ul_debugobj(cxt, "failed to found %s label driver", name));
	return NULL;
}

/**
 * fdisk_next_label:
 * @cxt: context instance
 * @lb: returns pointer to the next label
 *
 * <informalexample>
 *   <programlisting>
 *      // print all supported labels
 *	struct fdisk_context *cxt = fdisk_new_context();
 *	struct fdisk_label *lb = NULL;
 *
 *	while (fdisk_next_label(cxt, &lb) == 0)
 *		print("label name: %s\n", fdisk_label_get_name(lb));
 *	fdisk_free_context(cxt);
 *   </programlisting>
 * </informalexample>
 *
 * Returns: <0 in case of error, 0 on success, 1 at the end.
 */
int fdisk_next_label(struct fdisk_context *cxt, struct fdisk_label **lb)
{
	size_t i;
	struct fdisk_label *res = NULL;

	if (!lb || !cxt)
		return -EINVAL;

	if (!*lb)
		res = cxt->labels[0];
	else {
		for (i = 1; i < cxt->nlabels; i++) {
			if (*lb == cxt->labels[i - 1]) {
				res = cxt->labels[i];
				break;
			}
		}
	}

	*lb = res;
	return res ? 0 : 1;
}

/**
 * fdisk_get_nlabels:
 * @cxt: context
 *
 * Returns: number of supported label types
 */
size_t fdisk_get_nlabels(struct fdisk_context *cxt)
{
	return cxt ? cxt->nlabels : 0;
}

int __fdisk_switch_label(struct fdisk_context *cxt, struct fdisk_label *lb)
{
	if (!lb || !cxt)
		return -EINVAL;
	if (lb->disabled) {
		DBG(CXT, ul_debugobj(cxt, "*** attempt to switch to disabled label %s -- ignore!", lb->name));
		return -EINVAL;
	}
	cxt->label = lb;
	DBG(CXT, ul_debugobj(cxt, "--> switching context to %s!", lb->name));
	return 0;
}

/**
 * fdisk_switch_label:
 * @cxt: context
 * @name: label name (e.g. "gpt")
 *
 * Forces libfdisk to use the label driver.
 *
 * Returns: 0 on succes, <0 in case of error.
 */
int fdisk_switch_label(struct fdisk_context *cxt, const char *name)
{
	return __fdisk_switch_label(cxt, fdisk_get_label(cxt, name));
}


static void reset_context(struct fdisk_context *cxt)
{
	size_t i;

	DBG(CXT, ul_debugobj(cxt, "*** resetting context"));

	/* reset drives' private data */
	for (i = 0; i < cxt->nlabels; i++)
		fdisk_deinit_label(cxt->labels[i]);

	/* free device specific stuff */
	if (!cxt->parent && cxt->dev_fd > -1)
		close(cxt->dev_fd);
	free(cxt->dev_path);

	if (cxt->parent == NULL || cxt->parent->firstsector != cxt->firstsector)
		free(cxt->firstsector);

	/* initialize */
	cxt->dev_fd = -1;
	cxt->dev_path = NULL;
	cxt->firstsector = NULL;
	cxt->firstsector_bufsz = 0;

	fdisk_zeroize_device_properties(cxt);

	cxt->label = NULL;
}

/*
 * This function prints a warning if the device is not wiped (e.g. wipefs(8).
 * Please don't call this function if there is already a PT.
 *
 * Returns: 0 if nothing found, < 0 on error, 1 if found a signature
 */
static int warn_wipe(struct fdisk_context *cxt)
{
#ifdef HAVE_LIBBLKID
	blkid_probe pr;
#endif
	int rc = 0;

	assert(cxt);

	if (fdisk_dev_has_disklabel(cxt) || cxt->dev_fd < 0)
		return -EINVAL;
#ifdef HAVE_LIBBLKID
	DBG(CXT, ul_debugobj(cxt, "wipe check: initialize libblkid prober"));

	pr = blkid_new_probe();
	if (!pr)
		return -ENOMEM;
	rc = blkid_probe_set_device(pr, cxt->dev_fd, 0, 0);
	if (rc)
		return rc;

	blkid_probe_enable_superblocks(pr, 1);
	blkid_probe_set_superblocks_flags(pr, BLKID_SUBLKS_TYPE);
	blkid_probe_enable_partitions(pr, 1);

	/* we care about the first found FS/raid, so don't call blkid_do_probe()
	 * in loop or don't use blkid_do_fullprobe() ... */
	rc = blkid_do_probe(pr);
	if (rc == 0) {
		const char *name = NULL;

		if (blkid_probe_lookup_value(pr, "TYPE", &name, 0) == 0 ||
		    blkid_probe_lookup_value(pr, "PTTYPE", &name, 0) == 0) {
			fdisk_warnx(cxt, _(
				"%s: device contains a valid '%s' signature, it's "
				"strongly recommended to wipe the device by command wipefs(8) "
				"if this setup is unexpected to avoid "
				"possible collisions."), cxt->dev_path, name);
			rc = 1;
		}
	}

	blkid_free_probe(pr);
#endif
	return rc;
}

/**
 * fdisk_assign_device:
 * @fname: path to the device to be handled
 * @readonly: how to open the device
 *
 * Open the device, discovery topology, geometry, and detect disklabel.
 *
 * Returns: 0 on success, < 0 on error.
 */
int fdisk_assign_device(struct fdisk_context *cxt,
			const char *fname, int readonly)
{
	int fd;

	DBG(CXT, ul_debugobj(cxt, "assigning device %s", fname));
	assert(cxt);

	reset_context(cxt);

	fd = open(fname, (readonly ? O_RDONLY : O_RDWR ) | O_CLOEXEC);
	if (fd < 0)
		return -errno;

	cxt->readonly = readonly;
	cxt->dev_fd = fd;
	cxt->dev_path = strdup(fname);
	if (!cxt->dev_path)
		goto fail;

	fdisk_discover_topology(cxt);
	fdisk_discover_geometry(cxt);

	if (fdisk_read_firstsector(cxt) < 0)
		goto fail;

	/* detect labels and apply labes specific stuff (e.g geomery)
	 * to the context */
	fdisk_probe_labels(cxt);

	/* let's apply user geometry *after* label prober
	 * to make it possible to override in-label setting */
	fdisk_apply_user_device_properties(cxt);

	/* warn about obsolete stuff on the device if we aren't in
	 * list-only mode and there is not PT yet */
	if (!fdisk_is_listonly(cxt) && !fdisk_dev_has_disklabel(cxt))
		warn_wipe(cxt);

	DBG(CXT, ul_debugobj(cxt, "initialized for %s [%s]",
			      fname, readonly ? "READ-ONLY" : "READ-WRITE"));
	return 0;
fail:
	DBG(CXT, ul_debugobj(cxt, "failed to assign device"));
	return -errno;
}

int fdisk_deassign_device(struct fdisk_context *cxt, int nosync)
{
	assert(cxt);
	assert(cxt->dev_fd >= 0);

	if (cxt->readonly || nosync)
		close(cxt->dev_fd);

	else {
		if (fsync(cxt->dev_fd) || close(cxt->dev_fd)) {
			fdisk_warn(cxt, _("%s: close device failed"),
					cxt->dev_path);
			return -errno;
		}

		fdisk_info(cxt, _("Syncing disks."));
		sync();
	}
	cxt->dev_fd = -1;
	return 0;
}

int fdisk_is_readonly(struct fdisk_context *cxt)
{
	assert(cxt);
	return cxt->readonly;
}

/**
 * fdisk_free_context:
 * @cxt: fdisk context
 *
 * Deallocates context struct.
 */
void fdisk_free_context(struct fdisk_context *cxt)
{
	int i;

	if (!cxt)
		return;

	DBG(CXT, ul_debugobj(cxt, "freeing context %p for %s", cxt, cxt->dev_path));
	reset_context(cxt);

	/* deallocate label's private stuff */
	for (i = 0; i < cxt->nlabels; i++) {
		if (!cxt->labels[i])
			continue;
		if (cxt->labels[i]->op->free)
			cxt->labels[i]->op->free(cxt->labels[i]);
		else
			free(cxt->labels[i]);
	}

	free(cxt);
}

/**
 * fdisk_set_ask:
 * @cxt: context
 * @ask_cb: callback
 * @data: callback data
 *
 * Set callbacks for dialog driven partitioning and library warnings/errors.
 *
 * Returns: 0 on success, < 0 on error.
 */
int fdisk_set_ask(struct fdisk_context *cxt,
		int (*ask_cb)(struct fdisk_context *, struct fdisk_ask *, void *),
		void *data)
{
	assert(cxt);

	cxt->ask_cb = ask_cb;
	cxt->ask_data = data;
	return 0;
}

/**
 * fdisk_enable_details:
 * @cxt: context
 * @enable: true/flase
 *
 * Enables or disables "details" display mode. This function has effect to
 * fdisk_partition_to_string() function.
 *
 * Returns: 0 on success, < 0 on error.
 */
int fdisk_enable_details(struct fdisk_context *cxt, int enable)
{
	assert(cxt);
	cxt->display_details = enable ? 1 : 0;
	return 0;
}

/**
 * fdisk_is_details:
 * @cxt: context
 *
 * Returns: 1 if details are enabled
 */
int fdisk_is_details(struct fdisk_context *cxt)
{
	assert(cxt);
	return cxt->display_details == 1;
}

/**
 * fdisk_enable_listonly:
 * @cxt: context
 * @enable: true/flase
 *
 * Just list partition only, don't care about another details, mistakes, ...
 *
 * Returns: 0 on success, < 0 on error.
 */
int fdisk_enable_listonly(struct fdisk_context *cxt, int enable)
{
	assert(cxt);
	cxt->listonly = enable ? 1 : 0;
	return 0;
}

/**
 * fdisk_is_listonly:
 * @cxt: context
 *
 * Returns: 1 if list-only mode enabled
 */
int fdisk_is_listonly(struct fdisk_context *cxt)
{
	assert(cxt);
	return cxt->listonly == 1;
}


/**
 * fdisk_set_unit:
 * @cxt: context
 * @str: "cylinder" or "sector".
 *
 * This is pure shit, unfortunately for example Sun addresses begin of the
 * partition by cylinders...
 *
 * Returns: 0 on succes, <0 on error.
 */
int fdisk_set_unit(struct fdisk_context *cxt, const char *str)
{
	assert(cxt);

	cxt->display_in_cyl_units = 0;

	if (!str)
		return 0;

	if (strcmp(str, "cylinder") == 0 || strcmp(str, "cylinders") == 0)
		cxt->display_in_cyl_units = 1;

	else if (strcmp(str, "sector") == 0 || strcmp(str, "sectors") == 0)
		cxt->display_in_cyl_units = 0;

	DBG(CXT, ul_debugobj(cxt, "display unit: %s", fdisk_get_unit(cxt, 0)));
	return 0;
}

/**
 * fdisk_get_unit:
 * @cxt: context
 *
 * Returns: unit name.
 */
const char *fdisk_get_unit(struct fdisk_context *cxt, int n)
{
	assert(cxt);

	if (fdisk_use_cylinders(cxt))
		return P_("cylinder", "cylinders", n);
	return P_("sector", "sectors", n);
}

/**
 * fdisk_use_cylinders:
 * @@cxt: context
 *
 * Returns 1 if user wants to display in cylinders.
 */
int fdisk_use_cylinders(struct fdisk_context *cxt)
{
	assert(cxt);
	return cxt->display_in_cyl_units == 1;
}

/**
 * fdisk_get_units_per_sector:
 * @cxt: context
 *
 * This is neccessary only for brain dead situations when we use "cylinders";
 *
 * Returns: number of "units" per sector, default is 1 if display unit is sector.
 */
unsigned int fdisk_get_units_per_sector(struct fdisk_context *cxt)
{
	assert(cxt);

	if (fdisk_use_cylinders(cxt)) {
		assert(cxt->geom.heads);
		return cxt->geom.heads * cxt->geom.sectors;
	}
	return 1;
}

/**
 * fdisk_get_optimal_iosize:
 * @cxt: context
 *
 * Returns: optimal I/O size
 */
unsigned long fdisk_get_optimal_iosize(struct fdisk_context *cxt)
{
	assert(cxt);
	return cxt->optimal_io_size;
}

/**
 * fdisk_get_minimal_iosize:
 * @cxt: context
 *
 * Returns: minimal I/O size
 */
unsigned long fdisk_get_minimal_size(struct fdisk_context *cxt)
{
	assert(cxt);
	return cxt->min_io_size;
}

/**
 * fdisk_get_physector_size:
 * @cxt: context
 *
 * Returns: physical sector size
 */
unsigned long fdisk_get_physector_size(struct fdisk_context *cxt)
{
	assert(cxt);
	return cxt->phy_sector_size;
}

/**
 * fdisk_get_sector_size:
 * @cxt: context
 *
 * Returns: sector size
 */
unsigned long fdisk_get_sector_size(struct fdisk_context *cxt)
{
	assert(cxt);
	return cxt->sector_size;
}

/**
 * fdisk_get_alignment_offset
 * @cxt: context
 *
 * Returns: alignment offset (used by 4K disks for backward compatibility with DOS tools).
 */
unsigned long fdisk_get_alignment_offset(struct fdisk_context *cxt)
{
	assert(cxt);
	return cxt->alignment_offset;
}

/**
 * fdisk_get_grain_size:
 * @cxt: context
 *
 * Returns: usual grain used to align partitions
 */
unsigned long fdisk_get_grain_size(struct fdisk_context *cxt)
{
	assert(cxt);
	return cxt->grain;
}

/**
 * fdisk_get_first_lba:
 * @cxt: context
 *
 * Returns: first possible LBA on disk for data partitions.
 */
sector_t fdisk_get_first_lba(struct fdisk_context *cxt)
{
	assert(cxt);
	return cxt->first_lba;
}

/**
 * fdisk_get_nsectors:
 * @cxt: context
 *
 * Returns: size of the device in (real) sectors.
 */
sector_t fdisk_get_nsectors(struct fdisk_context *cxt)
{
	assert(cxt);
	return cxt->total_sectors;
}

/**
 * fdisk_get_devname:
 * @cxt: context
 *
 * Returns: device name.
 */
const char *fdisk_get_devname(struct fdisk_context *cxt)
{
	assert(cxt);
	return cxt->dev_path;
}
