
#include "lsblk.h"
#include "sysfs.h"


void lsblk_reset_iter(struct lsblk_iter *itr, int direction)
{
	if (direction == -1)
		direction = itr->direction;

	memset(itr, 0, sizeof(*itr));
	itr->direction = direction;
}

struct lsblk_device *lsblk_new_device(struct lsblk_devtree *tree)
{
	struct lsblk_device *dev;

	dev = calloc(1, sizeof(*dev));
	if (!dev)
		return NULL;

	dev->refcount = 1;
	dev->removable = -1;
	dev->tree = tree;

        INIT_LIST_HEAD(&dev->deps);
	INIT_LIST_HEAD(&dev->ls_roots);
	INIT_LIST_HEAD(&dev->ls_devices);

	DBG(DEV, ul_debugobj(dev, "alloc"));
	return dev;
}

void lsblk_ref_device(struct lsblk_device *dev)
{
	if (dev)
		dev->refcount++;
}


static int device_remove_dependence(struct lsblk_device *dev, struct lsblk_devdep *dep)
{
	if (!dev || !dep || list_empty(&dev->deps))
		return -EINVAL;

	DBG(DEV, ul_debugobj(dev, "  %s: deallocate dependence 0x%p [%s]", dev->name, dep, dep->child->name));
	list_del_init(&dep->ls_deps);
	lsblk_unref_device(dep->child);
	free(dep);
	return 0;
}

static int device_remove_dependences(struct lsblk_device *dev)
{
	if (!dev)
		return -EINVAL;

	DBG(DEV, ul_debugobj(dev, "%s: remove all depencences", dev->name));
	while (!list_empty(&dev->deps)) {
		struct lsblk_devdep *dp = list_entry(dev->deps.next,
					struct lsblk_devdep, ls_deps);
		device_remove_dependence(dev, dp);
	}
	return 0;
}

void lsblk_unref_device(struct lsblk_device *dev)
{
	if (!dev)
		return;

	if (--dev->refcount <= 0) {
		DBG(DEV, ul_debugobj(dev, "dealloc"));

		device_remove_dependences(dev);
		lsblk_device_free_properties(dev->properties);

		free(dev->name);
		free(dev->dm_name);
		free(dev->filename);
		free(dev->mountpoint);

		ul_unref_path(dev->sysfs);

		free(dev);
	}
}

int lsblk_device_has_dependence(struct lsblk_device *dev, struct lsblk_device *child)
{
	struct lsblk_device *x = NULL;
	struct lsblk_iter itr;

	lsblk_reset_iter(&itr, LSBLK_ITER_FORWARD);

	while (lsblk_device_next_child(dev, &itr, &x) == 0) {
		if (x == child)
			return 1;
	}

	return 0;
}

int lsblk_device_new_dependence(struct lsblk_device *parent, struct lsblk_device *child)
{
	struct lsblk_devdep *dp;

	if (!parent || !child)
		return -EINVAL;

	if (lsblk_device_has_dependence(parent, child))
		return 1;

	dp = calloc(1, sizeof(*dp));
	if (!dp)
		return -ENOMEM;

	INIT_LIST_HEAD(&dp->ls_deps);

	lsblk_ref_device(child);
	dp->child = child;

        DBG(DEV, ul_debugobj(parent, "add dependence 0x%p [%s->%s]", dp, parent->name, child->name));
        list_add_tail(&dp->ls_deps, &parent->deps);

	return 0;
}

int lsblk_device_next_child(struct lsblk_device *dev,
			  struct lsblk_iter *itr,
			  struct lsblk_device **child)
{
	int rc = 1;

	if (!dev || !itr || !child)
		return -EINVAL;
	*child = NULL;

	if (!itr->head)
		LSBLK_ITER_INIT(itr, &dev->deps);
	if (itr->p != itr->head) {
		struct lsblk_devdep *dp = NULL;

		LSBLK_ITER_ITERATE(itr, dp, struct lsblk_devdep, ls_deps);

		*child = dp->child;
		rc = 0;
	}

	return rc;
}


struct lsblk_devtree *lsblk_new_devtree()
{
	struct lsblk_devtree *tr;

