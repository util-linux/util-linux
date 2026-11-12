/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Copyright (C) 2023 Thomas Weißschuh <thomas@t-8ch.de>
 * Copyright (C) 2026 Skye Soss <skye@soss.website>
 */

#include <sys/prctl.h>
#include <sys/syscall.h>
#include <cap-ng.h>

#include "setpriv-landlock.h"

#include "caputils.h"
#include "namespace.h"
#include "strutils.h"
#include "xalloc.h"
#include "nls.h"
#include "c.h"

enum landlock_rule_type {
	LANDLOCK_RULE_PATH_BENEATH = 1,
	LANDLOCK_RULE_NET_PORT,
	LANDLOCK_RULE_NAMESPACE,
	LANDLOCK_RULE_CAPABILITY,
};

struct landlock_ruleset_attr {
	uint64_t handled_access_fs;
	uint64_t handled_access_net;
	uint64_t scoped;
	uint64_t quiet_access_fs;
	uint64_t quiet_access_net;
	uint64_t quiet_scoped;
	uint64_t handled_permissions;
};

struct landlock_path_beneath_attr {
	uint64_t allowed_access;
	int32_t parent_fd;
} __attribute__((packed));

struct landlock_net_port_attr {
	uint64_t allowed_access;
	uint64_t port;
};

struct landlock_namespace_attr {
	uint64_t permissions;
	uint64_t allowed_namespace_types;
	uint64_t quiet_namespace_types;
};

struct landlock_capability_attr {
	uint64_t permissions;
	uint64_t allowed_capabilities;
	uint64_t quiet_capabilities;
};

#define LANDLOCK_CREATE_RULESET_VERSION			(1U << 0)
#define LANDLOCK_CREATE_RULESET_ERRATA			(1U << 1)

#define LANDLOCK_ADD_RULE_QUIET				(1U << 0)

#define LANDLOCK_RESTRICT_SELF_LOG_SAME_EXEC_OFF	(1U << 0)
#define LANDLOCK_RESTRICT_SELF_LOG_NEW_EXEC_ON		(1U << 1)
#define LANDLOCK_RESTRICT_SELF_LOG_SUBDOMAINS_OFF	(1U << 2)
#define LANDLOCK_RESTRICT_SELF_TSYNC			(1U << 3)
#define LANDLOCK_RESTRICT_SELF_NO_NEW_PRIVS		(1U << 4)

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

#define LANDLOCK_ACCESS_NET_BIND_TCP			(1ULL << 0)
#define LANDLOCK_ACCESS_NET_CONNECT_TCP			(1ULL << 1)
#define LANDLOCK_ACCESS_NET_BIND_UDP			(1ULL << 2)
#define LANDLOCK_ACCESS_NET_CONNECT_SEND_UDP		(1ULL << 3)

#define LANDLOCK_SCOPE_ABSTRACT_UNIX_SOCKET		(1ULL << 0)
#define LANDLOCK_SCOPE_SIGNAL				(1ULL << 1)

#define LANDLOCK_PERMISSION_NAMESPACE_USE		(1ULL << 0)
#define LANDLOCK_PERMISSION_CAPABILITY_USE		(1ULL << 1)

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
		struct landlock_net_port_attr net_port_attr;
		struct landlock_namespace_attr namespace_attr;
		struct landlock_capability_attr capability_attr;
	};
};

struct landlock_restriction {
	unsigned long long value;
	const char *type;
	const char *help;
};

