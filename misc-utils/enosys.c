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
#include <sys/syscall.h>

#include "c.h"
#include "exitcodes.h"
#include "nls.h"
#include "bitops.h"

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
#elif __s390x__
# 	 define SECCOMP_ARCH_NATIVE AUDIT_ARCH_S390X
#elif __s390__
# 	 define SECCOMP_ARCH_NATIVE AUDIT_ARCH_S390
#elif __PPC64__
#    if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
# 	 define SECCOMP_ARCH_NATIVE AUDIT_ARCH_PPC64
#    else
# 	 define SECCOMP_ARCH_NATIVE AUDIT_ARCH_PPC64LE
#    endif
#elif __powerpc__
#    define SECCOMP_ARCH_NATIVE AUDIT_ARCH_PPC
#elif __mips__
#    if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
# 	 define SECCOMP_ARCH_NATIVE AUDIT_ARCH_MIPS
#    else
# 	 define SECCOMP_ARCH_NATIVE AUDIT_ARCH_MIPSEL
#    endif
#elif __arc__
#    if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
# 	 define SECCOMP_ARCH_NATIVE AUDIT_ARCH_ARCV2BE
#    else
# 	 define SECCOMP_ARCH_NATIVE AUDIT_ARCH_ARCV2
#    endif
#elif __sparc__
#    if __SIZEOF_POINTER__ == 4
# 	 define SECCOMP_ARCH_NATIVE AUDIT_ARCH_SPARC
#    else
# 	 define SECCOMP_ARCH_NATIVE AUDIT_ARCH_SPARC64
#    endif
#else
#    error Unknown target architecture
#endif

#define UL_BPF_NOP (struct sock_filter) BPF_JUMP(BPF_JMP | BPF_JA, 0, 0, 0)
#define IS_LITTLE_ENDIAN (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)

#define syscall_nr (offsetof(struct seccomp_data, nr))
#define syscall_arch (offsetof(struct seccomp_data, arch))
#define syscall_arg(n) (offsetof(struct seccomp_data, args[n]))

static int set_seccomp_filter(const void *prog)
{
#if defined(__NR_seccomp) && defined(SECCOMP_SET_MODE_FILTER) && defined(SECCOMP_FILTER_FLAG_SPEC_ALLOW)
	if (!syscall(__NR_seccomp, SECCOMP_SET_MODE_FILTER, SECCOMP_FILTER_FLAG_SPEC_ALLOW, prog))
		return 0;
#endif

	return prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, prog);
}

struct syscall {
	const char *const name;
	long number;
};

static const struct syscall syscalls[] = {
#define UL_SYSCALL(name, nr) { name, nr },
#include "syscalls.h"
#undef UL_SYSCALL
};
static_assert(sizeof(syscalls) > 0, "no syscalls found");

static void __attribute__((__noreturn__)) usage(void)
{
	FILE *out = stdout;

	fputs(USAGE_HEADER, out);
	fprintf(out, _(" %s [options] -- <command>\n"), program_invocation_short_name);

	fputs(USAGE_OPTIONS, out);
	fputs(_(" -s, --syscall           syscall to block\n"), out);
	fputs(_(" -l, --list              list known syscalls\n"), out);

	fputs(USAGE_SEPARATOR, out);
	fprintf(out, USAGE_HELP_OPTIONS(25));

	fprintf(out, USAGE_MAN_TAIL("enosys(1)"));

	exit(EXIT_SUCCESS);
}

int main(int argc, char **argv)
{
	int c;
	size_t i;
	bool found;
	static const struct option longopts[] = {
		{ "syscall", required_argument, NULL, 's' },
		{ "list",    no_argument,       NULL, 'l' },
		{ "version", no_argument,       NULL, 'V' },
		{ "help",    no_argument,       NULL, 'h' },
		{ 0 }
	};

	bool blocked_syscalls[ARRAY_SIZE(syscalls)] = {};

	while ((c = getopt_long (argc, argv, "+Vhs:l", longopts, NULL)) != -1) {
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
				errx(EXIT_FAILURE, _("Unknown syscall '%s'"), optarg);
			break;
		case 'l':
			for (i = 0; i < ARRAY_SIZE(syscalls); i++)
				printf("%s\n", syscalls[i].name);
			return EXIT_SUCCESS;
		case 'V':
			print_version(EXIT_SUCCESS);
		case 'h':
			usage();
		default:
			errtryhelp(EXIT_FAILURE);
		}
	}

	if (optind >= argc)
		errtryhelp(EXIT_FAILURE);

#define N_FILTERS (ARRAY_SIZE(syscalls) * 2 + 12)

	struct sock_filter filter[N_FILTERS] = {
		[0]  = BPF_STMT(BPF_LD | BPF_W | BPF_ABS, syscall_arch),
		[1]  = BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SECCOMP_ARCH_NATIVE, 1, 0),
		[2]  = BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_TRAP),

		/* Blocking "execve" normally would also block our own call to
		 * it and the end of main. To distinguish between our execve
		 * and the execve to be blocked, compare the environ pointer.
		 *
		 * See https://lore.kernel.org/all/CAAnLoWnS74dK9Wq4EQ-uzQ0qCRfSK-dLqh+HCais-5qwDjrVzg@mail.gmail.com/
		 */
		[3]  = BPF_STMT(BPF_LD | BPF_W | BPF_ABS, syscall_nr),
		[4]  = BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_execve, 0, 5),
		[5]  = BPF_STMT(BPF_LD | BPF_W | BPF_ABS, syscall_arg(2) + 4 * !IS_LITTLE_ENDIAN),
		[6]  = BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, (uint64_t)(uintptr_t) environ, 0, 3),
		[7]  = BPF_STMT(BPF_LD | BPF_W | BPF_ABS, syscall_arg(2) + 4 * IS_LITTLE_ENDIAN),
		[8]  = BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, (uint64_t)(uintptr_t) environ >> 32, 0, 1),
		[9]  = BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),

		[10] = BPF_STMT(BPF_LD | BPF_W | BPF_ABS, syscall_nr),

		[N_FILTERS - 1] = BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
	};
	static_assert(ARRAY_SIZE(filter) <= BPF_MAXINSNS, "bpf filter too big");

	for (i = 0; i < ARRAY_SIZE(syscalls); i++) {
		struct sock_filter *f = &filter[11 + i * 2];

		*f = (struct sock_filter) BPF_JUMP(
				BPF_JMP | BPF_JEQ | BPF_K,
				syscalls[i].number,
				0, 1);
		*(f + 1) = blocked_syscalls[i]
			? (struct sock_filter) BPF_STMT(
					BPF_RET | BPF_K, SECCOMP_RET_ERRNO | ENOSYS)
			: UL_BPF_NOP;
	}

	struct sock_fprog prog = {
		.len    = ARRAY_SIZE(filter),
		.filter = filter,
	};

	/* *SET* below will return EINVAL when either the filter is invalid or
	 * seccomp is not supported. To distinguish those cases do a *GET* here
	 */
	if (prctl(PR_GET_SECCOMP) == -1 && errno == EINVAL)
		err(EXIT_NOTSUPP, _("Seccomp non-functional"));

	if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0))
		err_nosys(EXIT_FAILURE, _("Could not run prctl(PR_SET_NO_NEW_PRIVS)"));

	if (set_seccomp_filter(&prog))
		err_nosys(EXIT_FAILURE, _("Could not seccomp filter"));

	if (execvp(argv[optind], argv + optind))
		err(EXIT_NOTSUPP, _("Could not exec"));
}
