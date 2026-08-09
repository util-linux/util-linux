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
	uint64_t handled_access_net;
	uint64_t scoped;
};

struct landlock_path_beneath_attr {
	uint64_t allowed_access;
	int32_t parent_fd;
} __attribute__((packed));

struct landlock_net_port_attr {
	uint64_t allowed_access;
	uint64_t port;
};

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

#define LANDLOCK_ACCESS_NET_BIND_TCP			(1ULL << 0)
#define LANDLOCK_ACCESS_NET_CONNECT_TCP			(1ULL << 1)

#define LANDLOCK_SCOPE_ABSTRACT_UNIX_SOCKET		(1ULL << 0)
#define LANDLOCK_SCOPE_SIGNAL				(1ULL << 1)

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
};

static const struct landlock_restriction landlock_scope_restriction[] = {
	{ LANDLOCK_SCOPE_ABSTRACT_UNIX_SOCKET, "abstract-unix-socket", N_("access to abstract UNIX sockets") },
	{ LANDLOCK_SCOPE_SIGNAL,	       "signal",	       N_("ability to send signals to processes") },
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

static uint64_t landlock_abi_fs_mask(void)
{
	int abi = supported_landlock_abi();
	size_t n = ARRAY_SIZE(landlock_access_fs_mask);
	size_t idx = (size_t) (abi - 1);

	if (idx >= n)
		idx = n - 1;
	return landlock_access_fs_mask[idx];
}

static uint64_t landlock_abi_net_mask(void)
{
	int abi = supported_landlock_abi();
	size_t n = ARRAY_SIZE(landlock_access_net_mask);
	size_t idx = (size_t) (abi - 1);

	if (idx >= n)
		idx = n - 1;
	return landlock_access_net_mask[idx];
}

static uint64_t landlock_abi_scope_mask(void)
{
	int abi = supported_landlock_abi();
	size_t n = ARRAY_SIZE(landlock_access_scope_mask);
	size_t idx = (size_t) (abi - 1);

	if (idx >= n)
		idx = n - 1;
	return landlock_access_scope_mask[idx];
}

static long landlock_fs_access_to_mask(const char *str, size_t len)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(landlock_access_fs); i++)
		if (strncmp(landlock_access_fs[i].type, str, len) == 0)
			return landlock_access_fs[i].value;
	return -1;
}

static uint64_t parse_landlock_fs_access(const char *list)
{
	unsigned long r = 0, unsupported;
	unsigned i;

	if (string_to_bitmask(list, &r, landlock_fs_access_to_mask))
		errx(EXIT_FAILURE,
		     _("could not parse Landlock fs access: %s"), list);

	unsupported = r & ~landlock_abi_fs_mask();
	if (unsupported) {
		for (i = 0; i < ARRAY_SIZE(landlock_access_fs); i++)
			if (landlock_access_fs[i].value & unsupported)
				errx(EXIT_FAILURE,
				     _("Landlock fs access right is not supported by the running kernel: %s"),
				     landlock_access_fs[i].type);
	}

	return r;
}

static long landlock_net_access_to_mask(const char *str, size_t len)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(landlock_access_net); i++) {
		if (strlen(landlock_access_net[i].type) == len
				&& memcmp(landlock_access_net[i].type, str, len) == 0)
			return landlock_access_net[i].value;
	}
	return -1;
}

static uint64_t parse_landlock_net_access(const char *list)
{
	unsigned long r = 0, unsupported;
	unsigned i;

	if (string_to_bitmask(list, &r, landlock_net_access_to_mask))
		errx(EXIT_FAILURE,
		     _("could not parse Landlock net access: %s"), list);

	unsupported = r & ~landlock_abi_net_mask();
	if (unsupported) {
		for (i = 0; i < ARRAY_SIZE(landlock_access_net); i++)
			if (landlock_access_net[i].value & unsupported)
				errx(EXIT_FAILURE,
				     _("Landlock net access right is not supported by the running kernel: %s"),
				     landlock_access_net[i].type);
	}

	return r;
}

static long landlock_scope_restriction_to_mask(const char *str, size_t len)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(landlock_scope_restriction); i++)
		if (strlen(landlock_scope_restriction[i].type) == len
				&& memcmp(landlock_scope_restriction[i].type, str, len) == 0)
			return landlock_scope_restriction[i].value;
	return -1;
}