static const struct landlock_restriction landlock_access_fs[] = {
	{ LANDLOCK_ACCESS_FS_EXECUTE,	   "execute",	   N_("execute a file") },
	{ LANDLOCK_ACCESS_FS_WRITE_FILE,   "write-file",   N_("open a file with write access") },
	{ LANDLOCK_ACCESS_FS_READ_FILE,    "read-file",    N_("open a file with read access") },
	{ LANDLOCK_ACCESS_FS_READ_DIR,	   "read-dir",	   N_("open a directory or list its content") },
	{ LANDLOCK_ACCESS_FS_REMOVE_DIR,   "remove-dir",   N_("remove an empty directory or rename one")  },
	{ LANDLOCK_ACCESS_FS_REMOVE_FILE,  "remove-file",  N_("unlink (or rename) a file") },
	{ LANDLOCK_ACCESS_FS_MAKE_CHAR,    "make-char",    N_("create (or rename or link) a character device") },
	{ LANDLOCK_ACCESS_FS_MAKE_DIR,	   "make-dir",	   N_("create (or rename) a directory") },
	{ LANDLOCK_ACCESS_FS_MAKE_REG,	   "make-reg",	   N_("create (or rename or link) a regular file") },
	{ LANDLOCK_ACCESS_FS_MAKE_SOCK,    "make-sock",    N_("create (or rename or link) a UNIX domain socket") },
	{ LANDLOCK_ACCESS_FS_MAKE_FIFO,    "make-fifo",    N_("create (or rename or link) a named pipe") },
	{ LANDLOCK_ACCESS_FS_MAKE_BLOCK,   "make-block",   N_("create (or rename or link) a block device") },
	{ LANDLOCK_ACCESS_FS_MAKE_SYM,	   "make-sym",	   N_("create (or rename or link) a symbolic link") },
	{ LANDLOCK_ACCESS_FS_REFER,	   "refer",	   N_("link or rename a file from or to a different directory") },
	{ LANDLOCK_ACCESS_FS_TRUNCATE,	   "truncate",	   N_("truncate a file with truncate(2)") },
	{ LANDLOCK_ACCESS_FS_IOCTL_DEV,    "ioctl-dev",    N_("invoke ioctl(2) on an opened character or block device") },
	{ LANDLOCK_ACCESS_FS_RESOLVE_UNIX, "resolve-unix", N_("connect(2) or bind(2) a pathname UNIX domain socket") },
};

static const struct landlock_restriction landlock_access_net[] = {
	{ LANDLOCK_ACCESS_NET_BIND_TCP,		"bind-tcp",	    N_("bind(2) a TCP socket") },
	{ LANDLOCK_ACCESS_NET_CONNECT_TCP,	"connect-tcp",	    N_("connect(2) a TCP socket") },
	{ LANDLOCK_ACCESS_NET_BIND_UDP,		"bind-udp",	    N_("bind(2) a UDP socket") },
	{ LANDLOCK_ACCESS_NET_CONNECT_SEND_UDP,	"connect-send-udp", N_("connect(2) or send(2) a UDP socket") },
};

static const struct landlock_restriction landlock_scope_restriction[] = {
	{ LANDLOCK_SCOPE_ABSTRACT_UNIX_SOCKET, "abstract-unix-socket", N_("access to abstract UNIX sockets") },
	{ LANDLOCK_SCOPE_SIGNAL,	       "signal",	       N_("ability to send signals to processes") },
};

static const struct landlock_restriction landlock_permission[] = {
	{ LANDLOCK_PERMISSION_NAMESPACE_USE,  "namespace-use",  N_("use of namespace types") },
	{ LANDLOCK_PERMISSION_CAPABILITY_USE, "capability-use", N_("use of Linux capabilities") },
};

static const struct landlock_restriction landlock_namespace_type[] = {
	{ CLONE_NEWNS,	   "mnt",    N_("mount namespace") },
	{ CLONE_NEWNET,	   "net",    N_("network namespace") },
	{ CLONE_NEWPID,	   "pid",    N_("PID namespace") },
	{ CLONE_NEWUTS,	   "uts",    N_("UTS namespace") },
	{ CLONE_NEWIPC,	   "ipc",    N_("System V IPC namespace") },
	{ CLONE_NEWUSER,   "user",   N_("user namespace") },
	{ CLONE_NEWCGROUP, "cgroup", N_("cgroup namespace") },
	{ CLONE_NEWTIME,   "time",   N_("time namespace") },
};

/* cumulative access_fs rights supported by each Landlock ABI version, indexed by (abi - 1) */
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

