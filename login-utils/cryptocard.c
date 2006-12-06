/* cryptocard.c - support for the CRYPTOCard
   RB-1 Challenge-Response Token, initial code by 
   bentson@grieg.seaslug.org (Randolph Bentson) on 3-Dec-96,
   Hacked severely by poe@daimi.aau.dk.
   This relies on an implementation of DES in a library, currently
   it interfaces with the koontz-des.tar.gz implementation which
   can be found in:

      ftp://ftp.funet.fi/pub/crypt/cryptography/symmetric/des/

      (Link with the fdes.o file from that distribution)

   and with Eric A. Young's libdes implementation used in SSLeay. Also
   available from the above ftp site. Link with the libdes.a library.

   The sources for this code are maintained in

      ftp://ftp.daimi.aau.dk/pub/linux/poe/poeigl-X.XX.tar.gz

      1999-02-22 Arkadiusz Mi¶kiewicz <misiek@pld.ORG.PL>
      - added Native Language Support

*/
#ifdef CRYPTOCARD

/******************** CONFIGURATION section *****************************/
/*--------------- select ONE DES implementation ------------------------*/
/*#define KOONTZ_DES */
#define EAY_LIBDES
/*--------------- define if on little endian machine (Intel x86) -------*/
#define LITTLE_ENDIAN
/******************** end of CONFIGURATION section **********************/

#define _BSD_SOURCE 
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <syslog.h>
#include <pwd.h>
#include <sys/param.h>
#include <sys/stat.h>
#include "nls.h"

#ifdef KOONTZ_DES
#include "../koontz-des/des.h"
#endif /* KOONTZ_DES */

#ifdef EAY_LIBDES
#include "../libdes/des.h"
#endif /* EAY_LIBDES */

#include "cryptocard.h"

static char *
generate_challenge(void)
{
    static char challenge_str[30];
    int rfd;
    unsigned long clong;

    /* create and present a challenge string */
    if ((rfd = open("/dev/urandom", O_RDONLY)) < 0) {
	syslog(LOG_NOTICE, _("couldn't open /dev/urandom"));
	return NULL;
    }
    if (read(rfd, &clong, 4) < 4) {
	close(rfd);
	syslog(LOG_NOTICE, _("couldn't read random data from /dev/urandom"));
	return NULL;
    }
    close(rfd);

    sprintf(challenge_str,"%08lu", clong);
    return challenge_str;
}

static char *
get_key()
{
    int success = 0;
    char keyfile[MAXPATHLEN];
    static char key[10];
    int rfd;
    struct stat statbuf;

    if (strlen(pwd->pw_dir) + 13 > sizeof(keyfile))
	goto bail_out;
    sprintf(keyfile, "%s/.cryptocard", pwd->pw_dir);

    if ((rfd = open(keyfile, O_RDONLY)) < 0) {
	syslog(LOG_NOTICE, _("can't open %s for reading"), keyfile);
	goto bail_out;
    }
    if (fstat(rfd, &statbuf) < 0) {
	syslog(LOG_NOTICE, _("can't stat(%s)"), keyfile);
	goto close_and_bail_out;
    }
    if ((statbuf.st_uid != pwd->pw_uid)
	|| ((statbuf.st_mode & S_IFMT) != S_IFREG)
	|| (statbuf.st_mode & (S_IRWXU|S_IRWXG|S_IRWXO))) {
	syslog(LOG_NOTICE, _("%s doesn't have the correct filemodes"), keyfile);
	goto close_and_bail_out;
    }
    
    if (read(rfd, key, 8) < 8) {
	syslog(LOG_NOTICE, _("can't read data from %s"), keyfile);
	goto close_and_bail_out;
    }

    key[8] = 0;
    success = 1;
    
close_and_bail_out:
    close(rfd);

bail_out:
    if (success)
	return key;
    else 
	return NULL;
}

static int
check_response(char *challenge, char *response, char *key)
{
    char buf[20];

#ifdef KOONTZ_DES
    extern void des (union LR_block *);
    extern void loadkey(char *,int);
    extern void set_des_mode(int);

    union LR_block data;

    strncpy((char *)data.string, (char *)challenge, 8);
    set_des_mode(ENCRYPT);
    loadkey(key, NOSHIFT);
    des(&data);

    memset(key, 0, 8); /* no need for the secret key anymore, scratch it */

    sprintf(buf, "%2.2X%2.2X%2.2X%2.2X",
	    (int)(data.LR[0]) & 0xff,
	    (int)(data.LR[0]>>8) & 0xff,
	    (int)(data.LR[0]>>16) & 0xff,
	    (int)(data.LR[0]>>24) & 0xff);
#endif /* KOONTZ_DES */
#ifdef EAY_LIBDES
    des_cblock       res;
    des_key_schedule ks;
    
    des_set_key((des_cblock *)key, ks);
    memset(key, 0, 8);
    des_ecb_encrypt((des_cblock *)challenge, &res, ks, DES_ENCRYPT);

#ifdef LITTLE_ENDIAN
    /* use this on Intel x86 boxes */
    sprintf(buf, "%2.2X%2.2X%2.2X%2.2X",
	    res[0], res[1], res[2], res[3]);
#else /* ie. BIG_ENDIAN */
    /* use this on big endian RISC boxes */
    sprintf(buf, "%2.2X%2.2X%2.2X%2.2X",
	    res[3], res[2], res[1], res[0]);
#endif /* LITTLE_ENDIAN */
#endif /* EAY_LIBDES */

    /* return success only if ALL requirements have been met */
    if (strncmp(buf, response, 8) == 0)
	return 1;

    return 0;
}

int
cryptocard(void)
{
    char prompt[80];
    char *challenge;
    char *key;
    char *response;

    challenge = generate_challenge();
    if (challenge == NULL) return 0;

    if (strlen(challenge) + 13 > sizeof(prompt)) return 0;
    sprintf(prompt, "%s Password: ", challenge);

    alarm((unsigned int)timeout);  /* give user time to fiddle with card */
    response = getpass(prompt);  /* presents challenge and gets response */

    if (response == NULL) return 0;

    /* This requires some explanation: As root we may not be able to
       read the directory of the user if it is on an NFS mounted
       filesystem. We temporarily set our effective uid to the user-uid
       making sure that we keep root privs. in the real uid. 

       A portable solution would require a fork(), but we rely on Linux
       having the BSD setreuid() */

    {
	uid_t ruid = getuid();
	gid_t egid = getegid();

	setregid(-1, pwd->pw_gid);
	setreuid(0, pwd->pw_uid);

	/* now we can access the file */
	/* get the (properly qualified) key */
	key = get_key();

	/* reset to root privs */
	setuid(0); /* setreuid doesn't do it alone! */
	setreuid(ruid, 0);
	setregid(-1, egid);

	if (key == NULL) return 0;
    }

    return check_response(challenge, response, key);
}

#endif /* CRYPTOCARD */
