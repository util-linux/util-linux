
#include "fdiskP.h"
#include "pathnames.h"

#include <ctype.h>

/**
 * SECTION: utils
 * @title: Utils
 * @short_description: misc fdisk functions
 */

/*
 * Zeros in-memory first sector buffer
 */
int fdisk_init_firstsector_buffer(struct fdisk_context *cxt)
{
	if (!cxt)
		return -EINVAL;

	if (!cxt->firstsector || cxt->firstsector_bufsz != cxt->sector_size) {
		/* Let's allocate a new buffer if no allocated yet, or the
		 * current buffer has incorrect size */
		if (!cxt->parent || cxt->parent->firstsector != cxt->firstsector)
			free(cxt->firstsector);

		DBG(CXT, ul_debugobj(cxt, "initialize in-memory first sector "
				"buffer [sector_size=%lu]", cxt->sector_size));
		cxt->firstsector = calloc(1, cxt->sector_size);
		if (!cxt->firstsector)
			return -ENOMEM;

		cxt->firstsector_bufsz = cxt->sector_size;
		return 0;
	}

	DBG(CXT, ul_debugobj(cxt, "zeroize in-memory first sector buffer"));
	memset(cxt->firstsector, 0, cxt->firstsector_bufsz);
	return 0;
}

int fdisk_read_firstsector(struct fdisk_context *cxt)
{
	ssize_t r;
	int rc;

	assert(cxt);
	assert(cxt->sector_size);

	rc = fdisk_init_firstsector_buffer(cxt);
	if (rc)
		return rc;

	assert(cxt->sector_size == cxt->firstsector_bufsz);

	DBG(CXT, ul_debugobj(cxt, "reading first sector "
				"buffer [sector_size=%lu]", cxt->sector_size));

	r = lseek(cxt->dev_fd, 0, SEEK_SET);
	if (r == -1)
	{
		DBG(CXT, ul_debugobj(cxt, "failed to seek to first sector %m"));
		return -errno;
	}

	r = read(cxt->dev_fd, cxt->firstsector, cxt->sector_size);

	if (r != cxt->sector_size) {
		if (!errno)
			errno = EINVAL;	/* probably too small file/device */
		DBG(CXT, ul_debugobj(cxt, "failed to read first sector %m"));
		return -errno;
	}

	return 0;
}

/**
 * fdisk_partname:
 * @dev: device name
 * @partno: partition name
 *
 * Return: allocated buffer with partition name, use free() to deallocate.
 */
char *fdisk_partname(const char *dev, size_t partno)
{
	char *res = NULL;
	const char *p = "";
	int w = 0;

	if (!dev || !*dev) {
		if (asprintf(&res, "%zd", partno) > 0)
			return res;
		return NULL;
	}

	w = strlen(dev);
	if (isdigit(dev[w - 1]))
#ifdef __GNU__
		p = "s";
#else
		p = "p";
#endif

	/* devfs kludge - note: fdisk partition names are not supposed
	   to equal kernel names, so there is no reason to do this */
	if (strcmp(dev + w - 4, "disc") == 0) {
		w -= 4;
		p = "part";
	}

	/* udev names partitions by appending -partN
	   e.g. ata-SAMSUNG_SV8004H_0357J1FT712448-part1 */
	if ((strncmp(dev, _PATH_DEV_BYID, sizeof(_PATH_DEV_BYID) - 1) == 0) ||
	     strncmp(dev, _PATH_DEV_BYPATH, sizeof(_PATH_DEV_BYPATH) - 1) == 0) {
	       p = "-part";
	}

	if (asprintf(&res, "%.*s%s%zu", w, dev, p, partno) > 0)
		return res;

	return NULL;
}

#ifdef TEST_PROGRAM
struct fdisk_label *fdisk_new_dos_label(struct fdisk_context *cxt) { return NULL; }
struct fdisk_label *fdisk_new_bsd_label(struct fdisk_context *cxt) { return NULL; }

int test_partnames(struct fdisk_test *ts, int argc, char *argv[])
{
	size_t i;
	const char *disk = argv[1];

	for (i = 0; i < 5; i++) {
		char *p = fdisk_partname(disk, i + 1);
		if (p)
			printf("%zu: '%s'\n", i + 1, p);
		free(p);
	}

	return 0;
}

int main(int argc, char *argv[])
{
	struct fdisk_test tss[] = {
		{ "--partnames",  test_partnames,  "<diskname>" },
		{ NULL }
	};

	return fdisk_run_test(tss, argc, argv);
}

#endif
