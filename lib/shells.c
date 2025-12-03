/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <sys/types.h>
#include <pwd.h>
#include <stdlib.h>
#include <paths.h>
#include <unistd.h>
#include <sys/syslog.h>
#if defined (HAVE_LIBECONF) && defined (USE_VENDORDIR)
#include <libeconf.h>
#endif

#include "closestream.h"
#include "shells.h"

#if defined (HAVE_LIBECONF) && defined (USE_VENDORDIR)
static econf_file *open_etc_shells(void)
{
	econf_err error;
	econf_file *key_file = NULL;

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
		return NULL;
	}

	return key_file;
}
#endif

/*
 *  print_shells () -- /etc/shells is outputted to stdout.
 */
extern void print_shells(FILE *out, const char *format)
{
#if defined (HAVE_LIBECONF) && defined (USE_VENDORDIR)
        size_t size = 0;
        econf_err error;
        char **keys = NULL;
        econf_file *key_file = open_etc_shells();

	if (!key_file)
	  return;

        error = econf_getKeys(key_file, NULL, &size, &keys);
        if (error) {
                econf_free(key_file);
                errx(EXIT_FAILURE,
                  _("Cannot evaluate entries in shells files: %s"),
                  econf_errString(error));
        }

        for (size_t i = 0; i < size; i++) {
	        fprintf(out, format, keys[i]);
        }
        econf_free(keys);
        econf_free(key_file);
#else
        char *s;

	setusershell();
        while ((s = getusershell()))
	        fprintf(out, format, s);
        endusershell();
#endif
}


/*
 *  is_known_shell() -- if the given shell appears in /etc/shells
 *  or vendor defined files.
 *  Return 1 if found and return 0 if not found.
 */
extern int is_known_shell(const char *shell_name)
{
	int ret = 0;

	if (!shell_name)
		return 0;

#if defined (HAVE_LIBECONF) && defined (USE_VENDORDIR)
	char *val = NULL;
	econf_err error;
        econf_file *key_file = open_etc_shells();

	if (!key_file)
	  return 0;

	error = econf_getStringValue (key_file, NULL, shell_name, &val);
	if (error) {
	        if (error != ECONF_NOKEY)
		       syslog(LOG_ALERT,
		               _("Cannot evaluate entries in shells files: %s"),
			      econf_errString(error));
	} else
	        ret = 1;

	free(val);
	econf_free(key_file);
#else
	char *s;

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

const char *ul_default_shell(int flags, const struct passwd *pw)
{
	const char *shell = NULL;

	if (!(flags & UL_SHELL_NOENV)) {
		shell = getenv("SHELL");
		if (shell && *shell)
			return shell;
	}
	if (!(flags & UL_SHELL_NOPWD)) {
		if (!pw)
			pw = getpwuid(getuid());
		if (pw)
			shell = pw->pw_shell;
		if (shell && *shell)
			return shell;
	}

	return _PATH_BSHELL;
}