static uint64_t parse_landlock_scope_restriction(const char *list)
{
	unsigned long r = 0, unsupported;
	size_t i;

	if (string_to_bitmask(list, &r, landlock_scope_restriction_to_mask))
		errx(EXIT_FAILURE,
		     _("could not parse Landlock scope restriction: %s"), list);

	unsupported = r & ~landlock_abi_scope_mask();
	if (unsupported) {
		for (i = 0; i < ARRAY_SIZE(landlock_scope_restriction); i++)
			if (landlock_scope_restriction[i].value & unsupported)
				errx(EXIT_FAILURE,
				     _("Landlock scope restriction is not supported by the running kernel: %s"),
				     landlock_scope_restriction[i].type);
	}

	return r;
}

void parse_landlock_access(struct setpriv_landlock_opts *opts, const char *str)
{
	uint64_t mask;
	const char *type;

	/* without argument, match all supported by the current kernel */
	if (strcmp(str, "fs") == 0 || strcmp(str, "fs:") == 0) {
		opts->access_fs |= landlock_abi_fs_mask();
		return;
	}

	type = ul_startswith(str, "fs:");
	if (type) {
		opts->access_fs |= parse_landlock_fs_access(type);
		return;
	}

	if (strcmp(str, "net") == 0 || strcmp(str, "net:") == 0) {
		mask = landlock_abi_net_mask();
		if (mask == 0)
			errx(EXIT_FAILURE,
			     _("Landlock net is not supported"));
		opts->access_net |= mask;
		return;
	}

	type = ul_startswith(str, "net:");
	if (type) {
		opts->access_net |= parse_landlock_net_access(type);
		return;
	}

	if (strcmp(str, "scope") == 0 || strcmp(str, "scope:") == 0) {
		mask = landlock_abi_scope_mask();
		if (mask == 0)
			errx(EXIT_FAILURE,
			     _("Landlock scope is not supported"));
		opts->scoped |= mask;
		return;
	}

	type = ul_startswith(str, "scope:");
	if (type) {
		opts->scoped |= parse_landlock_scope_restriction(type);
		return;
	}

	errx(EXIT_FAILURE, _("invalid Landlock access: %s"), str);
}

