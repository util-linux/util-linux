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
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <linux/audit.h>
#include <sys/prctl.h>

#include "c.h"
#include "exitcodes.h"

#if __x86_64__
#    define SECCOMP_ARCH_NATIVE AUDIT_ARCH_X86_64
#elif __i386__
#    define SECCOMP_ARCH_NATIVE AUDIT_ARCH_I386
#elif __arm__
#    define SECCOMP_ARCH_NATIVE AUDIT_ARCH_ARM
#elif __aarch64__
#    define SECCOMP_ARCH_NATIVE AUDIT_ARCH_AARCH64
#elif __riscv
#    if __riscv_xlen == 32
#        define SECCOMP_ARCH_NATIVE AUDIT_ARCH_RISCV32
#    elif __riscv_xlen == 64
#        define SECCOMP_ARCH_NATIVE AUDIT_ARCH_RISCV64
#    endif
#elif __s390__
# 	 define SECCOMP_ARCH_NATIVE AUDIT_ARCH_S390
#elif __s390x__
# 	 define SECCOMP_ARCH_NATIVE AUDIT_ARCH_S390X
#elif __PPC64__
#    if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
# 	 define SECCOMP_ARCH_NATIVE AUDIT_ARCH_PPC64
#    else
# 	 define SECCOMP_ARCH_NATIVE AUDIT_ARCH_PPC64LE
#    endif
#else
#    error Unknown target architecture
#endif

#define syscall_nr (offsetof(struct seccomp_data, nr))

struct syscall {
	const char *const name;
	int number;
};

const struct syscall syscalls[] = {
	{ "move_mount", __NR_move_mount },
	{ "open_tree", __NR_open_tree },
	{ "fsopen", __NR_fsopen },
};

int main(int argc, char **argv)
{
	int c;
	size_t i;
	bool found;
	static const struct option longopts[] = {
		{ "syscall", required_argument, NULL, 's' },
		{ 0 }
	};

	bool blocked_syscalls[ARRAY_SIZE(syscalls)] = {};

	while ((c = getopt_long (argc, argv, "s:", longopts, NULL)) != -1) {
		switch (c) {
		case 's':
			found = 0;
			for (i = 0; i < ARRAY_SIZE(syscalls); i++) {
				if (strcmp(optarg, syscalls[i].name) == 0) {
					blocked_syscalls[i] = true;
					found = 1;
					break;
				}
			}
			if (!found)
				errx(EXIT_FAILURE, "Unknown syscall '%s'", optarg);
			break;
		default:
			errx(EXIT_FAILURE, "Unknown option");
		}
	}

	if (optind >= argc)
		errx(EXIT_FAILURE, "No executable specified");

#define N_FILTERS (ARRAY_SIZE(syscalls) + 3)

	struct sock_filter filter[N_FILTERS] = {
		[0]             = BPF_STMT(BPF_LD | BPF_W | BPF_ABS, syscall_nr),

		[N_FILTERS - 2] = BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
		[N_FILTERS - 1] = BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | ENOSYS),
	};

	const struct sock_filter nop = BPF_JUMP(BPF_JMP | BPF_JA, 0, 0, 0);

	for (i = 0; i < ARRAY_SIZE(syscalls); i++) {
		if (blocked_syscalls[i]) {
			const struct sock_filter block = BPF_JUMP(
					BPF_JMP | BPF_JEQ | BPF_K,
					syscalls[i].number,
					N_FILTERS - 3 - i, 0);
			filter[i + 1] = block;
		} else {
			filter[i + 1] = nop;
		}
	}

	struct sock_fprog prog = {
		.len    = ARRAY_SIZE(filter),
		.filter = filter,
	};

	if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0))
		err(EXIT_NOTSUPP, "prctl(PR_SET_NO_NEW_PRIVS)");

	if (prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog))
		err(EXIT_NOTSUPP, "prctl(PR_SET_SECCOMP)");

	if (execvp(argv[optind], argv + optind))
		err(EXIT_NOTSUPP, "Could not exec");
}
