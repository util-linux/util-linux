
#include "fdiskP.h"

/*
 * Zeros in-memory first sector buffer
 */
void fdisk_zeroize_firstsector(struct fdisk_context *cxt)
{
	if (!cxt || !cxt->firstsector)
		return;

	DBG(CONTEXT, dbgprint("zeroize in-memory first sector buffer"));
	memset(cxt->firstsector, 0, cxt->sector_size);
}

int fdisk_read_firstsector(struct fdisk_context *cxt)
{
	ssize_t r;

	assert(cxt);
	assert(cxt->sector_size);

	DBG(TOPOLOGY, dbgprint("initialize first sector "
				"buffer [sector_size=%lu]", cxt->sector_size));

	if (!cxt->firstsector) {
		cxt->firstsector = calloc(1, cxt->sector_size);
		if (!cxt->firstsector)
			return -ENOMEM;
	} else
		fdisk_zeroize_firstsector(cxt);

	r = read(cxt->dev_fd, cxt->firstsector, cxt->sector_size);

	if (r != cxt->sector_size) {
		if (!errno)
			errno = EINVAL;	/* probably too small file/device */
		DBG(TOPOLOGY, dbgprint("failed to read first sector %m"));
		return -errno;
	}

	return 0;
}
