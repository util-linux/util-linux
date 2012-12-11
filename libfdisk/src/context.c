
#include "fdiskP.h"

struct fdisk_context *fdisk_new_context(void)
{
	struct fdisk_context *cxt;
	size_t i;

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

	DBG(CONTEXT, dbgprint("supported labels:"));
	for (i = 0; i < cxt->nlabels; i++) {
		DBG(CONTEXT, dbgprint(" %s", cxt->labels[i]->name));
		cxt->labels[i]->cxt = cxt;
	}

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

static void reset_context(struct fdisk_context *cxt)
{
	size_t nlbs, i;
	struct fdisk_label *lbs[ ARRAY_SIZE(cxt->labels) ];

	DBG(CONTEXT, dbgprint("\n-----\nresetting context %p", cxt));

	/* reset drives' private data */
	for (i = 0; i < cxt->nlabels; i++)
		fdisk_deinit_label(cxt->labels[i]);

	/* remember permanent setting */
	memcpy(lbs, cxt->labels, sizeof(lbs));
	nlbs = cxt->nlabels;

	/* free device specific stuff */
	if (cxt->dev_fd > -1)
		close(cxt->dev_fd);
	free(cxt->dev_path);
	free(cxt->firstsector);

	/* the reset */
	memset(cxt, 0, sizeof(*cxt));

	/* initialize */
	cxt->dev_fd = -1;

	/* set permanent setting */
	memcpy(cxt->labels, lbs, sizeof(lbs));
	cxt->nlabels = nlbs;
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
