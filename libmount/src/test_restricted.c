// SPDX-License-Identifier: LGPL-2.1-or-later
/* Regression test for a setuid helper that becomes root before using libmount. */
#include "c.h"
#include "libmount.h"

int main(void)
{
	struct libmnt_context *cxt;
	int restricted;

	/* A nosuid filesystem or missing setuid support cannot exercise AT_SECURE. */
	if (getuid() == 0 || geteuid() != 0 || !is_privileged_execution()) {
		warnx("setuid execution unavailable (ruid=%lu, euid=%lu, secure=%d)",
		      (unsigned long) getuid(), (unsigned long) geteuid(),
		      is_privileged_execution());
		return 77;
	}

	if (setuid(0) != 0)
		err(EXIT_FAILURE, "setuid");

	cxt = mnt_new_context();
	if (!cxt)
		err(EXIT_FAILURE, "mnt_new_context");
	restricted = mnt_context_is_restricted(cxt);
	mnt_free_context(cxt);
	if (restricted)
		errx(EXIT_FAILURE, "context is restricted after setuid(0)");

	puts("unrestricted after setuid(0)");
	return EXIT_SUCCESS;
}
