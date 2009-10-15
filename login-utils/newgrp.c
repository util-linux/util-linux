/* setgrp.c - by Michael Haardt. Set the gid if possible */
/* Added a bit more error recovery/reporting - poe */
/* Vesa Roukonen added code for asking password */
/* Currently maintained at ftp://ftp.daimi.aau.dk/pub/linux/poe/ */

/* 1999-02-22 Arkadiusz Mi¶kiewicz <misiek@pld.ORG.PL>
 * - added Native Language Support
 */

#include <unistd.h>
#include <pwd.h>
#include <grp.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include "c.h"
#include "pathnames.h"
#include "my_crypt.h"
#include "nls.h"

/* try to read password from gshadow */
static char *
get_gshadow_pwd(char *groupname)
{
	char buf[BUFSIZ];
	char *pwd = NULL;
	FILE *f = fopen(_PATH_GSHADOW, "r");

	if (groupname == NULL || *groupname == '\0' || f == NULL)
		return NULL;

	while(fgets(buf, sizeof buf, f))
	{
		char *cp = strchr (buf, ':');
		if (!cp)
			continue;				/* any junk in gshadow? */
		*cp = '\0';
		if (strcmp(buf, groupname) == 0)
		{
			if (cp-buf >= BUFSIZ)
				break;				/* only group name on line */
			pwd = cp+1;
			if ((cp = strchr(pwd, ':')) && pwd == cp+1 )
				pwd = NULL;			/* empty password */
			else if (cp)
				*cp = '\0';
			break;
		}
	}
	fclose(f);
	return pwd ? strdup(pwd) : NULL;
}

static int
allow_setgid(struct passwd *pe, struct group *ge) 
{
    char **look;
    int notfound = 1;
    char *pwd, *xpwd;

    if (getuid() == 0) return TRUE;	/* root may do anything */
    if (ge->gr_gid == pe->pw_gid) return TRUE; /* You can switch back to your default group */

    look = ge->gr_mem;
    while (*look && (notfound = strcmp(*look++,pe->pw_name)));

    if(!notfound) return TRUE;		/* member of group => OK */

    /* Ask for password. Often there is no password in /etc/group, so
       contrary to login et al. we let an empty password mean the same
       as * in /etc/passwd */

    /* check /etc/gshadow */
    if (!(pwd = get_gshadow_pwd(ge->gr_name)))
        pwd = ge->gr_passwd;

    if(pwd && *pwd && (xpwd = getpass(_("Password: ")))) {
        if(strcmp(pwd, crypt(xpwd, pwd)) == 0)
	   return TRUE;		/* password accepted */
     }

    return FALSE;			/* default to denial */
}

int 
main(int argc, char *argv[])
{
    struct passwd *pw_entry;
    struct group *gr_entry;
    char *shell;

    setlocale(LC_ALL, "");
    bindtextdomain(PACKAGE, LOCALEDIR);
    textdomain(PACKAGE);

    if (!(pw_entry = getpwuid(getuid()))) {
	perror(_("newgrp: Who are you?"));
	exit(1);
    }

    shell = (pw_entry->pw_shell[0] ? pw_entry->pw_shell : _PATH_BSHELL);

    if (argc < 2) {
	if(setgid(pw_entry->pw_gid) < 0) {
	    perror(_("newgrp: setgid"));
	    exit(1);
	}
    } else {
	errno = 0;
	if (!(gr_entry = getgrnam(argv[1]))) {
	    if (errno)
		    perror(_("newgrp: No such group."));			/* error */
	    else
		    fprintf(stderr, "%s\n", _("newgrp: No such group."));	/* no group */
	    exit(1);
	} else {
	    if(allow_setgid(pw_entry, gr_entry)) {
		if(setgid(gr_entry->gr_gid) < 0) {
		    perror(_("newgrp: setgid"));
		    exit(1);
		}
	    } else {
		puts(_("newgrp: Permission denied"));
		exit(1);
	    }
	}
    }

    if(setuid(getuid()) < 0) {
	perror(_("newgrp: setuid"));
	exit(1);
    }

    fflush(stdout); fflush(stderr);
    execl(shell,shell,(char*)0);
    perror(_("No shell"));
    fflush(stderr);
    exit(1);
}
