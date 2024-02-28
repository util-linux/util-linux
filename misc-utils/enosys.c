/*
 * Copyright (C) 2023 Thomas Weißschuh <thomas@t-8ch.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <stddef.h>
#include <stdbool.h>
#include <getopt.h>

#include <linux/unistd.h>
#include <linux/audit.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>

#include "c.h"
#include "exitcodes.h"
#include "nls.h"
#include "bitops.h"
#include "audit-arch.h"
#include "list.h"
#include "xalloc.h"
#include "strutils.h"
#include "seccomp.h"
#include "all-io.h"

#define IS_LITTLE_ENDIAN (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)

#define syscall_nr (offsetof(struct seccomp_data, nr))
#define syscall_arch (offsetof(struct seccomp_data, arch))
#define _syscall_arg(n) (offsetof(struct seccomp_data, args[n]))
#define syscall_arg_lower32(n) (_syscall_arg(n) + 4 * !IS_LITTLE_ENDIAN)
#define syscall_arg_upper32(n) (_syscall_arg(n) + 4 * IS_LITTLE_ENDIAN)

struct syscall {
	const char *const name;
	long number;
};

/* When the alias arrays are empty the compiler emits -Wtype-limits warnings.
 * Avoid those by going through this function. */
static inline bool lt(size_t a, size_t b)
{
	return a < b;
}

static const struct syscall syscalls[] = {
#define UL_SYSCALL(name, nr) { name, nr },
#include "syscalls.h"
#undef UL_SYSCALL
};

static const struct syscall errnos[] = {
#define UL_ERRNO(name, nr) { name, nr },
#include "errnos.h"
#undef UL_ERRNO
};

static const struct syscall ioctls[] = {
	{ "FIOCLEX", FIOCLEX },
};

static void __attribute__((__noreturn__)) usage(void)
{
	FILE *out = stdout;

	fputs(USAGE_HEADER, out);
	fprintf(out, _(" %s [options] -- <command>\n"), program_invocation_short_name);

	fputs(USAGE_OPTIONS, out);
	fputs(_(" -s, --syscall           syscall to block\n"), out);
	fputs(_(" -i, --ioctl             ioctl to block\n"), out);
	fputs(_(" -l, --list              list known syscalls\n"), out);
	fputs(_(" -d, --dump[=<file>]     dump seccomp bytecode\n"), out);

	fputs(USAGE_SEPARATOR, out);
	fprintf(out, USAGE_HELP_OPTIONS(25));

	fprintf(out, USAGE_MAN_TAIL("enosys(1)"));

	exit(EXIT_SUCCESS);
}

struct blocked_number {
	struct list_head head;
	long number;
	int ret;
};

static struct blocked_number *parse_block(const char *s, int ret, const struct syscall entities[], size_t n_entities)
{
	struct blocked_number *blocked;
	const char *name, *error_name;
	long blocked_number;
	char *colon;
	bool found;
	size_t i;

	colon = strchr(s, ':');
	if (colon) {
		name = xstrndup(s, colon - s);
		error_name = colon + 1;

		found = 0;
		for (i = 0; i < ARRAY_SIZE(errnos); i++) {
			if (strcmp(error_name, errnos[i].name) == 0) {
				ret = errnos[i].number;
				found = 1;
				break;
			}
		}
		if (!found)
			ret = str2num_or_err(
					colon + 1, 10, _("Unknown errno"), 0, INT_MAX);
	} else {
		name = s;
	}

	found = 0;
	for (i = 0; i < n_entities; i++) {
		if (strcmp(name, entities[i].name) == 0) {
			blocked_number = entities[i].number;
			found = 1;
			break;
		}
	}
	if (!found)
		blocked_number = str2num_or_err(
				name, 10, _("Unknown syscall"), 0, LONG_MAX);

	blocked = xmalloc(sizeof(*blocked));
	blocked->number = blocked_number;
	blocked->ret = ret;

	if (name != s)
		free((char *)name);

	return blocked;
}

