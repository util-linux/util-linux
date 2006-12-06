/*
 * fs-util	A simple generic frontend for the for the fsck and mkfs
 *		programs under Linux.  See the manual pages for details.
 *
 * Usage:	fsck [-AV] [-t fstype] [fs-options] device
 *		mkfs [-V] [-t fstype] [fs-options] device< [size]
 *
 * Authors:	David Engel, <david@ods.com>
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 */


#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <mntent.h>
#include <unistd.h>
#include <getopt.h>


#ifndef DEFAULT_FSTYPE
#   define DEFAULT_FSTYPE	"minix"
#endif

#define _PATH_PROG	"%s.%s"
#define _PROG_FSCK	"fsck"

#define EXIT_OK          0
#define EXIT_NONDESTRUCT 1
#define EXIT_DESTRUCT    2
#define EXIT_UNCORRECTED 4
#define EXIT_ERROR       8
#define EXIT_USAGE       16
#define EXIT_LIBRARY     128

static char *Version = "1.8";
static char *ignored_types[] = {
  "ignore",
  "iso9660",
  "msdos",
  "nfs",
  "proc",
  "sw",
  "swap",
  NULL
};


/* Execute a program. */
int do_exec(char *prog, char **argv, int verbose)
{
    char *args[33];
    register int i;
    int pid, status;

    /* Build the vector. */
    i = 0;
    args[i++] = prog;
    while(*argv != NULL && i < 32)
        args[i++] = *argv++;
    args[i] = NULL;

    if (verbose) {
	i = 0;
	while(args[i] != NULL) {
	    printf("%s ", args[i]);
	    i++;
	}
	printf("\n");
	if (verbose > 1)
	    return EXIT_OK;
    }

    /* Fork and execute the correct program. */
    if ((pid = fork()) < 0) {
        perror("fork");
	status = EXIT_ERROR;
    } else if (pid == 0) {
	(void) execvp(args[0], args);
  	perror(args[0]);
	exit(EXIT_ERROR);
    } else {
        while(wait(&status) != pid)
	    ;
	status = WEXITSTATUS(status);
    }

    return status;
}


/* Check if we have to ignore a file system type. */
int ignore(char *type, char *opts)
{
    char *cp;
    char **ip;

    ip = ignored_types;
    while (*ip != NULL) {
	if (!strcmp(type, *ip))
	    return 1;
	ip++;
    }

    for (cp = strtok(opts, ","); cp != NULL; cp = strtok(NULL, ",")) {
	if (!strcmp(cp, "noauto"))
	    return 1;
    }

    return 0;
}


/* Check all file systems, using the /etc/fstab table. */
int check_all(int verbose, char **argv)
{
    char path[PATH_MAX];
    char *args[33];
    FILE *mntfile;
    struct mntent *mp;
    register int i;
    int status = EXIT_OK;

    if (verbose)
        printf("Checking all file systems.\n");

    /* Create an array of arguments. */
    i = 0;
    while (*argv != NULL && i < 32)
	args[i++] = *argv++;
    args[i] = NULL;
    args[i + 1] = NULL;

    /* Open the mount table. */
    if ((mntfile = setmntent(MNTTAB, "r")) == NULL) {
	perror(MNTTAB);
	exit(EXIT_ERROR);
    }

    /* Walk through the /etc/fstab file. */
    while ((mp = getmntent(mntfile)) != NULL) {
	if (verbose)
	    printf("%-7s %-15s %-15s ", mp->mnt_type,
		   mp->mnt_fsname, mp->mnt_dir);
	if (ignore(mp->mnt_type, mp->mnt_opts)) {
	    if (verbose)
	        printf("(ignored)\n");
	    continue;
	}

	/* Build program name. */
	sprintf(path, _PATH_PROG, _PROG_FSCK, mp->mnt_type);
	args[i] = mp->mnt_fsname;
	status |= do_exec(path, args, verbose);
    }

    (void) endmntent(mntfile);

    return status;
}


