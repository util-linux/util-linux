
#include "fdiskP.h"

struct fdisk_context *fdisk_new_context(void)
{
	struct fdisk_context *cxt;

	cxt = calloc(1, sizeof(*cxt));
	if (!cxt)
		return NULL;

	DBG(LABEL, dbgprint("new context %p allocated", cxt));
	cxt->dev_fd = -1;

	/*
	 * Allocate label specific structs.
	 *
	 * This is necessary (for example) to store label specific
	 * context setting.
	 */
	cxt->labels[ cxt->nlabels++ ] = fdisk_new_gpt_label(cxt);
	cxt->labels[ cxt->nlabels++ ] = fdisk_new_dos_label(cxt);
	cxt->labels[ cxt->nlabels++ ] = fdisk_new_aix_label(cxt);
	cxt->labels[ cxt->nlabels++ ] = fdisk_new_bsd_label(cxt);
	cxt->labels[ cxt->nlabels++ ] = fdisk_new_mac_label(cxt);
	cxt->labels[ cxt->nlabels++ ] = fdisk_new_sgi_label(cxt);
	cxt->labels[ cxt->nlabels++ ] = fdisk_new_sun_label(cxt);

	return cxt;
}

/*
 * Returns the current label if no name specified.
 */
struct fdisk_label *fdisk_context_get_label(struct fdisk_context *cxt, const char *name)
{
	size_t i;

	assert(cxt);

	if (!name)
		return cxt->label;

	for (i = 0; i < cxt->nlabels; i++)
		if (strcmp(cxt->labels[i]->name, name) == 0)
			return cxt->labels[i];

	DBG(LABEL, dbgprint("failed to found %s label driver\n", name));
	return NULL;
}

int __fdisk_context_switch_label(struct fdisk_context *cxt,
				 struct fdisk_label *lb)
{
	if (!lb)
		return -EINVAL;
	cxt->label = lb;
	DBG(LABEL, dbgprint("--> switching context to %s!", lb->name));
	return 0;
}

int fdisk_context_switch_label(struct fdisk_context *cxt, const char *name)
{
	return __fdisk_context_switch_label(cxt,
			fdisk_context_get_label(cxt, name));
}


static void reset_context(struct fdisk_context *cxt)
{
	size_t i;

	DBG(CONTEXT, dbgprint("\n-----\nresetting context %p", cxt));

	/* reset drives' private data */
	for (i = 0; i < cxt->nlabels; i++)
		fdisk_deinit_label(cxt->labels[i]);

	/* free device specific stuff */
	if (cxt->dev_fd > -1)
		close(cxt->dev_fd);
	free(cxt->dev_path);
	free(cxt->firstsector);

	/* initialize */
	cxt->dev_fd = -1;
	cxt->dev_path = NULL;
	cxt->firstsector = NULL;

	cxt->io_size = 0;
	cxt->optimal_io_size = 0;
	cxt->min_io_size = 0;
	cxt->phy_sector_size = 0;
	cxt->sector_size = 0;
	cxt->alignment_offset = 0;
	cxt->grain = 0;
	cxt->first_lba = 0;
	cxt->total_sectors = 0;

	memset(&cxt->geom, 0, sizeof(struct fdisk_geometry));

	cxt->label = NULL;
}

/**
 * fdisk_context_assign_device:
 * @fname: path to the device to be handled
 * @readonly: how to open the device
 *
 * If the @readonly flag is set to false, fdisk will attempt to open
 * the device with read-write mode and will fallback to read-only if
 * unsuccessful.
 *
 * Returns: 0 on sucess, < 0 on error.
 */
int fdisk_context_assign_device(struct fdisk_context *cxt,
				const char *fname, int readonly)
{
	int fd;

	DBG(CONTEXT, dbgprint("assigning device %s", fname));
	assert(cxt);

	reset_context(cxt);

	if (readonly == 1 || (fd = open(fname, O_RDWR)) < 0) {
		if ((fd = open(fname, O_RDONLY)) < 0)
			return -errno;
		readonly = 1;
	}

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
	fdisk_reset_alignment(cxt);

	DBG(CONTEXT, dbgprint("context %p initialized for %s [%s]",
			      cxt, fname,
			      readonly ? "READ-ONLY" : "READ-WRITE"));
	return 0;
fail:
	DBG(CONTEXT, dbgprint("failed to assign device"));
	return -errno;
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

	DBG(CONTEXT, dbgprint("freeing context %p for %s", cxt, cxt->dev_path));
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
 * fdisk_context_set_ask:
 * @cxt: context
 * @ask_cb: callback
 * @data: callback data
 *
 * Returns: 0 on sucess, < 0 on error.
 */
int fdisk_context_set_ask(struct fdisk_context *cxt,
		int (*ask_cb)(struct fdisk_context *, struct fdisk_ask *, void *),
		void *data)
{
	assert(cxt);

	cxt->ask_cb = ask_cb;
	cxt->ask_data = data;
	return 0;
}