void parse_landlock_rule(struct setpriv_landlock_opts *opts, const char *str)
{
	struct landlock_rule_entry *rule = xmalloc(sizeof(*rule));
	const char *accesses, *path, *portstr;
	char *accesses_part;
	int parent_fd;
	uint16_t portnum;

	accesses = ul_startswith(str, "path-beneath:");
	if (accesses) {
		path = strchr(accesses, ':');
		if (!path)
			errx(EXIT_FAILURE, _("invalid Landlock rule: %s"), str);
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
			err(EXIT_FAILURE, _("could not open file for Landlock: %s"), path);

		rule->path_beneath_attr.parent_fd = parent_fd;

		list_add(&rule->head, &opts->rules);
		return;
	}

	accesses = ul_startswith(str, "net-port:");
	if (accesses) {
		portstr = strchr(accesses, ':');
		if (!portstr)
			errx(EXIT_FAILURE, _("invalid Landlock rule: %s"), str);
		rule->rule_type = LANDLOCK_RULE_NET_PORT;

		accesses_part = xstrndup(accesses, portstr - accesses);
		if (accesses_part[0] != '\0')
			rule->net_port_attr.allowed_access = parse_landlock_net_access(accesses_part);
		else
			rule->net_port_attr.allowed_access = 0;
		free(accesses_part);

		portstr++;

		portnum = strtou16_or_err(portstr, "could not parse port number");

		rule->net_port_attr.port = portnum;

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
	struct landlock_path_beneath_attr path_beneath_attr;
	struct landlock_net_port_attr net_port_attr;

	list_for_each(entry, &opts->rules) {
		rule = list_entry(entry, struct landlock_rule_entry, head);
		if (rule->rule_type == LANDLOCK_RULE_PATH_BENEATH && !opts->access_fs) {
			errx(EXIT_FAILURE,
				_("Landlock path-beneath rule requires a filesystem access restriction (--landlock-access fs)"));
		}
		if (rule->rule_type == LANDLOCK_RULE_NET_PORT && !opts->access_net) {
			errx(EXIT_FAILURE,
				_("Landlock net-port rule requires a network access restriction (--landlock-access net)"));
		}
	}

	if (!opts->access_fs && !opts->access_net && !opts->scoped)
		return;

	const struct landlock_ruleset_attr ruleset_attr = {
		.handled_access_fs = opts->access_fs,
		.handled_access_net = opts->access_net,
		.scoped = opts->scoped,
	};

	fd = landlock_create_ruleset(&ruleset_attr, sizeof(ruleset_attr), 0);
	if (fd == -1)
		err(SETPRIV_EXIT_PRIVERR, _("landlock_create_ruleset failed"));

	list_for_each(entry, &opts->rules) {
		rule = list_entry(entry, struct landlock_rule_entry, head);

		if (rule->rule_type == LANDLOCK_RULE_PATH_BENEATH) {
			path_beneath_attr = rule->path_beneath_attr;
			if (!path_beneath_attr.allowed_access)
				path_beneath_attr.allowed_access = opts->access_fs;

			ret = landlock_add_rule(fd, rule->rule_type, &path_beneath_attr, 0);
		} else if (rule->rule_type == LANDLOCK_RULE_NET_PORT) {
			net_port_attr = rule->net_port_attr;
			if (!net_port_attr.allowed_access)
				net_port_attr.allowed_access = opts->access_net;

			ret = landlock_add_rule(fd, rule->rule_type, &net_port_attr, 0);
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

	fputs(USAGE_SEPARATOR, out);
	fputs(_(" available Landlock filesystem rights are:\n"), out);
	for (i = 0; i < ARRAY_SIZE(landlock_access_fs); i++) {
		fprintf(out, "  %12s - %s\n", landlock_access_fs[i].type,
					_(landlock_access_fs[i].help));
	}

	fputs(USAGE_SEPARATOR, out);
	fputs(_(" available Landlock network rights are:\n"), out);
	for (i = 0; i < ARRAY_SIZE(landlock_access_net); i++) {
		fprintf(out, "  %16s - %s\n", landlock_access_net[i].type,
					_(landlock_access_net[i].help));
	}

	fputs(USAGE_SEPARATOR, out);
	fputs(_(" available Landlock scope restrictions are:\n"), out);
	for (i = 0; i < ARRAY_SIZE(landlock_scope_restriction); i++) {
		fprintf(out, "  %20s - %s\n", landlock_scope_restriction[i].type,
					_(landlock_scope_restriction[i].help));
	}
}

void list_landlock_support(void)
{
	int abi, errata;
	unsigned i;
	uint64_t fs_mask, net_mask, scope_mask;

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

	fs_mask = landlock_abi_fs_mask();
	net_mask = landlock_abi_net_mask();
	scope_mask = landlock_abi_scope_mask();

	printf(_("access:"));
	if (fs_mask)
		printf(" fs");
	if (net_mask)
		printf(" net");
	if (scope_mask)
		printf(" scope");
	printf("\n");

	printf(_("%s rights:"), "fs");
	for (i = 0; i < ARRAY_SIZE(landlock_access_fs); i++)
		if (landlock_access_fs[i].value & fs_mask)
			printf(" %s", landlock_access_fs[i].type);
	printf("\n");

	printf(_("%s rules: %s\n"), "fs", "path-beneath");

	if (net_mask) {
		printf(_("%s rights:"), "net");
		for (i = 0; i < ARRAY_SIZE(landlock_access_net); i++)
			if (landlock_access_net[i].value & net_mask)
				printf(" %s", landlock_access_net[i].type);
		printf("\n");

		printf(_("%s rules: %s\n"), "net", "net-port");
	}

	if (scope_mask) {
		printf(_("%s restrictions:"), "scope");
		for (i = 0; i < ARRAY_SIZE(landlock_scope_restriction); i++)
			if (landlock_scope_restriction[i].value & scope_mask)
				printf(" %s", landlock_scope_restriction[i].type);
		printf("\n");
	}
}

void list_landlock_access(void)
{
	if (landlock_abi_fs_mask())
		printf("fs\n");
	if (landlock_abi_net_mask())
		printf("net\n");
	if (landlock_abi_scope_mask())
		printf("scope\n");
}

void list_landlock_rights(const char *access)
{
	uint64_t mask;
	size_t i;

	if (strcmp(access, "fs") == 0) {
		mask = landlock_abi_fs_mask();
		for (i = 0; i < ARRAY_SIZE(landlock_access_fs); i++)
			if (landlock_access_fs[i].value & mask)
				printf("%s\n", landlock_access_fs[i].type);
	} else if (strcmp(access, "net") == 0) {
		mask = landlock_abi_net_mask();
		for (i = 0; i < ARRAY_SIZE(landlock_access_net); i++)
			if (landlock_access_net[i].value & mask)
				printf("%s\n", landlock_access_net[i].type);
	} else if (strcmp(access, "scope") == 0) {
		mask = landlock_abi_scope_mask();
		for (i = 0; i < ARRAY_SIZE(landlock_scope_restriction); i++)
			if (landlock_scope_restriction[i].value & mask)
				printf("%s\n", landlock_scope_restriction[i].type);
	} else
		errx(EXIT_FAILURE, _("unknown Landlock access: %s"), access);
}
