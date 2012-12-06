
#include "fdiskP.h"

/**
 * fdisk_new_context:
 * @fname: path to the device to be handled
 * @readonly: how to open the device
 *
 * If the @readonly flag is set to false, fdisk will attempt to open
 * the device with read-write mode and will fallback to read-only if
 * unsuccessful.
 *
 * Returns: newly allocated fdisk context or NULL upon failure.
 */
struct fdisk_context *fdisk_new_context_from_filename(const char *fname, int readonly)
{
	int fd, errsv = 0;
	struct fdisk_context *cxt = NULL;

	DBG(CONTEXT, dbgprint("initializing context for %s", fname));

	if (readonly == 1 || (fd = open(fname, O_RDWR)) < 0) {
		if ((fd = open(fname, O_RDONLY)) < 0)
			return NULL;
		readonly = 1;
	}

	cxt = calloc(1, sizeof(*cxt));
	if (!cxt)
		goto fail;

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
	return cxt;
fail:
	errsv = errno;
	fdisk_free_context(cxt);
	errno = errsv;

	DBG(CONTEXT, dbgprint("failed to initialize context for %s: %m", fname));
	return NULL;
}

/**
 * fdisk_free_context:
 * @cxt: fdisk context
 *
 * Deallocates context struct.
 */
void fdisk_free_context(struct fdisk_context *cxt)
{
	if (!cxt)
		return;

	DBG(CONTEXT, dbgprint("freeing context %p for %s", cxt, cxt->dev_path));
	close(cxt->dev_fd);
	free(cxt->dev_path);
	free(cxt->firstsector);
	free(cxt);
}