/* Lookup filesys in /etc/fstab and return the corresponding entry. */
struct mntent *lookup(char *filesys)
{
    FILE *mntfile;
    struct mntent *mp;

    /* No filesys name given. */
    if (filesys == NULL)
        return NULL;

    /* Open the mount table. */
    if ((mntfile = setmntent(MNTTAB, "r")) == NULL) {
	perror(MNTTAB);
	exit(EXIT_ERROR);
    }

    while ((mp = getmntent(mntfile)) != NULL) {
	if (!strcmp(filesys, mp->mnt_fsname) ||
	    !strcmp(filesys, mp->mnt_dir))
	    break;
    }

    (void) endmntent(mntfile);

    return mp;
}


void usage(int fsck, char *prog)
{
    if (fsck) {
	fprintf(stderr, "Usage: fsck [-AV] [-t fstype] [fs-options] filesys\n");
    } else {
	fprintf(stderr, "Usage: mkfs [-V] [-t fstype] [fs-options] filesys [size]\n");
    }

    exit(EXIT_USAGE);
}


void main(int argc, char *argv[])
{
    char path[PATH_MAX];
    char *oldpath, newpath[PATH_MAX];
    register char *sp;
    struct mntent *fsent;
    char *fstype = NULL;
    int verbose = 0;
    int doall = 0;
    int i, fsck, more;

    /* Must be 1 for "fsck" and 0 for "mkfs". */
    if ((sp = strrchr(argv[0], '/')) != NULL)
        sp++;
    else
        sp = argv[0];
    if (!strcmp(sp, _PROG_FSCK))
        fsck = 1;
    else
        fsck = 0;

    /* Check commandline options. */
    opterr = 0;
    more = 0;
    while ((more == 0) && ((i = getopt(argc, argv, "AVt:")) != EOF))
	switch(i) {
	  case 'A':
	    doall++;
	    break;
	  case 'V':
	    verbose++;
	    break;
	  case 't':
	    if (optarg == NULL)
	        usage(fsck, sp);
	    fstype = optarg;
	    break;
	  default:
	    more = 1;
	    break;		/* start of specific arguments */
	}

    /* Did we get any specific arguments? */
    if (more)
        optind--;

    /* Print our version number if requested. */
    if (verbose)
        printf("%s (fsutil) version %s (%s)\n", argv[0],
	       Version, __DATE__);

    /* Update our PATH to include /etc/fs and /etc. */
    strcpy(newpath, "PATH=/etc/fs:/etc:");
    if ((oldpath = getenv("PATH")) != NULL)
        strcat(newpath, oldpath);
    putenv(newpath);
    
    /* If -A was specified ("check all"), double-check. */
    if (doall) {
	if (!fsck || (fstype != NULL))
	    usage(fsck, sp);
	exit(check_all(verbose, &argv[optind]));
    } else {
	/* If -t wasn't specified, we must deduce fstype. */
	if (fstype == NULL) {
	    /* make sure that "filesys" was specified */
	    if (optind >= argc)
		usage(fsck, sp);
	    /* then try looking for it in /etc/fstab */
	    if ((fsent = lookup(argv[argc - 1])) != NULL) {
	        argv[argc - 1] = fsent->mnt_fsname;
		fstype = fsent->mnt_type;
	    } else {
	        if (!fsck && optind < argc-1) {
		    if ((fsent = lookup(argv[argc - 2])) != NULL) {
		        argv[argc - 2] = fsent->mnt_fsname;
			fstype = fsent->mnt_type;
		    }
		}
	    }
	    /* if we still don't know, use the default */
	    if (fstype == NULL) fstype = DEFAULT_FSTYPE;
	}

	/* Build program name. */
	sprintf(path, _PATH_PROG, sp, fstype);
	exit(do_exec(path, &argv[optind], verbose));
    }
    /*NOTREACHED*/
}
