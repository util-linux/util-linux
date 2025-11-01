/*
 * tag.c - allocation/initialization/free routines for tag structs
 *
 * Copyright (C) 2001 Andreas Dilger
 * Copyright (C) 2003 Theodore Ts'o
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 * %End-Header%
 */

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "blkidP.h"

static blkid_tag blkid_new_tag(void)
{
	blkid_tag tag;

	if (!(tag = calloc(1, sizeof(struct blkid_struct_tag))))
		return NULL;

	DBG(TAG, ul_debugobj(tag, "alloc"));
	INIT_LIST_HEAD(&tag->bit_tags);
	INIT_LIST_HEAD(&tag->bit_names);

	return tag;
}

void blkid_free_tag(blkid_tag tag)
{
	if (!tag)
		return;

	DBG(TAG, ul_debugobj(tag, "freeing tag %s (%s)", tag->bit_name, tag->bit_val));

	list_del(&tag->bit_tags);	/* list of tags for this device */
	list_del(&tag->bit_names);	/* list of tags with this type */

	free(tag->bit_name);
	free(tag->bit_val);

	free(tag);
}

/*
 * Find the desired tag on a device.  If value is NULL, then the
 * first such tag is returned, otherwise return only exact tag if found.
 */
blkid_tag blkid_find_tag_dev(blkid_dev dev, const char *type)
{
	struct list_head *p;

	list_for_each(p, &dev->bid_tags) {
		blkid_tag tmp = list_entry(p, struct blkid_struct_tag,
					   bit_tags);

		if (!strcmp(tmp->bit_name, type))
			return tmp;
	}
	return NULL;
}

static int blkid_compare_tag_value(blkid_tag tag, const char *value)
{
	static const char *caseinsensitive_tags[] = { "UUID", "PARTUUID" };

	for (size_t i = 0; i < ARRAY_SIZE(caseinsensitive_tags); i++)
	{
		if (strcmp(tag->bit_name, caseinsensitive_tags[i]) != 0)
			return strcasecmp(tag->bit_val, value);
	}
	return strcmp(tag->bit_val, value);
}

int blkid_dev_has_tag(blkid_dev dev, const char *type,
			     const char *value)
{
	blkid_tag		tag;

	tag = blkid_find_tag_dev(dev, type);
	if (!value)
		return (tag != NULL);
	if (!tag || blkid_compare_tag_value(tag, value) != 0)
		return 0;
	return 1;
}

/*
 * Find the desired tag type in the cache.
 * We return the head tag for this tag type.
 */
static blkid_tag blkid_find_head_cache(blkid_cache cache, const char *type)
{
	blkid_tag head = NULL, tmp;
	struct list_head *p;

	if (!cache || !type)
		return NULL;

	list_for_each(p, &cache->bic_tags) {
		tmp = list_entry(p, struct blkid_struct_tag, bit_tags);
		if (!strcmp(tmp->bit_name, type)) {
			DBG(TAG, ul_debug("found cache tag head %s", type));
			head = tmp;
			break;
		}
	}
	return head;
}

/*
 * Set a tag on an existing device.
 *
 * If value is NULL, then delete the tags from the device.
 */
int blkid_set_tag(blkid_dev dev, const char *name,
		  const char *value, const int vlength)
{
	blkid_tag	t = NULL, head = NULL;
	char		*val = NULL;
	char		**dev_var = NULL;

	if (value && !(val = strndup(value, vlength)))
		return -BLKID_ERR_MEM;

	/*
	 * Certain common tags are linked directly to the device struct
	 * We need to know what they are before we do anything else because
	 * the function name parameter might get freed later on.
	 */
	if (!strcmp(name, "TYPE"))
		dev_var = &dev->bid_type;
	else if (!strcmp(name, "LABEL"))
		dev_var = &dev->bid_label;
	else if (!strcmp(name, "UUID"))
		dev_var = &dev->bid_uuid;

	t = blkid_find_tag_dev(dev, name);
	if (!value) {
		if (t)
			blkid_free_tag(t);
	} else if (t) {
		if (!blkid_compare_tag_value(t, val)) {
			/* Same thing, exit */
			free(val);
			return 0;
		}
		DBG(TAG, ul_debugobj(t, "update (%s) '%s' -> '%s'", t->bit_name, t->bit_val, val));
		free(t->bit_val);
		t->bit_val = val;
	} else {
		/* Existing tag not present, add to device */
		if (!(t = blkid_new_tag()))
			goto errout;
		t->bit_name = strdup(name);
		t->bit_val = val;
		t->bit_dev = dev;

		DBG(TAG, ul_debugobj(t, "setting (%s) '%s'", t->bit_name, t->bit_val));
		list_add_tail(&t->bit_tags, &dev->bid_tags);

		if (dev->bid_cache) {
			head = blkid_find_head_cache(dev->bid_cache,
						     t->bit_name);
			if (!head) {
				head = blkid_new_tag();
				if (!head)
					goto errout;

				DBG(TAG, ul_debugobj(head, "creating new cache tag head %s", name));
				head->bit_name = strdup(name);
				if (!head->bit_name)
					goto errout;
				list_add_tail(&head->bit_tags,
					      &dev->bid_cache->bic_tags);
			}
			list_add_tail(&t->bit_names, &head->bit_names);
		}
	}

	/* Link common tags directly to the device struct */
	if (dev_var)
		*dev_var = val;

	if (dev->bid_cache)
		dev->bid_cache->bic_flags |= BLKID_BIC_FL_CHANGED;
	return 0;

errout:
	if (t)
		blkid_free_tag(t);
	else
		free(val);
	if (head)
		blkid_free_tag(head);
	return -BLKID_ERR_MEM;
}


