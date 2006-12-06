/* 
 * passwd.c - change password on an account
 *
 * Initially written for Linux by Peter Orbaek <poe@daimi.aau.dk>
 * Currently maintained at ftp://ftp.daimi.aau.dk/pub/linux/poe/
 *
 * Hacked by Alvaro Martinez Echevarria, alvaro@enano.etsit.upm.es,
 * to allow peaceful coexistence with yp. Nov 94.
 *
 * Hacked to allow root to set passwd from command line.
 * by Arpad Magossanyi (mag@tas.vein.hu)
 */

/*
 * Sun Oct 15 13:18:34 1995  Martin Schulze  <joey@finlandia.infodrom.north.de>
 *
 *	I have completely rewritten the whole argument handling (what?)
 *	to support two things. First I wanted "passwd $user $pw" to

        (a very bad idea; command lines are visible to people doing ps
	or running a background job that just collects all command lines)

 *	work and second I wanted simplicity checks to be done for
 *	root, too. Only root can turn this off using the -f
 *	switch. Okay, I started with this to support -V version
 *	information, but one thing comes to the next. *sigh*
 *	In a later step perhaps we'll be able to support shadow
 *	passwords. (?)
 *
 *	I have also included a DEBUG mode (-DDEBUG) to test the
 *	argument handling _without_ any write attempt to
 *	/etc/passwd.
 *
 *	If you're paranoid about security on your system, you may want
 *	to add -DLOGALL to CFLAGS. This will turn on additional syslog
 *	logging of every password change. (user changes are logged as
 *	auth.notice, but changing root's password is logged as
 *	auth.warning. (Of course, the password itself is not logged.)
 */

 /* 1999-02-22 Arkadiusz Mi¶kiewicz <misiek@pld.ORG.PL>
  * - added Native Language Support
  * Sun Mar 21 1999 - Arnaldo Carvalho de Melo <acme@conectiva.com.br>
  * - fixed strerr(errno) in gettext calls
  */

/*
 * Usage: passwd [username [password]]
 * Only root may use the one and two argument forms. 
 */
 
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <unistd.h>
#include <stdarg.h>
#include <termios.h>
#include <getopt.h>
#include <malloc.h>
#include <fcntl.h>
#include <pwd.h>
#include <ctype.h>
#include <time.h>
#include <string.h>
#include <errno.h>
#include <sys/resource.h>
#include <stdlib.h>
#include "my_crypt.h"
#include "setpwnam.h"
#include "islocal.h"
#include "xstrncpy.h"
#include "nls.h"
#include "env.h"

#ifndef _PATH_CHFN
# define _PATH_CHFN "/usr/bin/chfn"
# define _PATH_CHSH "/usr/bin/chsh"
#endif

#define LOGALL

#ifdef LOGALL
#include <syslog.h>
#endif /* LOGALL */

#define ascii_to_bin(c) ((c)>='a'?(c-59):(c)>='A'?((c)-53):(c)-'.')
#define bin_to_ascii(c) ((c)>=38?((c)-38+'a'):(c)>=12?((c)-12+'A'):(c)+'.')

static void
pexit(char *str, ...)
{
    va_list vlst;

    va_start(vlst, str);
    vfprintf(stderr, str, vlst);
    fprintf(stderr, ": ");
    perror("");
    va_end(vlst);
    exit(1);
}

/* 
 * Do various checks for stupid passwords here... 
 *
 * This would probably be the best place for checking against 
 * dictionaries. :-)
 */
static int
check_passwd_string(char *passwd, char *string) {
    int r;
    char *p, *q;

    r = 0;
    /* test for string at the beginning of passwd */
    for (p = passwd, q = string; *q && *p; q++, p++) {
	if(tolower(*p) != tolower(*q)) {
	    r++;
	    break;
	}
    }
	
    /* test for reverse string at the beginning of passwd */
    for (p = passwd, q = string + strlen(string)-1;
	*p && q >= string; p++, q--) {
	if(tolower(*p) != tolower(*q)) {
	    r++;
	    break;
	}
    }

    /* test for string at the end of passwd */
    for (p = passwd + strlen(passwd)-1, q = string + strlen(string)-1;
	 q >= string && p >= passwd; q--, p--) {
	if(tolower(*p) != tolower(*q)) {
	    r++;
	    break;
	}
    }
	
    /* test for reverse string at the beginning of passwd */
    for (p = passwd + strlen(passwd)-1, q = string;
	p >= passwd && *q; p--, q++) {
	if(tolower(*p) != tolower(*q)) {
	    r++;
	    break;
	}
    }

    if (r != 4) {
	return 0;
    }
    return 1;
}
	