	tr = calloc(1, sizeof(*tr));
	if (!tr)
		return NULL;

	tr->refcount = 1;

	INIT_LIST_HEAD(&tr->roots);
	INIT_LIST_HEAD(&tr->devices);

	DBG(TREE, ul_debugobj(tr, "alloc"));
	return tr;
}

void lsblk_ref_devtree(struct lsblk_devtree *tr)
{
	if (tr)
		tr->refcount++;
}

void lsblk_unref_devtree(struct lsblk_devtree *tr)
{
	if (!tr)
		return;

	if (--tr->refcount <= 0) {
		DBG(TREE, ul_debugobj(tr, "dealloc"));

		while (!list_empty(&tr->devices)) {
			struct lsblk_device *dev = list_entry(tr->devices.next,
						struct lsblk_device, ls_devices);
			lsblk_devtree_remove_device(tr, dev);
		}
		free(tr);
	}
}

int lsblk_devtree_add_root(struct lsblk_devtree *tr, struct lsblk_device *dev)
{
	if (!lsblk_devtree_has_device(tr, dev))
		lsblk_devtree_add_device(tr, dev);

	/* We don't increment reference counter for tr->roots list. The primary
	 * reference is tr->devices */

        DBG(TREE, ul_debugobj(tr, "add root device 0x%p [%s]", dev, dev->name));
        list_add_tail(&dev->ls_roots, &tr->roots);
	return 0;
}

int lsblk_devtree_next_root(struct lsblk_devtree *tr,
			    struct lsblk_iter *itr,
			    struct lsblk_device **dev)
{
	int rc = 1;

	if (!tr || !itr || !dev)
		return -EINVAL;
	*dev = NULL;
	if (!itr->head)
		LSBLK_ITER_INIT(itr, &tr->roots);
	if (itr->p != itr->head) {
		LSBLK_ITER_ITERATE(itr, *dev, struct lsblk_device, ls_roots);
		rc = 0;
	}
	return rc;
}

int lsblk_devtree_add_device(struct lsblk_devtree *tr, struct lsblk_device *dev)
{
	lsblk_ref_device(dev);

        DBG(TREE, ul_debugobj(tr, "add device 0x%p [%s]", dev, dev->name));
        list_add_tail(&dev->ls_devices, &tr->devices);
	return 0;
}

int lsblk_devtree_next_device(struct lsblk_devtree *tr,
			    struct lsblk_iter *itr,
			    struct lsblk_device **dev)
{
	int rc = 1;

	if (!tr || !itr || !dev)
		return -EINVAL;
	*dev = NULL;
	if (!itr->head)
		LSBLK_ITER_INIT(itr, &tr->devices);
	if (itr->p != itr->head) {
		LSBLK_ITER_ITERATE(itr, *dev, struct lsblk_device, ls_devices);
		rc = 0;
	}
	return rc;
}

int lsblk_devtree_has_device(struct lsblk_devtree *tr, struct lsblk_device *dev)
{
	struct lsblk_device *x = NULL;
	struct lsblk_iter itr;

	lsblk_reset_iter(&itr, LSBLK_ITER_FORWARD);

	while (lsblk_devtree_next_device(tr, &itr, &x) == 0) {
		if (x == dev)
			return 1;
	}

	return 0;
}

struct lsblk_device *lsblk_devtree_get_device(struct lsblk_devtree *tr, const char *name)
{
	struct lsblk_device *dev = NULL;
	struct lsblk_iter itr;

	lsblk_reset_iter(&itr, LSBLK_ITER_FORWARD);

	while (lsblk_devtree_next_device(tr, &itr, &dev) == 0) {
		if (strcmp(name, dev->name) == 0)
			return dev;
	}

	return NULL;
}

int lsblk_devtree_remove_device(struct lsblk_devtree *tr, struct lsblk_device *dev)
{
        DBG(TREE, ul_debugobj(tr, "remove device 0x%p [%s]", dev, dev->name));

	if (!lsblk_devtree_has_device(tr, dev))
		return 1;

	list_del_init(&dev->ls_roots);
	list_del_init(&dev->ls_devices);
	lsblk_unref_device(dev);

	return 0;
}

