/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Copyright (C) 2024 Thomas Weißschuh <thomas@t-8ch.de>
 */

#ifndef UL_SECCOMP_H
#define UL_SECCOMP_H

#include <linux/filter.h>
#include <linux/seccomp.h>
#include <sys/prctl.h>

static int ul_set_seccomp_filter_spec_allow(const struct sock_fprog *prog)
{
#if defined(__NR_seccomp) && defined(SECCOMP_SET_MODE_FILTER) && defined(SECCOMP_FILTER_FLAG_SPEC_ALLOW)
	if (!syscall(__NR_seccomp, SECCOMP_SET_MODE_FILTER, SECCOMP_FILTER_FLAG_SPEC_ALLOW, prog))
		return 0;
#endif

	return prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, prog);
}

#endif /* UL_SECCOMP_H */