/* cumulative access_net rights supported by each Landlock ABI version, indexed by (abi - 1) */
static const uint64_t landlock_access_net_mask[] = {
	/* ABI 1 */ 0,
	/* ABI 2 */ 0,
	/* ABI 3 */ 0,
	/* ABI 4 */ (LANDLOCK_ACCESS_NET_CONNECT_TCP << 1) - 1,
	/* ABI 5 */ (LANDLOCK_ACCESS_NET_CONNECT_TCP << 1) - 1,
	/* ABI 6 */ (LANDLOCK_ACCESS_NET_CONNECT_TCP << 1) - 1,
	/* ABI 7 */ (LANDLOCK_ACCESS_NET_CONNECT_TCP << 1) - 1,
	/* ABI 8 */ (LANDLOCK_ACCESS_NET_CONNECT_TCP << 1) - 1,
	/* ABI 9 */ (LANDLOCK_ACCESS_NET_CONNECT_TCP << 1) - 1,
	/* ABI 10 */ (LANDLOCK_ACCESS_NET_CONNECT_SEND_UDP << 1) - 1,
};

/* cumulative scope restrictions supported by each Landlock ABI version, indexed by (abi - 1) */
static const uint64_t landlock_access_scope_mask[] = {
	/* ABI 1 */ 0,
	/* ABI 2 */ 0,
	/* ABI 3 */ 0,
	/* ABI 4 */ 0,
	/* ABI 5 */ 0,
	/* ABI 6 */ (LANDLOCK_SCOPE_SIGNAL << 1) - 1,
};

/* cumulative permissions supported by each Landlock ABI version, indexed by (abi - 1) */
static const uint64_t landlock_access_permission_mask[] = {
	/* ABI 1 */ 0,
	/* ABI 2 */ 0,
	/* ABI 3 */ 0,
	/* ABI 4 */ 0,
	/* ABI 5 */ 0,
	/* ABI 6 */ 0,
	/* ABI 7 */ 0,
	/* ABI 8 */ 0,
	/* ABI 9 */ 0,
	/* ABI 10 */ 0,
	/* ABI 11 */ 0,
	/* ABI 12 */ LANDLOCK_PERMISSION_NAMESPACE_USE | LANDLOCK_PERMISSION_CAPABILITY_USE,
};

struct landlock_category {
	const char *name;
	const struct landlock_restriction *items;
	size_t nitems;
	/* cumulative rights supported by each Landlock ABI version, indexed by (abi - 1) */
	const uint64_t *abi_masks;
	size_t nabi_masks;
	const char *rules;		/* rule types, NULL if there are none */
	const char *support_label;	/* --landlock-support label for the rights */
	const char *usage_title;	/* --help section title */
	const char *parse_err;
	const char *unsupported_err;
};

/*
 * Converts a comma-separated list of names from @items to a bitmask. Every
 * name must be known; returns -1 otherwise.
 */
static int landlock_names_to_mask(const struct landlock_restriction *items, size_t nitems,
				  const char *list, uint64_t *mask)
{
	size_t i, len;

	for (;;) {
		len = strcspn(list, ",");
		for (i = 0; i < nitems; i++)
			if (strlen(items[i].type) == len
					&& memcmp(items[i].type, list, len) == 0)
				break;
		if (i == nitems)
			return -1;
		*mask |= items[i].value;

		if (list[len] == '\0')
			return 0;
		list += len + 1;
	}
}

