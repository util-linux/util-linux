/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Copyright (C) 2023 Thomas Weißschuh <thomas@t-8ch.de>
 */

#include <sys/prctl.h>
#include <sys/syscall.h>

#include "setpriv-landlock.h"

#include "strutils.h"
#include "xalloc.h"
#include "nls.h"
#include "c.h"

enum landlock_rule_type {
	LANDLOCK_RULE_PATH_BENEATH = 1,
	LANDLOCK_RULE_NET_PORT = 2,
};

struct landlock_ruleset_attr {
	uint64_t handled_access_fs;
};

struct landlock_path_beneath_attr {
	uint64_t allowed_access;
	int32_t parent_fd;
} __attribute__((packed));

#define LANDLOCK_CREATE_RULESET_VERSION			(1U << 0)
#define LANDLOCK_CREATE_RULESET_ERRATA			(1U << 1)

#define LANDLOCK_ACCESS_FS_EXECUTE			(1ULL << 0)
#define LANDLOCK_ACCESS_FS_WRITE_FILE			(1ULL << 1)
#define LANDLOCK_ACCESS_FS_READ_FILE			(1ULL << 2)
#define LANDLOCK_ACCESS_FS_READ_DIR			(1ULL << 3)
#define LANDLOCK_ACCESS_FS_REMOVE_DIR			(1ULL << 4)
#define LANDLOCK_ACCESS_FS_REMOVE_FILE			(1ULL << 5)
#define LANDLOCK_ACCESS_FS_MAKE_CHAR			(1ULL << 6)
#define LANDLOCK_ACCESS_FS_MAKE_DIR			(1ULL << 7)
#define LANDLOCK_ACCESS_FS_MAKE_REG			(1ULL << 8)
#define LANDLOCK_ACCESS_FS_MAKE_SOCK			(1ULL << 9)
#define LANDLOCK_ACCESS_FS_MAKE_FIFO			(1ULL << 10)
#define LANDLOCK_ACCESS_FS_MAKE_BLOCK			(1ULL << 11)
#define LANDLOCK_ACCESS_FS_MAKE_SYM			(1ULL << 12)
#define LANDLOCK_ACCESS_FS_REFER			(1ULL << 13)
#define LANDLOCK_ACCESS_FS_TRUNCATE			(1ULL << 14)
#define LANDLOCK_ACCESS_FS_IOCTL_DEV			(1ULL << 15)
#define LANDLOCK_ACCESS_FS_RESOLVE_UNIX			(1ULL << 16)

static inline int landlock_create_ruleset(
		const struct landlock_ruleset_attr *attr,
		size_t size, uint32_t flags)
{
	return syscall(__NR_landlock_create_ruleset, attr, size, flags);
}

static inline int landlock_add_rule(
		int ruleset_fd, enum landlock_rule_type rule_type,
		const void *rule_attr, uint32_t flags)
{
	return syscall(__NR_landlock_add_rule, ruleset_fd, rule_type,
		       rule_attr, flags);
}

static inline int landlock_restrict_self(int ruleset_fd, uint32_t flags)
{
	return syscall(__NR_landlock_restrict_self, ruleset_fd, flags);
}

#define SETPRIV_EXIT_PRIVERR 127	/* how we exit when we fail to set privs */

struct landlock_rule_entry {
	struct list_head head;
	enum landlock_rule_type rule_type;
	union {
		struct landlock_path_beneath_attr path_beneath_attr;
	};
};

static const struct {
	unsigned long long value;
	const char *type;
	const char *help;
} landlock_access_fs[] = {
	{ LANDLOCK_ACCESS_FS_EXECUTE,      "execute",      N_("execute a file") },
	{ LANDLOCK_ACCESS_FS_WRITE_FILE,   "write-file",   N_("open a file with write access") },
	{ LANDLOCK_ACCESS_FS_READ_FILE,    "read-file",    N_("open a file with read access") },
	{ LANDLOCK_ACCESS_FS_READ_DIR,     "read-dir",     N_("open a directory or list its content") },
	{ LANDLOCK_ACCESS_FS_REMOVE_DIR,   "remove-dir",   N_("remove an empty directory or rename one")  },
	{ LANDLOCK_ACCESS_FS_REMOVE_FILE,  "remove-file",  N_("unlink (or rename) a file") },
	{ LANDLOCK_ACCESS_FS_MAKE_CHAR,    "make-char",    N_("create (or rename or link) a character device") },
	{ LANDLOCK_ACCESS_FS_MAKE_DIR,     "make-dir",     N_("create (or rename) a directory") },
	{ LANDLOCK_ACCESS_FS_MAKE_REG,     "make-reg",     N_("create (or rename or link) a regular file") },
	{ LANDLOCK_ACCESS_FS_MAKE_SOCK,    "make-sock",    N_("create (or rename or link) a UNIX domain socket") },
	{ LANDLOCK_ACCESS_FS_MAKE_FIFO,    "make-fifo",    N_("create (or rename or link) a named pipe") },
	{ LANDLOCK_ACCESS_FS_MAKE_BLOCK,   "make-block",   N_("create (or rename or link) a block device") },
	{ LANDLOCK_ACCESS_FS_MAKE_SYM,     "make-sym",     N_("create (or rename or link) a symbolic link") },
	{ LANDLOCK_ACCESS_FS_REFER,        "refer",        N_("link or rename a file from or to a different directory") },
	{ LANDLOCK_ACCESS_FS_TRUNCATE,     "truncate",     N_("truncate a file with truncate(2)") },
	{ LANDLOCK_ACCESS_FS_IOCTL_DEV,    "ioctl-dev",    N_("invoke ioctl(2) on an opened character or block device") },
	{ LANDLOCK_ACCESS_FS_RESOLVE_UNIX, "resolve-unix", N_("connect(2) or bind(2) a pathname UNIX domain socket") },
};

