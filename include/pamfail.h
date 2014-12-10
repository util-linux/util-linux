/*
 * No copyright is claimed.  This code is in the public domain; do with
 * it what you wish.
 *
 * Written by Karel Zak <kzak@redhat.com>
 */
#ifndef UTIL_LINUX_PAMFAIL_H
#include <security/pam_appl.h>
#ifdef HAVE_SECURITY_PAM_MISC_H
# include <security/pam_misc.h>
#elif defined(HAVE_SECURITY_OPENPAM_H)
# include <security/openpam.h>
#endif
#include "c.h"

static inline int
pam_fail_check(pam_handle_t *pamh, int retcode)
{
	if (retcode == PAM_SUCCESS)
		return 0;
	warnx("%s", pam_strerror(pamh, retcode));
	pam_end(pamh, retcode);
	return 1;
}

#endif /* UTIL_LINUX_PAMFAIL_H */
