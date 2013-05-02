
#include "fdiskP.h"
#include "pathnames.h"

#include <ctype.h>


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

/*
 * Return allocated buffer with partition name
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
		p = "p";

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
struct fdisk_label *fdisk_new_mac_label(struct fdisk_context *cxt) { return NULL; }
struct fdisk_label *fdisk_new_sgi_label(struct fdisk_context *cxt) { return NULL; }
struct fdisk_label *fdisk_new_sun_label(struct fdisk_context *cxt) { return NULL; }

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