/* cumulative access_fs rights supported by each landlock ABI version, indexed by (abi - 1) */
static const uint64_t landlock_access_fs_mask[] = {
	/* ABI 1 */ (LANDLOCK_ACCESS_FS_MAKE_SYM << 1) - 1,
	/* ABI 2 */ (LANDLOCK_ACCESS_FS_REFER << 1) - 1,
	/* ABI 3 */ (LANDLOCK_ACCESS_FS_TRUNCATE << 1) - 1,
	/* ABI 4 */ (LANDLOCK_ACCESS_FS_TRUNCATE << 1) - 1,
	/* ABI 5 */ (LANDLOCK_ACCESS_FS_IOCTL_DEV << 1) - 1,
	/* ABI 6 */ (LANDLOCK_ACCESS_FS_IOCTL_DEV << 1) - 1,
	/* ABI 7 */ (LANDLOCK_ACCESS_FS_IOCTL_DEV << 1) - 1,
	/* ABI 8 */ (LANDLOCK_ACCESS_FS_IOCTL_DEV << 1) - 1,
	/* ABI 9 */ (LANDLOCK_ACCESS_FS_RESOLVE_UNIX << 1) - 1,
};

static int supported_landlock_abi(void)
{
	static int abi = -1;

	if (abi < 0) {
		errno = 0;
		abi = landlock_create_ruleset(NULL, 0, LANDLOCK_CREATE_RULESET_VERSION);
		if (abi <= 0) {
			switch (errno) {
			case ENOSYS:
				err(EXIT_FAILURE, _("landlock is not supported"));
			case EOPNOTSUPP:
				err(EXIT_FAILURE, _("landlock is supported but currently disabled"));
			default:
				err(EXIT_FAILURE, _("failed to obtain Landlock ABI version"));
			}
		}
	}
	return abi;
}

static uint64_t landlock_abi_fs_mask(void)
{
	int abi = supported_landlock_abi();
	size_t n = ARRAY_SIZE(landlock_access_fs_mask);
	size_t idx = (size_t) (abi - 1);

	if (idx >= n)
		idx = n - 1;
	return landlock_access_fs_mask[idx];
}

static long landlock_access_to_mask(const char *str, size_t len)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(landlock_access_fs); i++)
		if (strncmp(landlock_access_fs[i].type, str, len) == 0)
			return landlock_access_fs[i].value;
	return -1;
}

static uint64_t parse_landlock_fs_access(const char *list)
{
	unsigned long r = 0;

	if (string_to_bitmask(list, &r, landlock_access_to_mask))
		errx(EXIT_FAILURE,
		     _("could not parse landlock fs access: %s"), list);

	return r;
}

void parse_landlock_access(struct setpriv_landlock_opts *opts, const char *str)
{
	const char *type;

	type = ul_startswith(str, "fs:");

	/* without argument, match all supported by the current kernel */
	if (strcmp(str, "fs") == 0 || (type && type[0] == '\0')) {
		opts->access_fs |= landlock_abi_fs_mask();
		return;
	}

	if (type)
		opts->access_fs |= parse_landlock_fs_access(type);
}

void parse_landlock_rule(struct setpriv_landlock_opts *opts, const char *str)
{
	struct landlock_rule_entry *rule = xmalloc(sizeof(*rule));
	const char *accesses, *path;
	char *accesses_part;
	int parent_fd;

	accesses = ul_startswith(str, "path-beneath:");
	if (!accesses)
		errx(EXIT_FAILURE, _("invalid landlock rule: %s"), str);
	path = strchr(accesses, ':');
	if (!path)
		errx(EXIT_FAILURE, _("invalid landlock rule: %s"), str);
	rule->rule_type = LANDLOCK_RULE_PATH_BENEATH;

	accesses_part = xstrndup(accesses, path - accesses);
	if (accesses_part[0] != '\0')
		rule->path_beneath_attr.allowed_access = parse_landlock_fs_access(accesses_part);
	else
		rule->path_beneath_attr.allowed_access = 0;
	free(accesses_part);

	path++;

	parent_fd = open(path, O_RDONLY | O_PATH | O_CLOEXEC);
	if (parent_fd == -1)
		err(EXIT_FAILURE, _("could not open file for landlock: %s"), path);

	rule->path_beneath_attr.parent_fd = parent_fd;

	list_add(&rule->head, &opts->rules);
}