/*
 * Parse a "NAME=value" string.  This is slightly different than
 * parse_token, because that will end an unquoted value at a space, while
 * this will assume that an unquoted value is the rest of the token (e.g.
 * if we are passed an already quoted string from the command-line we don't
 * have to both quote and escape quote so that the quotes make it to
 * us).
 *
 * Returns 0 on success, and -1 on failure.
 */
int blkid_parse_tag_string(const char *token, char **ret_type, char **ret_val)
{
	char *name, *value, *cp;

	DBG(TAG, ul_debug("trying to parse '%s' as a tag", token));

	if (!token || !(cp = strchr(token, '=')))
		return -1;

	name = strdup(token);
	if (!name)
		return -1;
	value = name + (cp - token);
	*value++ = '\0';
	if (*value == '"' || *value == '\'') {
		char c = *value++;
		if (!(cp = strrchr(value, c)))
			goto errout; /* missing closing quote */
		*cp = '\0';
	}

	if (ret_val) {
		value = *value ? strdup(value) : NULL;
		if (!value)
			goto errout;
		*ret_val = value;
	}

	if (ret_type)
		*ret_type = name;
	else
		free(name);

	return 0;

errout:
	DBG(TAG, ul_debug("parse error: '%s'", token));
	free(name);
	return -1;
}

/*
 * Tag iteration routines for the public libblkid interface.
 *
 * These routines do not expose the list.h implementation, which are a
 * contamination of the namespace, and which force us to reveal far, far
 * too much of our internal implementation.  I'm not convinced I want
 * to keep list.h in the long term, anyway.  It's fine for kernel
 * programming, but performance is not the #1 priority for this
 * library, and I really don't like the trade-off of type-safety for
 * performance for this application.  [tytso:20030125.2007EST]
 */

/*
 * This series of functions iterate over all tags in a device
 */
#define TAG_ITERATE_MAGIC	0x01a5284c

struct blkid_struct_tag_iterate {
	int			magic;
	blkid_dev		dev;
	struct list_head	*p;
};

blkid_tag_iterate blkid_tag_iterate_begin(blkid_dev dev)
{
	blkid_tag_iterate	iter;

	if (!dev) {
		errno = EINVAL;
		return NULL;
	}

	iter = malloc(sizeof(struct blkid_struct_tag_iterate));
	if (iter) {
		iter->magic = TAG_ITERATE_MAGIC;
		iter->dev = dev;
		iter->p	= dev->bid_tags.next;
	}
	return (iter);
}

/*
 * Return 0 on success, -1 on error
 */
int blkid_tag_next(blkid_tag_iterate iter,
			  const char **type, const char **value)
{
	blkid_tag tag;

	if (!type || !value ||
	    !iter || iter->magic != TAG_ITERATE_MAGIC ||
	    iter->p == &iter->dev->bid_tags)
		return -1;

	*type = NULL;
	*value = NULL;
	tag = list_entry(iter->p, struct blkid_struct_tag, bit_tags);
	*type = tag->bit_name;
	*value = tag->bit_val;
	iter->p = iter->p->next;
	return 0;
}

void blkid_tag_iterate_end(blkid_tag_iterate iter)
{
	if (!iter || iter->magic != TAG_ITERATE_MAGIC)
		return;
	iter->magic = 0;
	free(iter);
}