static int
check_passwd(char *passwd, char *oldpasswd, char *user, char *gecos) {
    int ucase, lcase, digit, other;
    char *c, *g, *p;

    if ( (strlen(passwd) < 6) ) {
	printf(_("The password must have at least 6 characters, try again.\n"));
	return 0;
    }
	
    other = digit = ucase = lcase = 0;
    for (p = passwd; *p; p++) {
	ucase = ucase || isupper(*p);
	lcase = lcase || islower(*p);
	digit = digit || isdigit(*p);
	other = other || !isalnum(*p);
    }
	
    if ( (other + digit + ucase + lcase) < 2) {
	printf(_("The password must contain characters out of two of "
		 "the following\n"
		 "classes:  upper and lower case letters, digits and "
		 "non alphanumeric\n"
		 "characters. See passwd(1) for more information.\n"));
	return 0;
    }
	
    if ( oldpasswd[0] && !strncmp(oldpasswd, crypt(passwd, oldpasswd), 13) ) {
	printf(_("You cannot reuse the old password.\n"));
	return 0;
    }
	
    if ( !check_passwd_string(passwd, user) ) {
	printf(_("Please don't use something like your username as password!\n"));
	return 0;
    }

    /* check against realname */
    if ( (c = index(gecos, ',')) ) {
	if ( c-gecos && (g = (char *)malloc (c-gecos+1)) ) {
	    strncpy (g, gecos, c-gecos);
	    g[c-gecos] = 0;
	    while ( (c=rindex(g, ' ')) ) {
		if ( !check_passwd_string(passwd, c+1) ) {
		    printf(_("Please don't use something like your realname as password!\n"));
		    free (g);
		    return 0;
		}
		*c = '\0';
	    } /* while */
	    if ( !check_passwd_string(passwd, g) ) {
		printf(_("Please don't use something like your realname as password!\n"));
		free (g);
		return 0;
	    }
	    free (g);
	} /* if malloc */
    }

    /*
     * if ( !check_password_dict(passwd) ) ...
     */

    return 1; /* fine */
}

#if 0
static void
usage(void) {
    printf (_("Usage: passwd [username [password]]\n"));
    printf(_("Only root may use the one and two argument forms.\n"));
}
#endif