static const struct landlock_category landlock_categories[SETPRIV_LANDLOCK_NCATEGORIES] = {
	[SETPRIV_LANDLOCK_FS] = {
		.name = "fs",
		.items = landlock_access_fs,
		.nitems = ARRAY_SIZE(landlock_access_fs),
		.abi_masks = landlock_access_fs_mask,
		.nabi_masks = ARRAY_SIZE(landlock_access_fs_mask),
		.rules = "path-beneath",
		.support_label = N_("%s rights:"),
		.usage_title = N_(" available Landlock filesystem rights are:\n"),
		.parse_err = N_("could not parse Landlock fs access: %s"),
		.unsupported_err = N_("Landlock fs access right is not supported by the running kernel: %s"),
	},
	[SETPRIV_LANDLOCK_NET] = {
		.name = "net",
		.items = landlock_access_net,
		.nitems = ARRAY_SIZE(landlock_access_net),
		.abi_masks = landlock_access_net_mask,
		.nabi_masks = ARRAY_SIZE(landlock_access_net_mask),
		.rules = "net-port",
		.support_label = N_("%s rights:"),
		.usage_title = N_(" available Landlock network rights are:\n"),
		.parse_err = N_("could not parse Landlock net access: %s"),
		.unsupported_err = N_("Landlock net access right is not supported by the running kernel: %s"),
	},
	[SETPRIV_LANDLOCK_SCOPE] = {
		.name = "scope",
		.items = landlock_scope_restriction,
		.nitems = ARRAY_SIZE(landlock_scope_restriction),
		.abi_masks = landlock_access_scope_mask,
		.nabi_masks = ARRAY_SIZE(landlock_access_scope_mask),
		.support_label = N_("%s restrictions:"),
		.usage_title = N_(" available Landlock scope restrictions are:\n"),
		.parse_err = N_("could not parse Landlock scope restriction: %s"),
		.unsupported_err = N_("Landlock scope restriction is not supported by the running kernel: %s"),
	},
	[SETPRIV_LANDLOCK_PERMISSION] = {
		.name = "permission",
		.items = landlock_permission,
		.nitems = ARRAY_SIZE(landlock_permission),
		.abi_masks = landlock_access_permission_mask,
		.nabi_masks = ARRAY_SIZE(landlock_access_permission_mask),
		.rules = "namespace capability",
		.support_label = N_("%s rights:"),
		.usage_title = N_(" available Landlock permissions are:\n"),
		.parse_err = N_("could not parse Landlock permission: %s"),
		.unsupported_err = N_("Landlock permission is not supported by the running kernel: %s"),
	},
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
				err(EXIT_FAILURE, _("Landlock is not supported"));
			case EOPNOTSUPP:
				err(EXIT_FAILURE, _("Landlock is supported but currently disabled"));
			default:
				err(EXIT_FAILURE, _("failed to obtain Landlock ABI version"));
			}
		}
	}
	return abi;
}

static uint64_t landlock_abi_mask(const struct landlock_category *cat)
{
	int abi = supported_landlock_abi();
	size_t idx = (size_t) (abi - 1);

	if (idx >= cat->nabi_masks)
		idx = cat->nabi_masks - 1;
	return cat->abi_masks[idx];
}

static uint64_t parse_landlock_rights(const struct landlock_category *cat, const char *list)
{
	uint64_t r = 0, unsupported;
	size_t i;

	if (landlock_names_to_mask(cat->items, cat->nitems, list, &r))
		errx(EXIT_FAILURE, _(cat->parse_err), list);

	unsupported = r & ~landlock_abi_mask(cat);
	if (unsupported) {
		for (i = 0; i < cat->nitems; i++)
			if (cat->items[i].value & unsupported)
				errx(EXIT_FAILURE, _(cat->unsupported_err), cat->items[i].type);
	}

	return r;
}

static uint64_t parse_landlock_namespace_types(const char *list)
{
	uint64_t r = 0;
	size_t i;

	if (strcmp(list, "all") == 0) {
		for (i = 0; i < ARRAY_SIZE(landlock_namespace_type); i++)
			r |= landlock_namespace_type[i].value;
		return r;
	}

	if (landlock_names_to_mask(landlock_namespace_type,
				   ARRAY_SIZE(landlock_namespace_type), list, &r))
		errx(EXIT_FAILURE,
		     _("could not parse Landlock namespace types: %s"), list);

	return r;
}

static uint64_t parse_landlock_capabilities(const char *list)
{
	char *buf = xstrdup(list);
	char *str = buf;
	char *c;
	uint64_t r = 0;
	int cap, max = cap_last_cap();

	while ((c = strsep(&str, ","))) {
		if (strcmp(c, "all") == 0) {
			for (cap = 0; cap <= max && cap < 64; cap++)
				r |= 1ULL << cap;
			continue;
		}

		cap = capng_name_to_capability(c);
		if (cap < 0 && (sscanf(c, "cap_%d", &cap) != 1 || cap > max))
			cap = -1;
		if (cap < 0 || cap >= 64)
			errx(EXIT_FAILURE, _("unknown capability \"%s\""), c);
		r |= 1ULL << cap;
	}

	free(buf);
	return r;
}