/*
 * This function returns a device which matches a particular
 * type/value pair.  If there is more than one device that matches the
 * search specification, it returns the one with the highest priority
 * value.  This allows us to give preference to EVMS or LVM devices.
 */
blkid_dev blkid_find_dev_with_tag(blkid_cache cache,
					 const char *type,
					 const char *value)
{
	blkid_tag	head;
	blkid_dev	dev;
	int		pri;
	struct list_head *p;
	int		probe_new = 0, probe_all = 0;

	if (!cache || !type || !value)
		return NULL;

	blkid_read_cache(cache);

	DBG(TAG, ul_debug("looking for tag %s=%s in cache", type, value));

try_again:
	pri = -1;
	dev = NULL;
	head = blkid_find_head_cache(cache, type);

	if (head) {
		list_for_each(p, &head->bit_names) {
			blkid_tag tmp = list_entry(p, struct blkid_struct_tag,
						   bit_names);

			if (!blkid_compare_tag_value(tmp, value) &&
			    (tmp->bit_dev->bid_pri > pri) &&
			    !access(tmp->bit_dev->bid_name, F_OK)) {
				dev = tmp->bit_dev;
				pri = dev->bid_pri;
			}
		}
	}
	if (dev && !(dev->bid_flags & BLKID_BID_FL_VERIFIED)) {
		dev = blkid_verify(cache, dev);
		if (!dev || dev->bid_flags & BLKID_BID_FL_VERIFIED)
			goto try_again;
	}

	if (!dev && !probe_new) {
		if (blkid_probe_all_new(cache) < 0)
			return NULL;
		probe_new++;
		goto try_again;
	}

	if (!dev && !probe_all
	    && !(cache->bic_flags & BLKID_BIC_FL_PROBED)) {
		if (blkid_probe_all(cache) < 0)
			return NULL;
		probe_all++;
		goto try_again;
	}
	return dev;
}

#ifdef TEST_PROGRAM
#ifdef HAVE_GETOPT_H
#include <getopt.h>
#else
extern char *optarg;
extern int optind;
#endif

static void __attribute__((__noreturn__)) usage(char *prog)
{
	fprintf(stderr, "Usage: %s [-f blkid_file] [-m debug_mask] device "
		"[type value]\n",
		prog);
	fprintf(stderr, "\tList all tags for a device and exit\n");
	exit(1);
}

int main(int argc, char **argv)
{
	blkid_tag_iterate	iter;
	blkid_cache 		cache = NULL;
	blkid_dev		dev;
	int			c, ret, found;
	int			flags = BLKID_DEV_FIND;
	char			*tmp;
	char			*file = NULL;
	char			*devname = NULL;
	char			*search_type = NULL;
	char			*search_value = NULL;
	const char		*type, *value;

	while ((c = getopt (argc, argv, "m:f:")) != EOF)
		switch (c) {
		case 'f':
			file = optarg;
			break;
		case 'm':
		{
			int mask = strtoul (optarg, &tmp, 0);
			if (*tmp) {
				fprintf(stderr, "Invalid debug mask: %s\n",
					optarg);
				exit(1);
			}
			blkid_init_debug(mask);
			break;
		}
		case '?':
			usage(argv[0]);
		}
	if (argc > optind)
		devname = argv[optind++];
	if (argc > optind)
		search_type = argv[optind++];
	if (argc > optind)
		search_value = argv[optind++];
	if (!devname || (argc != optind))
		usage(argv[0]);

	if ((ret = blkid_get_cache(&cache, file)) != 0) {
		fprintf(stderr, "%s: error creating cache (%d)\n",
			argv[0], ret);
		exit(1);
	}

	dev = blkid_get_dev(cache, devname, flags);
	if (!dev) {
		fprintf(stderr, "%s: cannot find device in blkid cache\n",
			devname);
		exit(1);
	}
	if (search_type) {
		found = blkid_dev_has_tag(dev, search_type, search_value);
		printf("Device %s: (%s, %s) %s\n", blkid_dev_devname(dev),
		       search_type, search_value ? search_value : "NULL",
		       found ? "FOUND" : "NOT FOUND");
		return(!found);
	}
	printf("Device %s...\n", blkid_dev_devname(dev));

	iter = blkid_tag_iterate_begin(dev);
	while (blkid_tag_next(iter, &type, &value) == 0) {
		printf("\tTag %s has value %s\n", type, value);
	}
	blkid_tag_iterate_end(iter);

	blkid_put_cache(cache);
	return (0);
}
#endif
