/* passwd.c - change password on an account
 * Initially written for Linux by Peter Orbaek <poe@daimi.aau.dk>
 * Currently maintained at ftp://ftp.daimi.aau.dk/pub/linux/poe/
 */

/* Hacked by Alvaro Martinez Echevarria, alvaro@enano.etsit.upm.es,
   to allow peaceful coexistence with yp. Nov 94. */
/* Hacked to allow root to set passwd from command line.
   by Arpad Magossanyi (mag@tas.vein.hu) */

/*
 * Usage: passwd [username [password]]
 * Only root may use the one and two argument forms. 
 */
 
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <pwd.h>
#include <ctype.h>
#include <time.h>
#include <string.h>
#include <errno.h>
#include <sys/resource.h>

extern int is_local(char *);

#define ascii_to_bin(c) ((c)>='a'?(c-59):(c)>='A'?((c)-53):(c)-'.')
#define bin_to_ascii(c) ((c)>=38?((c)-38+'a'):(c)>=12?((c)-12+'A'):(c)+'.')

#define MAX_LENGTH	1024

static void
pexit(str)
     char *str;
{
    perror(str);
    exit(1);
}

int
main(argc, argv)
     int argc;
     char *argv[];
{
    struct passwd *pe;
    uid_t gotuid = getuid();
    char *pwdstr = NULL, *cryptstr, *oldstr;
    char pwdstr1[10];
    int ucase, lcase, other;
    char *p, *q, *user;
    time_t tm;
    char salt[2];
    FILE *fd_in, *fd_out;
    char line[MAX_LENGTH];
    char colonuser[16];
    int error=0;
    int r;
    int ptmp;
#ifndef USE_SETPWNAM
    struct rlimit rlim;
#endif

    if(argc > 3) {
	puts("Too many arguments");
	exit(1);
    } else if(argc >= 2) {
	if(gotuid) {
	    puts("Only root can change the password for others");
	    exit(1);
	}
	user = argv[1];
	
	if (argc == 3) pwdstr = argv[2];
	
    } else {
	if (!(user = getlogin())) {
	    if (!(pe = getpwuid( getuid() ))) {
		pexit("Cannot find login name");
	    } else
	      user = pe->pw_name;
	}
    }

#ifndef USE_SETPWNAM
    umask(022);

    rlim.rlim_cur = rlim.rlim_max = RLIM_INFINITY;
    setrlimit(RLIMIT_CPU, &rlim);
    setrlimit(RLIMIT_FSIZE, &rlim);
    setrlimit(RLIMIT_STACK, &rlim);
    setrlimit(RLIMIT_DATA, &rlim);
    setrlimit(RLIMIT_RSS, &rlim);
    rlim.rlim_cur = rlim.rlim_max = 0;
    setrlimit(RLIMIT_CORE, &rlim);
#endif
    
    if(!(pe = getpwnam(user))) {
	pexit("Can't find username anywhere. Are you really a user?");
    }
    
    if (!(is_local(user))) {
	puts("Sorry, I can only change local passwords. Use yppasswd instead.");
	exit(1);
    }
    
    /* if somebody got into changing utmp... */
    if(gotuid && gotuid != pe->pw_uid) {
	puts("UID and username does not match, imposter!");
	exit(1);
    }
    
    printf( "Changing password for %s\n", user );
    
    if(gotuid && pe->pw_passwd && pe->pw_passwd[0]) {
	oldstr = getpass("Enter old password: ");
	if(strncmp(pe->pw_passwd, crypt(oldstr, pe->pw_passwd), 13)) {
	    puts("Illegal password, imposter.");
	    exit(1);
	}
    }

    if (!pwdstr) {
	/* password not set on command line by root, ask for it ... */
	
      redo_it:
	pwdstr = getpass("Enter new password: ");
	if (pwdstr[0] == '\0') {
	    puts("Password not changed.");
	    exit(1);
	}
	
	if((strlen(pwdstr) < 6) && gotuid) {
	    puts("The password must have at least 6 characters, try again.");
	    goto redo_it;
	}
	
	other = ucase = lcase = 0;
	for(p = pwdstr; *p; p++) {
	    ucase = ucase || isupper(*p);
	    lcase = lcase || islower(*p);
	    other = other || !isalpha(*p);
	}
	
	if((!ucase || !lcase) && !other && gotuid) {
	    puts("The password must have both upper- and lowercase");
	    puts("letters, or non-letters; try again.");
	    goto redo_it;
	}
	
	if (pe->pw_passwd[0] 
	    && !strncmp(pe->pw_passwd, crypt(pwdstr, pe->pw_passwd), 13)
	    && gotuid) {
	    puts("You cannot reuse the old password.");
	    goto redo_it;
	}
	
	r = 0;
	for(p = pwdstr, q = pe->pw_name; *q && *p; q++, p++) {
	    if(tolower(*p) != tolower(*q)) {
		r = 1;
		break;
	    }
	}
	
	for(p = pwdstr + strlen(pwdstr)-1, q = pe->pw_name;
	    *q && p >= pwdstr; q++, p--) {
	    if(tolower(*p) != tolower(*q)) {
		r += 2;
		break;
	    }
	}
	
	if(gotuid && r != 3) {
	    puts("Please don't use something like your username as password!");
	    goto redo_it;
	}
	
	/* do various other checks for stupid passwords here... */
	
	strncpy(pwdstr1, pwdstr, 9);
	pwdstr = getpass("Re-type new password: ");
	
	if(strncmp(pwdstr, pwdstr1, 8)) {
	    puts("You misspelled it. Password not changed.");
	    exit(1);
	}
    } /* pwdstr != argv[2] i.e. password set on command line */
    
    time(&tm);
    salt[0] = bin_to_ascii(tm & 0x3f);
    salt[1] = bin_to_ascii((tm >> 6) & 0x3f);
    cryptstr = crypt(pwdstr, salt);

    if (pwdstr[0] == 0) cryptstr = "";

#ifdef USE_SETPWNAM
    pe->pw_passwd = cryptstr;
    if (setpwnam( pe ) < 0) {
       perror( "setpwnam" );
       printf( "Password *NOT* changed.  Try again later.\n" );
       exit( 1 );
    }
#else
    if ((ptmp = open("/etc/ptmp", O_CREAT|O_EXCL|O_WRONLY, 0600)) < 0) {
	pexit("Can't exclusively open /etc/ptmp, can't update password");
    }
    fd_out = fdopen(ptmp, "w");
    
    if(!(fd_in = fopen("/etc/passwd", "r"))) {
	pexit("Can't read /etc/passwd, can't update password");
    }
    
    strcpy(colonuser, user);
    strcat(colonuser, ":");
    while(fgets(line, sizeof(line), fd_in)) {
	if(!strncmp(line,colonuser,strlen(colonuser))) {
	    pe->pw_passwd = cryptstr;
	    if(putpwent(pe, fd_out) < 0) {
		error = 1;
	    }
	} else {
	    if(fputs(line,fd_out) < 0) {
		error = 1;
	    }
	}
	if(error) {
	    puts("Error while writing new password file, password not changed.");
	    fclose(fd_out);
	    endpwent();
	    unlink("/etc/ptmp");
	    exit(1);
	}
    }
    fclose(fd_in);
    fclose(fd_out);
    
    unlink("/etc/passwd.OLD");	/* passwd.OLD not required */
    if (link("/etc/passwd", "/etc/passwd.OLD")) 
      pexit("link(/etc/passwd, /etc/passwd.OLD) failed: no change");
    if (unlink("/etc/passwd") < 0)
      pexit("unlink(/etc/passwd) failed: no change");
    if (link("/etc/ptmp", "/etc/passwd") < 0)
      pexit("link(/etc/ptmp, /etc/passwd) failed: PASSWD file DROPPED!!");
    if (unlink("/etc/ptmp") < 0) 
      pexit("unlink(/etc/ptmp) failed: /etc/ptmp still exists");
    
    chmod("/etc/passwd", 0644);
    chown("/etc/passwd", 0, 0);
#endif
    
    puts("Password changed.");	
    exit(0);
}