void parse_landlock_access(struct setpriv_landlock_opts *opts, const char *str)
{
	const struct landlock_category *cat;
	const char *rights;
	uint64_t mask;
	size_t i;

	for (i = 0; i < SETPRIV_LANDLOCK_NCATEGORIES; i++) {
		cat = &landlock_categories[i];
		rights = ul_startswith(str, cat->name);
		if (!rights || (*rights != '\0' && *rights != ':'))
			continue;
		if (*rights == ':')
			rights++;

		if (*rights != '\0') {
			opts->handled[i] |= parse_landlock_rights(cat, rights);
			return;
		}

		/* without argument, match all supported by the current kernel */
		mask = landlock_abi_mask(cat);
		if (mask == 0)
			errx(EXIT_FAILURE, _("Landlock %s is not supported"), cat->name);
		opts->handled[i] |= mask;
		return;
	}

	errx(EXIT_FAILURE, _("invalid Landlock access: %s"), str);
}

/*
 * Parses the "<right>,...:" part of a "<type>:<right>,...:<argument>" rule,
 * returning the rights (0 if empty) and setting @arg to the argument.
 */
static uint64_t parse_landlock_rule_rights(const char *rule, const char *rights,
					   enum setpriv_landlock_category category,
					   const char **arg)
{
	const char *end = strchr(rights, ':');
	char *buf;
	uint64_t r = 0;

	if (!end)
		errx(EXIT_FAILURE, _("invalid Landlock rule: %s"), rule);

	if (end != rights) {
		buf = xstrndup(rights, end - rights);
		r = parse_landlock_rights(&landlock_categories[category], buf);
		free(buf);
	}

	*arg = end + 1;
	return r;
}