int main(int argc, char **argv)
{
	int c;
	size_t i;
	FILE *dump = NULL;
	static const struct option longopts[] = {
		{ "syscall",    required_argument, NULL, 's' },
		{ "ioctl",      required_argument, NULL, 'i' },
		{ "list",       no_argument,       NULL, 'l' },
		{ "list-ioctl", no_argument,       NULL, 'm' },
		{ "dump",       optional_argument, NULL, 'd' },
		{ "version",    no_argument,       NULL, 'V' },
		{ "help",       no_argument,       NULL, 'h' },
		{ 0 }
	};

	struct blocked_number *blocked;
	struct list_head *loop_ctr;
	struct list_head blocked_syscalls;
	bool blocking_execve = false;
	INIT_LIST_HEAD(&blocked_syscalls);
	struct list_head blocked_ioctls;
	INIT_LIST_HEAD(&blocked_ioctls);

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

	while ((c = getopt_long (argc, argv, "+Vhs:i:lmd::", longopts, NULL)) != -1) {
		switch (c) {
		case 's':
			blocked = parse_block(optarg, ENOSYS, syscalls, ARRAY_SIZE(syscalls));
			list_add(&blocked->head, &blocked_syscalls);
			if (blocked->number == __NR_execve)
				blocking_execve = true;

			break;
		case 'i':
			blocked = parse_block(optarg, ENOTTY, ioctls, ARRAY_SIZE(ioctls));
			list_add(&blocked->head, &blocked_ioctls);

			break;
		case 'l':
			for (i = 0; lt(i, ARRAY_SIZE(syscalls)); i++)
				printf("%5ld %s\n", syscalls[i].number, syscalls[i].name);
			return EXIT_SUCCESS;
		case 'm':
			for (i = 0; lt(i, ARRAY_SIZE(ioctls)); i++)
				printf("%5ld %s\n", ioctls[i].number, ioctls[i].name);
			return EXIT_SUCCESS;
		case 'd':
			if (optarg) {
				dump = fopen(optarg, "w");
				if (!dump)
					err(EXIT_FAILURE, _("Could not open %s"), optarg);
			} else {
				dump = stdout;
			}
			break;
		case 'V':
			print_version(EXIT_SUCCESS);
		case 'h':
			usage();
		default:
			errtryhelp(EXIT_FAILURE);
		}
	}

	if (!dump && optind >= argc)
		errtryhelp(EXIT_FAILURE);

	struct sock_filter filter[BPF_MAXINSNS];
	struct sock_filter *f = filter;

#define INSTR(_instruction)                                        \
	if (f == &filter[ARRAY_SIZE(filter)])                      \
		errx(EXIT_FAILURE, _("filter too big")); \
	*f++ = (struct sock_filter) _instruction

	INSTR(BPF_STMT(BPF_LD | BPF_W | BPF_ABS, syscall_arch));
	INSTR(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SECCOMP_ARCH_NATIVE, 1, 0));
	INSTR(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_TRAP));

	/* Blocking "execve" normally would also block our own call to
	 * it and the end of main. To distinguish between our execve
	 * and the execve to be blocked, compare the environ pointer.
	 *
	 * See https://lore.kernel.org/all/CAAnLoWnS74dK9Wq4EQ-uzQ0qCRfSK-dLqh+HCais-5qwDjrVzg@mail.gmail.com/
	 */
	if (blocking_execve) {
		INSTR(BPF_STMT(BPF_LD | BPF_W | BPF_ABS, syscall_nr));
		INSTR(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_execve, 0, 5));
		INSTR(BPF_STMT(BPF_LD | BPF_W | BPF_ABS, syscall_arg_lower32(2)));
		INSTR(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, (uint64_t)(uintptr_t) environ, 0, 3));
		INSTR(BPF_STMT(BPF_LD | BPF_W | BPF_ABS, syscall_arg_upper32(2)));
		INSTR(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, (uint64_t)(uintptr_t) environ >> 32, 0, 1));
		INSTR(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW));
	}

	INSTR(BPF_STMT(BPF_LD | BPF_W | BPF_ABS, syscall_nr));

	list_for_each(loop_ctr, &blocked_syscalls) {
		blocked = list_entry(loop_ctr, struct blocked_number, head);

		INSTR(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, blocked->number, 0, 1));
		INSTR(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | blocked->ret));
	}

	if (!list_empty(&blocked_ioctls)) {
		INSTR(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_ioctl, 1, 0));
		INSTR(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW));

		list_for_each(loop_ctr, &blocked_ioctls) {
			blocked = list_entry(loop_ctr, struct blocked_number, head);

			INSTR(BPF_STMT(BPF_LD | BPF_W | BPF_ABS, syscall_arg_lower32(1)));
			INSTR(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, (uint64_t) blocked->number, 0, 3));
			INSTR(BPF_STMT(BPF_LD | BPF_W | BPF_ABS, syscall_arg_upper32(1)));
			INSTR(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, (uint64_t) blocked->number >> 32, 0, 1));
			INSTR(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | blocked->ret));
		}
	}

	INSTR(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW));

	if (dump) {
		if (fwrite_all(filter, (f - filter) * sizeof(filter[0]), 1, dump))
			err(EXIT_FAILURE, _("Could not dump seccomp filter"));
		return EXIT_SUCCESS;
	}

	struct sock_fprog prog = {
		.len    = f - filter,
		.filter = filter,
	};

	/* *SET* below will return EINVAL when either the filter is invalid or
	 * seccomp is not supported. To distinguish those cases do a *GET* here
	 */
	if (prctl(PR_GET_SECCOMP) == -1 && errno == EINVAL)
		err(EXIT_NOTSUPP, _("Seccomp non-functional"));

	if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0))
		err_nosys(EXIT_FAILURE, _("Could not run prctl(PR_SET_NO_NEW_PRIVS)"));

	if (ul_set_seccomp_filter_spec_allow(&prog))
		err_nosys(EXIT_FAILURE, _("Could not seccomp filter"));

	if (execvp(argv[optind], argv + optind))
		err(EXIT_NOTSUPP, _("Could not exec"));
}
