/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <sys/syslog.h>
#if defined (HAVE_LIBECONF) && defined (USE_VENDORDIR)
#include <libeconf.h>
#endif

#include "closestream.h"
#include "shells.h"

/*
 *  is_known_shell() -- if the given shell appears in /etc/shells
 *  or vendor defined files.
 *  Return 1 if found and return 0 if not found.
 */
extern int is_known_shell(const char *shell_name)
{
	int ret = 0;

#if defined (HAVE_LIBECONF) && defined (USE_VENDORDIR)
	size_t size = 0;
	econf_err error;
	char **keys;
	econf_file *key_file;

	error = econf_readDirs(&key_file,
			       _PATH_VENDORDIR,
			       "/etc",
			       "shells",
			       NULL,
			       "", /* key only */
			       "#" /* comment */);
	if (error) {
		syslog(LOG_ALERT,
		       _("Cannot parse shells files: %s"),
		       econf_errString(error));
		exit(EXIT_FAILURE);
	}

	error = econf_getKeys(key_file, NULL, &size, &keys);
	if (error) {
		syslog(LOG_ALERT,
		       _("Cannot evaluate entries in shells files: %s"),
		       econf_errString(error));
		econf_free (key_file);
		exit(EXIT_FAILURE);
	}

	for (size_t i = 0; i < size; i++) {
		if (strcmp (keys[i], shell_name) == 0) {
			ret = 1;
			break;
		}
	}
	econf_free (key_file);	
#else
	char *s;

	if (!shell_name)
		return 0;

	setusershell();
	while ((s = getusershell())) {
		if (*s != '#' && strcmp(shell_name, s) == 0) {
			ret = 1;
			break;
		}
	}
	endusershell();
#endif
	return ret;
}

