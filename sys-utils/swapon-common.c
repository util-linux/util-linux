#include <blkid.h>

#include "c.h"
#include "nls.h"
#include "swapheader.h"
#include "swapon-common.h"
#include "xalloc.h"

/*
 * content of /proc/swaps and /etc/fstab
 */
static struct libmnt_table *swaps, *fstab;

struct libmnt_cache *mntcache;

struct libmnt_table *get_fstab(void)
{
	if (!fstab) {
		fstab = mnt_new_table();
		if (!fstab)
			return NULL;
		mnt_table_set_cache(fstab, mntcache);
		if (mnt_table_parse_fstab(fstab, NULL) != 0)
			return NULL;
	}

	return fstab;
}

struct libmnt_table *get_swaps(void)
{
	if (!swaps) {
		swaps = mnt_new_table();
		if (!swaps)
			return NULL;
		mnt_table_set_cache(swaps, mntcache);
		if (mnt_table_parse_swaps(swaps, NULL) != 0)
			return NULL;
	}

	return swaps;
}

void free_tables(void)
{
	mnt_unref_table(swaps);
	mnt_unref_table(fstab);
}

int match_swap(struct libmnt_fs *fs, void *data __attribute__((unused)))
{
	return fs && mnt_fs_is_swaparea(fs);
}

int is_active_swap(const char *filename)
{
	struct libmnt_table *st = get_swaps();
	return st && mnt_table_find_source(st, filename, MNT_ITER_BACKWARD);
}


int cannot_find(const char *special)
{
	warnx(_("cannot find the device for %s"), special);
	return -1;
}

/*
 * Lists with -L and -U option
 */
static const char **llist;
static size_t llct;
static const char **ulist;
static size_t ulct;


void add_label(const char *label)
{
	llist = (const char **) xrealloc(llist, (++llct) * sizeof(char *));
	llist[llct - 1] = label;
}

const char *get_label(size_t i)
{
	return i < llct ? llist[i] : NULL;
}

size_t numof_labels(void)
{
	return llct;
}

void add_uuid(const char *uuid)
{
	ulist = (const char **) xrealloc(ulist, (++ulct) * sizeof(char *));
	ulist[ulct - 1] = uuid;
}

const char *get_uuid(size_t i)
{
	return i < ulct ? ulist[i] : NULL;
}

size_t numof_uuids(void)
{
	return ulct;
}

blkid_probe get_swap_prober(const char *devname)
{
	blkid_probe pr;
	int rc;
	const char *version = NULL;
	char *swap_filter[] = { "swap", NULL };

	pr = blkid_new_probe_from_filename(devname);
	if (!pr) {
		warn(_("%s: unable to probe device"), devname);
		return NULL;
	}

	blkid_probe_enable_superblocks(pr, TRUE);
	blkid_probe_set_superblocks_flags(pr,
			BLKID_SUBLKS_LABEL | BLKID_SUBLKS_UUID |
			BLKID_SUBLKS_VERSION);

	blkid_probe_filter_superblocks_type(pr, BLKID_FLTR_ONLYIN, swap_filter);

	rc = blkid_do_safeprobe(pr);
	if (rc == -1)
		warn(_("%s: unable to probe device"), devname);
	else if (rc == -2)
		warnx(_("%s: ambivalent probing result, use wipefs(8)"), devname);
	else if (rc == 1)
		warnx(_("%s: not a valid swap partition"), devname);

	if (rc == 0) {
		/* Only the SWAPSPACE2 is supported. */
		if (blkid_probe_lookup_value(pr, "VERSION", &version, NULL) == 0
		    && version
		    && strcmp(version, stringify_value(SWAP_VERSION)))
			warnx(_("%s: unsupported swap version '%s'"),
						devname, version);
		else
			return pr;
	}

	blkid_free_probe(pr);
	return NULL;
}
