#include <selinux/context.h>
#include <selinux/selinux.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#include "selinux-utils.h"

int ul_setfscreatecon_from_file(char *orig_file)
{
	if (is_selinux_enabled() > 0) {
		char *scontext = NULL;

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

/* returns 1 if user has access to @class and @perm ("passwd", "chfn")
 *	or 0 on error,
 *	or 0 if has no access -- in this case sets @user_cxt to user-context
 */
int ul_selinux_has_access(const char *classstr, const char *perm, char **user_cxt)
{
	char *user;
	int rc;

	if (user_cxt)
		*user_cxt = NULL;

	if (getprevcon(&user) != 0)
		return 0;

	rc = selinux_check_access(user, user, classstr, perm, NULL);
	if (rc != 0 && user_cxt)
		*user_cxt = user;
	else
		freecon(user);

	return rc == 0 ? 1 : 0;
}