int
main(int argc, char *argv[]) {
    struct passwd *pe;
    uid_t gotuid = getuid();
    char *pwdstr = NULL, *cryptstr, *oldstr;
    char pwdstr1[10];
    char *user;
    time_t tm;
    char salt[2];
    int force_passwd = 0;
    int silent = 0;
    int c;
    int opt_index;
    int fullname = 0, shell = 0;
    static const struct option long_options[] =
      {
	{"fullname", no_argument, 0, 'f'},
	{"shell", no_argument, 0, 's'},
	{"force", no_argument, 0, 'o'},
	{"quiet", no_argument, 0, 'q'},
	{"silent", no_argument, 0, 'q'},
	{"version", no_argument, 0, 'v'},
	{0, 0, 0, 0}
	};

    sanitize_env();
    setlocale(LC_ALL, "");
    bindtextdomain(PACKAGE, LOCALEDIR);
    textdomain(PACKAGE);

    optind = 0;
    while ((c = getopt_long(argc, argv, "foqsvV",
			    long_options, &opt_index)) != -1) {
	switch (c) {
	case 'f':
	    fullname = 1;
	    break;
	case 's':
	    shell = 1;
	    break;
	case 'o':
	    force_passwd = 1;
	    break;
	case 'q':
	    silent = 1;
	    break;
	case 'V':
	case 'v':
	    printf("%s\n", util_linux_version);
	    exit(0);
	default:
	    fprintf(stderr, _("Usage: passwd [-foqsvV] [user [password]]\n"));
	    exit(1);
	} /* switch (c) */
    } /* while */

    if (fullname || shell) {
	char *args[100];
	int i, j, errsv;

	setuid(getuid()); /* drop special privs. */
	if (fullname)
	  args[0] = _PATH_CHFN;
	else
	  args[0] = _PATH_CHSH;

	for (i = optind, j = 1; (i < argc) && (j < 99); i++, j++)
	  args[j] = argv[i];

	args[j] = NULL;
	execv(args[0], args);
	errsv = errno;
	fprintf(stderr, _("Can't exec %s: %s\n"), args[0], strerror(errsv));
	exit(1);
    }
    
    switch (argc - optind) {
    case 0:
	/* Why use getlogin()? Some systems allow having several
	   usernames with the same uid, especially several root accounts.
	   One changes the password for the username, not the uid. */
	if ( !(user = getlogin()) || !*user ) {
	    if ( !(pe = getpwuid( getuid() )) ) {
		pexit(_("Cannot find login name"));
	    } else
		user = pe->pw_name;
	}
	break;
    case 1:
	if(gotuid) {
	    printf(_("Only root can change the password for others.\n"));
	    exit (1);
	} else
	    user = argv[optind];
	break;
    case 2:
	if(gotuid) {
	    printf(_("Only root can change the password for others.\n"));
	    exit(1);
	} else {
	    user = argv[optind];
	    pwdstr = argv[optind+1];
	}
	break;
    default:
	printf(_("Too many arguments.\n"));
	exit (1);
    } /* switch */

    if(!(pe = getpwnam(user))) {
	pexit(_("Can't find username anywhere. Is `%s' really a user?"), user);
    }
    
    if (!(is_local(user))) {
	puts(_("Sorry, I can only change local passwords. Use yppasswd instead."));
	exit(1);
    }
    
    /* if somebody got into changing utmp... */
    if(gotuid && gotuid != pe->pw_uid) {
	puts(_("UID and username does not match, imposter!"));
	exit(1);
    }
    
    if ( !silent )
	printf( _("Changing password for %s\n"), user );
    
    if ( (gotuid && pe->pw_passwd && pe->pw_passwd[0]) 
	|| (!gotuid && !strcmp(user,"root")) ) {
	oldstr = getpass(_("Enter old password: "));
	if(strncmp(pe->pw_passwd, crypt(oldstr, pe->pw_passwd), 13)) {
	    puts(_("Illegal password, imposter."));
	    exit(1);
	}
    }

    if ( pwdstr ) {   /* already set on command line */
	if ( !force_passwd && !check_passwd(pwdstr, pe->pw_passwd, user, pe->pw_gecos) )
	    exit (1);
    } else {
	/* password not set on command line by root, ask for it ... */
	
      redo_it:
	pwdstr = getpass(_("Enter new password: "));
	if (pwdstr[0] == '\0') {
	    puts(_("Password not changed."));
	    exit(1);
	}

	if ( (gotuid || (!gotuid && !force_passwd))
	     && !check_passwd(pwdstr, pe->pw_passwd, user, pe->pw_gecos) ) 
	    goto redo_it;
	
	xstrncpy(pwdstr1, pwdstr, sizeof(pwdstr1));
	pwdstr = getpass(_("Re-type new password: "));
	
	if(strncmp(pwdstr, pwdstr1, 8)) {
	    puts(_("You misspelled it. Password not changed."));
	    exit(1);
	}
    } /* pwdstr i.e. password set on command line */
    
    time(&tm); tm ^= getpid();
    salt[0] = bin_to_ascii(tm & 0x3f);
    salt[1] = bin_to_ascii((tm >> 6) & 0x3f);
    cryptstr = crypt(pwdstr, salt);

    if (pwdstr[0] == 0) cryptstr = "";

#ifdef LOGALL
    openlog("passwd", 0, LOG_AUTH);
    if (gotuid)
	syslog(LOG_NOTICE,_("password changed, user %s"),user);
    else {
	if ( !strcmp(user, "root") )
	    syslog(LOG_WARNING,_("ROOT PASSWORD CHANGED"));
	else
	    syslog(LOG_NOTICE,_("password changed by root, user %s"),user);
    }
    closelog();
#endif /* LOGALL */

    pe->pw_passwd = cryptstr;
#ifdef DEBUG
    printf (_("calling setpwnam to set password.\n"));
#else
    if (setpwnam( pe ) < 0) {
       perror( "setpwnam" );
       printf( _("Password *NOT* changed.  Try again later.\n" ));
       exit( 1 );
    }
#endif

    if ( !silent )
	printf(_("Password changed.\n"));	
    exit(0);
}
