/*
 * Copyright (C) 2016 David Sterba <dsterba@suse.cz>
 * Copyright (C) 2016 Stanislav Brabec <sbrabec@suse.cz>
 *
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 */

/*
 * SECTION: btrfs
 * @title: btrfs
 * @short_description: special function for btrfs
 *
 * btrfs contains function needed for manipulation with btrfs.
 */
#include <dirent.h>
#include <sys/ioctl.h>
#include <linux/magic.h>
#include "btrfs.h"

/*
 * btrfs_get_default_subvol_id:
 * @path: Path to mounted btrfs volume
 *
 * Searches for the btrfs default subvolume id.
 *
 * Returns: default subvolume id or UINT64_MAX (-1) in case of no
 * default subvolume or error. In case of error, errno is set
 * properly.
 */
uint64_t btrfs_get_default_subvol_id(const char *path)
{
	int iocret;
	int fd;
	DIR *dirstream = NULL;
	struct btrfs_ioctl_search_args args;
	struct btrfs_ioctl_search_key *sk = &args.key;
	struct btrfs_ioctl_search_header *sh;
	uint64_t found = UINT64_MAX;

	dirstream = opendir(path);
	if (!dirstream) {
		DBG(BTRFS, ul_debug("opendir() failed for \"%s\" [errno=%d %m]", path, errno));
		return UINT64_MAX;
	}
	fd = dirfd(dirstream);
	if (fd < 0) {
		DBG(BTRFS, ul_debug("dirfd(opendir()) failed for \"%s\" [errno=%d %m]", path, errno));
		goto out;
	}

	memset(&args, 0, sizeof(args));
	sk->tree_id = BTRFS_ROOT_TREE_OBJECTID;
	sk->min_objectid = BTRFS_ROOT_TREE_DIR_OBJECTID;
	sk->max_objectid = BTRFS_ROOT_TREE_DIR_OBJECTID;
	sk->min_type = BTRFS_DIR_ITEM_KEY;
	sk->max_type = BTRFS_DIR_ITEM_KEY;
	sk->max_offset = UINT64_MAX;
	sk->max_transid = UINT64_MAX;
	sk->nr_items = 1;

	iocret = ioctl(fd, BTRFS_IOC_TREE_SEARCH, &args);
	if (iocret < 0) {
		DBG(BTRFS, ul_debug("ioctl() failed for \"%s\" [errno=%d %m]", path, errno));
		goto out;
	}

	/* the ioctl returns the number of items it found in nr_items */
	if (sk->nr_items == 0) {
		DBG(BTRFS, ul_debug("root tree dir object id not found"));
		goto out;
	}
	DBG(BTRFS, ul_debug("found %d root tree dir object id items", sk->nr_items));

	sh = (struct btrfs_ioctl_search_header *)args.buf;

	if (sh->type == BTRFS_DIR_ITEM_KEY) {
		struct btrfs_dir_item *di;
		int name_len;
		char *name;

		di = (struct btrfs_dir_item *)(sh + 1);
		name_len = btrfs_stack_dir_name_len(di);
		name = (char *)(di + 1);

		if (!strncmp("default", name, name_len)) {
			found = btrfs_disk_key_objectid(&di->location);
			DBG(BTRFS, ul_debug("\"default\" id is %llu", (unsigned long long)found));
		} else {
			DBG(BTRFS, ul_debug("\"default\" id not found in tree root"));
			goto out;
		}
	} else {
		DBG(BTRFS, ul_debug("unexpected type found: %d", (int)sh->type));
		goto out;
	}

out:
	closedir(dirstream);

	return found;
}