void parse_landlock_rule(struct setpriv_landlock_opts *opts, const char *str)
{
	struct landlock_rule_entry *rule = xmalloc(sizeof(*rule));
	const char *rest, *arg;
	int parent_fd;

	rest = ul_startswith(str, "path-beneath:");
	if (rest) {
		rule->rule_type = LANDLOCK_RULE_PATH_BENEATH;
		rule->path_beneath_attr.allowed_access =
			parse_landlock_rule_rights(str, rest, SETPRIV_LANDLOCK_FS, &arg);

		parent_fd = open(arg, O_RDONLY | O_PATH | O_CLOEXEC);
		if (parent_fd == -1)
			err(EXIT_FAILURE, _("could not open file for Landlock: %s"), arg);
		rule->path_beneath_attr.parent_fd = parent_fd;

		list_add(&rule->head, &opts->rules);
		return;
	}

	rest = ul_startswith(str, "net-port:");
	if (rest) {
		rule->rule_type = LANDLOCK_RULE_NET_PORT;
		rule->net_port_attr.allowed_access =
			parse_landlock_rule_rights(str, rest, SETPRIV_LANDLOCK_NET, &arg);
		rule->net_port_attr.port = strtou16_or_err(arg, "could not parse port number");

		list_add(&rule->head, &opts->rules);
		return;
	}

	rest = ul_startswith(str, "namespace:");
	if (rest) {
		rule->rule_type = LANDLOCK_RULE_NAMESPACE;
		rule->namespace_attr.permissions = LANDLOCK_PERMISSION_NAMESPACE_USE;
		rule->namespace_attr.allowed_namespace_types = parse_landlock_namespace_types(rest);
		rule->namespace_attr.quiet_namespace_types = 0;

		list_add(&rule->head, &opts->rules);
		return;
	}

	rest = ul_startswith(str, "capability:");
	if (rest) {
		rule->rule_type = LANDLOCK_RULE_CAPABILITY;
		rule->capability_attr.permissions = LANDLOCK_PERMISSION_CAPABILITY_USE;
		rule->capability_attr.allowed_capabilities = parse_landlock_capabilities(rest);
		rule->capability_attr.quiet_capabilities = 0;

		list_add(&rule->head, &opts->rules);
		return;
	}

	errx(EXIT_FAILURE, _("invalid Landlock rule: %s"), str);
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
	size_t i;
	struct landlock_path_beneath_attr path_beneath_attr;
	struct landlock_net_port_attr net_port_attr;

	list_for_each(entry, &opts->rules) {
		rule = list_entry(entry, struct landlock_rule_entry, head);
		if (rule->rule_type == LANDLOCK_RULE_PATH_BENEATH && !opts->handled[SETPRIV_LANDLOCK_FS]) {
			errx(EXIT_FAILURE,
				_("Landlock path-beneath rule requires a filesystem access restriction (--landlock-access fs)"));
		}
		if (rule->rule_type == LANDLOCK_RULE_NET_PORT && !opts->handled[SETPRIV_LANDLOCK_NET]) {
			errx(EXIT_FAILURE,
				_("Landlock net-port rule requires a network access restriction (--landlock-access net)"));
		}
		if (rule->rule_type == LANDLOCK_RULE_NAMESPACE
				&& !(opts->handled[SETPRIV_LANDLOCK_PERMISSION] & LANDLOCK_PERMISSION_NAMESPACE_USE)) {
			errx(EXIT_FAILURE,
				_("Landlock namespace rule requires a namespace permission restriction (--landlock-access permission:namespace-use)"));
		}
		if (rule->rule_type == LANDLOCK_RULE_CAPABILITY
				&& !(opts->handled[SETPRIV_LANDLOCK_PERMISSION] & LANDLOCK_PERMISSION_CAPABILITY_USE)) {
			errx(EXIT_FAILURE,
				_("Landlock capability rule requires a capability permission restriction (--landlock-access permission:capability-use)"));
		}
	}

	for (i = 0; i < SETPRIV_LANDLOCK_NCATEGORIES; i++)
		if (opts->handled[i])
			break;
	if (i == SETPRIV_LANDLOCK_NCATEGORIES)
		return;

	const struct landlock_ruleset_attr ruleset_attr = {
		.handled_access_fs = opts->handled[SETPRIV_LANDLOCK_FS],
		.handled_access_net = opts->handled[SETPRIV_LANDLOCK_NET],
		.scoped = opts->handled[SETPRIV_LANDLOCK_SCOPE],
		.handled_permissions = opts->handled[SETPRIV_LANDLOCK_PERMISSION],
	};

	fd = landlock_create_ruleset(&ruleset_attr, sizeof(ruleset_attr), 0);
	if (fd == -1)
		err(SETPRIV_EXIT_PRIVERR, _("landlock_create_ruleset failed"));

	list_for_each(entry, &opts->rules) {
		rule = list_entry(entry, struct landlock_rule_entry, head);

		if (rule->rule_type == LANDLOCK_RULE_PATH_BENEATH) {
			path_beneath_attr = rule->path_beneath_attr;
			if (!path_beneath_attr.allowed_access)
				path_beneath_attr.allowed_access = opts->handled[SETPRIV_LANDLOCK_FS];

			ret = landlock_add_rule(fd, rule->rule_type, &path_beneath_attr, 0);
		} else if (rule->rule_type == LANDLOCK_RULE_NET_PORT) {
			net_port_attr = rule->net_port_attr;
			if (!net_port_attr.allowed_access)
				net_port_attr.allowed_access = opts->handled[SETPRIV_LANDLOCK_NET];

			ret = landlock_add_rule(fd, rule->rule_type, &net_port_attr, 0);
		} else if (rule->rule_type == LANDLOCK_RULE_NAMESPACE) {
			ret = landlock_add_rule(fd, rule->rule_type, &rule->namespace_attr, 0);
		} else if (rule->rule_type == LANDLOCK_RULE_CAPABILITY) {
			ret = landlock_add_rule(fd, rule->rule_type, &rule->capability_attr, 0);
		} else {
			abort();
		}

		if (ret == -1)
			err(SETPRIV_EXIT_PRIVERR, _("adding Landlock rule failed"));
	}

	if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) == -1)
		err(SETPRIV_EXIT_PRIVERR, _("disallow granting new privileges for Landlock failed"));

	if (landlock_restrict_self(fd, 0) == -1)
		err(SETPRIV_EXIT_PRIVERR, _("landlock_restrict_self failed"));
}