void init_landlock_opts(struct setpriv_landlock_opts *opts)
{
	INIT_LIST_HEAD(&opts->rules);
}

void do_landlock(const struct setpriv_landlock_opts *opts)
{
	struct landlock_rule_entry *rule;
	struct list_head *entry;
	int fd, ret;
	struct landlock_path_beneath_attr path_beneath_attr;

	list_for_each(entry, &opts->rules) {
		rule = list_entry(entry, struct landlock_rule_entry, head);
		if (rule->rule_type == LANDLOCK_RULE_PATH_BENEATH && !opts->access_fs) {
			errx(EXIT_FAILURE,
				_("landlock path-beneath rule requires a filesystem access restriction (--landlock-access fs)"));
		}
	}

	if (!opts->access_fs)
		return;

	const struct landlock_ruleset_attr ruleset_attr = {
		.handled_access_fs = opts->access_fs,
	};

	fd = landlock_create_ruleset(&ruleset_attr, sizeof(ruleset_attr), 0);
	if (fd == -1)
		err(SETPRIV_EXIT_PRIVERR, _("landlock_create_ruleset failed"));

	list_for_each(entry, &opts->rules) {
		rule = list_entry(entry, struct landlock_rule_entry, head);

		assert(rule->rule_type == LANDLOCK_RULE_PATH_BENEATH);

		path_beneath_attr = rule->path_beneath_attr;
		if (!path_beneath_attr.allowed_access)
			path_beneath_attr.allowed_access = opts->access_fs;

		ret = landlock_add_rule(fd, rule->rule_type, &path_beneath_attr, 0);
		if (ret == -1)
			err(SETPRIV_EXIT_PRIVERR, _("adding landlock rule failed"));
	}

	if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) == -1)
		err(SETPRIV_EXIT_PRIVERR, _("disallow granting new privileges for landlock failed"));

	if (landlock_restrict_self(fd, 0) == -1)
		err(SETPRIV_EXIT_PRIVERR, _("landlock_restrict_self failed"));
}

void usage_landlock(FILE *out)
{
	size_t i;

	fputs(USAGE_ARGUMENTS, out);
	fputs(_(" <access> is a landlock access; syntax is fs[:<right>, ...>]\n"), out);
	fputs(_(" <rule> is a landlock rule; syntax is <type>:<right>:<argument>\n"), out);

	fputs(USAGE_SEPARATOR, out);
	fputs(_(" available landlock rule types are:\n"), out);
	/* TRANSLATORS: Keep *{path-beneath}* untranslated, it's a type name */
	fputs(_("  path-beneath - filesystem based rule; <argument> is a path\n"), out);

	fputs(USAGE_SEPARATOR, out);
	fputs(_(" available landlock filesystems rights are:\n"), out);
	for (i = 0; i < ARRAY_SIZE(landlock_access_fs); i++) {
		fprintf(out, "  %12s - %s\n", landlock_access_fs[i].type,
					_(landlock_access_fs[i].help));
	}
}

void list_landlock_support(void)
{
	int abi, errata;
	unsigned i;
	uint64_t mask;

	abi = supported_landlock_abi();
	errata = landlock_create_ruleset(NULL, 0, LANDLOCK_CREATE_RULESET_ERRATA);
	if (errata < 0) errata = 0;

	printf(_("ABI version: %d\n"), abi);
	printf(_("ABI errata:"));
	for (i = 1; errata != 0; errata >>= 1, i++) {
		if (errata & 1)
			printf(" %u", i);
	}
	printf("\n");

	printf(_("access: %s\n"), "fs");

	mask = landlock_abi_fs_mask();
	printf(_("%s rights:"), "fs");
	for (i = 0; i < ARRAY_SIZE(landlock_access_fs); i++)
		if (landlock_access_fs[i].value & mask)
			printf(" %s", landlock_access_fs[i].type);

	printf("\n");

	printf(_("%s rules: %s\n"), "fs", "path-beneath");
}

void list_landlock_access(void)
{
	printf("fs\n");
}

void list_landlock_rights(const char *access)
{
	uint64_t mask;
	size_t i;

	if (strcmp(access, "fs") != 0)
		errx(EXIT_FAILURE, _("unknown landlock access: %s"), access);

	mask = landlock_abi_fs_mask();
	for (i = 0; i < ARRAY_SIZE(landlock_access_fs); i++)
		if (landlock_access_fs[i].value & mask)
			printf("%s\n", landlock_access_fs[i].type);
}
