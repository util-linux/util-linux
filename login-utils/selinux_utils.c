#include <selinux/context.h>
#include <selinux/selinux.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#include "selinux_utils.h"

access_vector_t get_access_vector(const char *tclass, const char *op)
{
	security_class_t tc = string_to_security_class(tclass);

	return tc ? string_to_av_perm(tc, op) : 0;
}

int setupDefaultContext(char *orig_file)
{
	if (is_selinux_enabled() > 0) {
		security_context_t scontext;
		if (getfilecon(orig_file, &scontext) < 0)
			return 1;
		if (setfscreatecon(scontext) < 0) {
			freecon(scontext);
			return 1;
		}
		freecon(scontext);
	}
	return 0;
}