static void usage_landlock_items(FILE *out, const char *title,
				 const struct landlock_restriction *items, size_t nitems)
{
	size_t i;
	int width = 0;

	for (i = 0; i < nitems; i++)
		width = max(width, (int) strlen(items[i].type));

	fputs(USAGE_SEPARATOR, out);
	fputs(_(title), out);
	for (i = 0; i < nitems; i++)
		fprintf(out, "  %*s - %s\n", width, items[i].type, _(items[i].help));
}

void usage_landlock(FILE *out)
{
	size_t i;

	fputs(USAGE_ARGUMENTS, out);
	fputs(_(" <access> is a Landlock access; syntax is <type>[:<right>,...]\n"), out);
	fputs(_(" <rule> is a Landlock rule; syntax is <type>:<right>:<argument>\n"), out);

	fputs(USAGE_SEPARATOR, out);
	fputs(_(" available Landlock rule types are:\n"), out);
	/* TRANSLATORS: Keep *{path-beneath}* untranslated, it's a type name */
	fputs(_("  path-beneath - filesystem based rule; <argument> is a path\n"), out);
	/* TRANSLATORS: Keep *{net-port}* untranslated, it's a type name */
	fputs(_("  net-port - network based rule; <argument> is a port number\n"), out);
	/* TRANSLATORS: Keep *{namespace}* untranslated, it's a type name */
	fputs(_("  namespace - namespace based rule; syntax is namespace:<type>[,<type>...]\n"), out);
	/* TRANSLATORS: Keep *{capability}* untranslated, it's a type name */
	fputs(_("  capability - capability based rule; syntax is capability:<cap>[,<cap>...]\n"), out);

	for (i = 0; i < SETPRIV_LANDLOCK_NCATEGORIES; i++)
		usage_landlock_items(out, landlock_categories[i].usage_title,
				     landlock_categories[i].items,
				     landlock_categories[i].nitems);

	usage_landlock_items(out, N_(" available Landlock namespace types are:\n"),
			     landlock_namespace_type, ARRAY_SIZE(landlock_namespace_type));
}

void list_landlock_support(void)
{
	const struct landlock_category *cat;
	uint64_t masks[SETPRIV_LANDLOCK_NCATEGORIES];
	int abi, errata;
	unsigned n;
	size_t i, j;

	abi = supported_landlock_abi();
	errata = landlock_create_ruleset(NULL, 0, LANDLOCK_CREATE_RULESET_ERRATA);
	if (errata < 0) errata = 0;

	printf(_("ABI version: %d\n"), abi);
	printf(_("ABI errata:"));
	for (n = 1; errata != 0; errata >>= 1, n++) {
		if (errata & 1)
			printf(" %u", n);
	}
	printf("\n");

	printf(_("access:"));
	for (i = 0; i < SETPRIV_LANDLOCK_NCATEGORIES; i++) {
		masks[i] = landlock_abi_mask(&landlock_categories[i]);
		if (masks[i])
			printf(" %s", landlock_categories[i].name);
	}
	printf("\n");

	for (i = 0; i < SETPRIV_LANDLOCK_NCATEGORIES; i++) {
		cat = &landlock_categories[i];
		if (!masks[i])
			continue;

		printf(_(cat->support_label), cat->name);
		for (j = 0; j < cat->nitems; j++)
			if (cat->items[j].value & masks[i])
				printf(" %s", cat->items[j].type);
		printf("\n");

		if (cat->rules)
			printf(_("%s rules: %s\n"), cat->name, cat->rules);
	}
}

void list_landlock_access(void)
{
	size_t i;

	for (i = 0; i < SETPRIV_LANDLOCK_NCATEGORIES; i++)
		if (landlock_abi_mask(&landlock_categories[i]))
			printf("%s\n", landlock_categories[i].name);
}

void list_landlock_rights(const char *access)
{
	const struct landlock_category *cat;
	uint64_t mask;
	size_t i, j;

	for (i = 0; i < SETPRIV_LANDLOCK_NCATEGORIES; i++) {
		cat = &landlock_categories[i];
		if (strcmp(access, cat->name) != 0)
			continue;

		mask = landlock_abi_mask(cat);
		for (j = 0; j < cat->nitems; j++)
			if (cat->items[j].value & mask)
				printf("%s\n", cat->items[j].type);
		return;
	}

	errx(EXIT_FAILURE, _("unknown Landlock access: %s"), access);
}
