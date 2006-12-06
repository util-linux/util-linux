/*-------------------------------------------------------------

The namei program

By: Roger S. Southwick

May 2, 1990


Modifications by Steve Tell  March 28, 1991

usage: namei pathname [pathname ... ]

This program reads it's arguments as pathnames to any type
of Unix file (symlinks, files, directories, and so forth). 
The program then follows each pathname until a terminal 
point is found (a file, directory, char device, etc).
If it finds a symbolic link, we show the link, and start
following it, indenting the output to show the context.

This program is useful for finding a "too many levels of
symbolic links" problems.

For each line output, the program puts a file type first:

   f: = the pathname we are currently trying to resolve
    d = directory
    D = directory that is a mount point
    l = symbolic link (both the link and it's contents are output)
    s = socket
    b = block device
    c = character device
    - = regular file
    ? = an error of some kind

The program prints an informative messages when we exceed
the maximum number of symbolic links this system can have.

The program exits with a 1 status ONLY if it finds it cannot
chdir to /,  or if it encounters an unknown file type.

1999-02-22 Arkadiusz Mi¶kiewicz <misiek@pld.ORG.PL>
- added Native Language Support

-------------------------------------------------------------*/

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/param.h>
#include "nls.h"

#define ERR	strerror(errno),errno

int symcount;
int mflag = 0;
int xflag = 0;

#ifndef MAXSYMLINKS
#define MAXSYMLINKS 256
#endif

static char *pperm(unsigned short);
static void namei(char *, int);
static void usage(void);

int
main(int argc, char **argv) {
    extern int optind;
    int c;
    char curdir[MAXPATHLEN];

    setlocale(LC_ALL, "");
    bindtextdomain(PACKAGE, LOCALEDIR);
    textdomain(PACKAGE);
    
    if(argc < 2)
	usage();

    while((c = getopt(argc, argv, "mx")) != -1){
	switch(c){
	    case 'm':
		mflag = !mflag;
		break;
	    
	    case 'x':
		xflag = !xflag;
		break;

	    case '?':
	    default:
		usage();
	}
    }

    if(getcwd(curdir, sizeof(curdir)) == NULL){
	(void)fprintf(stderr,
		      _("namei: unable to get current directory - %s\n"),
		      curdir);
	exit(1);
    }


    for(; optind < argc; optind++){
	(void)printf("f: %s\n", argv[optind]);
	symcount = 1;
	namei(argv[optind], 0);

	if(chdir(curdir) == -1){
	    (void)fprintf(stderr,
			  _("namei: unable to chdir to %s - %s (%d)\n"),
			  curdir, ERR);
	    exit(1);
	}
    }
    return 0;
}

static void
usage(void) {
    (void)fprintf(stderr,_("usage: namei [-mx] pathname [pathname ...]\n"));
    exit(1);
}

#ifndef NODEV
#define NODEV		(dev_t)(-1)
#endif

