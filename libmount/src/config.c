/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * This file is part of libmount from util-linux project.
 *
 * Copyright (C) 2026 Karel Zak <kzak@redhat.com>
 *
 * libmount is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 *
 * Read mount configuration files from /etc/mount/, /run/mount/, /usr/lib/mount/
 * using the ul_configs_file_list() infrastructure.
 *
 * Currently supported:
 *   fs.d/<fstype>.conf  -- per-filesystem settings (e.g., mounttype)
 */

#include "mountP.h"
#include "configs.h"
#include "fileutils.h"
#include "pathnames.h"
#include "strutils.h"

struct libmnt_confentry {
	struct list_head	entries;
	char			*dir;
	char			*name;
	char			*key;	/* NULL for sentinel */
	char			*value;
};

static void free_confentry(struct libmnt_confentry *en)
{
	if (!en)
		return;
	list_del_init(&en->entries);
	free(en->dir);
	free(en->name);
	free(en->key);
	free(en->value);
	free(en);
}

/*
 * Find cached entry. When @key is NULL, match any entry for dir+name
 * (to check if we have anything cached for this combination).
 * When @key is non-NULL, match exact key.
 */
static struct libmnt_confentry *get_entry(struct libmnt_context *cxt,
					  const char *dir,
					  const char *name,
					  const char *key)
{
	struct list_head *p;

	list_for_each(p, &cxt->config_entries) {
		struct libmnt_confentry *en = list_entry(p,
						struct libmnt_confentry, entries);

		if (strcmp(en->dir, dir) != 0
		    || strcmp(en->name, name) != 0)
			continue;

		if (!key)
			return en;	/* any entry for this dir+name */
		if (en->key && strcmp(en->key, key) == 0)
			return en;
	}

	return NULL;
}

#define is_parsed(_cxt, _dir, _name) \
	(get_entry((_cxt), (_dir), (_name), NULL) != NULL)

#define add_sentinel(_cxt, _dir, _name) \
	add_entry((_cxt), (_dir), (_name), NULL, NULL)

static int add_entry(struct libmnt_context *cxt,
		     const char *dir,
		     const char *name,
		     const char *key,
		     const char *value)
{
	struct libmnt_confentry *en;

	en = calloc(1, sizeof(*en));
	if (!en)
		return -ENOMEM;

	INIT_LIST_HEAD(&en->entries);
	en->dir = strdup(dir);
	en->name = strdup(name);
	en->key = key ? strdup(key) : NULL;
	en->value = value ? strdup(value) : NULL;

	if (!en->dir || !en->name
	    || (key && !en->key)
	    || (value && !en->value)) {
		free_confentry(en);
		return -ENOMEM;
	}

	list_add_tail(&en->entries, &cxt->config_entries);
	return 0;
}

/*
 * Parse "key = value" line.
 *
 * Returns 0 on success, 1 on skip (comment, blank, malformed).
 */
static int parse_config_line(char *buf, char **rkey, char **rval)
{
	char *p, *key, *val;

	p = strchr(buf, '#');
	if (p)
		*p = '\0';

	rtrim_whitespace((unsigned char *) buf);

	key = (char *) skip_blank(buf);
	if (!*key)
		return 1;

	val = strchr(key, '=');
	if (!val)
		return 1;

	*val++ = '\0';

	rtrim_whitespace((unsigned char *) key);
	val = (char *) skip_blank(val);

	if (!*key || !*val)
		return 1;

	*rkey = key;
	*rval = val;
	return 0;
}

/*
 * Read config file(s) for dir+name and cache all key=value pairs.
 * If no real entries were found, adds a sentinel (key==NULL) so we
 * don't re-read on the next call.
 *
 * Returns: 0 on success, <0 on error
 */
static int read_config(struct libmnt_context *cxt,
		       const char *dir,
		       const char *name)
{
	struct list_head file_list;
	struct list_head *current = NULL;
	char *filename = NULL;
	char confname[PATH_MAX];
	int count, nents = 0, rc = 0;

	snprintf(confname, sizeof(confname), "%s/%s", dir, name);

	count = ul_configs_file_list(&file_list,
				     "mount",
				     _PATH_SYSCONFDIR,
				     _PATH_RUNSTATEDIR,
				     _PATH_SYSCONFSTATICDIR,
				     confname,
				     "conf");
	if (count <= 0) {
		DBG_OBJ(CXT, cxt, ul_debug("config: no files for %s", confname));
		goto done;
	}

	while (ul_configs_next_filename(&file_list, &current, &filename) == 0) {
		struct stat st;
		FILE *f;
		char buf[BUFSIZ];

		DBG_OBJ(CXT, cxt, ul_debug("config: reading %s", filename));

		if (mnt_context_is_restricted(cxt)) {
			int fd = ul_open_no_symlinks(filename,
						O_RDONLY | O_CLOEXEC, 0);
			if (fd < 0)
				continue;
			f = fdopen(fd, "r");
			if (!f) {
				close(fd);
				continue;
			}
		} else {
			f = fopen(filename, "r");
			if (!f)
				continue;
		}

#ifndef TEST_PROGRAM
		{
			struct stat st;

			if (fstat(fileno(f), &st) != 0
			    || !S_ISREG(st.st_mode)
			    || st.st_uid != 0
			    || (st.st_mode & (S_IWGRP | S_IWOTH))) {
				DBG_OBJ(CXT, cxt, ul_debug("config: ignore unsafe %s", filename));
				fclose(f);
				continue;
			}
		}

		while (fgets(buf, sizeof(buf), f)) {
			char *key = NULL, *val = NULL;

			if (parse_config_line(buf, &key, &val) != 0)
				continue;

			if (!get_entry(cxt, dir, name, key)) {
				rc = add_entry(cxt, dir, name, key, val);
				if (rc < 0) {
					fclose(f);
					goto done;
				}
				nents++;
			}
		}

		fclose(f);
	}

done:
	if (rc == 0 && nents == 0)
		rc = add_sentinel(cxt, dir, name);

	if (count > 0)
		ul_configs_free_list(&file_list);
	return rc;
}

/**
 * mnt_config_get_value:
 * @cxt: mount context
 * @dir: config subdirectory (e.g., "fs.d")
 * @name: config name without suffix (e.g., "ntfs")
 * @key: key to look up (e.g., "mounttype")
 *
 * Reads /etc/mount/&lt;dir&gt;/&lt;name&gt;.conf (and drop-ins from /run, /usr/lib)
 * and returns the value for @key. Results are cached on the context so
 * each dir+name combination is parsed only once.
 *
 * Returns: value string (owned by context, do not free), or NULL
 */
const char *mnt_config_get_value(struct libmnt_context *cxt,
				 const char *dir,
				 const char *name,
				 const char *key)
{
	struct libmnt_confentry *en;

	if (!cxt || !dir || !name || !key)
		return NULL;

	en = get_entry(cxt, dir, name, key);
	if (en)
		return en->value;

	if (is_parsed(cxt, dir, name))
		return NULL;

	if (read_config(cxt, dir, name) < 0)
		return NULL;

	en = get_entry(cxt, dir, name, key);
	return en ? en->value : NULL;
}

/*
 * Deallocate all cached config entries.
 */
void mnt_free_config(struct libmnt_context *cxt)
{
	if (!cxt)
		return;

	while (!list_empty(&cxt->config_entries)) {
		struct libmnt_confentry *en = list_entry(
					cxt->config_entries.next,
					struct libmnt_confentry, entries);
		free_confentry(en);
	}
}