static void
namei(char *file, int lev) {
    char *cp;
    char buf[BUFSIZ], sym[BUFSIZ];
    struct stat stb;
    int i;
    dev_t lastdev = NODEV;

    /*
     * See if the file has a leading /, and if so cd to root
     */
    
    if(*file == '/'){
	while(*file == '/')
	    file++;
	
	if(chdir("/") == -1){
	    (void)fprintf(stderr,_("namei: could not chdir to root!\n"));
	    exit(1);
	}
	for(i = 0; i < lev; i++)
	    (void)printf("  ");

	if(stat("/", &stb) == -1){
	    (void)fprintf(stderr, _("namei: could not stat root!\n"));
	    exit(1);
	}
	lastdev = stb.st_dev;

	if(mflag)
	    (void)printf(" d%s /\n", pperm(stb.st_mode));
	else
	    (void)printf(" d /\n");
    }

    for(;;){

	if (strlen(file) >= BUFSIZ) {
		fprintf(stderr,_("namei: buf overflow\n"));
		return;
	}

	/*
	 * Copy up to the next / (or nil) into buf
	 */
	
	for(cp = buf; *file != '\0' && *file != '/'; cp++, file++)
	    *cp = *file;
	
	while(*file == '/')	/* eat extra /'s	*/
	    file++;

	*cp = '\0';

	if(buf[0] == '\0'){

	    /*
	     * Buf is empty, so therefore we are done
	     * with this level of file
	     */

	    return;
	}

	for(i = 0; i < lev; i++)
	    (void)printf("  ");

	/*
	 * See what type of critter this file is
	 */
	
	if(lstat(buf, &stb) == -1){
	    (void)printf(" ? %s - %s (%d)\n", buf, ERR);
	    return;
	}

	switch(stb.st_mode & S_IFMT){
	    case S_IFDIR:

		/*
		 * File is a directory, chdir to it
		 */
		
		if(chdir(buf) == -1){
		    (void)printf(_(" ? could not chdir into %s - %s (%d)\n"), buf, ERR );
		    return;
		}
		if(xflag && lastdev != stb.st_dev && lastdev != NODEV){
		    /* Across mnt point */
		    if(mflag)
			(void)printf(" D%s %s\n", pperm(stb.st_mode), buf);
		    else
			(void)printf(" D %s\n", buf);
		}
		else {
		    if(mflag)
			(void)printf(" d%s %s\n", pperm(stb.st_mode), buf);
		    else
			(void)printf(" d %s\n", buf);
		}
		lastdev = stb.st_dev;

		(void)fflush(stdout);
		break;

	    case S_IFLNK:
		/*
		 * Sigh, another symlink.  Read its contents and
		 * call namei()
		 */
		
		bzero(sym, BUFSIZ);
		if(readlink(buf, sym, BUFSIZ) == -1){
		    (void)printf(_(" ? problems reading symlink %s - %s (%d)\n"), buf, ERR);
		    return;
		}

		if(mflag)
		    (void)printf(" l%s %s -> %s", pperm(stb.st_mode), buf, sym);
		else
		    (void)printf(" l %s -> %s", buf, sym);

		if(symcount > 0 && symcount++ > MAXSYMLINKS){
		    (void)printf(_("  *** EXCEEDED UNIX LIMIT OF SYMLINKS ***\n"));
		    symcount = -1;
		} else {
		    (void)printf("\n");
		    namei(sym, lev + 1);
		}
		break;

	    case S_IFCHR:
		if(mflag)
		    (void)printf(" c%s %s\n", pperm(stb.st_mode), buf);
		else
		    (void)printf(" c %s\n", buf);
		break;
	    
	    case S_IFBLK:
		if(mflag)
		    (void)printf(" b%s %s\n", pperm(stb.st_mode), buf);
		else
		    (void)printf(" b %s\n", buf);
		break;
	    
	    case S_IFSOCK:
		if(mflag)
		    (void)printf(" s%s %s\n", pperm(stb.st_mode), buf);
		else
		    (void)printf(" s %s\n", buf);
		break;

	    case S_IFREG:
		if(mflag)
		    (void)printf(" -%s %s\n", pperm(stb.st_mode), buf);
		else
		    (void)printf(" - %s\n", buf);
		break;
		
	    default:
		(void)fprintf(stderr,_("namei: unknown file type 0%06o on file %s\n"), stb.st_mode, buf );
		exit(1);
	    
	}
    }
}

/* Take a 
 * Mode word, as from a struct stat, and return
 * a pointer to a static string containing a printable version like ls.
 * For example 0755 produces "rwxr-xr-x"
 */
static char *
pperm(unsigned short mode) {
	unsigned short m;
	static char buf[16];
	char *bp;
	char *lschars = "xwrxwrxwr";  /* the complete string backwards */
	char *cp;
	int i;

	for(i = 0, cp = lschars, m = mode, bp = &buf[8];
	    i < 9;
	    i++, cp++, m >>= 1, bp--) {

		if(m & 1)
			*bp = *cp;
		else
			*bp = '-';
	    }
	buf[9] = '\0';

	if(mode & S_ISUID)  {
		if(buf[2] == 'x')
			buf[2] = 's';
		else
			buf[2] = 'S';
	}
	if(mode & S_ISGID)  {
		if(buf[5] == 'x')
			buf[5] = 's';
		else
			buf[5] = 'S';
	}
	if(mode & S_ISVTX)  {
		if(buf[8] == 'x')
			buf[8] = 't';
		else
			buf[8] = 'T';
	}

	return &buf[0];
}

			
